#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint16_t DT ;
    uint16_t SCK ;
} hx711_pin ;

esp_err_t hx711_init(hx711_pin *hx);
esp_err_t hx711_read_raw(hx711_pin *hx,int32_t *reading);
esp_err_t hx711_tare(hx711_pin *hx, int32_t *offset);

#ifdef __cplusplus
}
#endif