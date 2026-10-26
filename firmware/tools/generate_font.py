"""Rasterize GB2312 and UI text for LVGL 8. Requires Pillow for regeneration."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import hashlib

ROOT = Path(__file__).resolve().parents[1]
font_path = ROOT / 'assets/NotoSansCJKsc-Regular.otf'
font = ImageFont.truetype(str(font_path), 18)
characters = set(chr(i) for i in range(32, 127))
# Include both GB2312 Hanzi levels and punctuation for unrestricted chat replies.
# 2 bpp retains antialiasing while fitting the existing 4 MiB application slot.
BPP = 2
for lead in range(0xa1, 0xf8):
    for trail in range(0xa1, 0xff):
        try:
            characters.update(bytes([lead,trail]).decode('gb2312'))
        except UnicodeDecodeError:
            pass
for path in [*(ROOT / 'main').rglob('*.c'), *(ROOT / 'templates').rglob('*.c')]:
    if path.name != 'font_ui.c':
        characters.update(c for c in path.read_text(encoding='utf-8') if ord(c) >= 160)
characters = sorted(characters, key=ord)
bitmap = []
descriptors = ['    {0},']
for char in characters:
    left, top, right, bottom = font.getbbox(char, anchor='ls')
    width, height = right-left, bottom-top
    image = Image.new('L', (max(width, 1), max(height, 1)), 0)
    ImageDraw.Draw(image).text((-left, -top), char, font=font, fill=255, anchor='ls')
    pixels = [round(v * 3 / 255) for v in image.tobytes()] if width*height else []
    offset = len(bitmap)
    for i in range(0, len(pixels), 4):
        packed = 0
        for j in range(4):
            packed |= (pixels[i+j] if i+j < len(pixels) else 0) << (6-2*j)
        bitmap.append(packed)
    descriptors.append('    {.bitmap_index=%d,.adv_w=%d,.box_w=%d,.box_h=%d,.ofs_x=%d,.ofs_y=%d}, /* U+%04X */' %
                       (offset, round(font.getlength(char)*16), width, height, left, -bottom, ord(char)))
source = ['/* Generated Noto CJK subset; SIL OFL 1.1, see assets/OFL.txt. */',
          '#include "lvgl.h"', 'static const uint8_t bitmap[] = {']
for i in range(0, len(bitmap), 24):
    source.append('    ' + ','.join(str(b) for b in bitmap[i:i+24]) + ',')
source += ['};', 'static const lv_font_fmt_txt_glyph_dsc_t glyphs[] = {', *descriptors, '};',
           'static const uint16_t unicodes[] = {',
           '    ' + ','.join(str(ord(c)-32) for c in characters), '};',
           'static const lv_font_fmt_txt_cmap_t cmaps[] = {{',
           f'    .range_start=32,.range_length={ord(characters[-1])-31},.glyph_id_start=1,',
           f'    .unicode_list=unicodes,.list_length={len(characters)},.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY',
           '}};',
           'static lv_font_fmt_txt_glyph_cache_t cache;',
           'static lv_font_fmt_txt_dsc_t dsc = {',
           f'    .glyph_bitmap=bitmap,.glyph_dsc=glyphs,.cmaps=cmaps,.cmap_num=1,.bpp={BPP},.cache=&cache',
           '};', 'const lv_font_t font_ui = {',
           '    .get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,',
           '    .line_height=26,.base_line=6,.subpx=LV_FONT_SUBPX_NONE,.dsc=&dsc,.fallback=&lv_font_montserrat_14', '};', '']
(ROOT / 'main/shell/font_ui.c').write_text('\n'.join(source), encoding='utf-8')
(ROOT / 'assets/font-characters.txt').write_text(''.join(characters), encoding='utf-8')
print(f'{len(characters)} glyphs, {len(bitmap)} bitmap bytes, source SHA256 {hashlib.sha256(font_path.read_bytes()).hexdigest()}')
