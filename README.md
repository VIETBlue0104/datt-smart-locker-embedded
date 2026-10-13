# DATT Smart Locker Embedded

Firmware ESP32 dùng ESP-IDF. Chức năng hiện tại gồm đọc mã vạch GM65 qua UART và đọc khối lượng từ load cell qua HX711.

## Cấu trúc mã nguồn

```text
datt-smart-locker-embedded/
|-- main/
|   |-- main.c                         # Diem khoi dong app_main()
|   `-- CMakeLists.txt
|-- components/
|   |-- locker_app/
|   |   |-- include/locker_app.h       # API khoi dong ung dung
|   |   |-- locker_app.c               # Khoi tao va dieu phoi cac chuc nang
|   |   |-- locker_config.h            # GPIO, UART, hieu chuan, chu ky doc
|   |   `-- CMakeLists.txt
|   |-- gm65_uart/
|   |   |-- include/gm65_uart.h        # API va cau hinh may quet
|   |   |-- gm65_uart.c                # UART va task nhan ma vach
|   |   `-- CMakeLists.txt
|   |-- hx711/
|   |   |-- include/hx711.h            # API va cau hinh HX711
|   |   |-- hx711.c                    # GPIO, timeout, du lieu ADC 24 bit
|   |   `-- CMakeLists.txt
|   `-- weight_sensor/
|       |-- include/weight_sensor.h    # API can
|       |-- weight_sensor.c            # Tare, doi sang gram, nguong ve 0
|       `-- CMakeLists.txt
|-- docs/                              # Tai lieu du an
|-- hardware/                          # So do nguyen ly va PCB
|-- CMakeLists.txt
`-- sdkconfig                          # Cau hinh ESP-IDF tren may
```

`main` gọi `locker_app`. Ứng dụng sử dụng `gm65_uart` và `weight_sensor`; `weight_sensor` sử dụng driver `hx711`. Mỗi component khai báo phụ thuộc trong `CMakeLists.txt` và công khai API qua thư mục `include/`.

Driver HX711 chỉ xử lý giao tiếp phần cứng. Module `weight_sensor` quản lý offset, lấy trung bình khi tare và chuyển số đọc sang gram. `locker_app` quyết định thứ tự khởi tạo, thời điểm tare và chu kỳ hiển thị kết quả. Khi thêm chức năng điều khiển khóa, cảm biến cửa hoặc kết nối mạng, tạo component tương ứng rồi gọi từ `locker_app`.

## Cấu hình và luồng hoạt động

Sửa thông số phần cứng và vận hành tại `components/locker_app/locker_config.h`:

| Thông số | Giá trị hiện tại |
| --- | --- |
| GM65 | UART2, RX GPIO16, 9600 baud, 8N1 |
| HX711 | DOUT GPIO4, SCK GPIO5, kênh A, gain 128 |
| Timeout chờ HX711 | 1 giây |
| Tare | Trung bình 10 mẫu sau khi chờ 500 ms |
| Hệ số hiệu chuẩn | 2280.0, cần hiệu chuẩn với quả cân đã biết |
| Ngưỡng trả về 0 | Dưới 5 gram |
| Chu kỳ đọc cân | 500 ms |

Khi khởi động, ứng dụng tạo task nhận mã vạch, khởi tạo HX711, chờ ổn định và tare với load cell không tải. Sau đó ứng dụng đọc và in khối lượng định kỳ; task GM65 tiếp tục nhận mã vạch đồng thời. Công thức hiện tại giữ giá trị tuyệt đối của khối lượng sau khi trừ offset. Nếu tare thất bại, phần đọc cân kết thúc và task GM65 vẫn chạy.

Mỗi HX711 có một `hx711_t` và mỗi cân có một `weight_sensor_t`, giúp mở rộng sang nhiều cân với trạng thái riêng. Khởi tạo bằng context được đặt về 0, tare trước khi đọc gram và tuần tự hóa truy cập nếu nhiều task dùng chung một cân. Module GM65 hiện quản lý một máy quét, khởi tạo một lần; cấu hình đầu vào được sao chép trước khi tạo task.

## Build

Mở terminal ESP-IDF tại thư mục dự án (môi trường hiện tại: ESP-IDF 5.5.3, target ESP32):

```sh
idf.py build
idf.py -p COM5 flash monitor
```

Thay `COM5` bằng cổng của board khi cần. Chỉ chạy lệnh flash khi đã kết nối board và muốn nạp firmware.
