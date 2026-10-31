#include "locker_led.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#define LOCKER_LED_SPI_HOST SPI2_HOST
#define LOCKER_LED_LATCH_PIN GPIO_NUM_5
#define LOCKER_LED_OE_PIN GPIO_NUM_15

static spi_device_handle_t led_spi_device;
static uint8_t last_output_mask = UINT8_MAX;

esp_err_t locker_led_write(uint8_t output_mask)
{
    if (led_spi_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    output_mask &= 0x3F;
    if (output_mask == last_output_mask) {
        return ESP_OK;
    }

    esp_err_t err = spi_device_acquire_bus(led_spi_device, portMAX_DELAY);
    if (err != ESP_OK) {
        return err;
    }

    spi_transaction_t transaction = {
        .length = 8,
        .flags = SPI_TRANS_USE_TXDATA,
    };
    transaction.tx_data[0] = output_mask;
    err = spi_device_polling_transmit(led_spi_device, &transaction);
    if (err == ESP_OK) {
        err = gpio_set_level(LOCKER_LED_LATCH_PIN, 1);
        if (err == ESP_OK) {
            err = gpio_set_level(LOCKER_LED_LATCH_PIN, 0);
        }
    }

    spi_device_release_bus(led_spi_device);
    if (err == ESP_OK) {
        last_output_mask = output_mask;
    }
    return err;
}

esp_err_t locker_led_init(void)
{
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << LOCKER_LED_LATCH_PIN) | (1ULL << LOCKER_LED_OE_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&gpio_cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_set_level(LOCKER_LED_OE_PIN, 1);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_set_level(LOCKER_LED_LATCH_PIN, 0);
    if (err != ESP_OK) {
        return err;
    }

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = 1 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    err = spi_bus_add_device(LOCKER_LED_SPI_HOST, &device_config, &led_spi_device);
    if (err != ESP_OK) {
        return err;
    }

    err = locker_led_write(0);
    if (err != ESP_OK) {
        return err;
    }

    return gpio_set_level(LOCKER_LED_OE_PIN, 0);
}
