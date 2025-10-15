#include <stdio.h>
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define I2C_SCL 1 /*FIXME*/
#define I2C_SDA 2

/* Docs: https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf */
#define DISP_ADDRESS 0X3c
#define I2C_FREQ 100000

static i2c_master_dev_handle_t disp_handle;

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


void app_main(void){
    init_i2c();
    init_display();


}