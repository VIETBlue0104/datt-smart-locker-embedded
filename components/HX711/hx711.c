#include "hx711.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

// #define HX711_DOUT_GPIO GPIO_NUM_13
// #define HX711_SCK_GPIO GPIO_NUM_12
#define HX711_READY_TIMEOUT_US 1000000

esp_err_t hx711_init(hx711_pin *hx)
{
    gpio_config_t dout_config = {
        .pin_bit_mask = 1ULL << hx->DT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&dout_config);
    if (err != ESP_OK) {
        return err;
    }

    gpio_config_t sck_config = {
        .pin_bit_mask = 1ULL << hx->SCK,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&sck_config);
    if (err != ESP_OK) {
        return err;
    }

    return gpio_set_level(hx->SCK, 0);
}

esp_err_t hx711_read_raw(hx711_pin *hx,int32_t *reading)
{
    if (hx == NULL || reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    int64_t start_time = esp_timer_get_time();
    while (gpio_get_level(hx->DT) != 0) {
        if (esp_timer_get_time() - start_time >= HX711_READY_TIMEOUT_US) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    uint32_t raw = 0;
    for (int bit = 0; bit < 24; bit++) {
        gpio_set_level(hx->SCK, 1);
        esp_rom_delay_us(1);
        raw = (raw << 1) | (uint32_t)gpio_get_level(hx->DT);
        gpio_set_level(hx->SCK, 0);
        esp_rom_delay_us(1);
    }

    // Xung thứ 25 chọn kênh A, độ lợi 128 cho lần đọc tiếp theo
    gpio_set_level(hx->SCK, 1);
    esp_rom_delay_us(1);
    gpio_set_level(hx->SCK, 0);

    *reading = (raw & 0x800000U) ? (int32_t)raw - 0x1000000 : (int32_t)raw;
    return ESP_OK;
}

esp_err_t hx711_tare(hx711_pin *hx, int32_t *offset)
{
    if (hx == NULL || offset == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    #define HX711_TARE_SAMPLES 10
    int64_t total = 0;
    for (int sample = 0; sample < HX711_TARE_SAMPLES; sample++) {
        int32_t reading;
        esp_err_t err = hx711_read_raw(hx,&reading);
        if (err != ESP_OK) {
            return err;
        }
        total += reading;
    }

    *offset = (int32_t)(total / HX711_TARE_SAMPLES);
    return ESP_OK;
}