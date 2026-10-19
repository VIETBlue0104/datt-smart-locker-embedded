#include "mqtt.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "MQTT_LIB";
static esp_mqtt_client_handle_t client = NULL;
static mqtt_data_callback_t user_data_callback = NULL;

// Handler xử lý các sự kiện MQTT
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            my_mqtt_subscribe("ptit/verified/shiper", 1);
            my_mqtt_subscribe("ptit/verified/customer", 1);
            my_mqtt_subscribe("ptit/locker/open",1);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            if (user_data_callback) {
                // Tạo buffer tạm để chứa topic và data (tránh lỗi con trỏ)
                char topic_tmp[64] = {0};
                char data_tmp[256] = {0};
                memcpy(topic_tmp, event->topic, event->topic_len);
                memcpy(data_tmp, event->data, event->data_len);
                user_data_callback(topic_tmp, data_tmp, event->data_len);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            break;
    }
}

void my_mqtt_init(const char *uri, mqtt_data_callback_t data_cb) {
    user_data_callback = data_cb;// lưu callback của người dùng để gọi khi có data mới

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);// khởi tạo client MQTT với cấu hình đã cho
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);// Đăng kí handler xử lí các sự kiện MQTT
    esp_mqtt_client_start(client);// Bắt đầu kết nối đến Broker MQTT
}

int my_mqtt_publish(const char *topic, const char *data, int qos) {
    if (client == NULL) return -1;
    return esp_mqtt_client_publish(client, topic, data, 0, qos, 0);
}

int my_mqtt_subscribe(const char *topic, int qos) {
    if (client == NULL) return -1;
    return esp_mqtt_client_subscribe(client, topic, qos);
}