# Smart Locker - Firmware ESP32

Firmware cho tủ giao nhận hàng thông minh (ESP32 + ESP-IDF). Tủ giao tiếp với backend Spring Boot qua MQTT.
Mỗi tủ có một **Locker ID** riêng, backend dựa vào ID này để biết bản tin của tủ nào và mở đúng tủ.

Hướng dẫn topic, payload, luồng bản tin và cách theo dõi bằng MQTTX: [MQTT_GUIDE.md](./MQTT_GUIDE.md).

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

Vào **Smart Locker Config**, nhập `Locker ID` (ví dụ `khuon_vien`) và có thể cấu hình thêm `MQTT Username`, `MQTT Password` theo thông tin broker. Để trống các trường đăng nhập nếu broker không yêu cầu xác thực. Lưu, rồi build và flash lại.

Quy tắc:

- ID phải **trùng từng ký tự** với cột `locker_id` trong bảng `lockers` của database (phân biệt hoa thường, không có khoảng trắng).
- Mỗi tủ một ID riêng. ID cũng là `client_id` MQTT nên không được trùng giữa các tủ.
- Broker đang dùng là broker công cộng, nên đặt ID đủ riêng (ví dụ `ptit_khuon_vien`) để không trùng với người khác.

Khi chạy đúng, log hiển thị `MQTT_EVENT_CONNECTED, locker_id=<ID>`.

Username và password được nhúng vào firmware từ `sdkconfig`; không commit file cấu hình có credentials lên repository công khai. Kết nối MQTT hiện sử dụng TLS để bảo vệ credentials khi truyền tới broker.

## Quy ước MQTT

- Ký hiệu `{id}` là Locker ID, `N` là số ngăn (**tính từ 1**).
- **Tủ -> backend:** mọi topic đều nằm dưới `ptit/{id}/...`; payload là **JSON** với trường `locker_id`, ngoại trừ trạng thái kết nối.
- **Backend -> tủ:** ID nằm trong **topic** (`ptit/{id}/...`), payload là chuỗi thuần ngắn. Tủ chỉ subscribe topic của chính nó.
- Các API publish của firmware nhận đường dẫn tương đối với Locker ID; firmware tự thêm `ptit/{id}/`. Topic dùng chữ thường, phân biệt hoa thường.
- Firmware cắt tiền tố `ptit/{id}/` khỏi topic nhận về; callback xử lý các tên như `ptit/locker/open`.
- Đây là hợp đồng topic mới: backend cần chuyển sang các topic bên dưới; firmware không còn publish các topic cũ toàn cục như `ptit/QR_send`, và tên `shiper` đã được chuẩn hóa thành `shipper`.

### 1. Trạng thái kết nối (tủ -> backend)

| Topic | Payload | Ghi chú |
|---|---|---|
| `ptit/{id}/status` | `ONLINE` / `OFFLINE` (chuỗi thuần) | `retain`, QoS 1. Gửi `ONLINE` khi kết nối và mỗi 30 giây. `OFFLINE` do Last Will, broker tự phát khi tủ mất kết nối |

### 2. Thao tác trên màn hình (tủ -> backend)

| Topic | Payload |
|---|---|
| `ptit/{id}/qr/shipper` | `{"locker_id":"...","qr":"<mã shipper>"}` |
| `ptit/{id}/qr/order` | `{"locker_id":"...","qr":"<mã đơn hàng>"}` |
| `ptit/{id}/qr/receive` | `{"locker_id":"...","qr":"<mã nhận hàng>"}` |
| `ptit/{id}/receive/phone` | `{"locker_id":"...","phone":"090..."}` |
| `ptit/{id}/receive/otp` | `{"locker_id":"...","otp":"123456"}` |
| `ptit/{id}/session/cancel` | `{"locker_id":"...","action":"..."}`; các action: `cancel_send`, `cancel_receive`, `cancel_order_scan`, `cancel_receive_phone`, `cancel_receive_qr`, `cancel_receive_otp` |
| `ptit/{id}/locker/scan_order_qr/request` | `{"locker_id":"...","action":"scan_order_qr"}`; thông báo bắt đầu quét, khác với bản tin QR thực tế |
| `ptit/{id}/receive/scan_qr/request` | `{"locker_id":"...","action":"scan_receive_qr"}`; thông báo bắt đầu quét, khác với bản tin QR thực tế |
| `ptit/{id}/locker/select_compartment` | `{"compartment":0,"qr":"<mã đơn>","locker_id":"..."}`; index bắt đầu từ 0, tủ mở ngăn ngay khi chọn |

Mỗi thao tác QR có topic riêng theo mục đích; không dùng chung topic để tránh backend nhầm QR shipper, QR đơn và QR nhận hàng.

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
| `ptit/{id}/verified/shipper` | `1` / `0` | Xác minh shipper thành công / thất bại |
| `ptit/{id}/verified/order` | `1` / `0` | QR đơn có lịch đặt trước / không có lịch đặt trước |
| `ptit/{id}/verified/customer` | `-1` / `0` / `n` | SĐT không tồn tại / không có đơn / có `n` đơn |
| `ptit/{id}/verified/qr_otp/failed` | `-1` | Nhập sai OTP hoặc QR nhận hàng |

> Chú ý chỉ số ngăn: database và topic `compartment/N` tính từ **1**, còn lệnh `locker/open` gửi xuống tủ tính từ **0** (backend tự đổi).

### 5. Chế độ bảo trì

Backend gửi lệnh retained (QoS 1) tới các topic:

| Topic | Payload | Tác dụng |
|---|---|---|
| `ptit/{id}/maintenance/locker` | `1` / `0` | Bật / tắt bảo trì toàn tủ. Khi bật, màn hình báo bảo trì, mọi thao tác thường và bản tin ứng dụng bị chặn, hai LED chuyển xanh dương. Kết nối MQTT và heartbeat `ONLINE` vẫn duy trì. |
| `ptit/{id}/maintenance/compartment` | `<idx>,<state>` ví dụ `0,1` hoặc `0,0` | Bật / tắt bảo trì ngăn có index `idx` (0 hoặc 1). Ngăn đó không nhận lệnh mở và không gửi status/event; LED ngăn chuyển xanh dương. Ngăn còn lại vẫn hoạt động bình thường. |

`1` là bảo trì, `0` là hoạt động bình thường. Backend cần publish trạng thái retained để tủ khôi phục chế độ sau khi reconnect/khởi động lại; chỉ xóa bảo trì sau khi admin được xác thực. Firmware không xác minh danh tính người publish nên broker/backend phải giới hạn quyền publish các topic này.

## Luồng hoạt động

**Shipper gửi hàng**

1. Bấm GỬI HÀNG, quét mã shipper -> `ptit/{id}/qr/shipper` -> backend trả `ptit/{id}/verified/shipper`.
2. Quét mã đơn -> firmware gửi yêu cầu `ptit/{id}/locker/scan_order_qr/request`, sau đó gửi QR lên `ptit/{id}/qr/order`. Backend trả `ptit/{id}/verified/order`: nếu `1`, backend gửi `ptit/{id}/locker/open` (`<idx>,deposit`) cho ngăn đã đặt; nếu `0`, TFT hiển thị ngăn trống khả dụng.
3. Với nhánh `0`, shipper chọn ngăn -> firmware mở ngăn ngay và gửi `ptit/{id}/locker/select_compartment` gồm `compartment`, `qr`, `locker_id` để backend lưu thông tin đơn.

**Khách nhận hàng**

1. Nhập SĐT -> `ptit/{id}/receive/phone` -> backend trả `ptit/{id}/verified/customer` (số đơn).
2. Chọn nhập OTP (`ptit/{id}/receive/otp`) hoặc quét QR (yêu cầu `ptit/{id}/receive/scan_qr/request`, mã gửi lên `ptit/{id}/qr/receive`).
3. Đúng: backend gửi `ptit/{id}/locker/open` (`<idx>,pickup`). Sai: gửi `ptit/{id}/verified/qr_otp/failed`; TFT báo sai và cho nhập lại.
4. Lấy hàng, đóng cửa -> event `pickup_success` hoặc `item_uncollected`.

## Kiểm tra bằng MQTTX

1. Kết nối `broker.emqx.io` bằng TLS (port `8883`), subscribe `ptit/#` (QoS 1) để xem mọi bản tin.
2. Sau khi tủ khởi động phải thấy `ptit/{id}/status` = `ONLINE`.
3. Giả lập backend: publish chuỗi thuần (chọn định dạng **Plaintext**, không chọn JSON), ví dụ:
   - topic `ptit/{id}/locker/open`, payload `0,deposit`
   - topic `ptit/{id}/verified/customer`, payload `2`
   - topic `ptit/{id}/maintenance/locker`, payload `1` hoặc `0`, bật **Retain** để trạng thái được khôi phục sau reconnect
   - topic `ptit/{id}/maintenance/compartment`, payload `0,1` hoặc `0,0`, bật **Retain**
4. Khi thử bằng MQTTX nên tắt backend để bản tin không bị trả lời chồng.

## Ghi chú

- MQTT dùng TLS với CA bundle mặc định của ESP-IDF để xác minh chứng chỉ broker. Firmware đồng bộ thời gian qua SNTP trước khi khởi tạo MQTT; nếu đồng bộ thất bại, MQTT không được khởi động thay vì hạ cấp về kết nối không mã hóa.
- TLS bảo vệ đường truyền và xác minh broker, nhưng broker công cộng vẫn cho phép người khác publish vào các topic công khai. Khi triển khai thật cần broker riêng có tài khoản và ACL; cân nhắc xác thực thiết bị hai chiều nếu yêu cầu.
- Wi-Fi SSID và mật khẩu hiện còn viết cứng trong `main.c`, nên chuyển sang `menuconfig` trước khi công khai repo.
