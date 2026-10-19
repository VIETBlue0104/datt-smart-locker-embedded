idf_component_register(
    SRCS "main.c" "display_touch.c" "gui.c"
    INCLUDE_DIRS "."
    REQUIRES espressif__esp_lcd_ili9341 atanisoft__esp_lcd_touch_xpt2046 esp_lcd lvgl esp_timer my_mqtt my_wifi
    HX711 GM65 LOCK
)