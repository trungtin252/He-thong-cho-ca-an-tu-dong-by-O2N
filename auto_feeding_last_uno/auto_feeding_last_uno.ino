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
bool isFeeding = false;
unsigned long lastCheck = 0;
unsigned long lastLogSend = 0;

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
    return;
  }

  pinMode(feedButton, INPUT_PULLUP);
  pinMode(feedMotor, OUTPUT);
  pinMode(releaseMotor, OUTPUT);
  digitalWrite(feedMotor, LOW);
  digitalWrite(releaseMotor, LOW);

  // ===== Khởi tạo RTC DS3231 =====
  if (!myRTC.begin()) {
    Serial.println(F("Không tìm thấy DS3231!"));
    while (1);
  }

  if (myRTC.lostPower()) {
    Serial.println(F("RTC mất nguồn, set lại thời gian..."));
    // Chỉ set khi RTC mất nguồn
    myRTC.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  // ===============================

  loadSchedule();
  Serial.println(F("Ready"));
}

void loop() {
  // Cập nhật trọng lượng
  LoadCell.update();
  currentWeight = LoadCell.getData();

  // Nút cho ăn thủ công
  if (digitalRead(feedButton) == LOW && !isFeeding) {
    delay(50);
    if (digitalRead(feedButton) == LOW) {
      feedProcess(10);  // 1.0kg
      writeLog(F("MANUAL"), 10);
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

  // Gửi log định kỳ (mỗi 2 phút để test)
  if (millis() - lastLogSend > 120000) {
    sendTodayLog();
    lastLogSend = millis();
  }

  delay(100);
}

void feedProcess(byte targetAmount) {
  isFeeding = true;
  float target = targetAmount / 10.0;   // đổi ra kg
  float initial = currentWeight;
  float fed = 0;
  float time = targetAmount * 12000;

  // Bắt đầu chạy motor nạp
  digitalWrite(feedMotor, HIGH);

  while (fed < target && isFeeding) {
    LoadCell.update();
    currentWeight = LoadCell.getData();
    fed = currentWeight - initial;
    Serial.print(F("Đã nạp: "));
    Serial.println(fed, 4);

    // Dừng khẩn cấp (giữ nút 2s)
    if (digitalRead(feedButton) == LOW) {
      delay(2000);
      if (digitalRead(feedButton) == LOW) {
        isFeeding = false;
      }
    }
    delay(100);
  }

  // Dừng motor nạp
  digitalWrite(feedMotor, LOW);

  // Nếu quá trình không bị hủy thì chạy motor xả
  if (isFeeding) {
    Serial.println(F("Đang xả xuống ao..."));
    digitalWrite(releaseMotor, HIGH);
    delay(1000);
    currentWeight2 = LoadCell.getData();
    while (currentWeight2 > 0.001 ) {
      LoadCell.update();
      currentWeight2 = LoadCell.getData();
      Serial.print(F("TL hien tai: "));
      Serial.println(currentWeight2, 4);
    }
    delay(1000);
    digitalWrite(releaseMotor, LOW);
    espSerial.println(F("FEED_DONE"));
    delay(100);
    Serial.println(F("Đã xả xong"));
  } 
   
  isFeeding = false;
}


void checkAutoFeed() {
  DateTime now = myRTC.now();
  byte h = now.hour();
  byte m = now.minute();

  for (byte i = 0; i < scheduleCount; i++) {
    if (schedule[i].enabled && schedule[i].hour == h && schedule[i].minute == m) {
      feedProcess(schedule[i].amount);
      writeLog(F("AUTO"), schedule[i].amount);
      delay(61000);  // Tránh lặp trong cùng phút
      break;
    }
  }
}

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
      feedProcess(amount);
      writeLog(F("SERVER"), amount);
    } else if (cmd.startsWith(F("SCHEDULE:"))) {
      updateSchedule(cmd.substring(9));
    } else if (cmd == F("DEL_LOG")) {
      deleteLog();
    }else if (cmd.startsWith(F("SETTIME:"))) {
      String data = cmd.substring(8);
      int y = data.substring(0,4).toInt();
      int M = data.substring(5,7).toInt();
      int d = data.substring(8,10).toInt();
      int h = data.substring(11,13).toInt();
      int m = data.substring(14,16).toInt();
      int s = data.substring(17,19).toInt();

      myRTC.adjust(DateTime(y, M, d, h, m, s));
      espSerial.println(F("RTC_UPDATED"));
    }else if (cmd == F("GET_TIME")) {
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
    }

  }
}


void writeLog(const __FlashStringHelper* type, byte amount) {
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
    logFile.println(F("KG"));
    logFile.close();
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
    schedule[0] = { 8, 0, 25, true };
    schedule[1] = { 12, 0, 15, true };
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
                          