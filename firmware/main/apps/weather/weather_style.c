#include "weather_style.h"
#include <math.h>
#include <stdio.h>
LV_FONT_DECLARE(font_paper72);
LV_FONT_DECLARE(font_paper52);
LV_FONT_DECLARE(font_blue72);
LV_FONT_DECLARE(font_blue52);
LV_FONT_DECLARE(font_gold72);
LV_FONT_DECLARE(font_gold52);
static const weather_palette_t palettes[WEATHER_THEMES]={
    {0xf5f3ec,0x263e36,0x5d7065,0xd6ded5,0xe7ece5,0x407c67},
    {0xedf3f9,0x173a5b,0x526b83,0xcedce9,0xe0eaf4,0x2569b7},
    {0x182126,0xf6f0e3,0xb6bfb9,0x354147,0x253239,0xe8be7b}
};
static unsigned checked(unsigned theme) { return theme<WEATHER_THEMES?theme:WEATHER_DEFAULT_THEME; }
const weather_palette_t *weather_palette(unsigned theme) { return &palettes[checked(theme)]; }
const char *weather_theme_name(unsigned theme)
{
    static const char *names[]={"纸白墨绿","雾蓝科技","曜石暖金"};
    return names[checked(theme)];
}
/* Tabular clock metrics share the original bitmaps; temperature fonts stay proportional. */
typedef struct {
    lv_font_t font;
    const lv_font_t *source,*colon_source;
    uint16_t digit_width,colon_width;
} clock_font_t;
static clock_font_t clock_fonts[2][WEATHER_THEMES];
static bool clock_glyph(const lv_font_t *font,lv_font_glyph_dsc_t *glyph,uint32_t letter,uint32_t next)
{
    const clock_font_t *clock=font->dsc;
    const lv_font_t *source=letter==':'?clock->colon_source:clock->source;
    if(!source->get_glyph_dsc(source,glyph,letter,0)) return false;
    if((letter>='0'&&letter<='9')||letter=='-') {
        glyph->adv_w=clock->digit_width;
        glyph->ofs_x=((int)clock->digit_width-(int)glyph->box_w)/2;
    }
    if(letter==':') {
        glyph->adv_w=clock->colon_width;
        glyph->ofs_x=((int)clock->colon_width-(int)glyph->box_w)/2;
    }
    return true;
}
static const uint8_t *clock_bitmap(const lv_font_t *font,uint32_t letter)
{
    const clock_font_t *clock=font->dsc;
    const lv_font_t *source=letter==':'?clock->colon_source:clock->source;
    return source->get_glyph_bitmap(source,letter);
}
static const lv_font_t *tabular_clock_font(unsigned theme,bool seconds)
{
    const lv_font_t *large[]={&font_paper72,&font_blue72,&font_gold72};
    clock_font_t *clock=&clock_fonts[seconds?1:0][checked(theme)];
    if(!clock->source) {
        clock->source=large[checked(theme)];
        clock->colon_source=large[checked(theme)];
        clock->font=*clock->source;
        clock->font.dsc=clock;
        clock->font.get_glyph_dsc=clock_glyph;
        clock->font.get_glyph_bitmap=clock_bitmap;
        for(uint32_t digit='0';digit<='9';digit++) {
            lv_font_glyph_dsc_t glyph;
            if(clock->source->get_glyph_dsc(clock->source,&glyph,digit,0)) {
                unsigned width=glyph.adv_w>glyph.box_w?glyph.adv_w:glyph.box_w;
                if(width>clock->digit_width) clock->digit_width=width;
            }
        }
        lv_font_glyph_dsc_t colon;
        if(clock->source->get_glyph_dsc(clock->source,&colon,':',0)) {
            clock->colon_width=colon.adv_w;
            /* Keep the original glyphs, tightening only separator spacing when needed. */
            unsigned available=(320-6*clock->digit_width)/2;
            if(seconds&&clock->colon_width>available) clock->colon_width=available;
        }
    }
    return &clock->font;
}
const lv_font_t *weather_clock_font(unsigned theme) { return tabular_clock_font(theme,false); }
const lv_font_t *weather_clock_seconds_font(unsigned theme) { return tabular_clock_font(theme,true); }
const lv_font_t *weather_temperature_font(unsigned theme)
{
    const lv_font_t *fonts[]={&font_paper52,&font_blue52,&font_gold52}; return fonts[checked(theme)];
}
lv_color_t weather_value_color(unsigned theme,bool dynamic,weather_metric_t metric,float value)
{
    const weather_palette_t *p=weather_palette(theme);
    if(!dynamic||!isfinite(value)) return lv_color_hex(p->ink);
    unsigned category=0;
    /* Visual ranges only, not weather-warning thresholds. 0 green, 1 blue, 2 amber, 3 red. */
    if(metric==WEATHER_TEMPERATURE) category=value<10?1:value<28?0:value<35?2:3;
    else if(metric==WEATHER_HUMIDITY) category=value<40?2:value<=70?0:1;
    else if(metric==WEATHER_WIND) category=value<3?0:value<6?1:value<10?2:3;
    static const uint32_t light[]={0x26704e,0x2067ad,0xa45116,0xb33d39};
    static const uint32_t dark[]={0x84d5aa,0x85bfff,0xf2be73,0xff9b91};
    return lv_color_hex(checked(theme)==2?dark[category]:light[category]);
}
void weather_color_number(char *out,size_t size,unsigned theme,bool dynamic,weather_metric_t metric,float value,unsigned decimals)
{
    lv_color_t color=weather_value_color(theme,dynamic,metric,value);
    uint32_t hex=lv_color_to32(color)&0xffffffu;
    snprintf(out,size,"#%06lx %.*f#",(unsigned long)hex,decimals?1:0,(double)value);
}
