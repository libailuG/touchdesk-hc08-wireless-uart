#pragma once
#include "lvgl.h"
LV_FONT_DECLARE(font_ui);
void ui_set_theme(unsigned theme);
unsigned ui_theme(void);
bool ui_dark_theme(void);
lv_color_t ui_foreground(void);
lv_color_t ui_surface(void);
lv_color_t ui_background(void);
lv_color_t ui_accent_foreground(void);
void ui_reset_page(lv_obj_t *root);
void ui_style_button(lv_obj_t *button);
lv_obj_t *ui_label(lv_obj_t *root,const char *text,int x,int y,int width);
lv_obj_t *ui_button(lv_obj_t *root,const char *text,int x,int y,int w,int h,
                    lv_event_cb_t cb,void *user);
