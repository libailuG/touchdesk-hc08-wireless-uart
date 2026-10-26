"""Generate small Noto fonts and an offline lunar calendar from HKO factual date tables.
Run with the bundled Python with Pillow. Never reads or operates hardware.
"""
from pathlib import Path
import datetime as dt
import re
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'main/apps/weather'
CACHE=ROOT/'assets/lunar-source'
CACHE.mkdir(exist_ok=True)
def download(year):
    path=CACHE/f'T{year}c.txt'
    if not path.exists():
        url=f'https://www.hko.gov.hk/tc/gts/time/calendar/text/files/T{year}c.txt'
        path.write_bytes(urllib.request.urlopen(url,timeout=30).read())
    return year,path.read_text(encoding='utf-8-sig')

rows=[]
with ThreadPoolExecutor(max_workers=4) as pool:
    for year,text in sorted(pool.map(download,range(2023,2037))):
        for line in text.splitlines():
            m=re.match(r'(\d{4})年(\d+)月(\d+)日\s+(\S+)',line)
            if not m: continue
            yy,mm,dd=map(int,m.group(1,2,3)); lunar=m.group(4)
            if lunar.endswith('月'):
                leap=lunar.startswith('閏'); lunar=lunar.removeprefix('閏')
                month=['正月','二月','三月','四月','五月','六月','七月','八月','九月','十月','十一月','十二月'].index(lunar)+1
                rows.append((yy*10000+mm*100+dd,dt.date(yy,mm,dd).toordinal(),month,leap))
assert len(rows)>160
source=['/* Offline date facts: Hong Kong Observatory Gregorian/Lunar conversion tables.',
        ' * Source: https://www.hko.gov.hk/tc/gts/time/conversion.htm',
        ' * Generated 2023-2036 month starts; UI supports Gregorian 2024-2035. */',
        '#include "weather.h"','#include <stdio.h>',
        'typedef struct { int date,ordinal; unsigned char month,leap; } lunar_start_t;',
        'static const lunar_start_t starts[]={']
source += [f'    {{{date},{ordinal},{month},{int(leap)}}},' for date,ordinal,month,leap in rows]
source+=['};',r'''
static int ordinal(int y,int m,int d)
{
    static const int before[]={0,31,59,90,120,151,181,212,243,273,304,334};
    int p=y-1;
    return p*365+p/4-p/100+p/400+before[m-1]+d+(m>2&&y%4==0&&(y%100!=0||y%400==0));
}
bool weather_lunar(int y,int m,int d,char *out,size_t size)
{
    static const char *months[]={"","正月","二月","三月","四月","五月","六月","七月","八月","九月","十月","冬月","腊月"};
    static const char *days[]={"","初一","初二","初三","初四","初五","初六","初七","初八","初九","初十","十一","十二","十三","十四","十五","十六","十七","十八","十九","二十","廿一","廿二","廿三","廿四","廿五","廿六","廿七","廿八","廿九","三十"};
    if(!out||size==0) return false;
    out[0]=0;
    if(y<2024||y>2035||m<1||m>12||d<1||d>31) return false;
    int key=y*10000+m*100+d;
    for(int i=(int)(sizeof(starts)/sizeof(starts[0]))-1;i>=0;i--) if(starts[i].date<=key) {
        int day=ordinal(y,m,d)-starts[i].ordinal+1;
        if(day<1||day>30) return false;
        snprintf(out,size,"%s%s%s",starts[i].leap?"闰":"",months[starts[i].month],days[day]);
        return true;
    }
    return false;
}
''']
(OUT/'weather_calendar.c').write_text('\n'.join(source),encoding='utf-8')

chars=set(chr(i) for i in range(32,127))|{'°','·','—','℃','‹','›','✓'}
for path in (ROOT/'main').rglob('*.c'):
    if path.name!='weather_fonts.c': chars.update(c for c in path.read_text(encoding='utf-8') if ord(c)>=160)
font_path=ROOT/'assets/NotoSansCJKsc-Regular.otf'
fonts=[]
def generate(name,size,characters,bold=0):
    font=ImageFont.truetype(str(font_path),size)
    data=[]; desc=['{0},']; unicode=[]
    for char in sorted(characters,key=ord):
        l,t,r,b=font.getbbox(char,anchor='ls',stroke_width=bold)
        w,h=r-l,b-t
        image=Image.new('L',(max(1,w),max(1,h)))
        ImageDraw.Draw(image).text((-l,-t),char,font=font,anchor='ls',fill=255,stroke_width=bold,stroke_fill=255)
        pix=[round(v/17) for v in image.tobytes()] if w*h else []
        index=len(data)
        for i in range(0,len(pix),2): data.append((pix[i]<<4)|(pix[i+1] if i+1<len(pix) else 0))
        desc.append(f'{{.bitmap_index={index},.adv_w={round(font.getlength(char)*16)},.box_w={w},.box_h={h},.ofs_x={l},.ofs_y={-b}}},')
        unicode.append(str(ord(char)-32))
    text=[f'static const uint8_t {name}_bitmap[]={{']
    text += [','.join(map(str,data[i:i+24]))+',' for i in range(0,len(data),24)]
    text += ['};',f'static const lv_font_fmt_txt_glyph_dsc_t {name}_glyphs[]={{',*desc,'};',
        f'static const uint16_t {name}_unicode[]={{'+','.join(unicode)+'};',
        f'static const lv_font_fmt_txt_cmap_t {name}_cmap[]={{'+f'{{.range_start=32,.range_length={max(map(ord,characters))-31},.glyph_id_start=1,.unicode_list={name}_unicode,.list_length={len(characters)},.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}}'+'};',
        f'static lv_font_fmt_txt_glyph_cache_t {name}_cache;',
        f'static lv_font_fmt_txt_dsc_t {name}_dsc={{.glyph_bitmap={name}_bitmap,.glyph_dsc={name}_glyphs,.cmaps={name}_cmap,.cmap_num=1,.bpp=4,.cache=&{name}_cache}};',
        f'const lv_font_t {name}={{.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height={size+6},.base_line=4,.dsc=&{name}_dsc}};']
    fonts.extend(text)
generate('font_weather12',12,chars)
generate('font_weather13',13,chars)
generate('font_weather16',16,chars)
generate('font_weather52',52,set('0123456789-°'))
generate('font_weather72',72,set('0123456789-:'),1)
(OUT/'weather_fonts.c').write_text('/* Generated from Noto Sans CJK; SIL OFL 1.1, assets/OFL.txt. */\n#include "lvgl.h"\n'+'\n'.join(fonts)+'\n',encoding='utf-8')
print(f'Generated lunar month starts: {len(rows)}; UI characters: {len(chars)}')
