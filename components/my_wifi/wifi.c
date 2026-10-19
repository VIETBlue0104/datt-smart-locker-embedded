#include "wifi.h"
#include <string.h>
const char *TAG = "SERVER";
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)// Khi wifi station bắt đầu  hoạt động, tự động kết bối
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Disconnected, retrying...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)// Khi wifi đã kết nối và nhận đc IP, in ra IP
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void flash_init() {
    esp_err_t ret = nvs_flash_init();// Khởi tạo flash để lưu trữ thông tin wifi
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());// Xóa flash nếu có lỗi
        ret = nvs_flash_init();// Khởi tạo lại
    }
    ESP_ERROR_CHECK(ret);// Kiểm tra lỗi

    
    ESP_ERROR_CHECK(esp_netif_init());// Khởi tạo TCP/IP adapter
    ESP_ERROR_CHECK(esp_event_loop_create_default());// Tạo event loop mặc định để xử lí sự kiện wifi và IP
}
void wifi_init_sta(const char* WIFI_SSID,const char* WIFI_PASS) {
    esp_netif_create_default_wifi_sta();// Tạo interface wifi station mặc định
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();// Cấu hình wifi mặc định
    esp_wifi_init(&cfg);// KHởi tạo wifi với cấu hình đã thiết lập

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,// Bắt tất cả sự kiện wifi
                                        &wifi_event_handler,// Hàm xử lí sự kiện wifi
                                        NULL,// k có dữ liệu truyền vào
                                        &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT,// Bắt sự kiện IP khi đã kết nối wifi và nhận đc IP
                                        IP_EVENT_STA_GOT_IP,// Chỉ bắt sự kiện khi đã nhận đc IP
                                        &wifi_event_handler,
                                        NULL,
                                        &instance_got_ip);// Đăng kí sự kiện IP

    wifi_config_t wifi_config = { 0 };

    
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));// Copy SSID
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));// Copy Password
    
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;// Chỉ kết nôi với AP có bảo mật WPA2 trở lên

    esp_wifi_set_mode(WIFI_MODE_STA);// Khởi động wifi ở chế độ station
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);// Cấu hình thông tin kết nố cho wifi
    esp_wifi_start();

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "Connecting to SSID:%s password:%s", WIFI_SSID, WIFI_PASS);
}
