#include "sdkconfig.h"
#include "mqtt.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

// ID tủ: cấu hình bằng `idf.py menuconfig` -> Smart Locker Config -> Locker ID
// Phải trùng với cột locker_id trong bảng lockers của database
#define LOCKER_ID            CONFIG_LOCKER_ID
#define TOPIC_PREFIX         "ptit/" LOCKER_ID "/"   // vd: "ptit/LOCKER_01/"
#define HEARTBEAT_PERIOD_MS  30000

static const char *TAG = "MQTT_LIB";
static esp_mqtt_client_handle_t client = NULL;
static mqtt_data_callback_t user_data_callback = NULL;

static char status_topic[64];
static volatile bool s_connected = false;

// Publish nguyên văn (dùng cho status/heartbeat và các hàm publish bên dưới)
static int publish_raw(const char *topic, const char *data, int qos, int retain) {
    if (client == NULL) return -1;
    return esp_mqtt_client_publish(client, topic, data, 0, qos, retain);
}

// Heartbeat chạy trong task riêng, tránh publish (có thể bị chặn) trong esp_timer
static void heartbeat_task(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
        if (s_connected) {
            publish_raw(status_topic, "ONLINE", 1, 1);
        }
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED, locker_id=%s", LOCKER_ID);
            s_connected = true;
            publish_raw(status_topic, "ONLINE", 1, 1);
            // Chỉ subscribe topic của chính tủ này
            my_mqtt_subscribe(TOPIC_PREFIX "verified/shiper", 1);
            my_mqtt_subscribe(TOPIC_PREFIX "verified/customer", 1);
            my_mqtt_subscribe(TOPIC_PREFIX "locker/open", 1);
            my_mqtt_subscribe(TOPIC_PREFIX "verified/QR_OTP/failed", 1);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            s_connected = false;
            break;

        case MQTT_EVENT_DATA: {
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            if (user_data_callback == NULL) break;

            // Bản tin bị chia mảnh -> bỏ qua (payload của dự án đều ngắn)
            if (event->total_data_len != event->data_len) {
                ESP_LOGW(TAG, "Ban tin bi chia manh, bo qua");
                break;
            }

            char topic_tmp[96] = {0};
            char data_tmp[256] = {0};
            if (event->topic_len <= 0 || event->topic_len >= (int)sizeof(topic_tmp)) {
                ESP_LOGW(TAG, "Topic qua dai, bo qua");
                break;
            }
            int dlen = event->data_len < (int)sizeof(data_tmp) - 1 ? event->data_len : (int)sizeof(data_tmp) - 1;
            memcpy(topic_tmp, event->topic, event->topic_len);
            memcpy(data_tmp, event->data, dlen);

            // "ptit/LOCKER_01/locker/open" -> "ptit/locker/open"
            // để main.c/gui.c vẫn so sánh topic như cũ
            const size_t plen = strlen(TOPIC_PREFIX);
            if (strncmp(topic_tmp, TOPIC_PREFIX, plen) != 0) {
                ESP_LOGW(TAG, "Topic la, bo qua: %s", topic_tmp);
                break;
            }
            char topic_out[96] = {0};
            snprintf(topic_out, sizeof(topic_out), "ptit/%s", topic_tmp + plen);
            user_data_callback(topic_out, data_tmp, dlen);
            break;
        }

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            break;
    }
}

void my_mqtt_init(const char *uri, mqtt_data_callback_t data_cb) {
    user_data_callback = data_cb;

    snprintf(status_topic, sizeof(status_topic), "ptit/status/%s", LOCKER_ID);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = LOCKER_ID,     // mỗi tủ một client_id, không được trùng
        .session.last_will = {
            .topic = status_topic,
            .msg = "OFFLINE",
            .qos = 1,
            .retain = 1,
        },
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    xTaskCreate(heartbeat_task, "mqtt_heartbeat", 2048, NULL, 1, NULL);
}

// Gửi {"locker_id":"...","<key>":"<value>"}
// vd: my_mqtt_publish_kv("ptit/rec/otp", "otp", "123456", 1)
//  -> {"locker_id":"LOCKER_01","otp":"123456"}
int my_mqtt_publish_kv(const char *topic, const char *key, const char *value, int qos) {
    if (client == NULL || topic == NULL || key == NULL) return -1;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return -1;
    cJSON_AddStringToObject(root, "locker_id", LOCKER_ID);
    cJSON_AddStringToObject(root, key, value ? value : "");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (out == NULL) return -1;

    int ret = publish_raw(topic, out, qos, 0);
    cJSON_free(out);
    return ret;
}

// Gửi payload vốn đã là JSON object: chèn thêm "locker_id" vào chính object đó
// vd {"status":"deposit_success"} -> {"status":"deposit_success","locker_id":"LOCKER_01"}
int my_mqtt_publish_json(const char *topic, const char *json_str, int qos) {
    if (client == NULL || json_str == NULL) return -1;

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "Payload khong phai JSON object: %s", json_str);
        return -1;
    }
    cJSON_DeleteItemFromObject(root, "locker_id");
    cJSON_AddStringToObject(root, "locker_id", LOCKER_ID);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (out == NULL) return -1;

    int ret = publish_raw(topic, out, qos, 0);
    cJSON_free(out);
    return ret;
}

int my_mqtt_subscribe(const char *topic, int qos) {
    if (client == NULL) return -1;
    return esp_mqtt_client_subscribe(client, topic, qos);
}