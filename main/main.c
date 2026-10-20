#include "esp_log.h"
#include "display_touch.h"
#include "gui.h"
#include "wifi.h"
#include "mqtt.h"
#include "hx711.h"
#include "gm65.h"
#include "driver/uart.h"
#include "lock.h"
#include <inttypes.h>

static const char *TAG = "main";
static const char *TAG_SENSOR = "SENSOR";
static const char *TAG_WARN = "WARNING";
static const char *TAG_EVENT = "EVENT";

#define HX711_ZERO_THRESHOLD_G 5.0f
#define HX711_CALIBRATION_FACTOR 2280.0f

#define MC38_PIN_1 GPIO_NUM_34  // Cảm biến từ cửa ngăn 1
#define MC38_PIN_2 GPIO_NUM_35  // Cảm biến từ cửa ngăn 2
#define BUZZER_PIN GPIO_NUM_27

QueueHandle_t qr_queue = NULL;
static volatile bool scan_enabled = false;
static char topic_rec_qr[64] = "ptit/Code"; // Giá trị mặc định ban đầu

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
static bool door_closed_status[2] = {true, true};
static bool prev_door_closed_status[2] = {true, true};
static float compartment_weight[2] = {0.0f, 0.0f};
static bool compartment_has_item[2] = {false, false};

static int door_open_counter[2] = {0, 0};
static bool compartment_warning_active[2] = {false, false};

// Cờ theo dõi phiên giao dịch
static bool waiting_for_deposit[2] = {false, false}; // Đang chờ shipper bỏ hàng
static bool waiting_for_pickup[2]  = {false, false}; // Đang chờ khách lấy hàng

void rec_topic(char *topic) {
    if (topic != NULL) {
        snprintf(topic_rec_qr, sizeof(topic_rec_qr), "%s", topic);
    }
}

void set_scan_enabled(bool enable) {
    scan_enabled = enable;
}

void set_compartment_expecting(int index, bool is_deposit) {
    if (index >= 0 && index < 2) {
        if (is_deposit) {
            waiting_for_deposit[index] = true;
            waiting_for_pickup[index]  = false;
        } else {
            waiting_for_pickup[index]  = true;
            waiting_for_deposit[index] = false;
        }
    }
}

void on_mqtt_data(const char *topic, const char *data, int data_len)
{
    ESP_LOGI(TAG, "MQTT Received [%s]: %s", topic, data);

    // 1. Xử lý xác minh shipper
    if (strcmp(topic, "ptit/verified/shiper") == 0) {
        if (strcmp(data, "1") == 0) {
            ESP_LOGI(TAG, "Xác minh thành công (1) -> Vào giao diện nhập SĐT");
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
    
    // 3. Xử lý lệnh mở khóa từ Server
    else if (strcmp(topic, "ptit/locker/open") == 0) {
        int compartment_idx = -1;
        char mode_str[16] = {0};

        if (sscanf(data, "%d,%15s", &compartment_idx, mode_str) == 2) {
            char open[32];
            if (compartment_idx >= 0 && compartment_idx < 2) {
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
                xQueueSend(qr_queue, data, 0);
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
            my_mqtt_publish(topic_rec_qr, qr_buffer, 1);
            scan_enabled = false; 
        }
    }
}

// Task cảnh báo riêng biệt (giám sát thời gian mở cửa, bật còi & hiển thị UI cảnh báo)
void warning_task(void *arg) {
    bool prev_any_warning = false; // Biến ghi nhớ trạng thái cảnh báo cũ

    while (1) {
        bool any_warning = false;

        for (int i = 0; i < 2; i++) {
            if (!door_closed_status[i]) {
                door_open_counter[i]++;
                if (door_open_counter[i] >= 60) { // Mở quá 30 giây
                    compartment_warning_active[i] = true;
                    ESP_LOGW(TAG_WARN, "Cảnh báo: Ngăn [%d] bị mở quá lâu không đóng!", i + 1);
                }
            } else {
                door_open_counter[i] = 0;
                compartment_warning_active[i] = false;
            }

            if (compartment_warning_active[i]) {
                any_warning = true;
            }
        }

        if (any_warning) {
            gpio_set_level(BUZZER_PIN, 1);
            gui_show_warning_screen("Chua dong ngan tu"); 
        } else {
            gpio_set_level(BUZZER_PIN, 0);
            
            // Chỉ gọi về màn hình chính MỘT LẦN DUY NHẤT khi vừa hết cảnh báo (vừa đóng cửa lại)
            if (prev_any_warning == true) {
                gui_show_home_screen(); 
            }
        }

        prev_any_warning = any_warning; // Cập nhật trạng thái cho chu kỳ sau
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Thêm mảng cờ toàn cục ở đầu file main.c nếu cần
static bool is_showing_exception[2] = {false, false};

void event_handler_task(void *arg) {
    while (1) {
        for (int i = 0; i < 2; i++) {
            // Phát hiện sự kiện cửa vừa sập đóng lại (Chuyển từ Mở sang Đóng)
            if (prev_door_closed_status[i] == false && door_closed_status[i] == true) {
                ESP_LOGI(TAG_EVENT, "Ngăn [%d] vừa được sập cửa đóng lại!", i + 1);

                char event_topic[64];
                char event_payload[128];
                snprintf(event_topic, sizeof(event_topic), "ptit/chung2004/compartment/%d/event", i + 1);

                // Trường hợp 1: Shipper gửi hàng
                if (waiting_for_deposit[i]) {
                    if (!compartment_has_item[i]) {
                        ESP_LOGW(TAG_EVENT, "Ngoại lệ: Shipper không bỏ hàng vào ngăn [%d]!", i + 1);
                        snprintf(event_payload, sizeof(event_payload), 
                                 "{\"status\":\"deposit_cancelled\", \"reason\":\"no_item_detected\", \"compartment\":%d}", i + 1);
                        my_mqtt_publish(event_topic, event_payload, 1);
                        
                        // CHỈ GỌI 1 LẦN NẾU CHƯA HIỂN THỊ LỖI
                        if (!is_showing_exception[i]) {
                            gui_show_exception("Failed! Chua bo hang vao trong");
                            is_showing_exception[i] = true;
                        }
                    } else {
                        ESP_LOGI(TAG_EVENT, "Gửi hàng thành công vào ngăn [%d]", i + 1);
                        snprintf(event_payload, sizeof(event_payload), 
                                 "{\"status\":\"deposit_success\", \"weight\":%.1f, \"compartment\":%d}", compartment_weight[i], i + 1);
                        my_mqtt_publish(event_topic, event_payload, 1);
                        gui_show_home_screen(); 
                        is_showing_exception[i] = false;
                    }
                    waiting_for_deposit[i] = false;
                }

                // Trường hợp 2: Khách lấy hàng
                if (waiting_for_pickup[i]) {
                    if (compartment_has_item[i]) {
                        ESP_LOGW(TAG_EVENT, "Ngoại lệ: Khách quên lấy hàng ra khỏi ngăn [%d]!", i + 1);
                        snprintf(event_payload, sizeof(event_payload), 
                                 "{\"status\":\"item_uncollected\", \"weight\":%.1f, \"compartment\":%d}", compartment_weight[i], i + 1);
                        my_mqtt_publish(event_topic, event_payload, 1);
                        
                        // CHỈ GỌI 1 LẦN NẾU CHƯA HIỂN THỊ LỖI
                        if (!is_showing_exception[i]) {
                            gui_show_exception("Failed! Chua lay hang ra");
                            is_showing_exception[i] = true;
                        }
                    } else {
                        ESP_LOGI(TAG_EVENT, "Khách đã lấy hàng thành công khỏi ngăn [%d]", i + 1);
                        snprintf(event_payload, sizeof(event_payload), 
                                 "{\"status\":\"pickup_success\", \"compartment\":%d}", i + 1);
                        my_mqtt_publish(event_topic, event_payload, 1);
                        gui_show_home_screen(); 
                        is_showing_exception[i] = false;
                    }
                    waiting_for_pickup[i] = false;
                }
            }

            // Nếu cửa mở ra lại thì reset cờ lỗi để sẵn sàng bắt sự kiện lần sau
            if (door_closed_status[i] == false) {
                is_showing_exception[i] = false;
            }

            prev_door_closed_status[i] = door_closed_status[i];
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
// Task sensor: CHỈ ĐỌC PHẦN CỨNG THUẦN TÚY (HX711 + MC38) và cập nhật MQTT trạng thái
void sensor_task(void *arg)
{
    int32_t offset[2] = {0};

    for (int i = 0; i < 2; i++) {
        if (hx711_tare(&hx[i], &offset[i]) != ESP_OK) {
            ESP_LOGE(TAG_SENSOR, "HX711 [%d] tare failed!", i);
        } else {
            ESP_LOGI(TAG_SENSOR, "Tare complete for [%d]. Offset=%" PRId32, i, offset[i]);
        }
    }

    bool prev_has_item[2] = {false, false};
    bool prev_door_closed[2] = {true, true};

    while (1) {
        // Đọc trạng thái cửa từ phần cứng (MC-38: 0 = Đóng, 1 = Mở)
        door_closed_status[0] = (gpio_get_level(MC38_PIN_1) == 0);
        door_closed_status[1] = (gpio_get_level(MC38_PIN_2) == 0);

        for (int i = 0; i < 2; i++) {
            int32_t raw;
            esp_err_t err = hx711_read_raw(&hx[i], &raw);
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
                compartment_has_item[i] = (weight_grams >= HX711_ZERO_THRESHOLD_G);

                // Gửi bản tin MQTT khi trạng thái cửa hoặc đồ vật thay đổi thực tế
                if (compartment_has_item[i] != prev_has_item[i] || door_closed_status[i] != prev_door_closed[i]) {
                    prev_has_item[i] = compartment_has_item[i];
                    prev_door_closed[i] = door_closed_status[i];

                    char topic[64];
                    char payload[64];

                    snprintf(topic, sizeof(topic), "ptit/chung2004/compartment/%d/status", i + 1);
                    snprintf(payload, sizeof(payload), "{\"has_item\":%d, \"door_closed\":%d, \"weight\":%.1f}", 
                               compartment_has_item[i], door_closed_status[i], compartment_weight[i]);

                    my_mqtt_publish(topic, payload, 1);
                    ESP_LOGI(TAG_SENSOR, "Published MQTT [%s]: %s", topic, payload);
                }

            } else {
                ESP_LOGW(TAG_SENSOR, "HX711 [%d] read failed: %s", i, esp_err_to_name(err));
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    // 1. Config Network
    flash_init();
    wifi_init_sta("iPhone", "12345678");
    vTaskDelay(pdMS_TO_TICKS(5000));
    my_mqtt_init("mqtt://broker.emqx.io", on_mqtt_data);
    qr_queue = xQueueCreate(10, 64);

    // 2. Config Device
    gpio_config_t mc38_conf = {
        .pin_bit_mask = (1ULL << MC38_PIN_1) | (1ULL << MC38_PIN_2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&mc38_conf);

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

    // 3. Config Task
    xTaskCreate(example_lvgl_port_task, "LVGL", 7 * 1024, NULL, 2, NULL);
    xTaskCreate(uart_read_task, "uart_echo_task", 3072, NULL, 3, NULL);
    xTaskCreate(sensor_task, "sensor_task", 4 * 1024, NULL, 3, NULL);
    xTaskCreate(warning_task, "warning_task", 2048, NULL, 2, NULL);           // Task giám sát còi/cảnh báo cửa mở lâu
    xTaskCreate(event_handler_task, "event_handler_task", 3 * 1024, NULL, 2, NULL); // Task xử lý ngoại lệ giao dịch khi đóng cửa
    xTaskCreate(business_logic_task, "State Machine", 4 * 1024, NULL, 2, NULL);
}
