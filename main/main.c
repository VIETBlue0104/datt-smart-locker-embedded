#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define HX711_DOUT_GPIO GPIO_NUM_4
#define HX711_SCK_GPIO GPIO_NUM_5
#define HX711_READY_TIMEOUT_US 1000000
#define HX711_TARE_SAMPLES 10
#define HX711_ZERO_THRESHOLD_G 5.0f

// Starting value only; calibrate with a known weight for accurate grams.
#define HX711_CALIBRATION_FACTOR 2280.0f

static esp_err_t hx711_init(void)
{
	gpio_config_t dout_config = {
		.pin_bit_mask = 1ULL << HX711_DOUT_GPIO,
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
		.pin_bit_mask = 1ULL << HX711_SCK_GPIO,
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	err = gpio_config(&sck_config);
	if (err != ESP_OK) {
		return err;
	}

	return gpio_set_level(HX711_SCK_GPIO, 0);
}

static esp_err_t hx711_read_raw(int32_t *reading)
{
	int64_t start_time = esp_timer_get_time();
	while (gpio_get_level(HX711_DOUT_GPIO) != 0) {
		if (esp_timer_get_time() - start_time >= HX711_READY_TIMEOUT_US) {
			return ESP_ERR_TIMEOUT;
		}
		vTaskDelay(pdMS_TO_TICKS(1));
	}

	uint32_t raw = 0;
	for (int bit = 0; bit < 24; bit++) {
		gpio_set_level(HX711_SCK_GPIO, 1);
		esp_rom_delay_us(1);
		raw = (raw << 1) | (uint32_t)gpio_get_level(HX711_DOUT_GPIO);
		gpio_set_level(HX711_SCK_GPIO, 0);
		esp_rom_delay_us(1);
	}

	// One extra pulse selects channel A with gain 128 for the next conversion.
	gpio_set_level(HX711_SCK_GPIO, 1);
	esp_rom_delay_us(1);
	gpio_set_level(HX711_SCK_GPIO, 0);

	*reading = (raw & 0x800000U) ? (int32_t)raw - 0x1000000 : (int32_t)raw;
	return ESP_OK;
}

static esp_err_t hx711_tare(int32_t *offset)
{
	int64_t total = 0;
	for (int sample = 0; sample < HX711_TARE_SAMPLES; sample++) {
		int32_t reading;
		esp_err_t err = hx711_read_raw(&reading);
		if (err != ESP_OK) {
			return err;
		}
		total += reading;
	}

	*offset = (int32_t)(total / HX711_TARE_SAMPLES);
	return ESP_OK;
}

void app_main(void)
{
	ESP_ERROR_CHECK(hx711_init());
	printf("HX711: GPIO DOUT=%d, SCK=%d. Keep the load cell unloaded for tare.\n",
		   HX711_DOUT_GPIO, HX711_SCK_GPIO);
	vTaskDelay(pdMS_TO_TICKS(500));

	int32_t offset;
	esp_err_t err = hx711_tare(&offset);
	if (err != ESP_OK) {
		printf("HX711 tare failed: %s\n", esp_err_to_name(err));
		return;
	}
	printf("Tare complete. Offset=%" PRId32 "\n", offset);

	while (1) {
		int32_t raw;
		err = hx711_read_raw(&raw);
		if (err == ESP_OK) {
			int32_t net_raw = raw - offset;
			float weight_grams = (float)net_raw / HX711_CALIBRATION_FACTOR;
			if (weight_grams < 0.0f) {
				weight_grams = -weight_grams;
			}
			if (weight_grams < HX711_ZERO_THRESHOLD_G) {
				weight_grams = 0.0f;
			}
			printf("Weight=%.2f g\n", weight_grams);
		} else {
			printf("HX711 read failed: %s\n", esp_err_to_name(err));
		}
		vTaskDelay(pdMS_TO_TICKS(500));
	}
}
