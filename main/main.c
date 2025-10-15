#include <stdio.h>
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LV_CONF_INCLUDE_SIMPLE 1
#include "lv_conf.h"
#include "lvgl.h"


#define I2C_SCL 1 /*FIXME*/
#define I2C_SDA 2

/* Docs: https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf */
#define DISP_ADDRESS 0X3c
#define I2C_FREQ 100000

static i2c_master_dev_handle_t disp_handle;

static lv_obj_t *counter_label;

void init_i2c(void){
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT_NUM_0,
        .scl_io_num = I2C_SCL,
        .sda_io_num = I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DISP_ADDRESS,
        .scl_speed_hz = I2C_FREQ,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &disp_handle));


}

void i2c_disp_send_cmd(uint8_t cmd){
    uint8_t data[2] = {0x00, cmd}; /*command mode*/
    ESP_ERROR_CHECK(
        i2c_master_transmit(disp_handle, data, sizeof(data), pdMS_TO_TICKS(100))
    );
}

void i2c_disp_send_data(uint8_t *data, size_t len){
    uint8_t *buf = malloc(len + 1);
    buf[0] = 0x40; /*data mode*/
    memcpy(&buf[1], data, len);
    ESP_ERROR_CHECK(
        i2c_master_transmit(disp_handle, buf, len + 1, pdMS_TO_TICKS(100)));
    free(buf);
}

void init_display(void){
    /*according to https://gist.github.com/pulsar256/564fda3b9e8fc6b06b89*/
    i2c_disp_send_cmd(0xAE); // Set display OFF

    i2c_disp_send_cmd(0xD4); // Set Display Clock Divide Ratio / OSC Frequency
    i2c_disp_send_cmd(0x80); // Display Clock Divide Ratio / OSC Frequency

    i2c_disp_send_cmd(0xA8); // Set Multiplex Ratio
    i2c_disp_send_cmd(0x3F); // Multiplex Ratio for 128x64 (64-1)

    i2c_disp_send_cmd(0xD3); // Set Display Offset
    i2c_disp_send_cmd(0x00); // Display Offset

    i2c_disp_send_cmd(0x40); // Set Display Start Line

    i2c_disp_send_cmd(0x8D); // Set Charge Pump
    i2c_disp_send_cmd(0x14); // Charge Pump (0x10 External, 0x14 Internal DC/DC)

    i2c_disp_send_cmd(0xA1); // Set Segment Re-Map
    i2c_disp_send_cmd(0xC8); // Set Com Output Scan Direction

    i2c_disp_send_cmd(0xDA); // Set COM Hardware Configuration
    i2c_disp_send_cmd(0x12); // COM Hardware Configuration

    i2c_disp_send_cmd(0x81); // Set Contrast
    i2c_disp_send_cmd(0xCF); // Contrast

    i2c_disp_send_cmd(0xD9); // Set Pre-Charge Period
    i2c_disp_send_cmd(0xF1); // Set Pre-Charge Period (0x22 External, 0xF1 Internal)

    i2c_disp_send_cmd(0xDB); // Set VCOMH Deselect Level
    i2c_disp_send_cmd(0x40); // VCOMH Deselect Level

    i2c_disp_send_cmd(0xA4); // Set all pixels OFF
    i2c_disp_send_cmd(0xA6); // Set display not inverted
    i2c_disp_send_cmd(0xAF); // Set display On
}

void lvgl_flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p){
    size_t width = lv_area_get_width(area);
    size_t height = lv_area_get_height(area);
    size_t len = (width * height) / 8;

    i2c_disp_send_data((uint8_t*)color_p, len);
    lv_disp_flush_ready(drv);
}

void init_graphics(void){
    lv_init();

    static lv_disp_draw_buf_t disp_buf;
    static lv_color_t buf1[128*8];
    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, 128*8);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 128;
    disp_drv.ver_res = 64;
    disp_drv.flush_cb = lvgl_flush_callback;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);
}

void lv_tick_task(void *arg){
    while(1){
        lv_tick_inc(1);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void lv_task(void *arg){
    while(1){
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}


void counter_task(void *arg){
    uint8_t counter = 0;

    while(1){
        char buf[3];
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
    counter_label = lv_label_create(lv_scr_act());
    lv_label_set_text(counter_label, "00");
    lv_obj_align(counter_label, LV_ALIGN_CENTER, 0, 0);

    xTaskCreate(counter_task, "counter_task", 2048, NULL, 5, NULL);
}


void app_main(void){
    init_i2c();
    init_display();
    init_graphics();

    create_counter();

    xTaskCreate(lv_tick_task, "lv_tick", 2048, NULL, 5, NULL);
    xTaskCreate(lv_task, "lv_task", 4096, NULL, 5, NULL);



}