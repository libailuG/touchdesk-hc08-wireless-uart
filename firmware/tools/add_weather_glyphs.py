"""Append missing weather glyphs to mapped font bitmaps without changing existing glyph data."""
from pathlib import Path
import bisect,hashlib,json,re,sys
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parents[1];raw=root/'assets/resource-data';path=root/'main/apps/weather/weather_fonts.c';text=path.read_text(encoding='utf-8');entries=json.loads((raw/'manifest.json').read_text(encoding='utf-8'))
for size in (12,13,16):
 name=f'font_weather{size}';font=ImageFont.truetype(str(root/'assets/NotoSansCJKsc-Regular.otf'),size);um=re.search(name+r'_unicode\[\]=\{(.*?)\}',text,re.S);unicode=[int(v) for v in um.group(1).split(',') if v.strip()]
 gm=re.search(r'(static const lv_font_fmt_txt_glyph_dsc_t '+name+r'_glyphs\[\]=\{)(.*?)(\};)',text,re.S);glyphs=re.findall(r'\{[^{}]*\},?',gm.group(2));assert len(glyphs)==len(unicode)+1
 entry=next(e for e in entries if e['key']==f'weather_fonts__{name}_bitmap');data=bytearray((raw/entry['file']).read_bytes())
 for char in ''.join(sys.argv[1:]):
  cp=ord(char)-32
  if cp in unicode:continue
  l,t,r,b=font.getbbox(char,anchor='ls');w,h=r-l,b-t;image=Image.new('L',(w,h));ImageDraw.Draw(image).text((-l,-t),char,font=font,anchor='ls',fill=255);px=[round(v/17) for v in image.getdata()];offset=len(data)
  for i in range(0,len(px),2):data.append((px[i]<<4)|(px[i+1] if i+1<len(px) else 0))
  pos=bisect.bisect_left(unicode,cp);unicode.insert(pos,cp);glyphs.insert(pos+1,'{.bitmap_index=%d,.adv_w=%d,.box_w=%d,.box_h=%d,.ofs_x=%d,.ofs_y=%d},'%(offset,round(font.getlength(char)*16),w,h,l,-b))
 text=text[:gm.start()]+gm.group(1)+'\n'+'\n'.join(g if g.endswith(',') else g+',' for g in glyphs)+'\n'+gm.group(3)+text[gm.end():]
 text=re.sub(r'('+name+r'_unicode\[\]=\{).*?(\})',lambda m:m.group(1)+','.join(map(str,unicode))+m.group(2),text,flags=re.S)
 text=re.sub(r'('+name+r'_cmap\[\]=\{\{[^\n]*?\.list_length=)\d+',lambda m:m.group(1)+str(len(unicode)),text)
 (raw/entry['file']).write_bytes(data);entry.update(size=len(data),sha256=hashlib.sha256(data).hexdigest())
path.write_text(text,encoding='utf-8');(raw/'manifest.json').write_text(json.dumps(entries,indent=2)+'\n',encoding='utf-8');print('Weather text glyphs ready:', ''.join(sys.argv[1:]))
