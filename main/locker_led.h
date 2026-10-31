#ifndef LOCKER_LED_H
#define LOCKER_LED_H

#include <stdint.h>
#include "esp_err.h"

#define LOCKER_LED1_RED   (1U << 0)
#define LOCKER_LED1_GREEN (1U << 1)
#define LOCKER_LED1_BLUE  (1U << 2)
#define LOCKER_LED2_RED   (1U << 3)
#define LOCKER_LED2_GREEN (1U << 4)
#define LOCKER_LED2_BLUE  (1U << 5)

esp_err_t locker_led_init(void);
esp_err_t locker_led_write(uint8_t output_mask);

#endif
