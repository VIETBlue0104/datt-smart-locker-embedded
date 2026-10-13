#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"

typedef struct {
	uart_port_t uart_num;
	gpio_num_t rx_gpio;
	int baud_rate;
} gm65_uart_config_t;

// Starts one scanner task and copies the configuration for its lifetime.
esp_err_t gm65_uart_init(const gm65_uart_config_t *config);
