# 🚪 IoT ESP32 - Hệ Thống Giám Sát Cửa Thông Minh (Smart Door Monitoring)

Hệ thống giám sát cửa thông minh sử dụng vi điều khiển ESP32, tích hợp cảm biến chuyển động, cảnh báo tại chỗ và quản lý từ xa qua Cloud MQTT. Dự án bao gồm khả năng thống kê dữ liệu và gửi thông báo khẩn cấp qua Email/Node-RED.

---

## 🛠 1. Sơ đồ đấu nối (Hardware Mapping)
Dựa trên cấu hình mô phỏng `diagram.json`:
* **ESP32 DevKit V4**: Bộ điều khiển trung tâm.
* **Cảm biến PIR (GPIO 13)**: Phát hiện trạng thái cửa (Mở/Đóng).
* **LED Đỏ (GPIO 2)**: Đèn báo hiệu trạng thái vật lý của cửa.
* **Buzzer (GPIO 4)**: Phát âm thanh cảnh báo (Lưu ý: Trong code cần định nghĩa chân 4 để khớp với sơ đồ nối dây).

## 📑 2. Nguyên lý hoạt động (Logic)
1.  **Khởi động**: Thiết bị kết nối WiFi `Wokwi-GUEST` và Broker `HiveMQ Cloud` qua cổng bảo mật 8883.
2.  **Giám sát**: 
    * **Mở cửa**: Khi PIR chuyển sang mức CAO, LED bật, còi bíp ngắn và bắt đầu tính giờ.
    * **Cảnh báo quá giờ**: Nếu cửa mở quá 10 phút ($600,000$ ms), còi hú liên tục và gửi cảnh báo `warning_door_open_>10m`.
    * **Đóng cửa**: Khi PIR về mức THẤP, hệ thống tính tổng thời gian mở (giây), gửi dữ liệu lên MQTT và báo trạng thái an toàn `door_closed_safe`.
3.  **Thống kê & Bảo trì**: Node-RED theo dõi số lần đóng/mở. Nếu vượt quá 5 lần, hệ thống yêu cầu bảo trì.

## 🚀 3. Cấu trúc dữ liệu MQTT
* **Topic gửi dữ liệu**: `iot/door/open_time` (Gửi số giây mở cửa).
* **Topic cảnh báo**: `iot/door/warning` (Gửi trạng thái cảnh báo/an toàn).
* **Dữ liệu JSON (Node-RED)**:
    ```json
    {
      "event": "warning / door_closed",
      "total_time_day": 120,
      "cycle_count": 5,
      "maintenance": "REQUIRED"
    }
    ```

## 📧 4. Cấu hình Cảnh báo Email (Node-RED)
Sử dụng node **Function** để phân luồng dữ liệu và node **Template** để định dạng nội dung Email:

### ⚠️ Cảnh Báo Khẩn Cấp
**Tiêu đề**: `⚠️ CẢNH BÁO: CỬA MỞ QUÁ GIỜ QUY ĐỊNH!`
**Nội dung**:
> ⚠️ **CẢNH BÁO KHẨN CẤP**
> 
> Cửa chưa đóng (Quá 10 phút).
> ⏱ Thời gian mở hiện tại: {{payload.total_time_day}} giây.
> (Vui lòng kiểm tra hệ thống cửa ngay lập tức !!!)

### 🛠 Yêu Cầu Bảo Trì
**Tiêu đề**: `🛠️ THÔNG BÁO: YÊU CẦU BẢO TRÌ HỆ THỐNG CỬA!`
**Nội dung**:
> 🛠️ **YÊU CẦU BẢO TRÌ**
> 
> 📊 Số lần đóng/mở: {{payload.cycle_count}} lần.
> 🔧 Tình trạng: CẦN KIỂM TRA ĐỊNH KỲ.

---
### PHỤ LỤC
### Phụ lục A:(Mã nguồn chương trình Arduino)
- https://github.com/LongPrince/IoT_CamBienChuyenDong/blob/master/src/main.cpp 

### Phụ lục B:(Mã nguồn của Web-Node-Red(Json)
- https://github.com/LongPrince/IoT_CamBienChuyenDong/blob/config-file/Nhom6.json 








