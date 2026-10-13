#include "locker_app.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gm65_uart.h"
#include "locker_config.h"
#include "weight_sensor.h"

void locker_app_run(void)
{
	const gm65_uart_config_t barcode_config = {
		.uart_num = LOCKER_GM65_UART_NUM,
		.rx_gpio = LOCKER_GM65_RX_GPIO,
		.baud_rate = LOCKER_GM65_BAUD_RATE,
	};
	const weight_sensor_config_t weight_config = {
		.hx711 = {
			.dout_gpio = LOCKER_HX711_DOUT_GPIO,
			.sck_gpio = LOCKER_HX711_SCK_GPIO,
			.ready_timeout_us = LOCKER_HX711_READY_TIMEOUT_US,
		},
		.tare_samples = LOCKER_WEIGHT_TARE_SAMPLES,
		.calibration_factor = LOCKER_WEIGHT_CALIBRATION_FACTOR,
		.zero_threshold_g = LOCKER_WEIGHT_ZERO_THRESHOLD_G,
	};
	weight_sensor_t weight_sensor = {0};

	ESP_ERROR_CHECK(gm65_uart_init(&barcode_config));
	ESP_ERROR_CHECK(weight_sensor_init(&weight_sensor, &weight_config));
	printf("HX711: GPIO DOUT=%d, SCK=%d. Keep the load cell unloaded for tare.\n",
		   LOCKER_HX711_DOUT_GPIO, LOCKER_HX711_SCK_GPIO);
	vTaskDelay(pdMS_TO_TICKS(LOCKER_WEIGHT_STARTUP_DELAY_MS));

	esp_err_t err = weight_sensor_tare(&weight_sensor);
	if (err != ESP_OK) {
		printf("HX711 tare failed: %s\n", esp_err_to_name(err));
		return;
	}
	printf("Tare complete. Offset=%" PRId32 "\n", weight_sensor.offset);

	while (1) {
		float weight_grams;
		err = weight_sensor_read_grams(&weight_sensor, &weight_grams);
		if (err == ESP_OK) {
			printf("Weight=%.2f g\n", weight_grams);
		} else {
			printf("HX711 read failed: %s\n", esp_err_to_name(err));
		}
		vTaskDelay(pdMS_TO_TICKS(LOCKER_WEIGHT_READ_INTERVAL_MS));
	}
}
