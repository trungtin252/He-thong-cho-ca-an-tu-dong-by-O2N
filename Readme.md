🐟 ESP8266 Feeding System

1. Giới thiệu

Đây là chương trình chạy trên ESP8266 dùng để kết nối Arduino (điều khiển máy cho cá ăn) với server.
ESP8266 sẽ:

Nhận dữ liệu từ Arduino (lịch cho ăn, nhật ký, thời gian thực).

Gửi dữ liệu này lên server qua HTTP/JSON.

Nhận lệnh từ Serial hoặc Server rồi forward xuống Arduino.

Gửi heartbeat và status định kỳ để giám sát hệ thống.

2. Yêu cầu phần cứng

ESP8266 (NodeMCU, Wemos D1 Mini, …).

Arduino Uno/Nano (hoặc tương tự).

RTC Module (DS3231 hoặc tương đương).

Module động cơ cho hệ thống cho ăn.

Dây nối UART:

ESP8266 D1 (GPIO5) → Arduino TX

ESP8266 D2 (GPIO4) → Arduino RX

3. Cách cài đặt
   Bước 1: Chuẩn bị

Cài Arduino IDE.

Cài ESP8266 board trong Arduino IDE (Board Manager → esp8266 by ESP8266 Community).

Cài thư viện cần thiết:

ESP8266WiFi

ESP8266HTTPClient

ArduinoJson

SoftwareSerial

Bước 2: Cấu hình WiFi và server

Trong file code:

const char* ssid = "Tên WiFi";
const char* password = "Mật khẩu WiFi";

const char* logServer = "http://your-server/receiver.php";
// const char* commandServer = "http://your-server/command.php"; // có thể bật lại

Thay ssid và password bằng WiFi của bạn.

Thay logServer bằng địa chỉ server của bạn.

Bước 3: Nạp code

Chọn board NodeMCU 1.0 (ESP-12E Module) hoặc board ESP8266 tương ứng.

Kết nối ESP8266 qua cáp USB.

Nhấn Upload.

Bước 4: Kiểm tra kết nối

Mở Serial Monitor (9600 baud).

Chờ ESP kết nối WiFi → sẽ in ra IP Address.

Nếu gửi dữ liệu thành công → log sẽ hiện "Schedule sent", "Logs sent", "Heartbeat sent".

4. Gửi lệnh điều khiển

Bạn có thể gửi lệnh JSON qua Serial Monitor (hoặc qua server).
Ví dụ:

Lấy logs
{ "command": "get_logs" }

Lấy schedule
{ "command": "get_schedule" }

Cho ăn 0.5kg
{ "command": "feed", "amount": 0.5 }

Cập nhật lịch
{ "command": "update_schedule", "schedule": "08:00,12:00,18:00" }

Xóa log
{ "command": "delete_log" }

Lấy thời gian RTC
{ "command": "get_time" }

Cập nhật thời gian RTC
{ "command": "set_time", "datetime": "2025-10-02 15:45:00" }

5. Dữ liệu ESP gửi lên server

ESP gửi dữ liệu qua HTTP POST với JSON:

Schedule

{
"type": "schedule",
"device": "feeding_system",
"timestamp": 123456,
"data": ["08:00", "12:00", "18:00"]
}

Logs

{
"type": "logs",
"date": "2025-10-02",
"data": "08:00 - Fed 0.5kg\n12:00 - Fed 0.5kg\n",
"device": "feeding_system",
"timestamp": 123456
}

Status

{
"type": "status",
"wifi_rssi": -60,
"free_heap": 35000,
"uptime": 123456,
"ip": "192.168.1.50"
}

Heartbeat

{
"type": "heartbeat",
"device": "feeding_system",
"timestamp": 123456
}

6. Chu kỳ hoạt động

ESP kết nối WiFi.

Nghe dữ liệu từ Arduino → nếu có thì gửi server.

Gửi heartbeat mỗi 5 phút.

Có thể nhận lệnh từ Serial hoặc server (nếu bật lại phần checkServerCommand()).

7. Debug & kiểm tra

Dùng Serial Monitor để xem log.

Test nhanh bằng cách gửi JSON trong Serial Monitor.

Server có thể viết bằng PHP để nhận/gửi lệnh (xem file receiver.php và command.php).
