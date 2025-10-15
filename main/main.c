#include <stdio.h>
#include "driver/i2c_master.h"

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
        i2c_master_transmit(disp_handle, data, sizeof(data), -1)
    );
}

void init_display(void){

}

void app_main(void){
    init_i2c();

}