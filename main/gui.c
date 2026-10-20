#include "gui.h"
#include "display_touch.h"
#include "esp_log.h"
#include "mqtt.h"

// Đặt ở đầu file gui.c
extern void set_scan_enabled(bool enable);
extern void rec_topic(char *topic) ;
extern QueueHandle_t qr_queue ;
static const char *TAG_GUI = "gui";

// Quản lý màn hình
static lv_obj_t *main_scr = NULL;
static lv_obj_t *method_scr = NULL;          // Màn hình lựa chọn (quét QR gửi)
static lv_obj_t *loading_scr = NULL;         // Màn hình chờ
static lv_obj_t *receive_scr = NULL;         // Màn hình nhận hàng (nhập SĐT)
static lv_obj_t *receive_method_scr = NULL;  // Màn hình chọn phương thức nhận (OTP hoặc Quét QR)
static lv_obj_t *otp_scr = NULL;             // Màn hình nhập OTP

// Con trỏ đối tượng UI
static lv_obj_t *lbl_loading_status = NULL;
static lv_obj_t *ta_recv_phone = NULL;
static lv_obj_t *ta_otp = NULL;
static lv_obj_t *kb_numpad = NULL;

// Prototypes
static void create_loading_screen(const char *text, lv_event_cb_t back_cb);
static void create_loading_screen_with_actions(const char *text, lv_event_cb_t back_cb, lv_event_cb_t confirm_cb);
static void create_main_screen(void);
static void create_method_screen(void);
static void create_receive_screen(void);
static void create_receive_method_screen(void);
static void create_otp_screen(void);

// Ngăn việc quét mã QR ngay khi nhấn các nút quay lại của màn hình quét
static void cancel_scan() {
    set_scan_enabled(false) ;
    if(qr_queue != NULL) {
        xQueueReset(qr_queue) ;
    }
}

/* =========================================================================
 * CALLBACK SỰ KIỆN & ĐIỀU HƯỚNG QUAY LẠI
 * ========================================================================= */

// Quay lại Màn hình chính (Hủy chung luồng Gửi)
static void back_to_main_cb_from_send_screen_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_screen_load(main_scr);
        my_mqtt_publish("/ptit/cancel", "Huy bo", 1);
        cancel_scan() ;
    }
}

// Quay lại Màn hình chính (Hủy luồng nhận)
static void back_to_main_cb_from_rec_screen_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_screen_load(main_scr);
        my_mqtt_publish("/ptit/cancel", "Huy bo", 1);
    }
}

// Quay lại Màn hình lựa chọn phương thức (Hủy quét mã đơn)
static void back_to_method_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        my_mqtt_publish("/ptit/locker/cancel_scan", "Huy quet ma", 1);
        cancel_scan() ;
        if (method_scr != NULL) {
            lv_screen_load(method_scr);
        } else {
            create_method_screen();
            lv_screen_load(method_scr);
        }
    }
}

// Quay lại Màn hình nhập SĐT từ màn hình OTP hoặc khi đang kiểm tra SĐT
static void back_to_receive_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        my_mqtt_publish("ptit/rec/cancel_phone", "Huy kiem tra SDT", 1);
        if (receive_scr != NULL) {
            lv_screen_load(receive_scr);
        } else {
            create_receive_screen();
            lv_screen_load(receive_scr);
        }
    }
}

// Quay lại Màn hình lựa chọn phương thức nhận hàng từ màn hình chờ xác minh QR
static void back_to_receive_method_from_QR_method_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        my_mqtt_publish("ptit/rec/cancel_QR", "Huy xac minh OTP", 1);
        cancel_scan() ;
        if (receive_method_scr != NULL) {
            lv_screen_load(receive_method_scr);
        } else {
            create_receive_method_screen();
            lv_screen_load(receive_method_scr);
        }
    }
}

// Quay lại màn hình lựa chọn phương thức nhận hàng từ màn hình nhập và chờ xác minh OTP
static void back_to_receive_method_from_otp_method_cb(lv_event_t *e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        my_mqtt_publish("ptit/rec/cancel_otp", "Huy xac minh OTP", 1);
        if (receive_method_scr != NULL) {
            lv_screen_load(receive_method_scr);
        } else {
            create_receive_method_screen();
            lv_screen_load(receive_method_scr);
        }
    }
}

static void back_home_cb (lv_event_t *e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        gui_show_home_screen() ;
    }
}
static void back_home_from_failed_cb (lv_event_t *e) {
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_screen_load(main_scr);
    }

}

// Callback khi nhấn Xác nhận trên màn hình thông báo có đơn -> chuyển sang màn hình chọn phương thức
static void confirm_has_order_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (receive_method_scr != NULL) {
            lv_screen_load(receive_method_scr);
        } else {
            create_receive_method_screen();
            lv_screen_load(receive_method_scr);
        }
    }
}

// Bấm "GỬI HÀNG" từ Màn hình chính
static void send_btn_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        create_loading_screen("Dua QR vao may", back_to_main_cb_from_send_screen_cb);
        lv_screen_load(loading_scr);
        rec_topic("ptit/QR_Shipper") ;
        set_scan_enabled(true) ;
    }
}

// Bấm "NHẬN HÀNG" từ Màn hình chính
static void receive_btn_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        create_receive_screen();
        lv_screen_load(receive_scr);
    }
}

// Bấm Nút: "QUÉT MÃ QR" (Gửi hàng)
static void scan_qr_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG_GUI, "Bat dau quet ma QR don hang");
        create_loading_screen("Dua ma van don vao may quet", back_to_method_cb);
        lv_screen_load(loading_scr);
        my_mqtt_publish("/ptit/locker/scan_order_qr_req", "Quet QR Don", 1);
        rec_topic("ptit/QR_send") ;
        set_scan_enabled(true) ;
    }
}

// Chọn phương thức "NHẬP MÃ OTP" khi nhận hàng
static void choose_otp_method_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        create_otp_screen();
        lv_screen_load(otp_scr);
    }
}

// Chọn phương thức "QUÉT MÃ QR" khi nhận hàng
static void choose_qr_method_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG_GUI, "Bat dau quet ma QR nhan hang");
        create_loading_screen("Dua ma QR nhan hang vao may", back_to_receive_method_from_QR_method_cb);
        lv_screen_load(loading_scr);
        my_mqtt_publish("/ptit/rec/scan_qr_req", "Quet QR Nhan", 1);
        rec_topic("ptit/QR_rec") ;
        set_scan_enabled(true) ;
    }
}

// Focus bàn phím ảo
static void ta_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        if (kb_numpad != NULL) {
            lv_keyboard_set_textarea(kb_numpad, ta);
        }
    }
}

// Xác nhận Nhận hàng (SĐT)
static void confirm_receive_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        const char *phone = lv_textarea_get_text(ta_recv_phone);
        create_loading_screen("Dang kiem tra SDT...", back_to_receive_cb);
        my_mqtt_publish("ptit/rec/phone", phone, 1);
        lv_screen_load(loading_scr);
    }
}

// Xác nhận OTP Nhận hàng
static void confirm_otp_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        const char *otp_code = lv_textarea_get_text(ta_otp);
        my_mqtt_publish("ptit/rec/otp", otp_code, 1);
        ESP_LOGI(TAG_GUI, "Xac nhan OTP: %s", otp_code);

        create_loading_screen("Dang xac minh OTP...", back_to_receive_method_from_otp_method_cb);
        lv_screen_load(loading_scr);
    }
}

/* =========================================================================
 * KHỞI TẠO CÁC MÀN HÌNH
 * ========================================================================= */

// 1. Màn hình chính
static void create_main_screen(void) {
    if (main_scr) {
        lv_obj_clean(main_scr);
    } else {
        main_scr = lv_obj_create(NULL);
    }

    lv_obj_set_style_bg_color(main_scr, lv_color_hex(0x1E1E2E), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(main_scr);
    lv_label_set_text(title, "TU GIAO HANG THONG MINH");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // Nút GỬI HÀNG
    lv_obj_t *btn_send = lv_button_create(main_scr);
    lv_obj_set_size(btn_send, 130, 130);
    lv_obj_align(btn_send, LV_ALIGN_LEFT_MID, 20, 15);
    lv_obj_set_style_bg_color(btn_send, lv_color_hex(0x2EB85C), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_send, 12, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_send, send_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_send = lv_label_create(btn_send);
    lv_label_set_text(lbl_send, "GUI HANG");
    lv_obj_center(lbl_send);

    // Nút NHẬN HÀNG
    lv_obj_t *btn_receive = lv_button_create(main_scr);
    lv_obj_set_size(btn_receive, 130, 130);
    lv_obj_align(btn_receive, LV_ALIGN_RIGHT_MID, -20, 15);
    lv_obj_set_style_bg_color(btn_receive, lv_color_hex(0x39F0D8), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_receive, 12, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_receive, receive_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_receive = lv_label_create(btn_receive);
    lv_label_set_text(lbl_receive, "NHAN HANG");
    lv_obj_set_style_text_color(lbl_receive, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_center(lbl_receive);
}

// 2. Màn hình Chờ / Thông báo linh hoạt (Hỗ trợ 1 hoặc 2 nút)
static void create_loading_screen_with_actions(const char *text, lv_event_cb_t back_cb, lv_event_cb_t confirm_cb) {
    if (loading_scr) {
        lv_obj_clean(loading_scr);
    } else {
        loading_scr = lv_obj_create(NULL);
    }

    lv_obj_set_style_bg_color(loading_scr, lv_color_hex(0x1E1E2E), LV_PART_MAIN);

    lbl_loading_status = lv_label_create(loading_scr);
    lv_label_set_text(lbl_loading_status, text);
    lv_obj_set_style_text_color(lbl_loading_status, lv_color_hex(0xFFD700), LV_PART_MAIN);
    lv_obj_center(lbl_loading_status);

    if (confirm_cb != NULL) {
        // Có nút Xác nhận -> Hiển thị 2 nút (Quay lại và Xác nhận)
        lv_obj_t *btn_back = lv_button_create(loading_scr);
        lv_obj_set_size(btn_back, 110, 35);
        lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 40, -15);
        lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xE55353), LV_PART_MAIN);
        lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl_back = lv_label_create(btn_back);
        lv_label_set_text(lbl_back, "Quay lai");
        lv_obj_center(lbl_back);

        lv_obj_t *btn_confirm = lv_button_create(loading_scr);
        lv_obj_set_size(btn_confirm, 110, 35);
        lv_obj_align(btn_confirm, LV_ALIGN_BOTTOM_RIGHT, -40, -15);
        lv_obj_set_style_bg_color(btn_confirm, lv_color_hex(0x2EB85C), LV_PART_MAIN);
        lv_obj_add_event_cb(btn_confirm, confirm_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *lbl_confirm = lv_label_create(btn_confirm);
        lv_label_set_text(lbl_confirm, "Xac nhan");
        lv_obj_center(lbl_confirm);
    } else if (back_cb != NULL){
        // Chỉ có 1 nút Quay lại ở giữa
        lv_obj_t *btn_back = lv_button_create(loading_scr);
        lv_obj_set_size(btn_back, 110, 35);
        lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -15);
        lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xE55353), LV_PART_MAIN);
        lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);//add event

        lv_obj_t *lbl_back = lv_label_create(btn_back);
        lv_label_set_text(lbl_back, "Quay lai");
        lv_obj_center(lbl_back);
    }
}

static void create_loading_screen(const char *text, lv_event_cb_t back_cb) {
    create_loading_screen_with_actions(text, back_cb, NULL);
}

// 3. Màn hình Lựa chọn Phương thức Gửi
static void create_method_screen(void) {
    if (method_scr) {
        lv_obj_clean(method_scr);
    } else {
        method_scr = lv_obj_create(NULL);
    }

    lv_obj_set_style_bg_color(method_scr, lv_color_hex(0x1E1E2E), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(method_scr);
    lv_label_set_text(title, "CHON PHUONG THUC");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    lv_obj_t *btn_qr = lv_button_create(method_scr);
    lv_obj_set_size(btn_qr, 180, 120);
    lv_obj_align(btn_qr, LV_ALIGN_CENTER, 0, -15);
    lv_obj_set_style_bg_color(btn_qr, lv_color_hex(0x10B981), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_qr, 12, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_qr, scan_qr_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_qr = lv_label_create(btn_qr);
    lv_label_set_text(lbl_qr, "QUET MA\nQR CODE");
    lv_obj_set_style_text_align(lbl_qr, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl_qr);

    lv_obj_t *btn_back = lv_button_create(method_scr);
    lv_obj_set_size(btn_back, 110, 35);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xE55353), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, back_to_main_cb_from_send_screen_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "Quay lai");
    lv_obj_center(lbl_back);
}

// 4. Màn hình Nhận hàng (SĐT)
static void create_receive_screen(void) {
    if (receive_scr) {
        lv_obj_clean(receive_scr);
    } else {
        receive_scr = lv_obj_create(NULL);
    }

    lv_obj_set_style_bg_color(receive_scr, lv_color_hex(0x1E1E2E), LV_PART_MAIN);

    lv_obj_t *lbl_title = lv_label_create(receive_scr);
    lv_label_set_text(lbl_title, "NHAN HANG");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 10, 10);

    ta_recv_phone = lv_textarea_create(receive_scr);
    lv_textarea_set_one_line(ta_recv_phone, true);
    lv_textarea_set_placeholder_text(ta_recv_phone, "So dien thoai");
    lv_obj_set_size(ta_recv_phone, 140, 40);
    lv_obj_align(ta_recv_phone, LV_ALIGN_TOP_LEFT, 10, 40);
    lv_obj_add_event_cb(ta_recv_phone, ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *btn_confirm = lv_button_create(receive_scr);
    lv_obj_set_size(btn_confirm, 140, 40);
    lv_obj_align(btn_confirm, LV_ALIGN_TOP_LEFT, 10, 95);
    lv_obj_set_style_bg_color(btn_confirm, lv_color_hex(0x2EB85C), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_confirm, confirm_receive_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_confirm = lv_label_create(btn_confirm);
    lv_label_set_text(lbl_confirm, "XAC NHAN");
    lv_obj_center(lbl_confirm);

    lv_obj_t *btn_back = lv_button_create(receive_scr);
    lv_obj_set_size(btn_back, 140, 40);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 150);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xE55353), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, back_to_main_cb_from_rec_screen_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "Quay lai");
    lv_obj_center(lbl_back);

    kb_numpad = lv_keyboard_create(receive_scr);
    lv_keyboard_set_mode(kb_numpad, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(kb_numpad, 150, 220);
    lv_obj_align(kb_numpad, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_keyboard_set_textarea(kb_numpad, ta_recv_phone);
}

// 5. Màn hình lựa chọn phương thức nhận hàng (OTP hoặc Quét QR)
static void create_receive_method_screen(void) {
    if (receive_method_scr) {
        lv_obj_clean(receive_method_scr);
    } else {
        receive_method_scr = lv_obj_create(NULL);
    }

    lv_obj_set_style_bg_color(receive_method_scr, lv_color_hex(0x1E1E2E), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(receive_method_scr);
    lv_label_set_text(title, "CHON PHUONG THUC NHAN");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // Nút Nhập OTP
    lv_obj_t *btn_otp = lv_button_create(receive_method_scr);
    lv_obj_set_size(btn_otp, 180, 55);
    lv_obj_align(btn_otp, LV_ALIGN_CENTER, 0, -25);
    lv_obj_set_style_bg_color(btn_otp, lv_color_hex(0x3B82F6), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_otp, 12, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_otp, choose_otp_method_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_otp = lv_label_create(btn_otp);
    lv_label_set_text(lbl_otp, "NHAP MA OTP");
    lv_obj_center(lbl_otp);

    // Nút Quét mã QR nhận
    lv_obj_t *btn_qr = lv_button_create(receive_method_scr);
    lv_obj_set_size(btn_qr, 180, 55);
    lv_obj_align(btn_qr, LV_ALIGN_CENTER, 0, 45);
    lv_obj_set_style_bg_color(btn_qr, lv_color_hex(0x10B981), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_qr, 12, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_qr, choose_qr_method_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_qr = lv_label_create(btn_qr);
    lv_label_set_text(lbl_qr, "QUET MA QR");
    lv_obj_center(lbl_qr);

    // Nút Quay lại
    lv_obj_t *btn_back = lv_button_create(receive_method_scr);
    lv_obj_set_size(btn_back, 110, 35);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xE55353), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, back_to_receive_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "Quay lai");
    lv_obj_center(lbl_back);
}

// 6. Màn hình nhập OTP Nhận hàng
static void create_otp_screen(void) {
    if (otp_scr) {
        lv_obj_clean(otp_scr);
    } else {
        otp_scr = lv_obj_create(NULL);
    }

    lv_obj_set_style_bg_color(otp_scr, lv_color_hex(0x1E1E2E), LV_PART_MAIN);

    lv_obj_t *lbl_title = lv_label_create(otp_scr);
    lv_label_set_text(lbl_title, "NHAP MA OTP");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 10, 10);

    ta_otp = lv_textarea_create(otp_scr);
    lv_textarea_set_one_line(ta_otp, true);
    lv_textarea_set_placeholder_text(ta_otp, "Ma OTP");
    lv_obj_set_size(ta_otp, 140, 40);
    lv_obj_align(ta_otp, LV_ALIGN_TOP_LEFT, 10, 40);
    lv_obj_add_event_cb(ta_otp, ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *btn_confirm = lv_button_create(otp_scr);
    lv_obj_set_size(btn_confirm, 140, 40);
    lv_obj_align(btn_confirm, LV_ALIGN_TOP_LEFT, 10, 95);
    lv_obj_set_style_bg_color(btn_confirm, lv_color_hex(0x2EB85C), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_confirm, confirm_otp_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_confirm = lv_label_create(btn_confirm);
    lv_label_set_text(lbl_confirm, "XAC NHAN");
    lv_obj_center(lbl_confirm);

    lv_obj_t *btn_back = lv_button_create(otp_scr);
    lv_obj_set_size(btn_back, 140, 40);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 150);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xE55353), LV_PART_MAIN);
    lv_obj_add_event_cb(btn_back, back_to_receive_method_from_otp_method_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "Quay lai");
    lv_obj_center(lbl_back);

    kb_numpad = lv_keyboard_create(otp_scr);
    lv_keyboard_set_mode(kb_numpad, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(kb_numpad, 150, 220);
    lv_obj_align(kb_numpad, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_keyboard_set_textarea(kb_numpad, ta_otp);
}

/* =========================================================================
 * CÁC HÀM GỌI TỪ NGOÀI (CÓ KHÓA LOCK)
 * ========================================================================= */

void gui_open_send_screen(void) {
    _lock_acquire(&lvgl_api_lock);
    create_method_screen();
    lv_screen_load(method_scr);
    _lock_release(&lvgl_api_lock);
}

void nhap_OTP_nhan(void) {
    _lock_acquire(&lvgl_api_lock);
    create_otp_screen();
    lv_screen_load(otp_scr);
    _lock_release(&lvgl_api_lock);
}

void gui_show_verify_failed(void) {
    _lock_acquire(&lvgl_api_lock);
    create_loading_screen("Ma shipper ko hop le", back_to_main_cb_from_send_screen_cb);
    _lock_release(&lvgl_api_lock);
}

void nhan_show_verify_failed(void) {
    _lock_acquire(&lvgl_api_lock);
    create_loading_screen("Sdt ko ton tai", back_to_receive_cb);
    _lock_release(&lvgl_api_lock);
}

// Màn hình thông báo có đơn (Hiển thị số lượng đơn kèm 2 nút Quay lại & Xác nhận)
void nhap_show_has_order(char *str) {
    _lock_acquire(&lvgl_api_lock);
    create_loading_screen_with_actions(str, back_to_receive_cb, confirm_has_order_cb);
    _lock_release(&lvgl_api_lock);
}

void nhap_show_no_order(void) {
    _lock_acquire(&lvgl_api_lock);
    create_loading_screen("Ban ko co don nao", back_to_receive_cb);
    _lock_release(&lvgl_api_lock);
}

void gui_init(void) {
    create_main_screen();
    lv_screen_load(main_scr);
}
void gui_show_home_screen(void) {
    _lock_acquire(&lvgl_api_lock);
    lv_screen_load(main_scr); // Chỉ load lại màn hình chính đã có sẵn
    _lock_release(&lvgl_api_lock);
}

void show_lock_open(char *str) {
    _lock_acquire(&lvgl_api_lock);
    create_loading_screen(str,back_home_cb) ;
    _lock_release(&lvgl_api_lock);
}

void gui_show_warning_screen(char *str) {
    _lock_acquire(&lvgl_api_lock) ;
    create_loading_screen(str,NULL) ;
    lv_screen_load(loading_scr);
    _lock_release(&lvgl_api_lock) ;
}
void gui_show_exception(char *str) {
    _lock_acquire(&lvgl_api_lock) ;
    create_loading_screen(str, back_home_from_failed_cb) ;
    _lock_release(&lvgl_api_lock) ;
}
