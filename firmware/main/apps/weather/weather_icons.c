/* Soft 3D weather artwork shared by desktop, current, hourly and daily forecasts. */
#include "weather_icons.h"
#include "weather_art.h"

static unsigned artwork(int c,bool day)
{
    if(c==0) return day?0:1;
    if(c==1||c==2) return day?2:3;
    if(c==3) return 4;
    if(c==45||c==48) return 5;
    if(c==51||c==53||c==55) return 6;
    if(c==56||c==57||c==66||c==67) return 14;
    if(c==61) return 7;
    if(c==63) return 12;
    if(c==80||c==81||c==82) return day?13:15;
    if(c==65) return 8;
    if(c==95||c==96||c==97||c==99) return 9;
    if(c==71||c==73||c==75||c==77||c==85||c==86) return 10;
    return 11;
}
static void draw(lv_event_t *e)
{
    lv_obj_t *obj=lv_event_get_target(e);
    const lv_img_dsc_t *image=lv_event_get_user_data(e);
    lv_area_t a; lv_obj_get_coords(obj,&a);
    int size=lv_obj_get_width(obj);
    lv_draw_img_dsc_t d; lv_draw_img_dsc_init(&d);
    d.pivot=(lv_point_t){0,0}; d.zoom=(size*256+image->header.w/2)/image->header.w;
    d.antialias=true;
    /* Source dimensions describe the pixel map; zoom gives the on-screen dimensions. */
    a.x2=a.x1+image->header.w-1; a.y2=a.y1+image->header.h-1;
    lv_draw_img(lv_event_get_draw_ctx(e),&d,&a,image);
}
lv_obj_t *weather_icon(lv_obj_t *parent,int x,int y,int size,int code,bool day,lv_color_t accent,lv_color_t background)
{
    (void)accent; (void)background;
    unsigned index=artwork(code,day);
    const lv_img_dsc_t *image=size>56?weather_art_196[index]:weather_art_56[index];
    lv_obj_t *obj=lv_obj_create(parent); lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,size,size);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj,draw,LV_EVENT_DRAW_MAIN,(void*)image);
    return obj;
}
