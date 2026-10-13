#include <stdbool.h>
#include <stdio.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gm65_uart.h"

#define GM65_RX_BUFFER_SIZE 256
#define GM65_BARCODE_MAX_LENGTH 128

static const char *TAG = "gm65";
static gm65_uart_config_t s_config;
static bool s_started;

static void gm65_uart_task(void *arg)
{
	const gm65_uart_config_t *config = arg;
	char barcode[GM65_BARCODE_MAX_LENGTH];
	size_t length = 0;
	bool overflow = false;
	uint8_t byte;

	while (true) {
		int bytes_read = uart_read_bytes(config->uart_num, &byte, 1, pdMS_TO_TICKS(100));
		if (bytes_read <= 0) {
			continue;
		}

		if (byte == '\r' || byte == '\n') {
			if (overflow) {
				ESP_LOGW(TAG, "Barcode exceeds %d characters; discarded",
						 GM65_BARCODE_MAX_LENGTH - 1);
			} else if (length > 0) {
				barcode[length] = '\0';
				printf("GM65 barcode: %s\n", barcode);
			}

			length = 0;
			overflow = false;
			continue;
		}

		if (length < sizeof(barcode) - 1) {
			barcode[length++] = (char)byte;
		} else {
			overflow = true;
		}
	}
}

esp_err_t gm65_uart_init(const gm65_uart_config_t *config)
{
	if (config == NULL || config->uart_num < UART_NUM_0 ||
		config->uart_num >= UART_NUM_MAX || !GPIO_IS_VALID_GPIO(config->rx_gpio) ||
		config->baud_rate <= 0) {
		return ESP_ERR_INVALID_ARG;
	}
	if (s_started) {
		return ESP_ERR_INVALID_STATE;
	}

	const uart_config_t uart_config = {
		.baud_rate = config->baud_rate,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};

	esp_err_t err = uart_driver_install(config->uart_num, GM65_RX_BUFFER_SIZE, 0, 0, NULL, 0);
	if (err != ESP_OK) {
		return err;
	}

	err = uart_param_config(config->uart_num, &uart_config);
	if (err != ESP_OK) {
		uart_driver_delete(config->uart_num);
		return err;
	}

	err = uart_set_pin(config->uart_num, UART_PIN_NO_CHANGE, config->rx_gpio,
					   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
	if (err != ESP_OK) {
		uart_driver_delete(config->uart_num);
		return err;
	}

	s_config = *config;
	if (xTaskCreate(gm65_uart_task, "gm65_uart", 3072, &s_config, 5, NULL) != pdPASS) {
		uart_driver_delete(config->uart_num);
		return ESP_ERR_NO_MEM;
	}
	s_started = true;

	ESP_LOGI(TAG, "UART%d ready: RX GPIO%d, %d baud, 8N1",
			 config->uart_num, config->rx_gpio, config->baud_rate);
	return ESP_OK;
}
