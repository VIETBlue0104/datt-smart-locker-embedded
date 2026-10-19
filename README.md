# Smart Delivery Locker - Embedded Firmware

Hệ thống điều khiển tủ giao hàng thông minh (Smart Locker 2 ngăn) sử dụng ESP32, FreeRTOS và giao thức MQTT kết nối Server.

---

## 📡 Danh sách MQTT Topics & Định dạng Payload

### 1. Topics Gửi Dữ Liệu từ ESP32 (Publish by ESP32)

#### ✉️ Mã QR quét từ đầu đọc GM65 (Theo từng luồng chức năng)
- **Xác minh Shipper:**
  * **Topic:** `ptit/QR_Shiper`
  * **QoS:** 1
  * **Mô tả:** ESP32 gửi mã QR định danh của Shipper lên Server để xác thực tài khoản.
  * **Payload:** `String` (Ví dụ: `SHIPPER_12345`)

- **Mã vận đơn / Gửi hàng:**
  * **Topic:** `ptit/QR_send`
  * **QoS:** 1
  * **Mô tả:** ESP32 gửi mã vận đơn do Shipper quét lên Server để liên kết đơn hàng với ngăn tủ.
  * **Payload:** `String` (Ví dụ: `ORDER_998877`)

- **Mã nhận hàng của Khách:**
  * **Topic:** `ptit/QR_rec`
  * **QoS:** 1
  * **Mô tả:** ESP32 gửi mã QR/mã OTP nhận hàng của khách lên Server để kiểm tra đơn hàng.
  * **Payload:** `String` (Ví dụ: `CUST_665544`)

---

#### 📊 Cập nhật Trạng thái Ngăn tủ & Cảm biến
- **Topic:** 
  * Ngăn 1: `ptit/chung2004/compartment/1/status`
  * Ngăn 2: `ptit/chung2004/compartment/2/status`
- **QoS:** 1
- **Mô tả:** Đẩy định kỳ hoặc khi có thay đổi từ cảm biến trọng lượng (HX711) và cảm biến cửa (MC-38).
- **Payload (JSON):**
  ```json
  {
    "has_item": 1,
    "door_closed": 1,
    "weight": 125.5
  }
--
#### 🚨 Báo cáo Sự kiện & Ngoại lệ Giao dịch
- **Topic:** 
  * Ngăn 1: ptit/chung2004/compartment/1/event
  * Ngăn 2: ptit/chung2004/compartment/2/event
- **QoS:** 1
- **Mô tả:**Gửi báo cáo lên Server khi xảy ra các sự kiện kết thúc giao dịch hoặc ngoại lệ (VD: Shipper hủy, không phát hiện hàng, khách lấy hàng thành công,...).
- **Payload (JSON):**
  ```json
  {
  "status": "deposit_cancelled",
  "reason": "no_item_detected",
  "compartment": 1
  }
  
  ```json
  {
    {"status":"item_uncollected", "weight":31.5, "compartment":2}
  }
  
  ```json
  {
    {"status":"deposit_success", "weight":8.4, "compartment":2}
  }

  ```json
  {
    {"status":"pickup_success", "compartment":2}
  }
  

### 2. Topics Lắng Nghe Kết Quả từ Server (Subscribe by ESP32)

- **Kết quả xác minh Shipper:**
  * **Topic:** `ptit/verified/shiper`
  * **Mô tả:** Hứng kết quả xác minh thành công hay thất bại từ Server cho tài khoản Shipper.
  * **Payload:** `"1"` (Thành công) / `"0"` (Thất bại)

- **Kết quả xác minh Khách hàng / Đơn hàng:**
  * **Topic:** `ptit/verified/customer`
  * **Mô tả:** Hứng kết quả kiểm tra đơn hàng/mã nhận hàng từ Server.
  * **Payload:** `">0"` (Tìm thấy số lượng đơn hàng) / `"0"` hoặc `"-1"` (Thất bại / Không có đơn)
 
****Lệnh mở khóa ngăn tủ chỉ định:**
  * **Topic:** ptit/locker/open
  * **Mô tả:** Nhận lệnh điều khiển từ Server để mở chốt khóa 12V cho ngăn tủ tương ứng.
  * **Payload:** <compartment_index>,<mode> (compartment_index: ngăn số mấy, mode: Shiper hay khách đang giao dịch)
