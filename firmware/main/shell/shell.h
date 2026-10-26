#pragma once
#include "lvgl.h"
void shell_init(void);
void shell_poll(void);
void shell_title(const char *title);
void shell_notice(const char *message);
void shell_desktop(lv_obj_t *root);
void shell_sleep(void);
/* UI-task only. Fullscreen apps provide their own exit/back controls. */
void shell_fullscreen(bool enabled);
