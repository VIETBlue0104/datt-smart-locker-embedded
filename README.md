# Smart Locker - Firmware ESP32

Firmware cho tủ giao nhận hàng thông minh (ESP32 + ESP-IDF). Tủ giao tiếp với backend Spring Boot qua MQTT.
Mỗi tủ có một **Locker ID** riêng, backend dựa vào ID này để biết bản tin của tủ nào và mở đúng tủ.

## Cấu trúc

```
main/                  main.c, gui.c (LVGL), display_touch.c
components/
  my_mqtt/             MQTT client, tự gắn locker_id, ONLINE/OFFLINE (Last Will)
  my_wifi/             Wi-Fi station
  HX711/               cân loadcell từng ngăn
  GM65/                đầu đọc QR (UART)
  LOCK/                điều khiển khóa solenoid 12V
```

## Cấu hình Locker ID

```cmd
idf.py menuconfig
```

Vào **Smart Locker Config -> Locker ID**, nhập ID (ví dụ `khuon_vien`), lưu, rồi build và flash lại.

Quy tắc:

- ID phải **trùng từng ký tự** với cột `locker_id` trong bảng `lockers` của database (phân biệt hoa thường, không có khoảng trắng).
- Mỗi tủ một ID riêng. ID cũng là `client_id` MQTT nên không được trùng giữa các tủ.
- Broker đang dùng là broker công cộng, nên đặt ID đủ riêng (ví dụ `ptit_khuon_vien`) để không trùng với người khác.

Khi chạy đúng, log hiển thị `MQTT_EVENT_CONNECTED, locker_id=<ID>`.

## Quy ước MQTT

- Ký hiệu `{id}` là Locker ID, `N` là số ngăn (**tính từ 1**).
- **Tủ -> backend:** payload luôn là **JSON**, có trường `locker_id` ở cấp ngoài cùng (trừ topic status).
- **Backend -> tủ:** ID nằm trong **topic** (`ptit/{id}/...`), payload là chuỗi thuần ngắn. Tủ chỉ subscribe topic của chính nó.
- Firmware tự cắt tiền tố `ptit/{id}/` khỏi topic nhận về, nên `main.c` vẫn so sánh theo tên ngắn (`ptit/locker/open`...).
- Topic phân biệt hoa thường.

### 1. Trạng thái kết nối (tủ -> backend)

| Topic | Payload | Ghi chú |
|---|---|---|
| `ptit/status/{id}` | `ONLINE` / `OFFLINE` (chuỗi thuần) | `retain`, QoS 1. Gửi `ONLINE` khi kết nối và mỗi 30 giây. `OFFLINE` do Last Will, broker tự phát khi tủ mất kết nối |

### 2. Thao tác trên màn hình (tủ -> backend)

| Topic | Payload |
|---|---|
| `ptit/QR_Shipper` | `{"locker_id":"...","qr":"<mã shipper>"}` |
| `ptit/QR_send` | `{"locker_id":"...","qr":"<mã đơn hàng>"}` |
| `ptit/QR_rec` | `{"locker_id":"...","qr":"<mã nhận hàng>"}` |
| `ptit/rec/phone` | `{"locker_id":"...","phone":"090..."}` |
| `ptit/rec/otp` | `{"locker_id":"...","otp":"123456"}` |
| `ptit/cancel` | `{"locker_id":"...","action":"cancel_send"}` hoặc `"cancel_receive"` |
| `ptit/locker/cancel_scan` | `{"locker_id":"...","action":"cancel_scan"}` |
| `ptit/locker/scan_order_qr_req` | `{"locker_id":"...","action":"scan_order_qr"}` |
| `ptit/rec/scan_qr_req` | `{"locker_id":"...","action":"scan_receive_qr"}` |
| `ptit/rec/cancel_phone` | `{"locker_id":"...","action":"cancel_phone"}` |
| `ptit/rec/cancel_qr` | `{"locker_id":"...","action":"cancel_qr"}` |
| `ptit/rec/cancel_otp` | `{"locker_id":"...","action":"cancel_otp"}` |

Mã QR được gửi lên topic nào tùy màn hình đang chờ quét (shipper, gửi hàng hoặc nhận hàng).

### 3. Cảm biến và sự kiện từng ngăn (tủ -> backend)

| Topic | Payload |
|---|---|
| `ptit/{id}/compartment/N/status` | `{"has_item":1,"door_closed":1,"weight":120.5,"locker_id":"..."}` |
| `ptit/{id}/compartment/N/event` | `{"status":"<sự kiện>", ..., "compartment":N,"locker_id":"..."}` |

Các giá trị `status` của event:

| status | Ý nghĩa | Trường kèm theo |
|---|---|---|
| `deposit_success` | Shipper bỏ hàng và đóng cửa | `weight` |
| `deposit_cancelled` | Đóng cửa nhưng không có hàng | `reason` = `no_item_detected` |
| `pickup_success` | Khách lấy hàng xong | `door_closed` (0 nếu lấy khi cửa còn mở, 1 nếu đã đóng) |
| `item_uncollected` | Khách đóng cửa nhưng còn hàng trong ngăn | `weight` |

### 4. Lệnh từ backend xuống tủ

| Topic | Payload | Ý nghĩa |
|---|---|---|
| `ptit/{id}/locker/open` | `<idx>,<mode>` ví dụ `0,deposit`, `1,pickup` | Mở ngăn. **`idx` tính từ 0**, `mode` là `deposit` (shipper gửi) hoặc `pickup` (khách lấy) |
| `ptit/{id}/verified/shiper` | `1` / `0` | Xác minh shipper thành công / thất bại |
| `ptit/{id}/verified/customer` | `-1` / `0` / `n` | SĐT không tồn tại / không có đơn / có `n` đơn |
| `ptit/{id}/verified/QR_OTP/failed` | `-1` | Nhập sai OTP hoặc QR nhận hàng |

> Chú ý chỉ số ngăn: database và topic `compartment/N` tính từ **1**, còn lệnh `locker/open` gửi xuống tủ tính từ **0** (backend tự đổi).

## Luồng hoạt động

**Shipper gửi hàng**

1. Bấm GỬI HÀNG, quét mã shipper -> `ptit/QR_Shipper` -> backend trả `verified/shiper`.
2. Quét mã đơn -> `ptit/QR_send` -> backend gửi `locker/open` (`<idx>,deposit`).
3. Tủ mở khóa, shipper bỏ hàng, đóng cửa -> tủ gửi event `deposit_success` hoặc `deposit_cancelled`.

**Khách nhận hàng**

1. Nhập SĐT -> `ptit/rec/phone` -> backend trả `verified/customer` (số đơn).
2. Chọn nhập OTP (`ptit/rec/otp`) hoặc quét QR (`ptit/QR_rec`).
3. Đúng: backend gửi `locker/open` (`<idx>,pickup`). Sai: `verified/QR_OTP/failed`, màn hình hiện "Nhap sai ma OTP" và quay lại nhập lại.
4. Lấy hàng, đóng cửa -> event `pickup_success` hoặc `item_uncollected`.

## Kiểm tra bằng MQTTX

1. Kết nối `broker.emqx.io`, subscribe `ptit/#` (QoS 1) để xem mọi bản tin.
2. Sau khi tủ khởi động phải thấy `ptit/status/{id}` = `ONLINE`.
3. Giả lập backend: publish chuỗi thuần (chọn định dạng **Plaintext**, không chọn JSON), ví dụ:
   - topic `ptit/{id}/locker/open`, payload `0,deposit`
   - topic `ptit/{id}/verified/customer`, payload `2`
4. Khi thử bằng MQTTX nên tắt backend để bản tin không bị trả lời chồng.

## Ghi chú

- Nhánh này chưa có phần TLS cho MQTT và LED báo hiệu của nhánh `Embedded-Frimware`. Cần gộp hai nhánh trước khi dùng thực tế.
- Broker công cộng ai cũng publish được, nên lệnh mở khóa có thể bị giả mạo. Khi triển khai thật cần broker riêng có tài khoản, ACL và TLS.
- Wi-Fi SSID và mật khẩu hiện còn viết cứng trong `main.c`, nên chuyển sang `menuconfig` trước khi công khai repo.
