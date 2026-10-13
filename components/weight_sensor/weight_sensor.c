#include "weight_sensor.h"

#include <math.h>
#include <stddef.h>

esp_err_t weight_sensor_init(weight_sensor_t *sensor, const weight_sensor_config_t *config)
{
	if (sensor == NULL || config == NULL || config->tare_samples == 0 ||
		!isfinite(config->calibration_factor) || config->calibration_factor == 0.0f ||
		!isfinite(config->zero_threshold_g) || config->zero_threshold_g < 0.0f) {
		return ESP_ERR_INVALID_ARG;
	}

	sensor->tared = false;
	esp_err_t err = hx711_init(&sensor->hx711, &config->hx711);
	if (err != ESP_OK) {
		return err;
	}
	sensor->config = *config;
	sensor->offset = 0;
	return ESP_OK;
}

esp_err_t weight_sensor_tare(weight_sensor_t *sensor)
{
	if (sensor == NULL) {
		return ESP_ERR_INVALID_ARG;
	}
	if (!sensor->hx711.initialized) {
		return ESP_ERR_INVALID_STATE;
	}

	int64_t total = 0;
	for (uint32_t sample = 0; sample < sensor->config.tare_samples; sample++) {
		int32_t reading;
		esp_err_t err = hx711_read_raw(&sensor->hx711, &reading);
		if (err != ESP_OK) {
			return err;
		}
		total += reading;
	}

	sensor->offset = (int32_t)(total / sensor->config.tare_samples);
	sensor->tared = true;
	return ESP_OK;
}

esp_err_t weight_sensor_read_grams(const weight_sensor_t *sensor, float *weight_grams)
{
	if (sensor == NULL || weight_grams == NULL) {
		return ESP_ERR_INVALID_ARG;
	}
	if (!sensor->tared) {
		return ESP_ERR_INVALID_STATE;
	}

	int32_t raw;
	esp_err_t err = hx711_read_raw(&sensor->hx711, &raw);
	if (err != ESP_OK) {
		return err;
	}

	int32_t net_raw = raw - sensor->offset;
	float grams = (float)net_raw / sensor->config.calibration_factor;
	if (grams < 0.0f) {
		grams = -grams;
	}
	if (grams < sensor->config.zero_threshold_g) {
		grams = 0.0f;
	}
	*weight_grams = grams;
	return ESP_OK;
}
