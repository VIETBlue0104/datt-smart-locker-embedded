#include "hx711.h"

#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t hx711_init(hx711_t *device, const hx711_config_t *config)
{
	if (device == NULL || config == NULL ||
		!GPIO_IS_VALID_GPIO(config->dout_gpio) ||
		!GPIO_IS_VALID_OUTPUT_GPIO(config->sck_gpio) ||
		config->dout_gpio == config->sck_gpio || config->ready_timeout_us <= 0) {
		return ESP_ERR_INVALID_ARG;
	}

	device->initialized = false;
	gpio_config_t dout_config = {
		.pin_bit_mask = 1ULL << config->dout_gpio,
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
		.pin_bit_mask = 1ULL << config->sck_gpio,
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	err = gpio_config(&sck_config);
	if (err != ESP_OK) {
		return err;
	}

	err = gpio_set_level(config->sck_gpio, 0);
	if (err == ESP_OK) {
		device->config = *config;
		device->initialized = true;
	}
	return err;
}

esp_err_t hx711_read_raw(const hx711_t *device, int32_t *reading)
{
	if (device == NULL || reading == NULL) {
		return ESP_ERR_INVALID_ARG;
	}
	if (!device->initialized) {
		return ESP_ERR_INVALID_STATE;
	}

	const hx711_config_t *config = &device->config;
	int64_t start_time = esp_timer_get_time();
	while (gpio_get_level(config->dout_gpio) != 0) {
		if (esp_timer_get_time() - start_time >= config->ready_timeout_us) {
			return ESP_ERR_TIMEOUT;
		}
		vTaskDelay(pdMS_TO_TICKS(1));
	}

	uint32_t raw = 0;
	for (int bit = 0; bit < 24; bit++) {
		gpio_set_level(config->sck_gpio, 1);
		esp_rom_delay_us(1);
		raw = (raw << 1) | (uint32_t)gpio_get_level(config->dout_gpio);
		gpio_set_level(config->sck_gpio, 0);
		esp_rom_delay_us(1);
	}

	// One extra pulse selects channel A with gain 128 for the next conversion.
	gpio_set_level(config->sck_gpio, 1);
	esp_rom_delay_us(1);
	gpio_set_level(config->sck_gpio, 0);

	*reading = (raw & 0x800000U) ? (int32_t)raw - 0x1000000 : (int32_t)raw;
	return ESP_OK;
}
