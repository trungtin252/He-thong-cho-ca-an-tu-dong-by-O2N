# 🐟 ESP8266 Feeding System

## 📘 1. Giới thiệu

Chương trình này chạy trên **ESP8266** để kết nối **Arduino** (điều khiển máy cho cá ăn) với **server**.  
ESP8266 sẽ đảm nhiệm các chức năng chính sau:

- 📡 **Nhận dữ liệu** từ Arduino (lịch cho ăn, nhật ký, thời gian thực).
- 🌐 **Gửi dữ liệu** này lên server qua HTTP/JSON.
- 🔁 **Nhận lệnh** từ Serial hoặc Server rồi **forward xuống Arduino**.
- ❤️ **Gửi heartbeat** và status định kỳ để giám sát hệ thống.

---

## ⚙️ 2. Yêu cầu phần cứng

| Thành phần              | Mô tả                     |
| ----------------------- | ------------------------- |
| 🧠 **ESP8266**          | NodeMCU, Wemos D1 Mini, … |
| 🤖 **Arduino Uno/Nano** | hoặc tương tự             |
| ⏰ **RTC Module**       | DS3231 hoặc tương đương   |
| ⚙️ **Module động cơ**   | Dùng cho hệ thống cho ăn  |
| 🔌 **Kết nối UART**     | ESP8266 ↔ Arduino         |

### 🔧 Kết nối UART

| ESP8266 Pin | Arduino Pin | Chức năng    |
| ----------- | ----------- | ------------ |
| D1 (GPIO5)  | TX          | Nhận dữ liệu |
| D2 (GPIO4)  | RX          | Gửi dữ liệu  |

---

## 🧩 3. Cách cài đặt

### 🪜 Bước 1: Chuẩn bị

1. Cài **Arduino IDE**.
2. Cài **ESP8266 board** trong Arduino IDE  
   _(Board Manager → esp8266 by ESP8266 Community)_
3. Cài **thư viện cần thiết**:
   ```cpp
   ESP8266WiFi
   ESP8266HTTPClient
   ArduinoJson
   SoftwareSerial
   ```

---

### 🌐 Bước 2: Cấu hình WiFi và server

Trong file code, thay đổi các thông tin dưới đây:

```cpp
const char* ssid = "Tên WiFi";
const char* password = "Mật khẩu WiFi";

const char* logServer = "http://your-server/receiver.php";
// const char* commandServer = "http://your-server/command.php"; // có thể bật lại
```

🔧 **Thay thế:**

- `ssid`, `password`: thông tin WiFi của bạn
- `logServer`: địa chỉ server của bạn

---

### 🚀 Bước 3: Nạp code

1. Chọn board: **NodeMCU 1.0 (ESP-12E Module)** hoặc board tương ứng.
2. Kết nối ESP8266 qua **USB**.
3. Nhấn **Upload** để nạp chương trình.

---

### 🔍 Bước 4: Kiểm tra kết nối

1. Mở **Serial Monitor (9600 baud)**.
2. Khi ESP kết nối WiFi thành công → sẽ in ra **IP Address**.
3. Nếu gửi dữ liệu thành công → sẽ thấy log như:
   ```
   Schedule sent
   Logs sent
   Heartbeat sent
   ```

---

## 🧠 4. Gửi lệnh điều khiển

Bạn có thể gửi **lệnh JSON** qua Serial Monitor (hoặc từ server).

| 📜 Lệnh                | 💬 Ví dụ JSON                                                       |
| ---------------------- | ------------------------------------------------------------------- |
| Lấy logs               | `{ "command": "get_logs" }`                                         |
| Lấy schedule           | `{ "command": "get_schedule" }`                                     |
| Cho ăn 0.5kg           | `{ "command": "feed", "amount": 0.5 }`                              |
| Cập nhật lịch          | `{ "command": "update_schedule", "schedule": "08:00,12:00,18:00" }` |
| Xóa log                | `{ "command": "delete_log" }`                                       |
| Lấy thời gian RTC      | `{ "command": "get_time" }`                                         |
| Cập nhật thời gian RTC | `{ "command": "set_time", "datetime": "2025-10-02 15:45:00" }`      |

---

## 📤 5. Dữ liệu ESP gửi lên server

ESP8266 gửi dữ liệu qua **HTTP POST** dạng **JSON**.

### 🗓️ Schedule

```json
{
  "type": "schedule",
  "device": "feeding_system",
  "timestamp": 123456,
  "data": ["08:00", "12:00", "18:00"]
}
```

### 📖 Logs

```json
{
  "type": "logs",
  "date": "2025-10-02",
  "data": "08:00 - Fed 0.5kg\n12:00 - Fed 0.5kg\n",
  "device": "feeding_system",
  "timestamp": 123456
}
```

### 🧾 Status

```json
{
  "type": "status",
  "wifi_rssi": -60,
  "free_heap": 35000,
  "uptime": 123456,
  "ip": "192.168.1.50"
}
```

### ❤️ Heartbeat

```json
{
  "type": "heartbeat",
  "device": "feeding_system",
  "timestamp": 123456
}
```

---

## 🔁 6. Chu kỳ hoạt động

1. 📶 Kết nối WiFi.
2. 🔍 Nghe dữ liệu từ Arduino → nếu có thì gửi lên server.
3. ❤️ Gửi **heartbeat** mỗi 5 phút.
4. 💬 Có thể nhận lệnh từ **Serial** hoặc **Server** _(nếu bật checkServerCommand())_.
