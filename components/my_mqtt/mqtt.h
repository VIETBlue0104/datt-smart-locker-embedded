#ifndef __MQTT_H
#define __MQTT_H

#include "mqtt_client.h"

// Callback để xử lý dữ liệu nhận được từ server
typedef void (*mqtt_data_callback_t)(const char *topic, const char *data, int data_len);

// Khởi tạo MQTT
void my_mqtt_init(const char *uri, mqtt_data_callback_t data_cb);

// Hàm Publish dữ liệu
int my_mqtt_publish(const char *topic, const char *data, int qos);

// Hàm Subscribe topic
int my_mqtt_subscribe(const char *topic, int qos);

#endif