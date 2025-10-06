# 🐟 Hệ Thống Cho Cá Ăn Tự Động (Automated Fish Feeding System)

## 📋 Mục lục

- [Giới thiệu](#-giới-thiệu)
- [Tính năng](#-tính-năng)
- [Yêu cầu phần cứng](#-yêu-cầu-phần-cứng)
- [Sơ đồ kết nối](#-sơ-đồ-kết-nối)
- [Cài đặt](#-cài-đặt)
- [Cấu hình](#-cấu-hình)
- [Sử dụng](#-sử-dụng)
- [Giao thức dữ liệu](#-giao-thức-dữ-liệu)
- [Troubleshooting](#-troubleshooting)

---

## 📘 Giới thiệu

Hệ thống cho cá ăn tự động IoT được xây dựng dựa trên **Arduino** và **ESP8266**, cho phép:

- 🤖 **Điều khiển tự động** theo lịch đã lập sẵn
- ⚖️ **Cân đo chính xác** lượng thức ăn bằng cảm biến Load Cell
- 📱 **Giám sát từ xa** qua WiFi
- 💾 **Lưu trữ dữ liệu** trên thẻ SD
- ⏰ **Đồng hồ thời gian thực** với RTC DS3231

### 🏗️ Kiến trúc hệ thống

```
┌─────────────┐      UART      ┌─────────────┐      HTTP      ┌─────────────┐
│   Arduino   │ ◄────────────► │  ESP8266    │ ◄────────────► │   Server    │
│   (Master)  │                │   (WiFi)    │                │  (Backend)  │
└─────────────┘                └─────────────┘                └─────────────┘
      │
      ├── Load Cell (HX711)
      ├── RTC DS3231
      ├── SD Card Module
      ├── Feed Motor
      ├── Release Motor
      └── Manual Button
```

---

## ✨ Tính năng

### 🎯 Chức năng chính

- ✅ **Cho ăn tự động** theo lịch (tối đa 5 lịch/ngày)
- ✅ **Cho ăn thủ công** bằng nút nhấn
- ✅ **Điều khiển từ xa** qua WiFi/Server
- ✅ **Cân đo chính xác** với Load Cell HX711
- ✅ **Lưu lịch sử** cho ăn trên thẻ SD
- ✅ **Đồng bộ thời gian** với RTC DS3231
- ✅ **Giám sát trạng thái** hệ thống realtime

### 🔧 Chức năng nâng cao

- 📊 Gửi log định kỳ lên server (mỗi 2 phút)
- ❤️ Heartbeat monitoring (mỗi 5 phút)
- 🛑 Dừng khẩn cấp (giữ nút 2 giây)
- 🔄 Cập nhật lịch từ xa
- ⏰ Đồng bộ thời gian RTC từ xa

---

## ⚙️ Yêu cầu phần cứng

### 📦 Danh sách linh kiện

| STT | Thành phần             | Số lượng | Ghi chú                |
| --- | ---------------------- | -------- | ---------------------- |
| 1   | 🧠 Arduino Uno         | 1        | Vi điều khiển chính    |
| 2   | 📡 ESP8266 NodeMCU     | 1        | Module WiFi            |
| 3   | ⚖️ Load Cell + HX711   | 1        | Cảm biến cân           |
| 4   | ⏰ RTC DS3231          | 1        | Đồng hồ thời gian thực |
| 5   | 💾 SD Card Module      | 1        | Lưu trữ dữ liệu        |
| 6   | ⚙️ DC Motor            | 2        | Motor nạp + xả thức ăn |
| 7   | 🔌 Relay Module        | 2        | Điều khiển motor       |
| 8   | 🔘 Push Button         | 1        | Nút cho ăn thủ công    |
| 9   | 🔋 Nguồn 5V            | 1        | Cấp nguồn cho hệ thống |
| 10  | 🔌 Dây nối, breadboard | -        | Kết nối các linh kiện  |

### 📚 Thư viện cần thiết

**Arduino:**

```cpp
- HX711_ADC
- SPI
- SD
- SoftwareSerial
- Wire
- RTClib
```

**ESP8266:**

```cpp
- ESP8266WiFi
- ESP8266HTTPClient
- ArduinoJson (v6.x)
- SoftwareSerial
```

---

## 🔌 Sơ đồ kết nối

### 🔧 Kết nối Arduino Uno

#### **Load Cell (HX711)**

| HX711 Pin | Arduino Pin |
| --------- | ----------- |
| DT        | A0          |
| SCK       | A1          |
| VCC       | 5V          |
| GND       | GND         |

#### **RTC DS3231**

| DS3231 Pin | Arduino Pin |
| ---------- | ----------- |
| SDA        | A4 (SDA)    |
| SCL        | A5 (SCL)    |
| VCC        | 5V          |
| GND        | GND         |

#### **SD Card Module**

| SD Card Pin | Arduino Pin |
| ----------- | ----------- |
| CS          | D10         |
| MOSI        | D11         |
| MISO        | D12         |
| SCK         | D13         |
| VCC         | 5V          |
| GND         | GND         |

#### **ESP8266 (UART)**

| ESP8266 Pin | Arduino Pin |
| ----------- | ----------- |
| D1 (GPIO5)  | D7 (TX)     |
| D2 (GPIO4)  | D6 (RX)     |
| VCC         | 3.3V/5V     |
| GND         | GND         |

#### **Các chân điều khiển**

| Thiết bị      | Arduino Pin | Ghi chú      |
| ------------- | ----------- | ------------ |
| Feed Motor    | D3          | Qua Relay    |
| Release Motor | D5          | Qua Relay    |
| Feed Button   | D2          | INPUT_PULLUP |

### ⚠️ Lưu ý quan trọng

- 🔴 **Motor** phải được điều khiển qua **Relay** hoặc **Driver**, không kết nối trực tiếp
- 🟡 **ESP8266** nên cấp nguồn **3.3V riêng** nếu có thể
- 🟢 **SD Card** sử dụng SPI chuẩn (D11, D12, D13 cố định)
- 🟣 **RTC DS3231** có pin CR2032 tích hợp để giữ giờ
- ⚫ Tất cả module phải **chung GND**

---

## 📥 Cài đặt

### 🪜 Bước 1: Cài đặt Arduino IDE

1. Tải Arduino IDE từ [arduino.cc](https://www.arduino.cc/en/software)
2. Cài đặt ESP8266 Board:
   - File → Preferences → Additional Board Manager URLs
   - Thêm: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
   - Tools → Board → Boards Manager → Tìm "esp8266" → Install

### 🪜 Bước 2: Cài đặt thư viện

**Cách 1: Qua Library Manager**

- Sketch → Include Library → Manage Libraries
- Tìm và cài đặt các thư viện đã liệt kê ở trên

**Cách 2: Thủ công**

```bash
git clone <repo-url>
cp -r libraries/* ~/Arduino/libraries/
```

### 🪜 Bước 3: Upload code

1. **Upload Arduino code:**

   - Chọn board: Arduino Uno
   - Chọn COM port
   - Upload code `arduino_feeding_system.ino`

2. **Upload ESP8266 code:**
   - Chọn board: NodeMCU 1.0 (ESP-12E Module)
   - Chọn COM port
   - Upload code `esp8266_wifi_bridge.ino`

---

## 🔧 Cấu hình

### 📝 Cấu hình ESP8266 (WiFi & Server)

Mở file `esp8266_wifi_bridge.ino` và chỉnh sửa:

```cpp
// WiFi credentials
const char* ssid = "TEN_WIFI_CUA_BAN";
const char* password = "MAT_KHAU_WIFI";

// Server endpoints
const char* logServer = "http://your-server.com/api/logs";
// const char* commandServer = "http://your-server.com/api/commands";
```

### ⚖️ Cấu hình Load Cell (Calibration)

Trong file Arduino, điều chỉnh hệ số hiệu chuẩn:

```cpp
LoadCell.setCalFactor(402018.2292);  // Thay bằng giá trị của bạn
```

**Cách hiệu chuẩn:**

1. Đặt Load Cell không có vật → ghi nhận giá trị
2. Đặt vật nặng đã biết (VD: 1kg) → ghi nhận giá trị
3. Tính toán CalFactor = (giá trị_raw / khối_lượng_thực)

### 📅 Cấu hình lịch mặc định

Lịch mặc định được tạo tự động nếu không có file `lich.txt`:

```cpp
schedule[0] = { 8, 0, 25, true };   // 08:00 - 2.5kg - Enabled
schedule[1] = { 12, 0, 15, true };  // 12:00 - 1.5kg - Enabled
schedule[2] = { 18, 0, 20, false }; // 18:00 - 2.0kg - Disabled
```

---

## 🎮 Sử dụng

### 🖐️ Điều khiển thủ công

- **Nhấn nút Feed Button** → Cho ăn 1.0kg ngay lập tức
- **Giữ nút 2 giây** trong quá trình cho ăn → Dừng khẩn cấp

### 💻 Điều khiển qua Serial

Gửi lệnh JSON qua Serial Monitor (9600 baud):

#### 📋 Lấy logs hôm nay

```json
{ "command": "get_logs" }
```

#### 📅 Lấy lịch hiện tại

```json
{ "command": "get_schedule" }
```

#### 🍽️ Cho ăn ngay (0.5kg)

```json
{ "command": "feed", "amount": 0.5 }
```

#### ⏰ Cập nhật lịch

```json
{
  "command": "update_schedule",
  "schedule": "08:00,2.5,1|12:00,1.5,1|18:00,2.0,0"
}
```

Format: `HH:MM,KG,ENABLE|...`

#### 🗑️ Xóa log

```json
{ "command": "delete_log" }
```

#### 🕐 Lấy thời gian RTC

```json
{ "command": "get_time" }
```

#### 🕐 Cập nhật thời gian RTC

```json
{ "command": "set_time", "datetime": "2025-10-06 15:45:00" }
```

#### 📊 Lấy trạng thái hệ thống

```json
{ "command": "status" }
```

---

## 📡 Giao thức dữ liệu

### 📤 ESP → Server (HTTP POST)

#### 1️⃣ Schedule Data

```json
{
  "type": "schedule",
  "device": "feeding_system",
  "timestamp": 123456789,
  "data": ["08:00,2.5,1", "12:00,1.5,1", "18:00,2.0,0"]
}
```

#### 2️⃣ Logs Data

```json
{
  "type": "logs",
  "date": "2025/10/6",
  "data": "2025/10/6 8:0:0,AUTO,2.5KG\n2025/10/6 12:0:0,AUTO,1.5KG\n",
  "device": "feeding_system",
  "timestamp": 123456789
}
```

#### 3️⃣ System Status

```json
{
  "type": "status",
  "device": "feeding_system",
  "wifi_rssi": -65,
  "free_heap": 35240,
  "uptime": 3600000,
  "ip": "192.168.1.100"
}
```

#### 4️⃣ Heartbeat

```json
{
  "type": "heartbeat",
  "device": "feeding_system",
  "timestamp": 123456789
}
```

### 📥 Arduino ← ESP (UART Protocol)

| Lệnh từ ESP    | Mô tả             | Response từ Arduino |
| -------------- | ----------------- | ------------------- |
| `GET_LOG`      | Lấy log hôm nay   | `LOGS:...`          |
| `GET_SCHEDULE` | Lấy lịch          | `SCHEDULES:...`     |
| `FEED:25`      | Cho ăn 2.5kg      | `FEED_DONE`         |
| `SCHEDULE:...` | Cập nhật lịch     | -                   |
| `DEL_LOG`      | Xóa file log      | `LOG_DELETED`       |
| `GET_TIME`     | Lấy thời gian RTC | `TIME:...`          |
| `SETTIME:...`  | Cập nhật RTC      | `RTC_UPDATED`       |

---

## 🔍 Troubleshooting

### ❌ Lỗi thường gặp

#### 1. ESP8266 không kết nối WiFi

```
✅ Kiểm tra SSID và password
✅ Đảm bảo WiFi là 2.4GHz (ESP8266 không hỗ trợ 5GHz)
✅ Kiểm tra tín hiệu WiFi
```

#### 2. SD Card không hoạt động

```
✅ Kiểm tra format SD Card (FAT16/FAT32)
✅ Kiểm tra dung lượng (tối đa 32GB cho FAT32)
✅ Kiểm tra kết nối SPI
✅ Thử chân CS khác nếu D10 bị xung đột
```

#### 3. Load Cell đọc sai

```
✅ Hiệu chuẩn lại CalFactor
✅ Kiểm tra kết nối HX711
✅ Đảm bảo Load Cell được đặt ổn định
✅ Tránh rung động khi đo
```

#### 4. RTC mất giờ

```
✅ Kiểm tra pin CR2032
✅ Thay pin mới
✅ Set lại thời gian qua lệnh SETTIME
```

#### 5. Motor không chạy

```
✅ Kiểm tra kết nối Relay
✅ Đảm bảo nguồn motor đủ mạnh
✅ Kiểm tra logic điều khiển (HIGH/LOW)
```

### 📊 Debug Mode

Mở Serial Monitor để xem log debug:

- **Arduino:** 9600 baud
- **ESP8266:** 9600 baud

---

## 📁 Cấu trúc thư mục

```
fish-feeding-system/
│
├── arduino_feeding_system/
│   └── arduino_feeding_system.ino    # Code Arduino chính
│
├── esp8266_wifi_bridge/
│   └── esp8266_wifi_bridge.ino       # Code ESP8266
│
├── libraries/                         # Thư viện cần thiết (nếu có)
│
├── docs/
│   ├── wiring_diagram.png            # Sơ đồ đấu nối
│   └── calibration_guide.md          # Hướng dẫn hiệu chuẩn
│
├── server/                            # Code backend (nếu có)
│   └── receiver.php
│
└── README.md                          # File này
```

---

## 🤝 Đóng góp

Mọi đóng góp đều được chào đón! Vui lòng:

1. Fork repository
2. Tạo branch mới (`git checkout -b feature/AmazingFeature`)
3. Commit thay đổi (`git commit -m 'Add some AmazingFeature'`)
4. Push lên branch (`git push origin feature/AmazingFeature`)
5. Tạo Pull Request

---

## 📄 License

Dự án này được phát hành dưới giấy phép MIT License.

---

## 📧 Liên hệ

- 📮 Email: your-email@example.com
- 🌐 Website: your-website.com
- 💬 Issues: [GitHub Issues](https://github.com/yourusername/fish-feeding-system/issues)

---

## 🙏 Credits

- HX711 Library: [bodge-it](https://github.com/bogde/HX711)
- RTClib: [Adafruit](https://github.com/adafruit/RTClib)
- ESP8266 Core: [ESP8266 Community](https://github.com/esp8266/Arduino)

---

**⭐ Nếu project này hữu ích, hãy cho một Star nhé! ⭐**
