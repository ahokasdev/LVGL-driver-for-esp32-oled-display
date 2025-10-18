#include <stdio.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LV_CONF_INCLUDE_SIMPLE 1

#include "lv_conf.h"
#include "lvgl.h"

#define I2C_SCL  19
#define I2C_SDA  21

#define DISP_ADDRESS  0X3c
#define I2C_FREQ      400000

static i2c_master_dev_handle_t  disp_handle;
static lv_obj_t                 *counter_label;
static SemaphoreHandle_t        flush_sem = NULL;
static uint8_t                  lv_buf[(128 * 64/8)+8];

void init_i2c(void){
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port   = I2C_NUM_0,
        .scl_io_num = I2C_SCL,
        .sda_io_num = I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus_handle;
    i2c_new_master_bus(&i2c_mst_config, &bus_handle);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DISP_ADDRESS,
        .scl_speed_hz = I2C_FREQ,
    };

    i2c_master_bus_add_device(bus_handle, &dev_cfg, &disp_handle);


}

void i2c_disp_send_cmd(uint8_t cmd){
    uint8_t data[2] = {0x00, cmd}; /*command mode*/
    
    i2c_master_transmit(disp_handle, data, sizeof(data), pdMS_TO_TICKS(1000));
    
}


void i2c_disp_send_data(uint8_t *data, size_t len){
    uint8_t *buf = malloc(len+1);
    buf[0] = 0x40; /*data mode*/
    memcpy(&buf[1], data, len);

    i2c_master_transmit(disp_handle, buf, len+1, pdMS_TO_TICKS(2000));
    free(buf);
}

void init_display(void){

    i2c_disp_send_cmd(0xAE); // Display OFF

    i2c_disp_send_cmd(0xD5); // Set display clock divide ratio/oscillator frequency
    i2c_disp_send_cmd(0x80); // Default value

    i2c_disp_send_cmd(0xA8); // Set multiplex ratio
    i2c_disp_send_cmd(0x3F); // 1/64 duty (64 COM lines)

    i2c_disp_send_cmd(0xD3); // Set display offset
    i2c_disp_send_cmd(0x00); // No offset

    i2c_disp_send_cmd(0x40); // Set display start line to 0

    i2c_disp_send_cmd(0x8D); // Charge pump setting
    i2c_disp_send_cmd(0x14); // Enable charge pump

    i2c_disp_send_cmd(0x20); // Set Memory Addressing Mode
    i2c_disp_send_cmd(0x00); // Horizontal addressing mode

    i2c_disp_send_cmd(0xA1); // Set segment re-map (A0h/A1h)
    i2c_disp_send_cmd(0xC8); // Set COM output scan direction

    i2c_disp_send_cmd(0xDA); // Set COM pins hardware configuration
    i2c_disp_send_cmd(0x12); // Alternative COM pin config

    i2c_disp_send_cmd(0x81); // Set contrast control
    i2c_disp_send_cmd(0xCF); // Max contrast

    i2c_disp_send_cmd(0xD9); // Set pre-charge period
    i2c_disp_send_cmd(0xF1); // Phase 1: 15 DCLK, Phase 2: 15 DCLK

    i2c_disp_send_cmd(0xDB); // Set VCOMH deselect level
    i2c_disp_send_cmd(0x40); // ~0.77 x VCC

    i2c_disp_send_cmd(0xA4); // Entire display ON (resume to RAM content)
    i2c_disp_send_cmd(0xA6); // Set normal display (not inverted)

    i2c_disp_send_cmd(0x2E); // Deactivate scroll

    i2c_disp_send_cmd(0xAF); // Display ON
}

void lvgl_flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map){

    if (flush_sem == NULL){
        lv_display_flush_ready(disp);
        return;
    }
    xSemaphoreTake(flush_sem, portMAX_DELAY);

    uint16_t width = 128;
    uint16_t height = 64;

    uint8_t buf[128];
    memset(buf, 0, sizeof(buf));

    uint16_t bytes = (width * height + 7) / 8;
    uint16_t bytes_per_row = width / 8;
    uint16_t bytes_per_page = bytes_per_row*8;
    uint8_t page = 0;
    uint8_t page_x = 0;
    uint8_t page_y = 0;
    uint8_t byte;

    px_map += 8;

    /*converted pixels to page format*/
    for (uint16_t i = 0; i <= bytes; i++){
        byte = px_map[i];

        for (int bit = 7; bit >= 0; bit--){ 
            if (byte & (1 << bit)){
                buf[page_x] |= (1 << page_y);
            }
            ++page_x;
        }
        if ((i+1) % bytes_per_row == 0 && i+1 != 0){
            page_y++;
            page_x = 0;
            
        }
        if((i+1)% bytes_per_page == 0 && i+1 != 0){
            /*page finished => send to display*/
            i2c_disp_send_cmd(0x21);
            i2c_disp_send_cmd(0);
            i2c_disp_send_cmd(127);
            i2c_disp_send_cmd(0x22);
            i2c_disp_send_cmd(page);
            i2c_disp_send_cmd(page);
            
            i2c_disp_send_data(buf, sizeof(buf));
            memset(buf, 0, sizeof(buf));
            page++;
            page_y = 0;
        }
    }

    lv_display_flush_ready(disp);
    xSemaphoreGive(flush_sem);
}

void init_graphics(void){
    flush_sem = xSemaphoreCreateBinary();
    if(flush_sem != NULL){
        xSemaphoreGive(flush_sem);
    }

    memset(lv_buf, 0, sizeof(lv_buf));

    lv_init();

    lv_display_t *display = lv_display_create(128, 64);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_I1);

    lv_display_set_flush_cb(display, lvgl_flush_callback);

    lv_display_set_buffers(display, lv_buf, NULL, sizeof(lv_buf), LV_DISPLAY_RENDER_MODE_FULL);

    lv_obj_t *scr = lv_screen_active();

    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

}

void lv_tick_task(void *arg){
    while(1){
        lv_tick_inc(10);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void lv_task(void *arg){
    while(1){
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


void counter_task(void *arg){
    uint8_t counter = 0;

    while(1){
        char buf[4];
        snprintf(buf, sizeof(buf), "%02d", counter);
        lv_label_set_text(counter_label, buf);

        if(counter >= 99){
            counter = 0;
        } else {
            counter++;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void create_counter(void){
    lv_obj_t *scr = lv_scr_act();


    counter_label = lv_label_create(scr);
    printf("Label created");
    lv_label_set_text(counter_label, "00");
    lv_obj_set_style_text_color(counter_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(counter_label, &lv_font_montserrat_44, 0);
    lv_obj_set_style_pad_all(counter_label, 0, 0);
    lv_obj_set_style_margin_all(counter_label, 0, 0);
    lv_obj_set_width(counter_label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(counter_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(counter_label, LV_ALIGN_CENTER, 0, 0);
    
    xTaskCreate(counter_task, "counter_task", 2048, NULL, 1, NULL);
    printf("Counter task created"); 
}


void app_main(void){
    init_i2c();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    init_display();
    vTaskDelay(pdMS_TO_TICKS(50));

    init_graphics();
    vTaskDelay(pdMS_TO_TICKS(50));

    xTaskCreate(lv_tick_task, "lv_tick", 4096, NULL, 1, NULL);
    xTaskCreate(lv_task, "lv_task", 8192, NULL, 3, NULL);

    vTaskDelay(pdMS_TO_TICKS(150));

    create_counter();
}