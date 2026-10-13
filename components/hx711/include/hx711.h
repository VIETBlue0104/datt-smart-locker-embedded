#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
	gpio_num_t dout_gpio;
	gpio_num_t sck_gpio;
	int64_t ready_timeout_us;
} hx711_config_t;

typedef struct {
	hx711_config_t config;
	bool initialized;
} hx711_t;

// Each device is used by one task; serialize access if sharing it between tasks.
esp_err_t hx711_init(hx711_t *device, const hx711_config_t *config);
esp_err_t hx711_read_raw(const hx711_t *device, int32_t *reading);
