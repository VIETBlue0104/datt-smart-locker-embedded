#include "sdkconfig.h"
#include "esp_log.h"
#include "display_touch.h"
#include "gui.h"
#include "wifi.h"
#include "mqtt.h"
#include "esp_netif_sntp.h"
#include "hx711.h"
#include "gm65.h"
#include "driver/uart.h"
#include "lock.h"
#include "cJSON.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "main";
static const char *TAG_SENSOR = "SENSOR";
static const char *TAG_WARN = "WARNING";
static const char *TAG_EVENT = "EVENT";

// ID của tủ, cấu hình bằng `idf.py menuconfig` -> Smart Locker Config -> Locker ID
// Phải trùng với cột locker_id trong bảng lockers của database
#define LOCKER_ID CONFIG_LOCKER_ID

// Topic tủ gửi lên backend (có ID tủ ở giữa)
#define TOPIC_EVENT_FMT  "compartment/%d/event"
#define TOPIC_STATUS_FMT "compartment/%d/status"

#define HX711_ZERO_THRESHOLD_G 5.0f
//#define HX711_CALIBRATION_FACTOR 434.0f
#define HX711_CALIBRATION_FACTOR 2280.f

#define MC38_PIN_1 GPIO_NUM_34  // Cảm biến từ cửa ngăn 1
#define MC38_PIN_2 GPIO_NUM_35  // Cảm biến từ cửa ngăn 2
#define IR_SENSOR_PIN_1 GPIO_NUM_5  // Cảm biến IR ngăn 1
#define IR_SENSOR_PIN_2 GPIO_NUM_15 // Cảm biến IR ngăn 2
#define IR_SENSOR_DETECTED_LEVEL 0  // Module IR xuất LOW khi phát hiện vật cản
#define BUZZER_PIN GPIO_NUM_27

static const gpio_num_t ir_sensor_pins[2] = {
    IR_SENSOR_PIN_1,
    IR_SENSOR_PIN_2,
};

static esp_err_t sync_time_for_tls(void)
{
    const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    esp_netif_sntp_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SNTP time sync failed: %s", esp_err_to_name(err));
        return err;
    }

    time_t now;
    struct tm utc_time;
    time(&now);
    if (gmtime_r(&now, &utc_time) == NULL || utc_time.tm_year < (2024 - 1900)) {
        ESP_LOGE(TAG, "System time is invalid; refusing to start MQTT over TLS");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

// Cảnh báo cửa mở quá lâu: 60 chu kỳ x 500ms = 30s
#define WARNING_OPEN_TICKS      60
// Chờ ngăn trống ổn định khi khách lấy hàng lúc cửa còn mở: 10 x 100ms = 1s
#define EMPTY_STABLE_TICKS      10
// Chờ loadcell ổn định sau khi đóng cửa: 10 x 100ms = 1s
#define CLOSE_SETTLE_TICKS      10

QueueHandle_t qr_queue = NULL;
static volatile bool scan_enabled = false;
static char topic_rec_qr[64] = "qr/receive";
static char current_order_qr[64] = {0};
static portMUX_TYPE current_order_qr_mux = portMUX_INITIALIZER_UNLOCKED;

hx711_pin hx[2] = {
    {//gpio 36 là Chân vp , còn 39 là chân VN
        .DT = GPIO_NUM_36,
        .SCK = GPIO_NUM_32
    },
    {
        .DT = GPIO_NUM_39,
        .SCK = GPIO_NUM_33
    }
};

// --- CÁC BIẾN CHIA SẺ TRẠNG THÁI GIỮA CÁC TASK ---
static volatile bool  door_closed_status[2] = {true, true};
static bool           prev_door_closed_status[2] = {true, true};
static volatile float compartment_weight[2] = {0.0f, 0.0f};
static volatile bool  compartment_has_item[2] = {false, false};

// warning_task SỞ HỮU: door_open_counter, compartment_warning_active
static int           door_open_counter[2] = {0, 0};
static volatile bool compartment_warning_active[2] = {false, false};
static volatile bool hx711_fault[2] = {true, true};
static volatile bool locker_maintenance = false;
static volatile bool compartment_maintenance[2] = {false, false};
static volatile bool force_compartment_status_publish[2] = {false, false};

// Cờ theo dõi phiên giao dịch
static volatile bool waiting_for_deposit[2] = {false, false}; // Đang chờ shipper bỏ hàng
static volatile bool waiting_for_pickup[2]  = {false, false}; // Đang chờ khách lấy hàng

// event_handler_task SỞ HỮU: các biến dưới đây
static bool pickup_seen_item[2]  = {false, false}; // Đã thấy hàng trong ngăn khi chờ lấy
static int  empty_stable_cnt[2]  = {0, 0};         // Đếm chu kỳ ngăn trống ổn định (cửa mở)
static int  close_settle_cnt[2]  = {0, 0};         // Đếm lùi chờ loadcell ổn định sau khi đóng cửa

static void reset_compartment_transaction(int index)
{
    waiting_for_deposit[index] = false;
    waiting_for_pickup[index] = false;
    pickup_seen_item[index] = false;
    empty_stable_cnt[index] = 0;
    close_settle_cnt[index] = 0;
}

void rec_topic(char *topic) {
    if (topic != NULL) {
        snprintf(topic_rec_qr, sizeof(topic_rec_qr), "%s", topic);
        if (strcmp(topic, "qr/order") == 0) {
            portENTER_CRITICAL(&current_order_qr_mux);
            current_order_qr[0] = '\0';
            portEXIT_CRITICAL(&current_order_qr_mux);
        }
    }
}

void set_scan_enabled(bool enable) {
    scan_enabled = enable;
}

void set_compartment_expecting(int index, bool is_deposit) {
    if (index >= 0 && index < 2) {
        pickup_seen_item[index] = false;
        empty_stable_cnt[index] = 0;
        if (is_deposit) {
            waiting_for_deposit[index] = true;
            waiting_for_pickup[index]  = false;
        } else {
            waiting_for_pickup[index]  = true;
            waiting_for_deposit[index] = false;
        }
    }
}

int locker_select_and_open_compartment(int index)
{
    if (index < 0 || index >= 2 || locker_maintenance ||
        compartment_maintenance[index] || hx711_fault[index] ||
        compartment_has_item[index] || !door_closed_status[index]) {
        return -1;
    }

    char order_qr[sizeof(current_order_qr)];
    portENTER_CRITICAL(&current_order_qr_mux);
    snprintf(order_qr, sizeof(order_qr), "%s", current_order_qr);
    portEXIT_CRITICAL(&current_order_qr_mux);
    if (order_qr[0] == '\0') {
        return -1;
    }

    cJSON *payload = cJSON_CreateObject();
    if (payload == NULL) {
        ESP_LOGE(TAG, "Failed to create compartment selection payload");
        return -1;
    }
    cJSON_AddNumberToObject(payload, "compartment", index);
    cJSON_AddStringToObject(payload, "qr", order_qr);
    char *json = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to serialize compartment selection payload");
        return -1;
    }

    set_compartment_expecting(index, true);
    open_lock(index);
    ESP_LOGI(TAG, "Shipper selected compartment %d; opening immediately", index + 1);

    int publish_result = my_mqtt_publish_json("locker/select_compartment", json, 1);
    cJSON_free(json);
    if (publish_result < 0) {
        ESP_LOGE(TAG, "Compartment %d opened but selection/QR publish failed", index + 1);
        return 0;
    }

    return 1;
}

/* ===========================================================================
 * NHẬN DỮ LIỆU MQTT TỪ BACKEND
 * Lưu ý: mqtt.c đã cắt prefix "ptit/<LOCKER_ID>/" nên topic ở đây vẫn có
 * callback topic có dạng "ptit/locker/open", "ptit/verified/customer", ...
 * =========================================================================

 */


void on_mqtt_data(const char *topic, const char *data, int data_len)
{
    ESP_LOGI(TAG, "MQTT Received [%s]: %s", topic, data);

    if (strcmp(topic, "ptit/maintenance/locker") == 0) {
        if (strcmp(data, "1") != 0 && strcmp(data, "0") != 0) {
            ESP_LOGW(TAG, "Invalid locker maintenance state: %s", data);
            return;
        }

        bool enabled = (data[0] == '1');
        if (locker_maintenance != enabled) {
            locker_maintenance = enabled;
            my_mqtt_set_maintenance(enabled);
            if (enabled) {
                set_scan_enabled(false);
                if (qr_queue != NULL) {
                    xQueueReset(qr_queue);
                }
                for (int i = 0; i < 2; i++) {
                    reset_compartment_transaction(i);
                }
            } else {
                for (int i = 0; i < 2; i++) {
                    force_compartment_status_publish[i] = true;
                }
            }
            gui_set_locker_maintenance(enabled);
            ESP_LOGW(TAG, "Locker maintenance %s", enabled ? "enabled" : "disabled");
        }
        return;
    }

    if (strcmp(topic, "ptit/maintenance/compartment") == 0) {
        int compartment_idx = -1;
        int enabled_value = -1;
        char trailing_char;
        if (sscanf(data, "%d,%d %c", &compartment_idx, &enabled_value, &trailing_char) != 2 ||
            compartment_idx < 0 || compartment_idx >= 2 ||
            (enabled_value != 0 && enabled_value != 1)) {
            ESP_LOGW(TAG, "Invalid compartment maintenance command: %s", data);
            return;
        }

        bool enabled = (enabled_value == 1);
        if (compartment_maintenance[compartment_idx] != enabled) {
            compartment_maintenance[compartment_idx] = enabled;
            my_mqtt_set_compartment_maintenance(compartment_idx, enabled);
            if (enabled) {
                reset_compartment_transaction(compartment_idx);
            } else {
                force_compartment_status_publish[compartment_idx] = true;
            }
            ESP_LOGW(TAG, "Compartment %d maintenance %s",
                     compartment_idx + 1, enabled ? "enabled" : "disabled");
        }
        return;
    }

    if (strcmp(topic, "ptit/verified/order") == 0) {
        if (strcmp(data, "1") == 0) {
            ESP_LOGI(TAG, "Order QR matches a reservation; waiting for backend locker/open command");
        } else if (strcmp(data, "0") == 0) {
            bool available[2];
            for (int i = 0; i < 2; i++) {
                available[i] = !locker_maintenance &&
                               !compartment_maintenance[i] &&
                               !hx711_fault[i] &&
                               !compartment_has_item[i] &&
                               door_closed_status[i];
            }
            ESP_LOGI(TAG, "Order QR has no reservation; showing available compartments");
            gui_show_compartment_selection(available);
        } else {
            ESP_LOGW(TAG, "Invalid order QR result: %s", data);
        }
        return;
    }

    if (locker_maintenance) {
        ESP_LOGW(TAG, "Ignoring command while locker is under maintenance");
        return;
    }

    // 1. Xử lý xác minh shipper
    if (strcmp(topic, "ptit/verified/shipper") == 0) {
        if (strcmp(data, "1") == 0) {
            ESP_LOGI(TAG, "Xác minh thành công (1) -> Vào giao diện gửi hàng");
            gui_open_send_screen();
        }
        else if (strcmp(data, "0") == 0) {
            ESP_LOGW(TAG, "Xác minh thất bại (0)");
            gui_show_verify_failed();
        }
    }
    // 2. Xử lý xác minh khách hàng nhận hàng
    else if (strcmp(topic, "ptit/verified/customer") == 0) {
        int result = atoi(data);
        if (result > 0) {
            char order_msg[32];
            snprintf(order_msg, sizeof(order_msg), "So dien thoai co %d don", result);
            nhap_show_has_order(order_msg);
        }
        else if (result == 0) {
            ESP_LOGW(TAG, "So dien thoai khong co don nao");
            nhap_show_no_order();
        }
        else if (result == -1) {
            ESP_LOGW(TAG, "So dien thoai khong ton tai");
            nhan_show_verify_failed();
        }
    }
    // 3. Nhập sai mã OTP / QR nhận hàng
    else if (strcmp(topic, "ptit/verified/qr_otp/failed") == 0) {
        if (strcmp(data, "-1") == 0) {
            ESP_LOGI(TAG, "Nhập sai mã nhận hàng rồi!");
            gui_show_wrong_otp();
        }
    }
    // 4. Xử lý lệnh mở khóa từ Server (chỉ tủ có đúng LOCKER_ID mới nhận được)
    else if (strcmp(topic, "ptit/locker/open") == 0) {
        int compartment_idx = -1;
        char mode_str[16] = {0};

        if (sscanf(data, "%d,%15s", &compartment_idx, mode_str) == 2) {
            char open[32];
            if (compartment_idx >= 0 && compartment_idx < 2) {
                if (compartment_maintenance[compartment_idx]) {
                    ESP_LOGW(TAG, "Ignoring open command for compartment %d under maintenance",
                             compartment_idx + 1);
                    return;
                }
                ESP_LOGI(TAG, "Nhận lệnh mở ngăn số: %d, chế độ: %s", compartment_idx, mode_str);
                if (strcmp(mode_str, "deposit") == 0) {
                    set_compartment_expecting(compartment_idx, true);
                }
                else if (strcmp(mode_str, "pickup") == 0) {
                    set_compartment_expecting(compartment_idx, false);
                }

                open_lock(compartment_idx);
                snprintf(open, sizeof(open), "Da mo ngan %d", compartment_idx + 1);
                show_lock_open(open);
            }
        }
        else {
            compartment_idx = atoi(data);
            if (compartment_idx >= 0 && compartment_idx < 2) {
                if (compartment_maintenance[compartment_idx]) {
                    ESP_LOGW(TAG, "Ignoring open command for compartment %d under maintenance",
                             compartment_idx + 1);
                    return;
                }
                ESP_LOGI(TAG, "Nhận lệnh mở ngăn số (mặc định): %d", compartment_idx);
                set_compartment_expecting(compartment_idx, true);
                open_lock(compartment_idx);
            }
        }
    }
}

// Task đọc dữ liệu từ module GM65
void uart_read_task(void *arg) {
    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);
    while (1) {
        int len = uart_read_bytes(UART_PORT_NUM, data, (BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        if (len > 0) {
            data[len] = '\0';
            ESP_LOGI(TAG, "Recv str: %s", (char *) data);
            if (qr_queue != NULL) {
                // Queue copy đúng 64 byte -> dùng buffer 64 byte đã xóa sạch
                char item[64] = {0};
                strncpy(item, (char *) data, sizeof(item) - 1);
                xQueueSend(qr_queue, item, 0);
            }
        }
    }
    free(data);
}

void business_logic_task(void *arg) {
    char qr_buffer[64];
    while (1) {
        if (qr_queue != NULL && xQueueReceive(qr_queue, &qr_buffer, portMAX_DELAY) == pdTRUE) {
            if (!scan_enabled) {
                ESP_LOGW(TAG, "Bỏ qua mã quét do chưa bấm nút quét!");
                continue;
            }

            ESP_LOGI(TAG, "Xử lý mã QR hợp lệ: %s", qr_buffer);
            // Payload gửi đi: {"locker_id":"...","qr":"<mã QR>"}
            bool is_order_qr = strcmp(topic_rec_qr, "qr/order") == 0;
            if (is_order_qr) {
                portENTER_CRITICAL(&current_order_qr_mux);
                snprintf(current_order_qr, sizeof(current_order_qr), "%s", qr_buffer);
                portEXIT_CRITICAL(&current_order_qr_mux);
            }
            int publish_result = my_mqtt_publish_kv(topic_rec_qr, "qr", qr_buffer, 1);
            if (publish_result < 0 && is_order_qr) {
                portENTER_CRITICAL(&current_order_qr_mux);
                current_order_qr[0] = '\0';
                portEXIT_CRITICAL(&current_order_qr_mux);
            }
            scan_enabled = false;
        }
    }
}
/*
* =========================================================================
 * PHÂN CÔNG:
 *  - warning_task      : đếm thời gian cửa mở, điều khiển CÒI, hiện màn hình
 *                        cảnh báo MỘT LẦN khi vừa vào trạng thái cảnh báo.
 *                        KHÔNG bao giờ gọi về home.
 *  - event_handler_task: xử lý giao dịch (deposit/pickup) và là task DUY NHẤT
 *                        quyết định khi nào về home / hiện lỗi.
 * ========================================================================= */

void warning_task(void *arg) {
    bool prev_compartment_warning[2] = {false, false};
    
    while (1) {
        bool any_warning = false;

        for (int i = 0; i < 2; i++) {
            if (locker_maintenance || compartment_maintenance[i]) {
                door_open_counter[i] = 0;
                compartment_warning_active[i] = false;
                continue;
            }

            if (!door_closed_status[i]) {
                if (door_open_counter[i] < WARNING_OPEN_TICKS) {
                    door_open_counter[i]++;
                }
                if (door_open_counter[i] >= WARNING_OPEN_TICKS) {
                    if (!compartment_warning_active[i]) {
                        ESP_LOGW(TAG_WARN, "Cảnh báo: Ngăn [%d] bị mở quá lâu không đóng!", i + 1);
                    }
                    compartment_warning_active[i] = true;
                }
            } else {
                door_open_counter[i] = 0;
                compartment_warning_active[i] = false;
            }

            if (compartment_warning_active[i]) {
                any_warning = true;
            }
        }

        // Còi theo trạng thái cảnh báo
        gpio_set_level(BUZZER_PIN, any_warning ? 1 : 0);

        bool warning_state_changed =
            compartment_warning_active[0] != prev_compartment_warning[0] ||
            compartment_warning_active[1] != prev_compartment_warning[1];

        // Cập nhật màn hình khi danh sách ngăn đang cảnh báo thay đổi.
        if (any_warning && warning_state_changed) {
            char warning_message[32];
            if (compartment_warning_active[0] && compartment_warning_active[1]) {
                snprintf(warning_message, sizeof(warning_message), "Chua dong ngan 1 va 2");
            } else if (compartment_warning_active[0]) {
                snprintf(warning_message, sizeof(warning_message), "Chua dong ngan 1");
            } else {
                snprintf(warning_message, sizeof(warning_message), "Chua dong ngan 2");
            }
            gui_show_warning_screen(warning_message);
        }

        prev_compartment_warning[0] = compartment_warning_active[0];
        prev_compartment_warning[1] = compartment_warning_active[1];
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void event_handler_task(void *arg) {
    while (1) {
        for (int i = 0; i < 2; i++) {
            char event_topic[64];
            char event_payload[128];
            snprintf(event_topic, sizeof(event_topic), TOPIC_EVENT_FMT, i + 1);

            if (locker_maintenance || compartment_maintenance[i]) {
                reset_compartment_transaction(i);
                prev_door_closed_status[i] = door_closed_status[i];
                continue;
            }

            // ===== A. KHÁCH LẤY HÀNG KHI CỬA CÒN MỞ -> xác nhận thành công ngay =====
            if (waiting_for_pickup[i] && !door_closed_status[i]) {
                if (compartment_has_item[i]) {
                    pickup_seen_item[i] = true;   // đã thấy hàng trong ngăn
                    empty_stable_cnt[i] = 0;
                } else if (pickup_seen_item[i]) {
                    // Hàng đã rời đi, chờ ngăn trống ổn định ~1s để tránh nhiễu
                    if (++empty_stable_cnt[i] >= EMPTY_STABLE_TICKS) {
                        ESP_LOGI(TAG_EVENT, "Ngăn [%d]: khách đã lấy hàng (cửa vẫn mở)", i + 1);
                        snprintf(event_payload, sizeof(event_payload),
                                 "{\"status\":\"pickup_success\", \"door_closed\":0, \"compartment\":%d}", i + 1);
                        my_mqtt_publish_json(event_topic, event_payload, 1);

                        waiting_for_pickup[i] = false;  // không gửi lặp khi đóng cửa
                        pickup_seen_item[i]   = false;
                        empty_stable_cnt[i]   = 0;
                        // Không đổi màn hình ở đây: warning_task sẽ cảnh báo nếu quá 30s,
                        // và khi cửa đóng thì phần B sẽ đưa về home.
                    }
                }
            } else {
                empty_stable_cnt[i] = 0;
            }

            // ===== B1. PHÁT HIỆN CỬA VỪA ĐÓNG (Mở -> Đóng) -> bắt đầu chờ cân ổn định =====
            if (!prev_door_closed_status[i] && door_closed_status[i]) {
                ESP_LOGI(TAG_EVENT, "Ngăn [%d] vừa được sập cửa đóng lại!", i + 1);
                close_settle_cnt[i] = CLOSE_SETTLE_TICKS;
            }

            // Cửa mở lại trong lúc đang chờ -> hủy đợt đánh giá
            if (!door_closed_status[i]) {
                close_settle_cnt[i] = 0;
            }

            // ===== B2. HẾT THỜI GIAN CHỜ -> đánh giá kết quả và quyết định màn hình =====
            if (close_settle_cnt[i] > 0 && --close_settle_cnt[i] == 0) {
                bool show_exception = false;

                // Trường hợp 1: Shipper gửi hàng
                if (waiting_for_deposit[i]) {
                    if (!compartment_has_item[i]) {
                        ESP_LOGW(TAG_EVENT, "Ngoại lệ: Shipper không bỏ hàng vào ngăn [%d]!", i + 1);
                        snprintf(event_payload, sizeof(event_payload),
                                 "{\"status\":\"deposit_cancelled\", \"reason\":\"no_item_detected\", \"compartment\":%d}", i + 1);
                        my_mqtt_publish_json(event_topic, event_payload, 1);
                        gui_show_exception("Failed! Chua bo hang vao trong");
                        show_exception = true;
                    } else {
                        ESP_LOGI(TAG_EVENT, "Gửi hàng thành công vào ngăn [%d]", i + 1);
                        snprintf(event_payload, sizeof(event_payload),
                                 "{\"status\":\"deposit_success\", \"weight\":%.1f, \"compartment\":%d}",
                                 compartment_weight[i], i + 1);
                        my_mqtt_publish_json(event_topic, event_payload, 1);
                    }
                    waiting_for_deposit[i] = false;
                }

                // Trường hợp 2: Khách lấy hàng (chưa được xác nhận sớm ở phần A)
                if (waiting_for_pickup[i]) {
                    if (compartment_has_item[i]) {
                        ESP_LOGW(TAG_EVENT, "Ngoại lệ: Khách quên lấy hàng ra khỏi ngăn [%d]!", i + 1);
                        snprintf(event_payload, sizeof(event_payload),
                                 "{\"status\":\"item_uncollected\", \"weight\":%.1f, \"compartment\":%d}",
                                 compartment_weight[i], i + 1);
                        my_mqtt_publish_json(event_topic, event_payload, 1);
                        gui_show_exception("Failed! Chua lay hang ra");
                        show_exception = true;
                    } else {
                        ESP_LOGI(TAG_EVENT, "Khách đã lấy hàng thành công khỏi ngăn [%d]", i + 1);
                        snprintf(event_payload, sizeof(event_payload),
                                 "{\"status\":\"pickup_success\", \"door_closed\":1, \"compartment\":%d}", i + 1);
                        my_mqtt_publish_json(event_topic, event_payload, 1);
                    }
                    waiting_for_pickup[i] = false;
                }

                // Không có lỗi -> về home (cũng thoát màn hình cảnh báo vốn không có nút).
                // Nếu ngăn còn lại vẫn đang cảnh báo thì giữ nguyên màn hình cảnh báo.
                int other = 1 - i;
                if (!show_exception && !compartment_warning_active[other]) {
                    gui_show_home_screen();
                }

                pickup_seen_item[i] = false;
                empty_stable_cnt[i] = 0;
            }

            prev_door_closed_status[i] = door_closed_status[i];
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Task sensor: đọc HX711, MC38, IR và cập nhật MQTT trạng thái
void sensor_task(void *arg)
{
    int32_t offset[2] = {0};

    for (int i = 0; i < 2; i++) {
        esp_err_t err = hx711_tare(&hx[i], &offset[i]);
        hx711_fault[i] = (err != ESP_OK);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_SENSOR, "HX711 [%d] tare failed!", i);
        } else {
            ESP_LOGI(TAG_SENSOR, "Tare complete for [%d]. Offset=%" PRId32, i, offset[i]);
        }
    }

    bool prev_has_item[2] = {false, false};
    bool prev_ir_detected[2] = {false, false};
    bool prev_door_closed[2] = {true, true};
    bool mqtt_status_error_logged[2] = {false, false};
    bool weight_has_item[2] = {false, false};

    while (1) {
        // Đọc trạng thái cửa từ phần cứng (MC-38: 0 = Đóng, 1 = Mở)
        door_closed_status[0] = (gpio_get_level(MC38_PIN_1) == 0);
        door_closed_status[1] = (gpio_get_level(MC38_PIN_2) == 0);

        for (int i = 0; i < 2; i++) {
            bool ir_detected =
                gpio_get_level(ir_sensor_pins[i]) == IR_SENSOR_DETECTED_LEVEL;
            int32_t raw;
            esp_err_t err = hx711_read_raw(&hx[i], &raw);
            hx711_fault[i] = (err != ESP_OK);
            if (err == ESP_OK) {
                int32_t net_raw = raw - offset[i];
                float weight_grams = (float)net_raw / HX711_CALIBRATION_FACTOR;

                if (weight_grams < 0.0f) {
                    weight_grams = -weight_grams;
                }
                if (weight_grams < HX711_ZERO_THRESHOLD_G) {
                    weight_grams = 0.0f;
                }

                // Lưu vào biến toàn cục để các task khác dùng chung
                compartment_weight[i] = weight_grams;
                weight_has_item[i] = (weight_grams >= HX711_ZERO_THRESHOLD_G);
            } else {
                ESP_LOGW(TAG_SENSOR, "HX711 [%d] read failed: %s", i, esp_err_to_name(err));
            }

            compartment_has_item[i] = ir_detected || weight_has_item[i];

            // Publish IR-only changes too, including when the load cell is unavailable.
            if ((force_compartment_status_publish[i] ||
                 compartment_has_item[i] != prev_has_item[i] ||
                 ir_detected != prev_ir_detected[i] ||
                 door_closed_status[i] != prev_door_closed[i]) &&
                !locker_maintenance && !compartment_maintenance[i] &&
                my_mqtt_is_connected()) {

                char topic[64];
                char payload[128];

                snprintf(topic, sizeof(topic), TOPIC_STATUS_FMT, i + 1);
                snprintf(payload, sizeof(payload),
                         "{\"has_item\":%d, \"ir_detected\":%d, \"door_closed\":%d, \"weight\":%.1f}",
                         compartment_has_item[i], ir_detected, door_closed_status[i],
                         compartment_weight[i]);

                int publish_result = my_mqtt_publish_json(topic, payload, 1);
                if (publish_result >= 0) {
                    prev_has_item[i] = compartment_has_item[i];
                    prev_ir_detected[i] = ir_detected;
                    prev_door_closed[i] = door_closed_status[i];
                    force_compartment_status_publish[i] = false;
                    mqtt_status_error_logged[i] = false;
                    ESP_LOGI(TAG_SENSOR, "Published MQTT [%s]: %s", topic, payload);
                } else if (!mqtt_status_error_logged[i]) {
                    ESP_LOGW(TAG_SENSOR, "Failed to publish MQTT status [%s]", topic);
                    mqtt_status_error_logged[i] = true;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Locker ID: %s", LOCKER_ID);

    // 1. Config Network
    flash_init();
    wifi_init_sta("P303", "12344321");
    vTaskDelay(pdMS_TO_TICKS(5000));
    qr_queue = xQueueCreate(10, 64);              // tạo queue trước khi MQTT/UART chạy
    bool mqtt_time_ready = (sync_time_for_tls() == ESP_OK);
    if (!mqtt_time_ready) {
        ESP_LOGE(TAG, "MQTT not started because TLS certificate validation requires valid system time");
    }

    // 2. Config Device
    gpio_config_t mc38_conf = {
        .pin_bit_mask = (1ULL << MC38_PIN_1) | (1ULL << MC38_PIN_2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&mc38_conf);

    gpio_config_t ir_sensor_conf = {
        .pin_bit_mask = (1ULL << IR_SENSOR_PIN_1) | (1ULL << IR_SENSOR_PIN_2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&ir_sensor_conf));

    gpio_config_t buzzer = {
        .pin_bit_mask = (1ULL << BUZZER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&buzzer);

    uart_init(); // GM65
    lock_init(); // Khởi tạo mạch khóa 12V
    for (int i = 0; i < 2; i++) {
        hx711_init(&hx[i]);
    }
    display_touch_init();

    _lock_acquire(&lvgl_api_lock);
    gui_init();
    _lock_release(&lvgl_api_lock);

    if (mqtt_time_ready) {
        my_mqtt_init("mqtts://broker.emqx.io", on_mqtt_data);
    }

    // 3. Config Task
    xTaskCreate(example_lvgl_port_task, "LVGL", 7 * 1024, NULL, 2, NULL);
    xTaskCreate(uart_read_task, "uart_echo_task", 3072, NULL, 3, NULL);
    xTaskCreate(sensor_task, "sensor_task", 4 * 1024, NULL, 3, NULL);
    xTaskCreate(warning_task, "warning_task", 3072, NULL, 2, NULL);                 // Còi + màn hình cảnh báo
    xTaskCreate(event_handler_task, "event_handler_task", 3 * 1024, NULL, 2, NULL); // Giao dịch + quyết định về home/lỗi
    xTaskCreate(business_logic_task, "State Machine", 4 * 1024, NULL, 2, NULL);
}


/*
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hx711.h"

#define HX711_SAMPLE_COUNT 10
#define HX711_CALIBRATION_FACTOR 434.0f

static const char *TAG = "HX711_TEST";
static hx711_pin scales[] = {
    {.DT = 36, .SCK = 32},
    {.DT = 39, .SCK = 33},
};
static int32_t offsets[sizeof(scales) / sizeof(scales[0])];

static esp_err_t read_average(hx711_pin *scale, int32_t *average)
{
    int64_t total = 0;
    for (int sample = 0; sample < HX711_SAMPLE_COUNT; sample++) {
        int32_t reading;
        esp_err_t err = hx711_read_raw(scale, &reading);
        if (err != ESP_OK) {
            return err;
        }
        total += reading;
    }

    *average = (int32_t)(total / HX711_SAMPLE_COUNT);
    return ESP_OK;
}

void app_main(void)
{
    const size_t scale_count = sizeof(scales) / sizeof(scales[0]);

    for (size_t i = 0; i < scale_count; i++) {
        ESP_ERROR_CHECK(hx711_init(&scales[i]));
        ESP_ERROR_CHECK(hx711_tare(&scales[i], &offsets[i]));
        ESP_LOGI(TAG, "Can %u da tru bi, offset=%" PRId32,
                 (unsigned)(i + 1), offsets[i]);
    }
    ESP_LOGI(TAG, "Dat qua can len cac can de kiem tra gia tri RAW.");

    while (true) {
        for (size_t i = 0; i < scale_count; i++) {
            int32_t raw_average;
            esp_err_t err = read_average(&scales[i], &raw_average);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Can %u doc that bai: %s",
                         (unsigned)(i + 1), esp_err_to_name(err));
                continue;
            }

            int64_t net_raw = (int64_t)raw_average - offsets[i];
            float weight_grams = (float)net_raw / HX711_CALIBRATION_FACTOR;
            if (weight_grams < 0.0f) {
                weight_grams = -weight_grams;
            }
            ESP_LOGI(TAG, "Can %u: RAW=%" PRId32 " | sau tru bi=%" PRId64
                     " | khoi luong uoc tinh=%.1f g",
                     (unsigned)(i + 1), raw_average, net_raw, weight_grams);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
*/