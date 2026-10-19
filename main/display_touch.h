#ifndef DISPLAY_TOUCH_H
#define DISPLAY_TOUCH_H

#include <sys/lock.h>

#ifdef __cplusplus
extern "C" {
#endif

// Khóa Lock chung cho LVGL API
extern _lock_t lvgl_api_lock;

/**
 * @brief Khởi tạo phần cứng màn hình ILI9341, Cảm ứng XPT2046 và thư viện LVGL.
 */
void display_touch_init(void);
void example_lvgl_port_task(void *arg) ;
#ifdef __cplusplus
}
#endif

#endif // DISPLAY_TOUCH_H