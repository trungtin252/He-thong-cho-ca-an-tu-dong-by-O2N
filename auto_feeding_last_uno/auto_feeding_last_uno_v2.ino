#include <HX711_ADC.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <RTClib.h>

// Khởi tạo thiết bị
HX711_ADC LoadCell(A0, A1);
SoftwareSerial espSerial(7, 6);
RTC_DS3231 myRTC;

// Pins Motor Van (IN1, IN2) - có encoder
const byte valveMotorIN1 = 4;
const byte valveMotorIN2 = 5;

// Encoder Pins (phải dùng chân interrupt 2, 3)
const byte encoderPinA = 2;
const byte encoderPinB = 3;

// Motor xả thức ăn
const byte releaseMotor = 8; 

// Biến encoder
volatile long encoderCount = 0;
volatile bool lastA = LOW;
volatile bool lastB = LOW;

// Cấu hình encoder cho van
long encoderOpenTarget = 400;   // Số xung để mở van hoàn toàn (tùy chỉnh)
long encoderCloseTarget = 0;     // Vị trí đóng van (0)
const int encoderTolerance = 5;  // Dung sai

// Biến toàn cục
float currentWeight = 0;
float currentWeight2 = 1000000;
bool isFeeding = false;
unsigned long lastTimeUpdate = 0;

// Timeout settings
float maxTimePerKg = 1.5;

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);
  Wire.begin();

  // Khởi tạo LoadCell
  LoadCell.begin();
  LoadCell.start(2000);
  LoadCell.setCalFactor(402018.2292);

  // Cấu hình motor van
  pinMode(valveMotorIN1, OUTPUT);
  pinMode(valveMotorIN2, OUTPUT);
  digitalWrite(valveMotorIN1, LOW);
  digitalWrite(valveMotorIN2, LOW);

  // Cấu hình motor xả
  pinMode(releaseMotor, OUTPUT);
  digitalWrite(releaseMotor, LOW);

  // Cấu hình encoder với interrupt
  pinMode(encoderPinA, INPUT_PULLUP);
  pinMode(encoderPinB, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(encoderPinA), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(encoderPinB), encoderISR, CHANGE);

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
  }

  delay(500);
  Serial.println(F("Ready"));
  espSerial.println(F("Arduino_OK"));
}

void loop() {
  LoadCell.update();
  currentWeight = LoadCell.getData();

  // Gửi thời gian cho ESP mỗi 1 giây
  if (millis() - lastTimeUpdate >= 1000) {
    lastTimeUpdate = millis();
    sendTimeToESP();
  }

  checkSerialCommands();

  // Kiểm tra lệnh ESP8266
  checkESPCommands();

  delay(100);
}

// ========== HÀM ENCODER ==========
void encoderISR() {
  bool A = digitalRead(encoderPinA);
  bool B = digitalRead(encoderPinB);
  
  if (A != lastA) {
    if (A == B) {
      encoderCount++;
    } else {
      encoderCount--;
    }
  }
  
  lastA = A;
  lastB = B;
}

// ========== ĐIỀU KHIỂN VAN ==========
void openValve() {
  Serial.println(F("Đang mở van..."));
  espSerial.println(F("VALVE_OPENING"));

  unsigned long lastEncoderSend = millis();

  while (encoderCount < encoderOpenTarget) {
    long distance = encoderOpenTarget - encoderCount;

    // Nếu gần đích, di chuyển theo kiểu step-by-step
    if (distance > 30) {
      // Giai đoạn mở nhanh
      digitalWrite(valveMotorIN1, HIGH);
      digitalWrite(valveMotorIN2, LOW);
    } else {
      // Giai đoạn gần đích - mở ngắt quãng
      digitalWrite(valveMotorIN1, HIGH);
      digitalWrite(valveMotorIN2, LOW);
      delay(50);       // chạy 50ms
      brakeValveMotor(); // hãm tạm thời
      delay(50);       // nghỉ 50ms
    }

    if (millis() - lastEncoderSend >= 500) {
      lastEncoderSend = millis();
      espSerial.print(F("ENCODER_POS:"));
      espSerial.println(encoderCount);
    }
  }

  brakeValveMotor();

  // Nếu vượt đích, quay ngược nhẹ
  if (encoderCount > encoderOpenTarget + encoderTolerance) {
    Serial.println(F("Bù quán tính - quay ngược nhẹ"));
    digitalWrite(valveMotorIN1, LOW);
    digitalWrite(valveMotorIN2, HIGH);
    delay(100);
    brakeValveMotor();
  }

  Serial.print(F("Van đã mở. Encoder: "));
  Serial.println(encoderCount);
  espSerial.print(F("VALVE_OPENED:"));
  espSerial.println(encoderCount);
}

void closeValve() {
  Serial.println(F("Đang đóng van..."));
  espSerial.println(F("VALVE_CLOSING"));

  unsigned long lastEncoderSend = millis();

  while (encoderCount > encoderCloseTarget + encoderTolerance) {
    long distance = encoderCount - encoderCloseTarget;

    // Nếu còn xa, đóng nhanh
    if (distance > 30) {
      digitalWrite(valveMotorIN1, LOW);
      digitalWrite(valveMotorIN2, HIGH);
    } else {
      // Gần tới thì step-by-step
      digitalWrite(valveMotorIN1, LOW);
      digitalWrite(valveMotorIN2, HIGH);
      delay(50);
      brakeValveMotor();
      delay(50);
    }

    if (millis() - lastEncoderSend >= 500) {
      lastEncoderSend = millis();
      espSerial.print(F("ENCODER_POS:"));
      espSerial.println(encoderCount);
    }
  }

  brakeValveMotor();

  // Nếu bị đóng quá, quay ngược nhẹ
  if (encoderCount < encoderCloseTarget - encoderTolerance) {
    Serial.println(F("Bù quán tính - quay thuận nhẹ"));
    digitalWrite(valveMotorIN1, HIGH);
    digitalWrite(valveMotorIN2, LOW);
    delay(100);
    brakeValveMotor();
  }

  Serial.print(F("Van đã đóng. Encoder: "));
  Serial.println(encoderCount);
  espSerial.print(F("VALVE_CLOSED:"));
  espSerial.println(encoderCount);
}


void stopValveMotor() {
  digitalWrite(valveMotorIN1, LOW);
  digitalWrite(valveMotorIN2, LOW);
}

// ========== HÀM GỬI THỜI GIAN ==========
void sendTimeToESP() {
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

// ========== QUY TRÌNH NẠP THỨC ĂN ==========
void feedProcess(float targetKg) {
  if (isFeeding) {
    espSerial.println(F("ERROR:ALREADY_FEEDING"));
    return;
  }
  
  isFeeding = true;
  bool feedSuccess = true;
  bool releaseSuccess = true;
  
  float initial = currentWeight;
  float fed = 0;
  unsigned long lastSendTime = 0;
  unsigned long feedStartTime = millis();
  
  unsigned long maxFeedTime = (unsigned long)(targetKg * maxTimePerKg * 60 * 1000);

  espSerial.println(F("FEED_START"));
  Serial.print(F("Bắt đầu nạp: "));
  Serial.print(targetKg, 1);
  Serial.println(F(" kg"));

  // ========== GIAI ĐOẠN 1: MỞ VAN ==========
  openValve();
  delay(500);

  // ========== GIAI ĐOẠN 2: CHỜ THỨC ĂN RƠI ==========
  while (fed < targetKg && isFeeding) {
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
      espSerial.print(currentWeight, 3);
      espSerial.print(F(",VALVE_OPEN"));
      espSerial.println();
    }

    // Kiểm tra timeout nạp
    if (millis() - feedStartTime > maxFeedTime) {
      espSerial.println(F("ERROR:FEED_TIMEOUT"));
      Serial.println(F("Lỗi: Timeout khi nạp thức ăn!"));
      feedSuccess = false;
      break;
    }

    delay(100);
  }

  // ========== GIAI ĐOẠN 3: ĐÓNG VAN ==========
  closeValve();

  // Kiểm tra van kín chưa
  if (!checkLeakByWeight()) {
    Serial.println(F("⚠️ Van chưa đóng kín - thử đóng lại..."));
    espSerial.println(F("WARNING:VALVE_NOT_TIGHT"));

    // Mở nhẹ rồi đóng lại
    openValve();
    delay(500);
    closeValve();

    delay(2000);

    if (!checkLeakByWeight()) {
      espSerial.println(F("ERROR:VALVE_LEAK"));
    } else {
      espSerial.println(F("VALVE_RESEALED"));
    }
  }

  // ========== GIAI ĐOẠN 4: XẢ THỨC ĂN ==========
  float releaseStart = currentWeight; 
  espSerial.println(F("RELEASING_START"));
  Serial.println(F("Bắt đầu xả thức ăn..."));

  digitalWrite(releaseMotor, HIGH);

  unsigned long releaseStartTime = millis();
  unsigned long maxReleaseTime = max(maxFeedTime * 2, (unsigned long)120000);

  currentWeight2 = LoadCell.getData();
  unsigned long lastReleaseSend = millis();
  unsigned long lastWeightChange = millis();
  float lastWeight = currentWeight2;

  while (currentWeight2 > 0.05) {  // Ngưỡng 50g
    LoadCell.update();
    currentWeight2 = LoadCell.getData();

    // Gửi trạng thái xả mỗi 1 giây
    if (millis() - lastReleaseSend >= 1000) {
      lastReleaseSend = millis();
      espSerial.print(F("RELEASING,"));
      espSerial.println(currentWeight2, 4);

      if (abs(currentWeight2 - lastWeight) > 0.01) {
        lastWeightChange = millis();
        lastWeight = currentWeight2;
      }
    }

    // Timeout nếu vượt quá thời gian tối đa
    if (millis() - releaseStartTime > maxReleaseTime) {
      float releasedAmount = releaseStart - currentWeight2;
      if (releasedAmount < 0) releasedAmount = 0;

      espSerial.print(F("RELEASE_TIMEOUT,"));
      espSerial.println(releasedAmount, 3);

      Serial.println(F("Lỗi: Timeout khi xả thức ăn!"));
      releaseSuccess = false;
      break;
    }

    // Timeout nếu trọng lượng không giảm trong 30 giây (kẹt)
    if (millis() - lastWeightChange > 30000 && currentWeight2 > 0.1) {
      float releasedAmount = releaseStart - currentWeight2;
      if (releasedAmount < 0) releasedAmount = 0;

      espSerial.print(F("RELEASE_STUCK,"));
      espSerial.println(releasedAmount, 3);

      Serial.println(F("Lỗi: Thức ăn bị kẹt khi xả!"));
      releaseSuccess = false;
      break;
    }

    delay(5000);
  }

  digitalWrite(releaseMotor, LOW);
  delay(500);

  float releasedAmount = releaseStart - currentWeight2;
  if (releasedAmount < 0) releasedAmount = 0;

  // ========== HOÀN TẤT ==========
  if (feedSuccess && releaseSuccess) {
    espSerial.print(F("FEED_DONE,"));
    espSerial.print(releasedAmount, 3);
    espSerial.println(F(",SUCCESS"));
    
    Serial.print(F("Hoàn thành! Đã xả: "));
    Serial.print(releasedAmount, 3);
    Serial.println(F(" kg"));
  } else {
    espSerial.print(F("FEED_FAIL,"));
    espSerial.println(releasedAmount, 3);   // <-- Thêm dòng này để ESP nhận lượng đã xả

    Serial.print(F("Có lỗi xảy ra. Đã xả: "));
    Serial.print(releasedAmount, 3);
    Serial.println(F(" kg"));
  }

  isFeeding = false;
}

bool checkLeakByWeight() {
  float w1 = LoadCell.getData();   // trọng lượng ban đầu sau khi đóng van
  delay(3000);                     // đợi 3 giây
  LoadCell.update();
  float w2 = LoadCell.getData();   // trọng lượng sau 3 giây

  if (w2 - w1 > 0.05) { // nếu tăng > 20g → van chưa kín
    return false;
  }
  return true;
}

// ========== XỬ LÝ LỆNH TỪ ESP ==========
void checkESPCommands() {
  if (espSerial.available()) {
    String cmd = espSerial.readStringUntil('\n');
    cmd.trim();
    Serial.println(cmd);

    if (cmd.startsWith(F("FEED:"))) {
      float amount = cmd.substring(5).toFloat();
      if (amount > 0 && amount <= 10) {
        feedProcess(amount);
      } else {
        espSerial.println(F("ERROR:INVALID_AMOUNT"));
      }
    } 
    else if (cmd == F("GET_WEIGHT")) {
      espSerial.print(F("WEIGHT:"));
      espSerial.println(currentWeight, 3);
    }
    else if (cmd == F("GET_ENCODER")) {
      espSerial.print(F("ENCODER:"));
      espSerial.println(encoderCount);
    }
    else if (cmd == F("RESET_ENCODER")) {
      encoderCount = 0;
      espSerial.println(F("ENCODER_RESET"));
      Serial.println(F("Encoder đã reset về 0"));
    }
    else if (cmd.startsWith(F("SET_ENCODER_TARGET:"))) {
      // Format: SET_ENCODER_TARGET:500
      long newTarget = cmd.substring(19).toInt();
      if (newTarget > 0 && newTarget < 10000) {
        encoderOpenTarget = newTarget;
        espSerial.print(F("ENCODER_TARGET_SET:"));
        espSerial.println(encoderOpenTarget);
      } else {
        espSerial.println(F("ERROR:INVALID_TARGET"));
      }
    }
    else if (cmd == F("OPEN_VALVE")) {
      openValve();
    }
    else if (cmd == F("CLOSE_VALVE")) {
      closeValve();
    }
    else if (cmd == F("GET_TIME")) {
      sendTimeToESP();
    }
    else if (cmd.startsWith(F("SETTIME:"))) {
      // Format: SETTIME:2025-01-15 10:30:00
      String data = cmd.substring(8);
      int y = data.substring(0, 4).toInt();
      int M = data.substring(5, 7).toInt();
      int d = data.substring(8, 10).toInt();
      int h = data.substring(11, 13).toInt();
      int m = data.substring(14, 16).toInt();
      int s = data.substring(17, 19).toInt();
      
      myRTC.adjust(DateTime(y, M, d, h, m, s));
      espSerial.println(F("RTC_UPDATED"));
      
      Serial.print(F("RTC updated: "));
      Serial.print(y); Serial.print('/');
      Serial.print(M); Serial.print('/');
      Serial.print(d); Serial.print(' ');
      Serial.print(h); Serial.print(':');
      Serial.print(m); Serial.print(':');
      Serial.println(s);
    }
    else if (cmd.startsWith(F("SET_MAXTIME:"))) {
      float newMaxTime = cmd.substring(12).toFloat();
      if (newMaxTime >= 0.5 && newMaxTime <= 10) {
        maxTimePerKg = newMaxTime;
        espSerial.print(F("MAXTIME_UPDATED:"));
        espSerial.println(maxTimePerKg, 1);
        
        Serial.print(F("MaxTime updated: "));
        Serial.print(maxTimePerKg, 1);
        Serial.println(F(" min/kg"));
      } else {
        espSerial.println(F("ERROR:INVALID_MAXTIME"));
      }
    }
    else if (cmd == F("GET_MAXTIME")) {
      espSerial.print(F("MAXTIME:"));
      espSerial.println(maxTimePerKg, 1);
    }
    else if (cmd == F("CHECK")) {
      espSerial.println(F("Arduino_OK"));
    } 
    else if (cmd == F("STOP_FEED")) {
      if (isFeeding) {
        isFeeding = false;
        stopValveMotor();
        digitalWrite(releaseMotor, LOW);
        espSerial.println(F("FEED_STOPPED"));
        Serial.println(F("Dừng khẩn cấp!"));
      }
    }
    else if (cmd == F("DEBUG_INFO")) {
      espSerial.print(F("DEBUG_INFO:"));
      espSerial.print(F("WEIGHT:"));
      espSerial.print(currentWeight, 3);
      espSerial.print(F(",ENCODER:"));
      espSerial.print(encoderCount);
      espSerial.print(F(",TARGET:"));
      espSerial.print(encoderOpenTarget);
      espSerial.print(F(",FEEDING:"));
      espSerial.print(isFeeding ? F("YES") : F("NO"));
      espSerial.print(F(",MAXTIME:"));
      espSerial.print(maxTimePerKg, 1);
      
      Serial.println(F("=== DEBUG INFO ==="));
      Serial.print(F("Weight: "));
      Serial.println(currentWeight, 3);
      Serial.print(F("Encoder: "));
      Serial.println(encoderCount);
      Serial.print(F("Target: "));
      Serial.println(encoderOpenTarget);
      Serial.print(F("Feeding: "));
      Serial.println(isFeeding ? F("YES") : F("NO"));
      Serial.print(F("MaxTime: "));
      Serial.print(maxTimePerKg, 1);
      Serial.println(F(" min/kg"));
    }
  }
}


void brakeValveMotor() {
  // Hãm mạnh bằng short brake
  digitalWrite(valveMotorIN1, HIGH);
  digitalWrite(valveMotorIN2, HIGH);
  
  // Thời gian hãm tùy theo khoảng cách còn lại
  long distance = abs(encoderCount - encoderOpenTarget);
  int brakeTime;
  
  if (distance > 50) {
    brakeTime = 200;  // Hãm mạnh khi còn xa
  } else if (distance > 20) {
    brakeTime = 150;  // Hãm vừa
  } else {
    brakeTime = 100;  // Hãm nhẹ khi gần đích
  }
  
  delay(brakeTime);
  
  // Ngắt hoàn toàn
  digitalWrite(valveMotorIN1, LOW);
  digitalWrite(valveMotorIN2, LOW);
}



// ========== HÀM NHẬN LỆNH TỪ SERIAL MONITOR ==========
void checkSerialCommands() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.length() == 0) return;
    Serial.print(F("[PC] Command: "));
    Serial.println(cmd);

    // --- Giống các lệnh của ESP ---
    if (cmd.startsWith(F("FEED:"))) {
      float amount = cmd.substring(5).toFloat();
      if (amount > 0 && amount <= 10) {
        feedProcess(amount);
      } else {
        Serial.println(F("ERROR:INVALID_AMOUNT"));
      }
    } 
    else if (cmd == F("GET_WEIGHT")) {
      Serial.print(F("WEIGHT:"));
      Serial.println(currentWeight, 3);
    }
    else if (cmd == F("GET_ENCODER")) {
      Serial.print(F("ENCODER:"));
      Serial.println(encoderCount);
    }
    else if (cmd == F("RESET_ENCODER")) {
      encoderCount = 0;
      Serial.println(F("ENCODER_RESET"));
    }
    else if (cmd.startsWith(F("SET_ENCODER_TARGET:"))) {
      long newTarget = cmd.substring(19).toInt();
      if (newTarget > 0 && newTarget < 10000) {
        encoderOpenTarget = newTarget;
        Serial.print(F("ENCODER_TARGET_SET:"));
        Serial.println(encoderOpenTarget);
      } else {
        Serial.println(F("ERROR:INVALID_TARGET"));
      }
    }
    else if (cmd == F("OPEN_VALVE")) {
      openValve();
    }
    else if (cmd == F("CLOSE_VALVE")) {
      closeValve();
    }
    else if (cmd.startsWith(F("SETTIME:"))) {
      String data = cmd.substring(8);
      int y = data.substring(0, 4).toInt();
      int M = data.substring(5, 7).toInt();
      int d = data.substring(8, 10).toInt();
      int h = data.substring(11, 13).toInt();
      int m = data.substring(14, 16).toInt();
      int s = data.substring(17, 19).toInt();
      
      myRTC.adjust(DateTime(y, M, d, h, m, s));
      Serial.println(F("RTC_UPDATED"));
    }
    else if (cmd.startsWith(F("SET_MAXTIME:"))) {
      float newMaxTime = cmd.substring(12).toFloat();
      if (newMaxTime >= 0.5 && newMaxTime <= 10) {
        maxTimePerKg = newMaxTime;
        Serial.print(F("MAXTIME_UPDATED:"));
        Serial.println(maxTimePerKg, 1);
      } else {
        Serial.println(F("ERROR:INVALID_MAXTIME"));
      }
    }
    else if (cmd == F("STOP_FEED")) {
      if (isFeeding) {
        isFeeding = false;
        stopValveMotor();
        digitalWrite(releaseMotor, LOW);
        Serial.println(F("FEED_STOPPED"));
      }
    }
    else if (cmd == F("DEBUG_INFO")) {
      Serial.println(F("=== DEBUG INFO ==="));
      Serial.print(F("Weight: "));
      Serial.println(currentWeight, 3);
      Serial.print(F("Encoder: "));
      Serial.println(encoderCount);
      Serial.print(F("Target: "));
      Serial.println(encoderOpenTarget);
      Serial.print(F("Feeding: "));
      Serial.println(isFeeding ? F("YES") : F("NO"));
      Serial.print(F("MaxTime: "));
      Serial.print(maxTimePerKg, 1);
      Serial.println(F(" min/kg"));
    }
    else if (cmd == F("HELP")) {
      Serial.println(F("=== DANH SÁCH LỆNH TEST ==="));
      Serial.println(F("FEED:x              - Nạp x kg"));
      Serial.println(F("OPEN_VALVE          - Mở van"));
      Serial.println(F("CLOSE_VALVE         - Đóng van"));
      Serial.println(F("GET_WEIGHT          - Xem khối lượng hiện tại"));
      Serial.println(F("GET_ENCODER         - Xem encoder"));
      Serial.println(F("RESET_ENCODER       - Reset encoder"));
      Serial.println(F("SET_ENCODER_TARGET:x - Đặt mục tiêu encoder"));
      Serial.println(F("SET_MAXTIME:x       - Đặt maxTimePerKg (phút/kg)"));
      Serial.println(F("STOP_FEED           - Dừng khẩn cấp"));
      Serial.println(F("==========================="));
    }
    else {
      Serial.println(F("UNKNOWN COMMAND. Gõ HELP để xem danh sách."));
    }
  }
}
