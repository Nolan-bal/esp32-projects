#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OLED_SCL_GPIO GPIO_NUM_17
#define OLED_SDA_GPIO GPIO_NUM_18
#define OLED_RESET_GPIO GPIO_NUM_21
#define OLED_I2C_ADDRESS 0x3C
#define OLED_WIDTH 128
#define OLED_PAGES 8

static const char *TAG = "oled";
static i2c_master_dev_handle_t oled_device;
static uint8_t oled_buffer[OLED_WIDTH * OLED_PAGES];

static esp_err_t oled_command(uint8_t command)
{
    uint8_t packet[] = {0x00, command};
    return i2c_master_transmit(oled_device, packet, sizeof(packet), -1);
}

static esp_err_t oled_data(const uint8_t *data, size_t length)
{
    uint8_t packet[OLED_WIDTH + 1];
    packet[0] = 0x40;
    memcpy(&packet[1], data, length);
    return i2c_master_transmit(oled_device, packet, length + 1, -1);
}

static esp_err_t oled_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &bus), TAG, "I2C bus init failed");

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device_config, &oled_device), TAG,
                        "OLED device init failed");

    gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << OLED_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&reset_config), TAG, "OLED reset init failed");
    gpio_set_level(OLED_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(OLED_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    const uint8_t init_commands[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
    };
    for (size_t index = 0; index < sizeof(init_commands); index++) {
        ESP_RETURN_ON_ERROR(oled_command(init_commands[index]), TAG, "OLED init command failed");
    }
    return ESP_OK;
}

static const uint8_t font_5x7[][5] = {
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00},
    ['-'] = {0x08, 0x08, 0x08, 0x08, 0x08},
    ['0'] = {0x3E, 0x51, 0x49, 0x45, 0x3E}, ['1'] = {0x00, 0x42, 0x7F, 0x40, 0x00},
    ['2'] = {0x42, 0x61, 0x51, 0x49, 0x46}, ['3'] = {0x21, 0x41, 0x45, 0x4B, 0x31},
    ['4'] = {0x18, 0x14, 0x12, 0x7F, 0x10}, ['5'] = {0x27, 0x45, 0x45, 0x45, 0x39},
    ['6'] = {0x3C, 0x4A, 0x49, 0x49, 0x30}, ['7'] = {0x01, 0x71, 0x09, 0x05, 0x03},
    ['8'] = {0x36, 0x49, 0x49, 0x49, 0x36}, ['9'] = {0x06, 0x49, 0x49, 0x29, 0x1E},
    ['A'] = {0x7E, 0x11, 0x11, 0x11, 0x7E}, ['B'] = {0x7F, 0x49, 0x49, 0x49, 0x36},
    ['C'] = {0x3E, 0x41, 0x41, 0x41, 0x22}, ['D'] = {0x7F, 0x41, 0x41, 0x22, 0x1C},
    ['E'] = {0x7F, 0x49, 0x49, 0x49, 0x41}, ['F'] = {0x7F, 0x09, 0x09, 0x09, 0x01},
    ['G'] = {0x3E, 0x41, 0x49, 0x49, 0x7A}, ['H'] = {0x7F, 0x08, 0x08, 0x08, 0x7F},
    ['I'] = {0x00, 0x41, 0x7F, 0x41, 0x00}, ['J'] = {0x20, 0x40, 0x41, 0x3F, 0x01},
    ['K'] = {0x7F, 0x08, 0x14, 0x22, 0x41}, ['L'] = {0x7F, 0x40, 0x40, 0x40, 0x40},
    ['M'] = {0x7F, 0x02, 0x0C, 0x02, 0x7F}, ['N'] = {0x7F, 0x04, 0x08, 0x10, 0x7F},
    ['O'] = {0x3E, 0x41, 0x41, 0x41, 0x3E}, ['P'] = {0x7F, 0x09, 0x09, 0x09, 0x06},
    ['Q'] = {0x3E, 0x41, 0x51, 0x21, 0x5E}, ['R'] = {0x7F, 0x09, 0x19, 0x29, 0x46},
    ['S'] = {0x46, 0x49, 0x49, 0x49, 0x31}, ['T'] = {0x01, 0x01, 0x7F, 0x01, 0x01},
    ['U'] = {0x3F, 0x40, 0x40, 0x40, 0x3F}, ['V'] = {0x1F, 0x20, 0x40, 0x20, 0x1F},
    ['W'] = {0x3F, 0x40, 0x38, 0x40, 0x3F}, ['X'] = {0x63, 0x14, 0x08, 0x14, 0x63},
    ['Y'] = {0x07, 0x08, 0x70, 0x08, 0x07}, ['Z'] = {0x61, 0x51, 0x49, 0x45, 0x43},
};

static void oled_text(const char *text, int row)
{
    size_t column = 0;
    while (*text != '\0' && column + 6 <= OLED_WIDTH) {
        unsigned char character = (unsigned char)*text++;
        if (character >= sizeof(font_5x7) / sizeof(font_5x7[0])) {
            character = ' ';
        }
        memcpy(&oled_buffer[row * OLED_WIDTH + column], font_5x7[character], 5);
        column += 6;
    }
}

static esp_err_t oled_refresh(void)
{
    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        ESP_RETURN_ON_ERROR(oled_command(0xB0 | page), TAG, "OLED page failed");
        ESP_RETURN_ON_ERROR(oled_command(0x00), TAG, "OLED column low failed");
        ESP_RETURN_ON_ERROR(oled_command(0x10), TAG, "OLED column high failed");
        ESP_RETURN_ON_ERROR(oled_data(&oled_buffer[page * OLED_WIDTH], OLED_WIDTH), TAG,
                            "OLED data failed");
    }
    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK(oled_init());
    memset(oled_buffer, 0, sizeof(oled_buffer));
    oled_text("HELLO", 1);
    oled_text("ESP32-S3", 3);
    ESP_ERROR_CHECK(oled_refresh());
    ESP_LOGI(TAG, "OLED initialized on SDA=GPIO18, SCL=GPIO17");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
