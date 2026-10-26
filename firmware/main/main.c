#include <stdlib.h>
#include <time.h>
#include "board.h"
#include "app_serial.h"
#include "app.h"
#include "shell.h"
#include "settings.h"
#include "usb_service.h"
#include "usb_app_mode.h"
#include "resource_store.h"
#include "music_service.h"
#include "wifi_service.h"
#include "xiaozhi_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "keymouse_service.h"
void app_main(void)
{
    /* PSRAM is initialized by IDF startup before the LVGL allocator is used. */
    lv_init(); board_init();
    esp_err_t resources_err=resource_store_init();
    if(resources_err!=ESP_OK) {
        ESP_LOGE("system","Resources unavailable: %s",esp_err_to_name(resources_err));
        lv_obj_t *error=lv_label_create(lv_scr_act());
        lv_label_set_text(error,"Resource data error.\nFlash the complete firmware\nincluding resources.bin.");
        lv_obj_set_width(error,300); lv_obj_center(error);
        for(;;) { lv_timer_handler(); vTaskDelay(pdMS_TO_TICKS(10)); }
    }
    board_storage_probe();
    esp_err_t settings_err=settings_init();
    esp_err_t usb_err=uart_console_init();
    setenv("TZ","CST-8",1);tzset();
    shell_init();
    app_process();
    if(settings_err!=ESP_OK) {
        ESP_LOGE("system","NVS unavailable: %s",esp_err_to_name(settings_err));
        shell_notice("设置不可用，使用默认值");
    }
    if(usb_err!=ESP_OK) {
        ESP_LOGE("system","UART console unavailable: %s",esp_err_to_name(usb_err));
        shell_notice("串口诊断初始化失败");
    }
    esp_reset_reason_t reset=esp_reset_reason();
    if(reset==ESP_RST_PANIC || reset==ESP_RST_TASK_WDT || reset==ESP_RST_INT_WDT || reset==ESP_RST_WDT)
        shell_notice("上次运行异常，已返回桌面");
    int64_t healthy_since=esp_timer_get_time();
    bool confirmed=false;
    for(;;) {
        if(xiaozhi_take_ui_request()) { extern const app_desc_t app_xiaozhi; app_request(&app_xiaozhi); }
        if(microphone_take_ui_request()) { extern const app_desc_t app_microphone; app_request(&app_microphone); }
        if(music_take_ui_request()) { extern const app_desc_t app_music; app_request(&app_music); }
        lv_timer_handler();
        shell_poll();
        serial_app_poll();
        app_process();
        usb_app_mode_poll();
        music_role_poll();
        keymouse_poll();
        if(!confirmed && esp_timer_get_time()-healthy_since>10000000) {
            const esp_partition_t *running=esp_ota_get_running_partition();
            esp_ota_img_states_t state;
            if(esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY) {
                if(settings_err==ESP_OK && usb_err==ESP_OK) ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
                else ESP_ERROR_CHECK(esp_ota_mark_app_invalid_rollback_and_reboot());
            }
            confirmed=true;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
