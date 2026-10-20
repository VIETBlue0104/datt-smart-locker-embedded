#ifndef GUI_H
#define GUI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void gui_init(void);
void gui_open_send_screen(void);    // Gọi khi Server gửi 1
void gui_show_verify_failed(void);  // Gọi khi Server gửi 0
void nhap_OTP_nhan(void) ;
void nhan_show_verify_failed(void) ;
void nhap_show_methods(void);
void nhap_show_no_order(void);
void nhap_show_has_order(char *str) ;
void gui_show_home_screen(void) ;
void show_lock_open(char *str) ;
void gui_show_warning_screen(char *str);
void gui_show_exception(char *str) ;
#ifdef __cplusplus
}
#endif

#endif // GUI_H
