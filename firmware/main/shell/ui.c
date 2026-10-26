#include "ui.h"
#include "weather_style.h"
static unsigned selected_theme=WEATHER_DEFAULT_THEME;
static lv_theme_t palette_theme;
static lv_disp_t *themed_display;
static unsigned applied_theme=WEATHER_THEMES;
static bool styles_ready;
static lv_style_t surface_style,pressed_style,accent_style,track_style,grid_style;
static void palette_apply(lv_theme_t *theme,lv_obj_t *obj)
{
    (void)theme;
    if(lv_obj_check_type(obj,&lv_dropdown_class)||lv_obj_check_type(obj,&lv_dropdownlist_class)||
       lv_obj_check_type(obj,&lv_textarea_class)||lv_obj_check_type(obj,&lv_chart_class)||
       lv_obj_check_type(obj,&lv_btn_class)) lv_obj_add_style(obj,&surface_style,LV_PART_MAIN);
    if(lv_obj_check_type(obj,&lv_btn_class)) {
        lv_obj_add_style(obj,&pressed_style,LV_STATE_PRESSED);
        lv_obj_add_style(obj,&accent_style,LV_STATE_CHECKED);
    }
    if(lv_obj_check_type(obj,&lv_dropdownlist_class)) {
        lv_obj_add_style(obj,&pressed_style,LV_PART_SELECTED);
        lv_obj_add_style(obj,&accent_style,LV_PART_SELECTED|LV_STATE_CHECKED);
        lv_obj_add_style(obj,&accent_style,LV_PART_SELECTED|LV_STATE_PRESSED);
    }
    if(lv_obj_check_type(obj,&lv_textarea_class)) {
        lv_obj_add_style(obj,&accent_style,LV_PART_SELECTED);
        lv_obj_add_style(obj,&grid_style,LV_PART_CURSOR);
    }
    if(lv_obj_check_type(obj,&lv_chart_class))lv_obj_add_style(obj,&grid_style,LV_PART_MAIN);
    if(lv_obj_check_type(obj,&lv_btnmatrix_class)) {
        lv_obj_add_style(obj,&surface_style,LV_PART_MAIN);
        lv_obj_add_style(obj,&surface_style,LV_PART_ITEMS);
        lv_obj_add_style(obj,&pressed_style,LV_PART_ITEMS|LV_STATE_PRESSED);
        lv_obj_add_style(obj,&accent_style,LV_PART_ITEMS|LV_STATE_CHECKED);
    }
    if(lv_obj_check_type(obj,&lv_slider_class)||lv_obj_check_type(obj,&lv_bar_class)) {
        lv_obj_add_style(obj,&track_style,LV_PART_MAIN);
        lv_obj_add_style(obj,&accent_style,LV_PART_INDICATOR);
        lv_obj_add_style(obj,&accent_style,LV_PART_KNOB);
    }
    if(lv_obj_check_type(obj,&lv_switch_class)) {
        lv_obj_add_style(obj,&track_style,LV_PART_MAIN);
        lv_obj_add_style(obj,&accent_style,LV_PART_INDICATOR|LV_STATE_CHECKED);
        lv_obj_add_style(obj,&surface_style,LV_PART_KNOB);
    }
}
void ui_set_theme(unsigned theme)
{
    selected_theme=theme<WEATHER_THEMES?theme:WEATHER_DEFAULT_THEME;
    lv_disp_t *display=lv_disp_get_default();
    if(!display)return;
    if(themed_display==display&&applied_theme==selected_theme)return;
    if(!styles_ready) {
        lv_style_init(&surface_style);lv_style_init(&pressed_style);lv_style_init(&accent_style);
        lv_style_init(&track_style);lv_style_init(&grid_style);styles_ready=true;
    }
    const weather_palette_t *p=weather_palette(selected_theme);
    lv_style_set_bg_color(&surface_style,lv_color_hex(p->surface));
    lv_style_set_text_color(&surface_style,lv_color_hex(p->ink));
    lv_style_set_border_color(&surface_style,lv_color_hex(p->line));
    lv_style_set_bg_color(&pressed_style,lv_color_hex(p->line));
    lv_style_set_text_color(&pressed_style,lv_color_hex(p->ink));
    lv_style_set_bg_color(&accent_style,lv_color_hex(p->accent));
    lv_style_set_text_color(&accent_style,ui_accent_foreground());
    lv_style_set_bg_color(&track_style,lv_color_hex(p->line));
    lv_style_set_line_color(&grid_style,lv_color_hex(p->line));
    lv_style_set_border_color(&grid_style,lv_color_hex(p->accent));
    lv_theme_t *base=lv_theme_default_init(display,lv_color_hex(p->accent),lv_color_hex(p->accent),ui_dark_theme(),LV_FONT_DEFAULT);
    palette_theme=*base;
    lv_theme_set_parent(&palette_theme,base);lv_theme_set_apply_cb(&palette_theme,palette_apply);
    lv_disp_set_theme(display,&palette_theme);
    themed_display=display;applied_theme=selected_theme;
    lv_obj_report_style_change(NULL);
}
unsigned ui_theme(void) { return selected_theme; }
bool ui_dark_theme(void) { return selected_theme==2; }
lv_color_t ui_foreground(void) { return lv_color_hex(weather_palette(selected_theme)->ink); }
lv_color_t ui_surface(void) { return lv_color_hex(weather_palette(selected_theme)->surface); }
lv_color_t ui_background(void) { return lv_color_hex(weather_palette(selected_theme)->bg); }
lv_color_t ui_accent_foreground(void) { return lv_color_hex(ui_dark_theme()?0x182126:0xffffff); }
void ui_reset_page(lv_obj_t *root)
{
    /* Position and size are local LVGL style properties too. Recolor the
     * reused page without erasing shell_fullscreen/shell_desktop geometry. */
    lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root,ui_background(),0);
    lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(root,ui_foreground(),0);
    lv_obj_set_style_text_font(root,&font_ui,0);
}
lv_obj_t *ui_label(lv_obj_t *root,const char *text,int x,int y,int width)
{
    lv_obj_t *l=lv_label_create(root);
    lv_obj_set_pos(l,x,y); lv_obj_set_width(l,width);
    lv_label_set_text(l,text);
    return l;
}
void ui_style_button(lv_obj_t *b)
{
    lv_obj_set_style_bg_color(b,ui_surface(),0);
    lv_obj_set_style_text_color(b,ui_foreground(),0);
    lv_obj_set_style_bg_color(b,lv_color_hex(weather_palette(selected_theme)->line),LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b,ui_foreground(),LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b,lv_color_hex(weather_palette(selected_theme)->accent),LV_STATE_PRESSED);
    lv_obj_set_style_border_width(b,2,LV_STATE_PRESSED);
}
lv_obj_t *ui_button(lv_obj_t *root,const char *text,int x,int y,int w,int h,
                    lv_event_cb_t cb,void *user)
{
    lv_obj_t *b=lv_btn_create(root);
    lv_obj_set_pos(b,x,y); lv_obj_set_size(b,w,h);
    lv_obj_clear_flag(b,LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_style_radius(b,12,0);
    ui_style_button(b);
    lv_obj_t *l=lv_label_create(b); lv_label_set_text(l,text);
    lv_obj_set_width(l,w-12); lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0); lv_obj_center(l);
    if(cb) lv_obj_add_event_cb(b,cb,LV_EVENT_ALL,user);
    return b;
}
