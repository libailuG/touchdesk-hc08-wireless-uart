/* Board adaptation from Waiken-Smart 01_lvgl_transplant: LCD pinout,
 * ST7796 panel driver and GT9xxx register protocol. See docs/DRIVER_ORIGIN.md. */
#include "board.h"
#include "input_guard.h"
#include <string.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_timer.h"
#include "st7796.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#define CHECK(x) ESP_ERROR_CHECK(x)
static i2c_master_dev_handle_t expander, touch;
static esp_lcd_panel_handle_t panel;
static uint16_t outputs;
static SemaphoreHandle_t output_lock;
static lv_disp_drv_t display;
static lv_color_t *rotation_dma;
static void rotation_rounder(lv_disp_drv_t *drv,lv_area_t *area) {
 (void)drv;area->x1&=~1;area->x2|=1;
}
esp_err_t board_display_landscape(bool enabled) {
 lv_disp_t *d=lv_disp_get_default();if(!d)return ESP_ERR_INVALID_STATE;
 TickType_t start=xTaskGetTickCount();
 while(d->driver->draw_buf->flushing){if(xTaskGetTickCount()-start>pdMS_TO_TICKS(250))return ESP_ERR_TIMEOUT;vTaskDelay(1);}
 if(enabled&&!rotation_dma){rotation_dma=heap_caps_aligned_alloc(4,LV_DISP_ROT_MAX_BUF,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);if(!rotation_dma)return ESP_ERR_NO_MEM;}
 board_input_cancel();d->driver->sw_rotate=1;d->driver->rounder_cb=enabled?rotation_rounder:NULL;lv_disp_set_rotation(d,enabled?LV_DISP_ROT_90:LV_DISP_ROT_NONE);
 if(!enabled){heap_caps_free(rotation_dma);rotation_dma=NULL;}
 return ESP_OK;
}
static int64_t last_touch_frame;
static bool pressed;
/* Reject a whole ambiguous contact sequence until an explicit zero-contact
 * frame, including multi-touch, invalid coordinates and controller faults. */
static bool touch_needs_release;
static unsigned touch_fault_epoch;
unsigned board_input_fault_epoch(void) { return touch_fault_epoch; }
static lv_point_t point;
static input_guard_t input_guard;
static bool backlight_on=true;
static int64_t last_activity;
static char product_id[5];
esp_err_t board_backlight(bool enabled)
{
    xSemaphoreTake(output_lock,portMAX_DELAY);
    uint16_t next=enabled ? outputs|8 : outputs&~8;
    uint8_t b[]={2,next&255,next>>8};
    esp_err_t err=i2c_master_transmit(expander,b,sizeof(b),100);
    if(err==ESP_OK) { outputs=next; backlight_on=enabled; }
    xSemaphoreGive(output_lock); return err;
}
esp_err_t board_sd_select(bool selected)
{
    xSemaphoreTake(output_lock,portMAX_DELAY);
    uint16_t next=selected?outputs&~4u:outputs|4u;
    uint8_t b[]={2,next&255,next>>8};
    esp_err_t err=i2c_master_transmit(expander,b,sizeof(b),100);
    if(err==ESP_OK) outputs=next;
    xSemaphoreGive(output_lock); return err;
}
void board_input_cancel(void)
{
    input_guard_cancel(&input_guard);
    lv_indev_reset(NULL,NULL);
}
bool board_touch_down(void) { return pressed; }
int64_t board_idle_us(void) { return esp_timer_get_time()-last_activity; }
const char *board_touch_id(void) { return product_id; }
/* Called only from app_main/LVGL task: one owner for the output latch. */
static void output_set(uint16_t mask, bool high)
{
    uint16_t next = high ? outputs | mask : outputs & ~mask;
    uint8_t b[] = {2, next & 255, next >> 8};
    CHECK(i2c_master_transmit(expander,b,sizeof(b),100));
    outputs = next; // update shadow only after successful write
}
static esp_err_t touch_read(uint16_t reg, uint8_t *buf, size_t n)
{
    uint8_t addr[] = {reg >> 8, reg & 255};
    return i2c_master_transmit_receive(touch,addr,2,buf,n,30);
}
static esp_err_t touch_write(uint16_t reg, uint8_t value)
{
    uint8_t b[] = {reg >> 8, reg & 255, value};
    return i2c_master_transmit(touch,b,3,30);
}
static void touch_sample(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    bool was_blocked=touch_needs_release;
    uint8_t status=0,xy[4];
    esp_err_t err=touch_read(0x814e,&status,1);
    if(err!=ESP_OK) { pressed=false; touch_needs_release=true; }
    else if(status&0x80) {
        uint8_t count=status&15;
        pressed=false;
        if(count==0) touch_needs_release=false;
        else if(count!=1) touch_needs_release=true;
        if(count==1 && touch_read(0x8150,xy,4)==ESP_OK) {
            unsigned x=xy[0]|(xy[1]<<8), y=xy[2]|(xy[3]<<8);
            if(x<320 && y<480) { point.x=x;point.y=y;pressed=!touch_needs_release; }
            else touch_needs_release=true;
        }
        else if(count==1) touch_needs_release=true;
        if(touch_write(0x814e,0)!=ESP_OK) { pressed=false; touch_needs_release=true; }
        last_touch_frame=esp_timer_get_time();
    }
    /* Goodix need not publish new frames while idle. Only a stale active
     * contact is a fault; latching an idle timeout would swallow the next tap. */
    if(pressed && esp_timer_get_time()-last_touch_frame>150000) { pressed=false; touch_needs_release=true; }
    if(!was_blocked && touch_needs_release) touch_fault_epoch++;
    if(pressed) last_activity=esp_timer_get_time();
    data->point=point;
    data->state=input_guard_filter(&input_guard,pressed,backlight_on)?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;
}
static bool transfer_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    lv_disp_flush_ready((lv_disp_drv_t*)ctx);
    return false;
}
static void flush(lv_disp_drv_t *drv,const lv_area_t *a,lv_color_t *pixels)
{
    /* LVGL's rotated temporary buffer lives in PSRAM. Partial rectangles do
     * not meet external-memory I80 DMA address/length alignment. Stage each
     * rotated chunk in internal DMA RAM; LVGL waits for completion before
     * submitting the next rotated chunk. Original portrait buffers stay direct. */
    if(esp_ptr_external_ram(pixels)) {
        size_t bytes=lv_area_get_size(a)*sizeof(*pixels);
        assert(rotation_dma&&bytes<=LV_DISP_ROT_MAX_BUF);
        memcpy(rotation_dma,pixels,bytes);pixels=rotation_dma;
    }
    CHECK(esp_lcd_panel_draw_bitmap(panel,a->x1,a->y1,a->x2+1,a->y2+1,pixels));
    // The DMA completion callback owns flush_ready; pixels stay alive until then.
}
static void tick(void *arg) { lv_tick_inc(1); }
void board_init(void)
{
    output_lock=xSemaphoreCreateMutex(); assert(output_lock);
    i2c_master_bus_handle_t bus0,bus1;
    i2c_master_bus_config_t b={.i2c_port=0,.sda_io_num=2,.scl_io_num=1,
        .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    CHECK(i2c_new_master_bus(&b,&bus0));
    i2c_device_config_t dev={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x20,.scl_speed_hz=400000};
    CHECK(i2c_master_bus_add_device(bus0,&dev,&expander));
    uint8_t reg=2,latch[2];
    CHECK(i2c_master_transmit_receive(expander,&reg,1,latch,2,100));
    outputs=latch[0]|(latch[1]<<8);
    output_set(0x0017,true); // reset high, TF deselected, active-low LED off
    output_set(0x0008,false);
    // Preserve every unrelated direction bit; P0.0..4 must be outputs.
    reg=6; CHECK(i2c_master_transmit_receive(expander,&reg,1,latch,2,100));
    uint8_t cfg[]={6,latch[0]&~0x1f,latch[1]};
    CHECK(i2c_master_transmit(expander,cfg,3,100));

    gpio_config_t rd={.pin_bit_mask=1ULL<<14,.mode=GPIO_MODE_OUTPUT};
    CHECK(gpio_config(&rd)); CHECK(gpio_set_level(14,1));
    esp_lcd_i80_bus_handle_t lcd_bus;
    esp_lcd_i80_bus_config_t lb={.clk_src=LCD_CLK_SRC_DEFAULT,.dc_gpio_num=38,.wr_gpio_num=45,
        .data_gpio_nums={13,12,11,10,9,46,3,8,18,17,16,15,7,6,5,4},.bus_width=16,
        .max_transfer_bytes=320*40*2,.psram_trans_align=64,.sram_trans_align=4};
    CHECK(esp_lcd_new_i80_bus(&lb,&lcd_bus));
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i80_config_t li={.cs_gpio_num=39,.pclk_hz=25000000,.trans_queue_depth=2,
        .dc_levels={.dc_idle_level=0,.dc_cmd_level=0,.dc_dummy_level=0,.dc_data_level=1},
        .on_color_trans_done=transfer_done,.user_ctx=&display,.lcd_cmd_bits=8,.lcd_param_bits=8};
    CHECK(esp_lcd_new_panel_io_i80(lcd_bus,&li,&io));
    output_set(2,false);vTaskDelay(pdMS_TO_TICKS(50));output_set(2,true);vTaskDelay(pdMS_TO_TICKS(200));
    esp_lcd_panel_dev_config_t pc={.reset_gpio_num=-1,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_BGR,.bits_per_pixel=16};
    CHECK(esp_lcd_new_panel_st7796(io,&pc,&panel));
    CHECK(esp_lcd_panel_reset(panel));CHECK(esp_lcd_panel_init(panel));
    CHECK(esp_lcd_panel_invert_color(panel,true));CHECK(esp_lcd_panel_set_gap(panel,0,0));
    CHECK(esp_lcd_panel_swap_xy(panel,false));CHECK(esp_lcd_panel_mirror(panel,true,false));
    CHECK(esp_lcd_panel_disp_on_off(panel,true));
    static lv_disp_draw_buf_t draw;
    void *p=heap_caps_malloc(320*40*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    void *q=heap_caps_malloc(320*40*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    assert(p && q);
    lv_disp_draw_buf_init(&draw,p,q,320*40);
    lv_disp_drv_init(&display);display.hor_res=320;display.ver_res=480;display.flush_cb=flush;display.draw_buf=&draw;
    lv_disp_t *registered_display=lv_disp_drv_register(&display);
    if(!registered_display) CHECK(ESP_ERR_NO_MEM);
    output_set(8,true);

    b.i2c_port=1;b.sda_io_num=41;b.scl_io_num=40;
    CHECK(i2c_new_master_bus(&b,&bus1));
    // Select 0x14 by driving INT high while releasing reset, then input mode.
    gpio_config_t intr={.pin_bit_mask=1ULL<<42,.mode=GPIO_MODE_OUTPUT};CHECK(gpio_config(&intr));
    output_set(1,false);CHECK(gpio_set_level(42,1));vTaskDelay(pdMS_TO_TICKS(20));
    output_set(1,true);vTaskDelay(pdMS_TO_TICKS(10));
    CHECK(gpio_set_level(42,0));vTaskDelay(pdMS_TO_TICKS(50));CHECK(gpio_set_direction(42,GPIO_MODE_INPUT));
    dev.device_address=0x14;CHECK(i2c_master_bus_add_device(bus1,&dev,&touch));
    char id[5]={0};CHECK(touch_read(0x8140,(uint8_t*)id,4));
    memcpy(product_id,id,sizeof(product_id));
    ESP_LOGI("board","Goodix product ID: %s",id);
    if(strncmp(id,"1151",4) && strncmp(id,"1158",4) && strncmp(id,"911",3) && strncmp(id,"9147",4)) {
        ESP_LOGE("board","Unsupported touch ID; check hardware"); CHECK(ESP_ERR_NOT_SUPPORTED);
    }
    CHECK(touch_write(0x8040,2));vTaskDelay(pdMS_TO_TICKS(10));CHECK(touch_write(0x8040,0));
    static lv_indev_drv_t input;lv_indev_drv_init(&input);input.type=LV_INDEV_TYPE_POINTER;input.read_cb=touch_sample;
    lv_indev_t *registered_input=lv_indev_drv_register(&input);
    if(!registered_input) CHECK(ESP_ERR_NO_MEM);
    esp_timer_handle_t timer;esp_timer_create_args_t t={.callback=tick,.name="lv_tick"};
    CHECK(esp_timer_create(&t,&timer));CHECK(esp_timer_start_periodic(timer,1000));
    last_activity=esp_timer_get_time();
}
