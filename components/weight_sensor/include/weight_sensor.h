#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "hx711.h"

typedef struct {
	hx711_config_t hx711;
	uint32_t tare_samples;
	float calibration_factor;
	float zero_threshold_g;
} weight_sensor_config_t;

typedef struct {
	hx711_t hx711;
	weight_sensor_config_t config;
	int32_t offset;
	bool tared;
} weight_sensor_t;

esp_err_t weight_sensor_init(weight_sensor_t *sensor, const weight_sensor_config_t *config);
// Tare with an unloaded load cell before reading grams.
esp_err_t weight_sensor_tare(weight_sensor_t *sensor);
esp_err_t weight_sensor_read_grams(const weight_sensor_t *sensor, float *weight_grams);
