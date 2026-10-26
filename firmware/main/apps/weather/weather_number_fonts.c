#include "resource_store.h"
#include "resource_manifest.h"
/* Generated OFL numeric fonts; source/license: assets/appearance-fonts/. */
#include "lvgl.h"

static const lv_font_fmt_txt_glyph_dsc_t font_paper72_glyphs[]={
{0},
{.bitmap_index=0,.adv_w=423,.box_w=26,.box_h=27,.ofs_x=0,.ofs_y=0},
{.bitmap_index=351,.adv_w=677,.box_w=42,.box_h=54,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=1485,.adv_w=466,.box_w=29,.box_h=52,.ofs_x=0,.ofs_y=0},
{.bitmap_index=2239,.adv_w=621,.box_w=39,.box_h=53,.ofs_x=0,.ofs_y=0},
{.bitmap_index=3273,.adv_w=607,.box_w=38,.box_h=53,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=4280,.adv_w=635,.box_w=40,.box_h=52,.ofs_x=0,.ofs_y=0},
{.bitmap_index=5320,.adv_w=607,.box_w=38,.box_h=53,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=6327,.adv_w=635,.box_w=40,.box_h=54,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=7407,.adv_w=564,.box_w=35,.box_h=53,.ofs_x=0,.ofs_y=0},
{.bitmap_index=8335,.adv_w=635,.box_w=40,.box_h=54,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=9415,.adv_w=635,.box_w=40,.box_h=54,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=10495,.adv_w=367,.box_w=23,.box_h=34,.ofs_x=0,.ofs_y=0},
};
static const uint16_t font_paper72_unicode[]={13,16,17,18,19,20,21,22,23,24,25,26};
static const lv_font_fmt_txt_cmap_t font_paper72_map[]={{.range_start=32,.range_length=27,.glyph_id_start=1,.unicode_list=font_paper72_unicode,.list_length=12,.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}};
static lv_font_fmt_txt_glyph_cache_t font_paper72_cache;
static lv_font_fmt_txt_dsc_t font_paper72_dsc={.glyph_bitmap=NULL,.glyph_dsc=font_paper72_glyphs,.cmaps=font_paper72_map,.cmap_num=1,.bpp=4,.cache=&font_paper72_cache};
const lv_font_t font_paper72={.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height=78,.base_line=4,.dsc=&font_paper72_dsc};

static const lv_font_fmt_txt_glyph_dsc_t font_paper52_glyphs[]={
{0},
{.bitmap_index=0,.adv_w=333,.box_w=21,.box_h=19,.ofs_x=0,.ofs_y=0},
{.bitmap_index=200,.adv_w=529,.box_w=33,.box_h=39,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=844,.adv_w=348,.box_w=22,.box_h=38,.ofs_x=0,.ofs_y=0},
{.bitmap_index=1262,.adv_w=484,.box_w=30,.box_h=38,.ofs_x=0,.ofs_y=0},
{.bitmap_index=1832,.adv_w=469,.box_w=29,.box_h=38,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=2383,.adv_w=484,.box_w=30,.box_h=38,.ofs_x=0,.ofs_y=0},
{.bitmap_index=2953,.adv_w=469,.box_w=29,.box_h=38,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=3504,.adv_w=499,.box_w=31,.box_h=39,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=4109,.adv_w=424,.box_w=26,.box_h=38,.ofs_x=0,.ofs_y=0},
{.bitmap_index=4603,.adv_w=484,.box_w=30,.box_h=39,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=5188,.adv_w=499,.box_w=31,.box_h=39,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=5793,.adv_w=363,.box_w=23,.box_h=39,.ofs_x=0,.ofs_y=0},
};
static const uint16_t font_paper52_unicode[]={13,16,17,18,19,20,21,22,23,24,25,144};
static const lv_font_fmt_txt_cmap_t font_paper52_map[]={{.range_start=32,.range_length=145,.glyph_id_start=1,.unicode_list=font_paper52_unicode,.list_length=12,.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}};
static lv_font_fmt_txt_glyph_cache_t font_paper52_cache;
static lv_font_fmt_txt_dsc_t font_paper52_dsc={.glyph_bitmap=NULL,.glyph_dsc=font_paper52_glyphs,.cmaps=font_paper52_map,.cmap_num=1,.bpp=4,.cache=&font_paper52_cache};
const lv_font_t font_paper52={.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height=58,.base_line=4,.dsc=&font_paper52_dsc};

static const lv_font_fmt_txt_glyph_dsc_t font_blue72_glyphs[]={
{0},
{.bitmap_index=0,.adv_w=368,.box_w=23,.box_h=26,.ofs_x=0,.ofs_y=0},
{.bitmap_index=299,.adv_w=512,.box_w=32,.box_h=53,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=1147,.adv_w=320,.box_w=20,.box_h=51,.ofs_x=0,.ofs_y=0},
{.bitmap_index=1657,.adv_w=496,.box_w=31,.box_h=52,.ofs_x=0,.ofs_y=0},
{.bitmap_index=2463,.adv_w=496,.box_w=31,.box_h=52,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=3269,.adv_w=528,.box_w=33,.box_h=51,.ofs_x=0,.ofs_y=0},
{.bitmap_index=4111,.adv_w=496,.box_w=31,.box_h=52,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=4917,.adv_w=496,.box_w=31,.box_h=53,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=5739,.adv_w=448,.box_w=28,.box_h=51,.ofs_x=0,.ofs_y=0},
{.bitmap_index=6453,.adv_w=496,.box_w=31,.box_h=53,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=7275,.adv_w=480,.box_w=30,.box_h=53,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=8070,.adv_w=288,.box_w=18,.box_h=35,.ofs_x=0,.ofs_y=0},
};
static const uint16_t font_blue72_unicode[]={13,16,17,18,19,20,21,22,23,24,25,26};
static const lv_font_fmt_txt_cmap_t font_blue72_map[]={{.range_start=32,.range_length=27,.glyph_id_start=1,.unicode_list=font_blue72_unicode,.list_length=12,.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}};
static lv_font_fmt_txt_glyph_cache_t font_blue72_cache;
static lv_font_fmt_txt_dsc_t font_blue72_dsc={.glyph_bitmap=NULL,.glyph_dsc=font_blue72_glyphs,.cmaps=font_blue72_map,.cmap_num=1,.bpp=4,.cache=&font_blue72_cache};
const lv_font_t font_blue72={.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height=78,.base_line=4,.dsc=&font_blue72_dsc};

static const lv_font_fmt_txt_glyph_dsc_t font_blue52_glyphs[]={
{0},
{.bitmap_index=0,.adv_w=272,.box_w=17,.box_h=19,.ofs_x=0,.ofs_y=0},
{.bitmap_index=162,.adv_w=368,.box_w=23,.box_h=36,.ofs_x=0,.ofs_y=0},
{.bitmap_index=576,.adv_w=224,.box_w=14,.box_h=36,.ofs_x=0,.ofs_y=0},
{.bitmap_index=828,.adv_w=352,.box_w=22,.box_h=36,.ofs_x=0,.ofs_y=0},
{.bitmap_index=1224,.adv_w=352,.box_w=22,.box_h=36,.ofs_x=0,.ofs_y=0},
{.bitmap_index=1620,.adv_w=384,.box_w=24,.box_h=36,.ofs_x=0,.ofs_y=0},
{.bitmap_index=2052,.adv_w=352,.box_w=22,.box_h=36,.ofs_x=0,.ofs_y=0},
{.bitmap_index=2448,.adv_w=352,.box_w=22,.box_h=36,.ofs_x=0,.ofs_y=0},
{.bitmap_index=2844,.adv_w=320,.box_w=20,.box_h=36,.ofs_x=0,.ofs_y=0},
{.bitmap_index=3204,.adv_w=368,.box_w=23,.box_h=36,.ofs_x=0,.ofs_y=0},
{.bitmap_index=3618,.adv_w=352,.box_w=22,.box_h=36,.ofs_x=0,.ofs_y=0},
{.bitmap_index=4014,.adv_w=304,.box_w=19,.box_h=42,.ofs_x=0,.ofs_y=0},
};
static const uint16_t font_blue52_unicode[]={13,16,17,18,19,20,21,22,23,24,25,144};
static const lv_font_fmt_txt_cmap_t font_blue52_map[]={{.range_start=32,.range_length=145,.glyph_id_start=1,.unicode_list=font_blue52_unicode,.list_length=12,.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}};
static lv_font_fmt_txt_glyph_cache_t font_blue52_cache;
static lv_font_fmt_txt_dsc_t font_blue52_dsc={.glyph_bitmap=NULL,.glyph_dsc=font_blue52_glyphs,.cmaps=font_blue52_map,.cmap_num=1,.bpp=4,.cache=&font_blue52_cache};
const lv_font_t font_blue52={.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height=58,.base_line=4,.dsc=&font_blue52_dsc};

static const lv_font_fmt_txt_glyph_dsc_t font_gold72_glyphs[]={
{0},
{.bitmap_index=0,.adv_w=528,.box_w=33,.box_h=23,.ofs_x=0,.ofs_y=0},
{.bitmap_index=380,.adv_w=768,.box_w=48,.box_h=53,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=1652,.adv_w=432,.box_w=27,.box_h=51,.ofs_x=0,.ofs_y=0},
{.bitmap_index=2341,.adv_w=640,.box_w=40,.box_h=52,.ofs_x=0,.ofs_y=0},
{.bitmap_index=3381,.adv_w=624,.box_w=39,.box_h=53,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=4415,.adv_w=704,.box_w=44,.box_h=51,.ofs_x=0,.ofs_y=0},
{.bitmap_index=5537,.adv_w=640,.box_w=40,.box_h=53,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=6597,.adv_w=656,.box_w=41,.box_h=53,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=7684,.adv_w=608,.box_w=38,.box_h=51,.ofs_x=0,.ofs_y=0},
{.bitmap_index=8653,.adv_w=640,.box_w=40,.box_h=53,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=9713,.adv_w=656,.box_w=41,.box_h=52,.ofs_x=0,.ofs_y=0},
{.bitmap_index=10779,.adv_w=320,.box_w=20,.box_h=35,.ofs_x=0,.ofs_y=-1},
};
static const uint16_t font_gold72_unicode[]={13,16,17,18,19,20,21,22,23,24,25,26};
static const lv_font_fmt_txt_cmap_t font_gold72_map[]={{.range_start=32,.range_length=27,.glyph_id_start=1,.unicode_list=font_gold72_unicode,.list_length=12,.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}};
static lv_font_fmt_txt_glyph_cache_t font_gold72_cache;
static lv_font_fmt_txt_dsc_t font_gold72_dsc={.glyph_bitmap=NULL,.glyph_dsc=font_gold72_glyphs,.cmaps=font_gold72_map,.cmap_num=1,.bpp=4,.cache=&font_gold72_cache};
const lv_font_t font_gold72={.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height=78,.base_line=4,.dsc=&font_gold72_dsc};

static const lv_font_fmt_txt_glyph_dsc_t font_gold52_glyphs[]={
{0},
{.bitmap_index=0,.adv_w=384,.box_w=24,.box_h=17,.ofs_x=0,.ofs_y=0},
{.bitmap_index=204,.adv_w=544,.box_w=34,.box_h=39,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=867,.adv_w=304,.box_w=19,.box_h=37,.ofs_x=0,.ofs_y=0},
{.bitmap_index=1219,.adv_w=448,.box_w=28,.box_h=38,.ofs_x=0,.ofs_y=0},
{.bitmap_index=1751,.adv_w=448,.box_w=28,.box_h=38,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=2283,.adv_w=496,.box_w=31,.box_h=37,.ofs_x=0,.ofs_y=0},
{.bitmap_index=2857,.adv_w=464,.box_w=29,.box_h=38,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=3408,.adv_w=480,.box_w=30,.box_h=38,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=3978,.adv_w=416,.box_w=26,.box_h=37,.ofs_x=0,.ofs_y=0},
{.bitmap_index=4459,.adv_w=464,.box_w=29,.box_h=39,.ofs_x=0,.ofs_y=-1},
{.bitmap_index=5025,.adv_w=480,.box_w=30,.box_h=38,.ofs_x=0,.ofs_y=0},
{.bitmap_index=5595,.adv_w=336,.box_w=21,.box_h=38,.ofs_x=0,.ofs_y=0},
};
static const uint16_t font_gold52_unicode[]={13,16,17,18,19,20,21,22,23,24,25,144};
static const lv_font_fmt_txt_cmap_t font_gold52_map[]={{.range_start=32,.range_length=145,.glyph_id_start=1,.unicode_list=font_gold52_unicode,.list_length=12,.type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}};
static lv_font_fmt_txt_glyph_cache_t font_gold52_cache;
static lv_font_fmt_txt_dsc_t font_gold52_dsc={.glyph_bitmap=NULL,.glyph_dsc=font_gold52_glyphs,.cmaps=font_gold52_map,.cmap_num=1,.bpp=4,.cache=&font_gold52_cache};
const lv_font_t font_gold52={.get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,.line_height=58,.base_line=4,.dsc=&font_gold52_dsc};

bool weather_number_fonts_bind_resources(void)
{
    font_paper72_dsc.glyph_bitmap=resource_store_data(RESOURCE_WEATHER_NUMBER_FONTS__FONT_PAPER72_BITMAP_OFFSET,RESOURCE_WEATHER_NUMBER_FONTS__FONT_PAPER72_BITMAP_SIZE);
    if(!font_paper72_dsc.glyph_bitmap) return false;
    font_paper52_dsc.glyph_bitmap=resource_store_data(RESOURCE_WEATHER_NUMBER_FONTS__FONT_PAPER52_BITMAP_OFFSET,RESOURCE_WEATHER_NUMBER_FONTS__FONT_PAPER52_BITMAP_SIZE);
    if(!font_paper52_dsc.glyph_bitmap) return false;
    font_blue72_dsc.glyph_bitmap=resource_store_data(RESOURCE_WEATHER_NUMBER_FONTS__FONT_BLUE72_BITMAP_OFFSET,RESOURCE_WEATHER_NUMBER_FONTS__FONT_BLUE72_BITMAP_SIZE);
    if(!font_blue72_dsc.glyph_bitmap) return false;
    font_blue52_dsc.glyph_bitmap=resource_store_data(RESOURCE_WEATHER_NUMBER_FONTS__FONT_BLUE52_BITMAP_OFFSET,RESOURCE_WEATHER_NUMBER_FONTS__FONT_BLUE52_BITMAP_SIZE);
    if(!font_blue52_dsc.glyph_bitmap) return false;
    font_gold72_dsc.glyph_bitmap=resource_store_data(RESOURCE_WEATHER_NUMBER_FONTS__FONT_GOLD72_BITMAP_OFFSET,RESOURCE_WEATHER_NUMBER_FONTS__FONT_GOLD72_BITMAP_SIZE);
    if(!font_gold72_dsc.glyph_bitmap) return false;
    font_gold52_dsc.glyph_bitmap=resource_store_data(RESOURCE_WEATHER_NUMBER_FONTS__FONT_GOLD52_BITMAP_OFFSET,RESOURCE_WEATHER_NUMBER_FONTS__FONT_GOLD52_BITMAP_SIZE);
    if(!font_gold52_dsc.glyph_bitmap) return false;
    return true;
}
