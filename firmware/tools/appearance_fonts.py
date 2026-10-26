"""Generate OFL numeric fonts for the three appearance presets."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import urllib.request,hashlib,json
ROOT=Path(__file__).resolve().parents[1]
assets=ROOT/'assets/appearance-fonts'; assets.mkdir(exist_ok=True)
fonts=[('paper','manrope','Manrope[wght].ttf',800,700),('blue','barlowcondensed','BarlowCondensed-SemiBold.ttf',600,600),('gold','outfit','Outfit[wght].ttf',600,500)]
out=['/* Generated OFL numeric fonts; source/license: assets/appearance-fonts/. */','#include "lvgl.h"']; manifest={}
for theme,family,filename,clock_weight,temp_weight in fonts:
    path=assets/filename
    url='https://raw.githubusercontent.com/google/fonts/main/ofl/'+family+'/'+urllib.parse.quote(filename)
    if not path.exists(): path.write_bytes(urllib.request.urlopen(url,timeout=30).read())
    license_path=assets/(family+'-OFL.txt')
    if not license_path.exists(): license_path.write_bytes(urllib.request.urlopen('https://raw.githubusercontent.com/google/fonts/main/ofl/'+family+'/OFL.txt',timeout=30).read())
    manifest[family]={'source':url,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
    for size,weight in ((72,clock_weight),(52,temp_weight)):
        font=ImageFont.truetype(str(path),size)
        try:
            axes=font.get_variation_axes(); font.set_variation_by_axes([weight if a['name']==b'Weight' else a['default'] for a in axes])
        except OSError: pass
        chars=sorted(set('0123456789-:' if size==72 else '0123456789-°'),key=ord)
        limit=284 if size==72 else 104
        widest=font.getlength('88:88:88' if size==72 else '-88°')
        scale=min(1,limit/widest)
        name=f'font_{theme}{size}'; data=[]; desc=['{0},']; codes=[]
        for ch in chars:
            l,t,r,b=font.getbbox(ch,anchor='ls'); w,h=r-l,b-t
            img=Image.new('L',(max(1,w),max(1,h))); ImageDraw.Draw(img).text((-l,-t),ch,font=font,anchor='ls',fill=255)
            sw=round(w*scale)
            if w and h and sw!=w: img=img.resize((max(1,sw),h),Image.Resampling.LANCZOS)
            pixels=[round(v/17) for v in img.tobytes()] if w*h else []
            index=len(data)
            data.extend((pixels[i]<<4)|(pixels[i+1] if i+1<len(pixels) else 0) for i in range(0,len(pixels),2))
            desc.append(f'{{.bitmap_index={index},.adv_w={round(font.getlength(ch)*scale*16)},.box_w={sw},.box_h={h},.ofs_x={round(l*scale)},.ofs_y={-b}}},')
            codes.append(str(ord(ch)-32))
        out.append(f'static const uint8_t {name}_bitmap[]={{')
        out.extend(','.join(map(str,data[i:i+32]))+',' for i in range(0,len(data),32)); out.append('};')
        out.extend([f'static const lv_font_fmt_txt_glyph_dsc_t {name}_glyphs[]={{',*desc,'};',f'static const uint16_t {name}_unicode[]={{'+','.join(codes)+'};',f'static const lv_font_fmt_txt_cmap_t {name}_map[]={{'+f'{{.range_start=32,.range_length={ord(chars[-1])-31},.glyph_id_start=1,.unicode_list={name}_unicode,.list_length={len(chars)},.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}}'+'};',f'static lv_font_fmt_txt_glyph_cache_t {name}_cache;',f'static lv_font_fmt_txt_dsc_t {name}_dsc={{.glyph_bitmap={name}_bitmap,.glyph_dsc={name}_glyphs,.cmaps={name}_map,.cmap_num=1,.bpp=4,.cache=&{name}_cache}};',f'const lv_font_t {name}={{.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height={size+6},.base_line=4,.dsc=&{name}_dsc}};'])
        print(name,'width',round(widest*scale,1),'horizontal scale',round(scale,3),'weight',weight)
(ROOT/'main/apps/weather/weather_number_fonts.c').write_text('\n'.join(out)+'\n',encoding='utf-8')
(assets/'sources.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
