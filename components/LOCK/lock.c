#include "lock.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

// Cấu hình các chân điều khiển khóa là Output
static const char *TAG = "Lock" ; 
void lock_init(void) {
    gpio_config_t lock_conf = {
        .pin_bit_mask = (1ULL << LOCK_PIN_1) | (1ULL << LOCK_PIN_2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&lock_conf);
    
    // Mặc định ở trạng thái đóng khóa (tùy thuộc vào mạch relay mức cao hay thấp, giả sử mức 0 là tắt, 1 là mở)
    gpio_set_level(LOCK_PIN_1, 0);
    gpio_set_level(LOCK_PIN_2, 0);
}

void open_lock(int compartment_index) {
    gpio_num_t target_pin;

    if (compartment_index == 0) {
        target_pin = LOCK_PIN_1;
    } else if (compartment_index == 1) {
        target_pin = LOCK_PIN_2;
    } else {
        ESP_LOGE(TAG, "Index ngan tu khong hop le: %d", compartment_index);
        return;
    }

    ESP_LOGI(TAG, "Dang mo khoa cho ngan so: %d (GPIO %d)", compartment_index + 1, target_pin);
    gpio_set_level(target_pin, 1);       // Bật Rơ-le kích mở khóa
    vTaskDelay(pdMS_TO_TICKS(3000));     // Giữ mở 3 giây
    gpio_set_level(target_pin, 0);       // Tắt Rơ-le
    ESP_LOGI(TAG, "Da dong khoa ngan so: %d", compartment_index + 1);
}

