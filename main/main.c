#include <stdio.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LV_COLOR_DEPTH 16
#define LV_USE_LOG 0
#define LV_FONT_MONSTERRAT_14 1

#include "lvgl.h"


#define I2C_SCL 19
#define I2C_SDA 21

/* Docs: https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf */
#define DISP_ADDRESS 0X3c
#define I2C_FREQ 100000

static i2c_master_dev_handle_t disp_handle;

static lv_obj_t *counter_label;

static SemaphoreHandle_t flush_sem = NULL;

void init_i2c(void){
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
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

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &disp_handle));


}

void i2c_disp_send_cmd(uint8_t cmd){
    uint8_t data[2] = {0x00, cmd}; /*command mode*/
    
    i2c_master_transmit(disp_handle, data, sizeof(data), pdMS_TO_TICKS(1000));
    

    vTaskDelay(pdMS_TO_TICKS(1));
}


/*void i2c_disp_send_data(uint8_t *data, size_t len){
    uint8_t *buf = malloc(len + 1);
    buf[0] = 0x40; data mode 
    memcpy(&buf[1], data, len);
    ESP_ERROR_CHECK(
        i2c_master_transmit(disp_handle, buf, len + 1, pdMS_TO_TICKS(100)));
    free(buf);
}*/
void i2c_disp_send_data(uint8_t *data, size_t len){
    if(len <= 1024){
        uint8_t *buf = malloc(len+1);

        buf[0] = 0x40;
        memcpy(&buf[1], data, len);

        esp_err_t ret = i2c_master_transmit(disp_handle, buf, len+1, pdMS_TO_TICKS(2000));

        free(buf);
        return;
    }


    const size_t chunk_size = 64;

    for (size_t offset = 0; offset < len; offset += chunk_size){
        size_t remaining = len - offset;
        size_t to_send = (remaining < chunk_size) ? remaining : chunk_size;

        uint8_t *buf = malloc(to_send + 1);
        buf[0] = 0x40; /* data mode */
        memcpy(&buf[1], &data[offset], to_send);

        esp_err_t ret = i2c_master_transmit(disp_handle, buf, to_send + 1, pdMS_TO_TICKS(500));

        if (ret != ESP_OK)
        {
            printf("ERROR at offset %d: 0x%x\n", offset, ret);
        }

        free(buf);

        vTaskDelay(pdMS_TO_TICKS(1));
    }
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

void lvgl_flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p){
    if(flush_sem == NULL){
        lv_disp_flush_ready(drv);
        return;
    }
    xSemaphoreTake(flush_sem, portMAX_DELAY);

    uint16_t width = area->x2 - area->x1 + 1;
    uint16_t height = area->y2 - area->y1 + 1;

    uint8_t start_page = area->y1 / 8;
    uint8_t end_page = area->y2 / 8;
    uint8_t num_pages = end_page - start_page + 1;

    size_t mono_size = width * num_pages;
    uint8_t *mono_buf = malloc(mono_size);
    memset(mono_buf, 0, mono_size);

    for (uint16_t page = 0; page < num_pages; page++){
        for (uint16_t x = 0; x < width; x++){
            uint8_t page_byte = 0;

            for (uint8_t bit = 0; bit < 8; bit++)
            {
                uint16_t src_y = page * 8 + bit;

                if (src_y < height)
                {
                    lv_color_t color = color_p[src_y * width + x];

                    if (color.full != 0)
                    {
                        page_byte |= (1 << bit);
                    }
                }
            }

            mono_buf[page * width + x] = page_byte;
        }
    }

    i2c_disp_send_cmd(0x21);
    i2c_disp_send_cmd(area->x1);
    i2c_disp_send_cmd(area->x2);

    i2c_disp_send_cmd(0x22);
    i2c_disp_send_cmd(start_page);
    i2c_disp_send_cmd(end_page);


    i2c_disp_send_data(mono_buf, mono_size);
    free(mono_buf);
    lv_disp_flush_ready(drv);

    xSemaphoreGive(flush_sem);
}

void init_graphics(void){
    flush_sem = xSemaphoreCreateBinary();
    if(flush_sem != NULL){
        xSemaphoreGive(flush_sem);
    }

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

    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
    lv_disp_set_default(disp);
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
        vTaskDelay(pdMS_TO_TICKS(10));
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

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void create_counter(void){
    counter_label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(counter_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(counter_label, "00");
    lv_obj_align(counter_label, LV_ALIGN_CENTER, 0, 0);

    xTaskCreate(counter_task, "counter_task", 2048, NULL, 5, NULL);
}

void clear_display(void)
{
    printf("Clearing display...\n");

    i2c_disp_send_cmd(0x21);
    i2c_disp_send_cmd(0);
    i2c_disp_send_cmd(127);
    i2c_disp_send_cmd(0x22);
    i2c_disp_send_cmd(0);
    i2c_disp_send_cmd(7);

    uint8_t zeros[128];
    memset(zeros, 0x00, 128);

    for (int page = 0; page < 8; page++)
    {
        i2c_disp_send_data(zeros, 128);
    }

    printf("Cleared!\n");
}

void draw_text_ok(void)
{
    printf("Drawing 'OK'...\n");

    uint8_t letter_O[8] = {
        0b00000000, // column 0 (tyhjä)
        0b01111110, // column 1 (vasen reuna)
        0b10000001, // column 2
        0b10000001, // column 3
        0b10000001, // column 4
        0b01111110, // column 5 (oikea reuna)
        0b00000000, // column 6 (tyhjä)
        0b00000000  // column 7 (väli)
    };

    // Kirjain K
    uint8_t letter_K[8] = {
        0b00000000, // column 0 (tyhjä)
        0b11111111, // column 1 (pystyviiva)
        0b00011000, // column 2
        0b00100100, // column 3
        0b01000010, // column 4
        0b10000001, // column 5
        0b00000000, // column 6 (tyhjä)
        0b00000000  // column 7
    };

    // Aseta page 3 (keskellä näyttöä), column 50
    i2c_disp_send_cmd(0x21);
    i2c_disp_send_cmd(50);
    i2c_disp_send_cmd(127);
    i2c_disp_send_cmd(0x22);
    i2c_disp_send_cmd(3);
    i2c_disp_send_cmd(3);

    // Lähetä "OK"
    i2c_disp_send_data(letter_O, 8);
    i2c_disp_send_data(letter_K, 8);

    printf("'OK' drawn!\n");
}

void app_main(void){
    init_i2c();
    vTaskDelay(pdMS_TO_TICKS(100));
    init_display();

    
    vTaskDelay(pdMS_TO_TICKS(200));
    /*
    init_graphics();
    vTaskDelay(pdMS_TO_TICKS(100)); 

    
    create_counter(); 

    vTaskDelay(pdMS_TO_TICKS(100));

    xTaskCreate(lv_tick_task, "lv_tick", 4096, NULL, 1, NULL);
    xTaskCreate(lv_task, "lv_task", 8192, NULL, 5, NULL);

    */

    while(1){
        clear_display();
        vTaskDelay(pdMS_TO_TICKS(1000));
        draw_text_ok();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}