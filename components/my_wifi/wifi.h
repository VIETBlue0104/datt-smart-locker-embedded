#ifndef __WIFI_H
#define __WIFI_H
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
void flash_init() ;
void wifi_init_sta(const char* WIFI_SSID, const char* WIFI_PASS) ;


#endif