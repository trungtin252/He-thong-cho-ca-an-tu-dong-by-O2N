#include <HX711_ADC.h>
#include <SPI.h>
#include <SD.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <RTClib.h> 

// Khởi tạo thiết bị
HX711_ADC LoadCell(A0, A1);
SoftwareSerial espSerial(7, 6);
RTC_DS3231 myRTC;

// Pins
const byte chipSelect = 10;
const byte feedButton = 2;
const byte feedMotor = 3;
const byte releaseMotor = 5; 

// Biến toàn cục
float currentWeight = 0;
float currentWeight2 = 1000000;
float maxTimePerKg = 1.8;
bool isFeeding = false;
unsigned long lastCheck = 0;


// Lịch cho ăn tối ưu
struct Schedule {
  byte hour;
  byte minute;
  byte amount;  // x0.1kg (25 = 2.5kg)
  bool enabled;
};

Schedule schedule[5];  // Tối đa 5 lịch
byte scheduleCount = 0;

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);
  Wire.begin();

  // Khởi tạo LoadCell
  LoadCell.begin();
  LoadCell.start(2000);
  LoadCell.setCalFactor(402018.2292);

  if (!SD.begin(chipSelect)) {
    Serial.println(F("SD failed"));
    delay(2000);
    espSerial.println(F("ERROR_SD"));
    return;
  }

  pinMode(feedButton, INPUT_PULLUP);
  pinMode(feedMotor, OUTPUT);
  pinMode(releaseMotor, OUTPUT);
  digitalWrite(feedMotor, LOW);
  digitalWrite(releaseMotor, LOW);

  // ===== Khởi tạo RTC DS3231 =====
  if (!myRTC.begin()) {
    delay(4000);
    espSerial.println(F("ERROR_RTC_FIND"));
    Serial.println(F("Không tìm thấy DS3231!"));
    while (1);
  }

  if (myRTC.lostPower()) {
    delay(3000);
    espSerial.println(F("ERROR_RTC_POWER_LOST"));
    // myRTC.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  // ===============================

  loadSchedule();
  delay(500);
  loadMaxTime();
  Serial.println(F("Ready"));
  espSerial.println(F("Arduino_OK"));
}

void loop() {
  LoadCell.update();
  currentWeight = LoadCell.getData();

  // Nút cho ăn thủ công
  if (digitalRead(feedButton) == LOW && !isFeeding) {
    delay(50);
    if (digitalRead(feedButton) == LOW) {
      espSerial.println(F("FEED_START"));
      bool success = feedProcess(10);  // 1.0kg
      writeLog(F("MANUAL"), 10, success);
      while (digitalRead(feedButton) == LOW);
    }
  }

  // Kiểm tra lịch (mỗi 10s)
  if (millis() - lastCheck > 10000) {
    checkAutoFeed();
    lastCheck = millis();
  }

  // Kiểm tra lệnh ESP8266
  checkESPCommands();

  delay(100);
}

bool feedProcess(byte targetAmount) {
  isFeeding = true;
  bool success = true;
  float target = targetAmount / 10.0;   // đổi ra kg
  float initial = currentWeight;
  float fed = 0;
  unsigned long lastSendTime = 0;
  unsigned long startFeedTime = millis();
  unsigned long maxFeedTime = (unsigned long)(target * maxTimePerKg * 60 * 1000);

  // Bắt đầu chạy motor nạp
  digitalWrite(feedMotor, HIGH);

  while (fed < target && isFeeding) {
    LoadCell.update();
    currentWeight = LoadCell.getData();
    fed = currentWeight - initial;
    if (fed < 0) fed = 0; 

    // Gửi dữ liệu về ESP mỗi 1 giây
    if (millis() - lastSendTime >= 1000) {
      lastSendTime = millis();
      espSerial.print(F("FEEDING,"));
      espSerial.print(fed, 3);
      espSerial.print(F(",")); 
      espSerial.println(currentWeight, 3);
    }

    // Kiểm tra quá thời gian 30 giây
    if (millis() - startFeedTime > maxFeedTime) {
      espSerial.println(F("FEED_ERROR,TIMEOUT"));
      isFeeding = false;
      success = false;
      break;
    }

    // Dừng khẩn cấp (giữ nút 2s)
    if (digitalRead(feedButton) == LOW) {
      delay(2000);
      if (digitalRead(feedButton) == LOW) {
        espSerial.println(F("FEED_ERROR,MANUAL_STOP"));
        isFeeding = false;
        success = false;
        break;
      }
    }

    delay(100);
  }

  // Dừng motor nạp
  digitalWrite(feedMotor, LOW);

  // Nếu quá trình không bị hủy thì chạy motor xả
  if (isFeeding) {
    digitalWrite(releaseMotor, HIGH);
    delay(1000);
    currentWeight2 = LoadCell.getData();
    unsigned long lastReleaseSend = millis();

    while (currentWeight2 > 0.01) {
      LoadCell.update();
      currentWeight2 = LoadCell.getData();

      if (millis() - lastReleaseSend >= 1000) {
        lastReleaseSend = millis();
        espSerial.print(F("RELEASING,"));
        espSerial.println(currentWeight2, 4);
      }
    }
    delay(5000);
    digitalWrite(releaseMotor, LOW);
    espSerial.println(F("FEED_DONE"));
    Serial.println(F("Đã xả xong"));
  }

  isFeeding = false;
  return success;
}

// ===============================
//      KIỂM TRA LỊCH TỰ ĐỘNG
// ===============================
void checkAutoFeed() {
  DateTime now = myRTC.now();
  byte h = now.hour();
  byte m = now.minute();

  for (byte i = 0; i < scheduleCount; i++) {
    if (schedule[i].enabled && schedule[i].hour == h && schedule[i].minute == m) {
      espSerial.println(F("FEED_START"));
      bool success = feedProcess(schedule[i].amount);
      writeLog(F("AUTO"), schedule[i].amount, success);
      delay(61000);
      break;
    }
  }
}

// ===============================
//       GHI LOG CÓ SUCCESS/FAIL
// ===============================
void writeLog(const __FlashStringHelper* type, byte amount, bool success) {
  DateTime now = myRTC.now();

  File logFile = SD.open(F("LOG.txt"), FILE_WRITE);
  if (logFile) {
    logFile.print(now.year());
    logFile.print('/');
    logFile.print(now.month());
    logFile.print('/');
    logFile.print(now.day());
    logFile.print(' ');
    logFile.print(now.hour());
    logFile.print(':');
    logFile.print(now.minute());
    logFile.print(':');
    logFile.print(now.second());
    logFile.print(',');
    logFile.print(type);
    logFile.print(',');
    logFile.print(amount / 10.0, 1);
    logFile.print(F("KG,"));
    logFile.println(success ? F("SUCCESS") : F("FAIL"));
    logFile.close();
  }
}

// ===============================
//      PHẦN CÒN LẠI GIỮ NGUYÊN
// ===============================

void checkESPCommands() {
  if (espSerial.available()) {
    String cmd = espSerial.readStringUntil('\n');
    cmd.trim();
    Serial.println(cmd);

    if (cmd == F("GET_LOG")) {
      sendTodayLog();
    } else if (cmd == F("GET_SCHEDULE")) {
      sendSchedule();
    } else if (cmd.startsWith(F("FEED:"))) {
      byte amount = cmd.substring(5).toInt();
      espSerial.println(F("FEED_START"));
      bool success = feedProcess(amount);
      writeLog(F("SERVER"), amount, success);
    } else if (cmd.startsWith(F("SCHEDULE:"))) {
      updateSchedule(cmd.substring(9));
    } else if (cmd == F("DEL_LOG")) {
      deleteLog();
    } else if (cmd.startsWith(F("SETTIME:"))) {
      String data = cmd.substring(8);
      int y = data.substring(0,4).toInt();
      int M = data.substring(5,7).toInt();
      int d = data.substring(8,10).toInt();
      int h = data.substring(11,13).toInt();
      int m = data.substring(14,16).toInt();
      int s = data.substring(17,19).toInt();
      myRTC.adjust(DateTime(y, M, d, h, m, s));
      espSerial.println(F("RTC_UPDATED"));
    } else if (cmd == F("GET_TIME")) {
      DateTime now = myRTC.now();
      espSerial.print(F("TIME:"));
      espSerial.print(now.year());
      espSerial.print('-');
      if (now.month() < 10) espSerial.print('0');
      espSerial.print(now.month());
      espSerial.print('-');
      if (now.day() < 10) espSerial.print('0');
      espSerial.print(now.day());
      espSerial.print(' ');
      if (now.hour() < 10) espSerial.print('0');
      espSerial.print(now.hour());
      espSerial.print(':');
      if (now.minute() < 10) espSerial.print('0');
      espSerial.print(now.minute());
      espSerial.print(':');
      if (now.second() < 10) espSerial.print('0');
      espSerial.println(now.second());
    }else if (cmd == F("GET_MAXTIME")) {
      espSerial.print(F("MAXTIME:"));
      espSerial.println(maxTimePerKg, 1);
    }
    else if (cmd.startsWith(F("SET_MAXTIME:"))) {
      float newTime = cmd.substring(12).toFloat();
      if (newTime > 0.5 && newTime < 10) {  // Giới hạn từ 0.5 - 10 phút/kg
        maxTimePerKg = newTime;
        saveMaxTime();  // ← LƯU VÀO SD
        espSerial.print(F("MAXTIME_UPDATED:"));
        espSerial.println(maxTimePerKg, 1);
        Serial.print(F("Cập nhật maxTimePerKg: "));
        Serial.println(maxTimePerKg);
      } else {
        espSerial.println(F("ERROR_INVALID_MAXTIME"));
      }
    }
    else if (cmd == F("RESET")) {
      deleteAllFiles();
      espSerial.println(F("SD_RESET_DONE"));
      Serial.println(F("Đã xóa toàn bộ nội dung thẻ SD!"));
      delay(10000);
      espSerial.println(F("Arduino_OK"));
    }
    else if (cmd == F("CHECK")) {
      espSerial.println(F("Arduino_OK"));
    }
  }
}

void deleteLog() {
  if (SD.exists(F("LOG.txt"))) {
    SD.remove(F("LOG.txt"));
    espSerial.println(F("LOG_DELETED"));
    Serial.println(F("LOG.txt da bi xoa"));
  } else {
    espSerial.println(F("LOG_NOT_FOUND"));
    Serial.println(F("Khong tim thay LOG.txt"));
  }
}

void loadSchedule() {
  scheduleCount = 0;
  File f = SD.open(F("lich.txt"), FILE_READ);
  if (!f) {
    schedule[0] = { 8, 0, 25, false };
    schedule[1] = { 12, 0, 15, false };
    schedule[2] = { 18, 0, 20, false };
    schedule[3] = { 0, 0, 0, false };
    schedule[4] = { 0, 0, 0, false };
    scheduleCount = 5;
    saveSchedule();
    return;
  }

  while (f.available() && scheduleCount < 5) {
    String line = f.readStringUntil('\n');
    line.trim();
    int c1 = line.indexOf(',');
    int c2 = line.lastIndexOf(',');

    if (c1 > 0 && c2 > c1) {
      String timeStr = line.substring(0, c1);
      byte amount = (line.substring(c1 + 1, c2).toFloat() * 10);
      bool enabled = line.substring(c2 + 1).toInt();
      int colon = timeStr.indexOf(':');
      if (colon > 0) {
        schedule[scheduleCount].hour = timeStr.substring(0, colon).toInt();
        schedule[scheduleCount].minute = timeStr.substring(colon + 1).toInt();
        schedule[scheduleCount].amount = amount;
        schedule[scheduleCount].enabled = enabled;
        scheduleCount++;
      }
    }
  }
  f.close();
}

void saveSchedule() {
  SD.remove(F("lich.txt"));
  File f = SD.open(F("lich.txt"), FILE_WRITE);
  if (f) {
    for (byte i = 0; i < scheduleCount; i++) {
      if (schedule[i].hour < 10) f.print('0');
      f.print(schedule[i].hour);
      f.print(':');
      if (schedule[i].minute < 10) f.print('0');
      f.print(schedule[i].minute);
      f.print(',');
      f.print(schedule[i].amount / 10.0, 1);
      f.print(',');
      f.println(schedule[i].enabled ? '1' : '0');
    }
    f.close();
  }
}

void sendTodayLog() {
  DateTime now = myRTC.now();
  File f = SD.open(F("LOG.txt"), FILE_READ);
  if (!f){
    delay(200);
    espSerial.println(F("No log find")); 
    return;
  }

  espSerial.print(F("LOGS:"));
  espSerial.print(now.year());
  espSerial.print('/');
  espSerial.print(now.month());
  espSerial.print('/');
  espSerial.print(now.day());
  espSerial.println();

  String today = String(now.year()) + "/" + String(now.month()) + "/" + String(now.day());

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.startsWith(today)) {
      espSerial.println(line);
      delay(10);
    }
  }
  f.close();
  espSerial.println(F("END_LOGS"));
}

void sendSchedule() {
  espSerial.println(F("SCHEDULES:"));
  for (byte i = 0; i < 5; i++) {
    if (schedule[i].hour < 10) espSerial.print('0');
    espSerial.print(schedule[i].hour);
    espSerial.print(':');
    if (schedule[i].minute < 10) espSerial.print('0');
    espSerial.print(schedule[i].minute);
    espSerial.print(',');
    espSerial.print(schedule[i].amount / 10.0, 1);
    espSerial.print(',');
    espSerial.println(schedule[i].enabled ? '1' : '0');
  }
  delay(100);
  espSerial.println(F("END_SCHEDULES"));
}

void updateSchedule(String data) {
  scheduleCount = 0;
  int start = 0;
  int end = data.indexOf('|');

  while (end >= 0 && scheduleCount < 5) {
    parseScheduleItem(data.substring(start, end));
    start = end + 1;
    end = data.indexOf('|', start);
  }

  if (start < data.length() && scheduleCount < 5) {
    parseScheduleItem(data.substring(start));
  }

  saveSchedule();
}

void parseScheduleItem(String item) {
  int c1 = item.indexOf(',');
  int c2 = item.lastIndexOf(',');

  if (c1 > 0 && c2 > c1) {
    String timeStr = item.substring(0, c1);
    byte amount = (item.substring(c1 + 1, c2).toFloat() * 10);
    bool enabled = item.substring(c2 + 1).toInt();
    int colon = timeStr.indexOf(':');
    if (colon > 0) {
      schedule[scheduleCount].hour = timeStr.substring(0, colon).toInt();
      schedule[scheduleCount].minute = timeStr.substring(colon + 1).toInt();
      schedule[scheduleCount].amount = amount;
      schedule[scheduleCount].enabled = enabled;
      scheduleCount++;
    }     
  }
}

void deleteAllFiles() {
  File root = SD.open("/");
  if (!root) {
    Serial.println(F("Không thể mở thư mục gốc của SD"));
    return;
  }

  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    String name = entry.name();
    entry.close();
    if (SD.remove(name)) {
      Serial.print(F("Đã xóa: "));
      Serial.println(name);
    } else {
      Serial.print(F("Không thể xóa: "));
      Serial.println(name);
    }
  }

  root.close();
}


void loadMaxTime() {
  File f = SD.open(F("maxtime.txt"), FILE_READ);
  if (f) {
    if (f.available()) {
      String data = f.readStringUntil('\n');
      data.trim();
      float value = data.toFloat();
      if (value > 0.5 && value < 10) {
        maxTimePerKg = value;
        Serial.print(F("Loaded maxTimePerKg: "));
        Serial.println(maxTimePerKg);
      }
    }
    f.close();
  } else {
    Serial.println(F("maxtime.txt not found, using default: 1.8"));
    saveMaxTime();  // Tạo file mới với giá trị mặc định
  }
}

void saveMaxTime() {
  SD.remove(F("maxtime.txt"));
  File f = SD.open(F("maxtime.txt"), FILE_WRITE);
  if (f) {
    f.println(maxTimePerKg, 1);
    f.close();
    Serial.print(F("Saved maxTimePerKg: "));
    Serial.println(maxTimePerKg);
  } else {
    Serial.println(F("Error saving maxtime.txt"));
  }
}
