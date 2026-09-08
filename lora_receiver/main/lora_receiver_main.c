#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LORA_SPI_HOST SPI2_HOST
#define LORA_SCK_GPIO GPIO_NUM_9
#define LORA_MOSI_GPIO GPIO_NUM_10
#define LORA_MISO_GPIO GPIO_NUM_11
#define LORA_NSS_GPIO GPIO_NUM_8
#define LORA_BUSY_GPIO GPIO_NUM_13
#define LORA_DIO1_GPIO GPIO_NUM_14
#define LORA_RESET_GPIO GPIO_NUM_12

/* E220 configuration read from the Ebyte config tool:
 * Frequency = 868.125 MHz, Air Data Rate = 2.4 kbps => SF9 + BW125kHz
 */
#define LORA_FREQUENCY_HZ 868125000ULL
#define LORA_SPREADING_FACTOR 9
#define LORA_BANDWIDTH 0x04
#define LORA_CODING_RATE 0x01
#define LORA_PREAMBLE_LENGTH 12
#define LORA_MAX_PAYLOAD 255
#define LORA_E80_TX_TEST 1
#define LORA_E80_TX_INTERVAL_MS 5000

#define LR1121_CMD_CLEAR_IRQ_STATUS 0x0114
#define LR1121_CMD_SET_DIO_IRQ_PARAMS 0x0113
#define LR1121_CMD_GET_VERSION 0x0101
#define LR1121_CMD_SET_STANDBY 0x011C
#define LR1121_CMD_SET_RX 0x0209
#define LR1121_CMD_SET_RF_FREQUENCY 0x020B
#define LR1121_CMD_SET_PACKET_TYPE 0x020E
#define LR1121_CMD_SET_MODULATION_PARAMS 0x020F
#define LR1121_CMD_SET_PACKET_PARAMS 0x0210
#define LR1121_CMD_GET_RX_BUFFER_STATUS 0x0203
#define LR1121_CMD_READ_BUFFER 0x010A
#define LR1121_CMD_GET_IRQ_STATUS 0x0112
#define LR1121_CMD_WRITE_BUFFER 0x010F
#define LR1121_CMD_SET_TX_PARAMS 0x0211
#define LR1121_CMD_SET_TX 0x020A
#define LR1121_CMD_SET_LORA_SYNC_WORD 0x022B

#define LR1121_PACKET_TYPE_LORA 0x01
#define LR1121_STANDBY_RC 0x00
#define LR1121_IRQ_RX_DONE (1U << 3)

static spi_device_handle_t lora_device;

static esp_err_t lora_wait_ready(void)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
    while (gpio_get_level(LORA_BUSY_GPIO) != 0) {
        if (xTaskGetTickCount() > deadline) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_OK;
}

static esp_err_t lora_command(uint16_t command, const uint8_t *arguments,
                              size_t argument_count, uint8_t *response,
                              size_t response_count)
{
    uint8_t tx[258] = {0};
    uint8_t rx[258] = {0};

    if (argument_count > 256 || response_count > 256) {
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_RETURN_ON_ERROR(lora_wait_ready(), "lr1121", "busy timeout");

    tx[0] = (uint8_t)(command >> 8);
    tx[1] = (uint8_t)command;
    if (arguments != NULL) {
        memcpy(&tx[2], arguments, argument_count);
    }

    spi_transaction_t transaction = {
        .length = (2 + argument_count) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    ESP_RETURN_ON_ERROR(spi_device_polling_transmit(lora_device, &transaction),
                        "lr1121", "command failed");

    if (response_count > 0) {
        ESP_RETURN_ON_ERROR(lora_wait_ready(), "lr1121", "response timeout");
        memset(tx, 0, sizeof(tx));
        memset(rx, 0, sizeof(rx));
        transaction.length = (response_count + 1) * 8;
        ESP_RETURN_ON_ERROR(spi_device_polling_transmit(lora_device, &transaction),
                            "lr1121", "response failed");
        memcpy(response, &rx[1], response_count);
    }
    return ESP_OK;
}

static esp_err_t lora_get_irq_status(uint32_t *irq)
{
    uint8_t response[4] = {0};
    ESP_RETURN_ON_ERROR(lora_command(LR1121_CMD_GET_IRQ_STATUS, NULL, 0,
                                     response, sizeof(response)),
                        "lr1121", "status read failed");
    *irq = ((uint32_t)response[0] << 24) | ((uint32_t)response[1] << 16) |
           ((uint32_t)response[2] << 8) | response[3];
    return ESP_OK;
}

static esp_err_t lora_transmit_test(void)
{
    static const uint8_t message[] = "E80_TEST";
    uint8_t write_args[2 + sizeof(message) - 1];
    write_args[0] = 0;
    write_args[1] = 0;
    memcpy(&write_args[2], message, sizeof(message) - 1);

    const uint8_t tx_power[] = {14, 0x04};
    const uint8_t tx_timeout[] = {0x00, 0x00, 0x00};
    ESP_RETURN_ON_ERROR(lora_command(LR1121_CMD_SET_STANDBY,
                                     (const uint8_t[]){LR1121_STANDBY_RC}, 1,
                                     NULL, 0), "lr1121", "TX standby failed");
    ESP_RETURN_ON_ERROR(lora_command(LR1121_CMD_SET_LORA_SYNC_WORD,
                                     (const uint8_t[]){0x12, 0x12}, 2,
                                     NULL, 0), "lr1121", "TX sync word failed");
    ESP_RETURN_ON_ERROR(lora_command(LR1121_CMD_WRITE_BUFFER, write_args,
                                     sizeof(write_args), NULL, 0),
                        "lr1121", "TX buffer failed");
    ESP_RETURN_ON_ERROR(lora_command(LR1121_CMD_SET_TX_PARAMS, tx_power,
                                     sizeof(tx_power), NULL, 0),
                        "lr1121", "TX power failed");
    ESP_RETURN_ON_ERROR(lora_command(LR1121_CMD_SET_TX, tx_timeout,
                                     sizeof(tx_timeout), NULL, 0),
                        "lr1121", "TX start failed");
    printf("[LoRa] E80 transmission: %s\n", message);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(700));
    return lora_command(LR1121_CMD_SET_RX, (const uint8_t[]){0xFF, 0xFF, 0xFF}, 3,
                        NULL, 0);
}

static esp_err_t lora_init(void)
{
    gpio_config_t input = {
        .pin_bit_mask = (1ULL << LORA_BUSY_GPIO) | (1ULL << LORA_DIO1_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input), "lr1121", "input GPIO setup failed");

    gpio_config_t reset = {
        .pin_bit_mask = 1ULL << LORA_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&reset), "lr1121", "reset GPIO setup failed");
    gpio_set_level(LORA_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LORA_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(300));

    spi_bus_config_t bus = {
        .sclk_io_num = LORA_SCK_GPIO,
        .mosi_io_num = LORA_MOSI_GPIO,
        .miso_io_num = LORA_MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 300,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LORA_SPI_HOST, &bus, SPI_DMA_CH_AUTO),
                        "lr1121", "SPI setup failed");

    spi_device_interface_config_t device = {
        .clock_speed_hz = 8000000,
        .mode = 0,
        .spics_io_num = LORA_NSS_GPIO,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(LORA_SPI_HOST, &device, &lora_device),
                        "lr1121", "SPI device setup failed");

    const uint8_t standby[] = {LR1121_STANDBY_RC};
    ESP_RETURN_ON_ERROR(lora_command(LR1121_CMD_SET_STANDBY, standby, sizeof(standby), NULL, 0),
                        "lr1121", "standby failed");

    const uint8_t packet_type[] = {LR1121_PACKET_TYPE_LORA};
    ESP_RETURN_ON_ERROR(lora_command(LR1121_CMD_SET_PACKET_TYPE, packet_type, sizeof(packet_type),
                                     NULL, 0), "lr1121", "packet type failed");

    uint32_t frequency = (uint32_t)((LORA_FREQUENCY_HZ * 33554432ULL) / 32000000ULL);
    const uint8_t frequency_args[] = {
        (uint8_t)(frequency >> 24), (uint8_t)(frequency >> 16),
        (uint8_t)(frequency >> 8), (uint8_t)frequency,
    };
    ESP_RETURN_ON_ERROR(lora_command(LR1121_CMD_SET_RF_FREQUENCY, frequency_args,
                                     sizeof(frequency_args), NULL, 0),
                        "lr1121", "frequency failed");

    const uint8_t modulation[] = {
        LORA_SPREADING_FACTOR, LORA_BANDWIDTH, LORA_CODING_RATE, 0x00,
    };
    ESP_RETURN_ON_ERROR(lora_command(LR1121_CMD_SET_MODULATION_PARAMS, modulation,
                                     sizeof(modulation), NULL, 0),
                        "lr1121", "modulation failed");

    const uint8_t packet[] = {
        (uint8_t)(LORA_PREAMBLE_LENGTH >> 8), (uint8_t)LORA_PREAMBLE_LENGTH,
        0x00, LORA_MAX_PAYLOAD, 0x01, 0x00,
    };
    ESP_RETURN_ON_ERROR(lora_command(LR1121_CMD_SET_PACKET_PARAMS, packet, sizeof(packet),
                                     NULL, 0), "lr1121", "packet parameters failed");

    const uint8_t sync_word[] = {0x12, 0x12};
    ESP_RETURN_ON_ERROR(lora_command(LR1121_CMD_SET_LORA_SYNC_WORD,
                                     sync_word, sizeof(sync_word), NULL, 0),
                        "lr1121", "sync word failed");

    const uint8_t irq[] = {
        0x00, 0x00, 0x00, 0x08,
        0x00, 0x00, 0x00, 0x08,
    };
    ESP_RETURN_ON_ERROR(lora_command(LR1121_CMD_SET_DIO_IRQ_PARAMS, irq, sizeof(irq), NULL, 0),
                        "lr1121", "IRQ setup failed");
    return ESP_OK;
}

static bool lora_payload_is_text(const uint8_t *payload, uint8_t length)
{
    for (uint8_t index = 0; index < length; index++) {
        if ((payload[index] < 32 || payload[index] > 126) &&
            payload[index] != '\r' && payload[index] != '\n' && payload[index] != '\t') {
            return false;
        }
    }
    return true;
}

static void lora_receive_task(void *argument)
{
    uint8_t rx_status[2];
    uint8_t payload[LORA_MAX_PAYLOAD + 1];
    uint8_t read_args[2];
    TickType_t next_test = xTaskGetTickCount() + pdMS_TO_TICKS(LORA_E80_TX_INTERVAL_MS);

    while (true) {
        if (LORA_E80_TX_TEST && xTaskGetTickCount() >= next_test) {
            lora_transmit_test();
            next_test = xTaskGetTickCount() + pdMS_TO_TICKS(LORA_E80_TX_INTERVAL_MS);
        }
        uint32_t irq = 0;
        if (gpio_get_level(LORA_DIO1_GPIO) != 0) {
            if (lora_get_irq_status(&irq) != ESP_OK) {
                printf("[LoRa] IRQ read error\n");
                fflush(stdout);
            } else if ((irq & LR1121_IRQ_RX_DONE) == 0) {
                printf("[LoRa] DIO1 active, IRQ=0x%08" PRIx32 "\n", irq);
                fflush(stdout);
            }
        }
        if ((irq & LR1121_IRQ_RX_DONE) != 0 &&
            lora_command(LR1121_CMD_GET_RX_BUFFER_STATUS, NULL, 0, rx_status,
                         sizeof(rx_status)) == ESP_OK) {
            uint8_t length = rx_status[0];
            read_args[0] = rx_status[1];
            read_args[1] = length;
            if (lora_command(LR1121_CMD_READ_BUFFER, read_args, sizeof(read_args),
                             payload, length) == ESP_OK) {
                if (lora_payload_is_text(payload, length)) {
                    payload[length] = '\0';
                    printf("[LoRa] %s\n", (char *)payload);
                    fflush(stdout);
                }
            }

            const uint8_t clear_irq[] = {0x00, 0x00, 0x00, 0x08};
            lora_command(LR1121_CMD_CLEAR_IRQ_STATUS, clear_irq, sizeof(clear_irq), NULL, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    printf("Starting LR1121 receiver...\n");
    fflush(stdout);
    ESP_ERROR_CHECK(lora_init());
    printf("LR1121 SPI initialized\n");
    uint8_t version[4] = {0};
    ESP_ERROR_CHECK(lora_command(LR1121_CMD_GET_VERSION, NULL, 0, version, sizeof(version)));
    printf("LR1121 detected: hw=%u device=%u firmware=%u.%u\n",
           version[0], version[1], version[2], version[3]);

    const uint8_t receive_timeout[] = {0xFF, 0xFF, 0xFF};
    ESP_ERROR_CHECK(lora_command(LR1121_CMD_SET_RX, receive_timeout,
                                 sizeof(receive_timeout), NULL, 0));
    printf("LR1121 reception active\n");
    fflush(stdout);
    xTaskCreate(lora_receive_task, "lora_receive_task", 4096, NULL, 5, NULL);
}
