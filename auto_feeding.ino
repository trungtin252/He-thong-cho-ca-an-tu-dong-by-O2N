#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <DS3231.h>


RTClib myRTC;
File logFile;

// Cấu hình chân
const int chipSelect = 10;
const int feederPin = 3;  
const int buttonPin = 2;

struct FeedingSchedule {
  byte hour;
  byte minute;
  byte duration;
  bool enabled;
};

FeedingSchedule schedule[5] = {  
  {6, 0, 5, true},     // Cho ăn lúc 6:00 trong 5s
  {12, 0, 5, true},    // Cho ăn lúc 12:00 trong 5s
  {18, 0, 5, true},    // Cho ăn lúc 18:00 trong 5s
  {0, 0, 0, false},    // Slot trống
  {0, 0, 0, false}     // Slot trống
};
const byte maxSchedules = 5;

// Biến trạng thái
bool isFeeding = false;
unsigned long feedingStartTime = 0;
byte currentFeedingDuration = 0;
bool buttonPressed = false;
unsigned long lastButtonPress = 0;
byte lastMinute = 255;

// Buffer cho serial input
String serialBuffer = "";

// Hàm in số 2 chữ số
void printTwoDigits(Print &out, int num) {
  if (num < 10) out.print('0');
  out.print(num);
}

void setup() {
  Serial.begin(9600);
  Wire.begin();
  delay(500);
  
  Serial.println(F("=== HE THONG CHO CA AN TU DONG ==="));
  Serial.println(F("Nano Ready!"));
  
  pinMode(feederPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  digitalWrite(feederPin, LOW);
  
  Serial.print(F("Init SD..."));
  if (!SD.begin(chipSelect)) {
    Serial.println(F("Failed!"));
    return;
  }
  Serial.println(F("OK"));
  
  // Load schedule từ SD card
  loadScheduleFromSD();
  
  // Tạo file header nếu chưa tồn tại
  if (!SD.exists("log.txt")) {
    logFile = SD.open("log.txt", FILE_WRITE);
    if (logFile) {
      logFile.println(F("=== LICH SU CHO AN ==="));
      logFile.close();
    }
  }
  
  displaySchedule();
  Serial.println(F("Ready!"));
}

void loop() {
  delay(1000);
  
  DateTime now = myRTC.now();
  
  if (now.minute() != lastMinute) {
    printTime(now);
    lastMinute = now.minute();
  }
  
  handleButton();
  checkSchedule(now);
  handleFeeding();
  handleSerial();
}

void handleButton() {
  bool pressed = digitalRead(buttonPin) == LOW;
  
  if (pressed && !buttonPressed && (millis() - lastButtonPress > 50)) {
    buttonPressed = true;
    lastButtonPress = millis();
    
    if (!isFeeding) {
      Serial.println(F("Manual feed!"));
      startFeeding(5, 'M');
    }
  } else if (!pressed) {
    buttonPressed = false;
  }
}

void checkSchedule(DateTime now) {
  if (isFeeding) return;
  
  for (byte i = 0; i < maxSchedules; i++) {
    if (schedule[i].enabled && 
        now.hour() == schedule[i].hour && 
        now.minute() == schedule[i].minute && 
        now.second() == 0) {
      
      Serial.print(F("Auto feed: "));
      printTwoDigits(Serial, schedule[i].hour);
      Serial.print(':');
      printTwoDigits(Serial, schedule[i].minute);
      Serial.println();
      
      startFeeding(schedule[i].duration, 'A');
      break;
    }
  }
}

void startFeeding(byte duration, char type) {
  isFeeding = true;
  feedingStartTime = millis();
  currentFeedingDuration = duration;
  
  digitalWrite(feederPin, HIGH);
  
  Serial.print(F("Feeding start: "));
  Serial.print(duration);
  Serial.println(F("s"));
  
  logEvent(duration, type);
}

void handleFeeding() {
  if (isFeeding) {
    unsigned long elapsed = (millis() - feedingStartTime) / 1000;
    
    if (elapsed >= currentFeedingDuration) {
      isFeeding = false;
      digitalWrite(feederPin, LOW);
      Serial.println(F("Feeding done!"));
    }
  }
}

void logEvent(byte duration, char type) {
  logFile = SD.open("log.txt", FILE_WRITE);
  if (logFile) {
    DateTime now = myRTC.now();
    
    printTwoDigits(logFile, now.day());
    logFile.print('/');
    printTwoDigits(logFile, now.month());
    logFile.print('/');
    logFile.print(now.year() % 100);
    logFile.print(' ');
    printTwoDigits(logFile, now.hour());
    logFile.print(':');
    printTwoDigits(logFile, now.minute());
    logFile.print(F(" - "));
    logFile.print(duration);
    logFile.print(F("s - "));
    logFile.println(type);
    logFile.close();

    Serial.print(F("Log saved: "));
    printTwoDigits(Serial, now.day());
    Serial.print('/');
    printTwoDigits(Serial, now.month());
    Serial.print('/');
    Serial.print(now.year() % 100);
    Serial.print(' ');
    printTwoDigits(Serial, now.hour());
    Serial.print(':');
    printTwoDigits(Serial, now.minute());
    Serial.print(F(" - "));
    Serial.print(duration);
    Serial.print(F("s - "));
    Serial.println(type);
  }
}

void printTime(DateTime dt) {
  Serial.print(F("Time: "));
  Serial.print(dt.year());
  Serial.print('/');
  printTwoDigits(Serial, dt.month());
  Serial.print('/');
  printTwoDigits(Serial, dt.day());
  Serial.print(' ');
  printTwoDigits(Serial, dt.hour());
  Serial.print(':');
  printTwoDigits(Serial, dt.minute());
  Serial.print(':');
  printTwoDigits(Serial, dt.second());
  Serial.println();
}

void displaySchedule() {
  Serial.println(F("\n=== LICH CHO AN ==="));
  for (byte i = 0; i < maxSchedules; i++) {
    Serial.print(F("L"));
    Serial.print(i + 1);
    Serial.print(F(": "));
    
    if (schedule[i].enabled) {
      printTwoDigits(Serial, schedule[i].hour);
      Serial.print(':');
      printTwoDigits(Serial, schedule[i].minute);
      Serial.print(F(" - "));
      Serial.print(schedule[i].duration);
      Serial.print(F("s - ON"));
    } else {
      Serial.print(F("EMPTY"));
    }
    Serial.println();
  }
  Serial.println(F("Button: Manual Feed"));
  Serial.println(F("Commands: L=Log, S=Status, V=View, H=Help"));
  Serial.println(F("         SET1=06:00:5 (Set slot 1)"));
  Serial.println(F("         ON1, OFF1 (Enable/Disable)"));
  Serial.println(F("         DEL1 (Delete slot 1)"));
  Serial.println(F("         SAVE (Save to SD)"));
  Serial.println(F("==================\n"));
}

void readLog() {
  logFile = SD.open("log.txt");
  if (logFile) {
    Serial.println(F("\n=== FEEDING LOG ==="));
    while (logFile.available()) {
      Serial.write(logFile.read());
    }
    logFile.close();
    Serial.println(F("=== END ===\n"));
  } else {
    Serial.println(F("Log error!"));
  }
}

void showStatus() {
  DateTime now = myRTC.now();
  
  Serial.println(F("\n=== STATUS ==="));
  Serial.print(F("Time: "));
  printTime(now);
  
  Serial.print(F("Feeding: "));
  Serial.println(isFeeding ? F("YES") : F("NO"));
  
  if (isFeeding) {
    unsigned long elapsed = (millis() - feedingStartTime) / 1000;
    Serial.print(F("Progress: "));
    Serial.print(elapsed);
    Serial.print('/');
    Serial.print(currentFeedingDuration);
    Serial.println('s');
  }
  
  Serial.print(F("Feeder: "));
  Serial.println(digitalRead(feederPin) ? F("ON") : F("OFF"));
  Serial.println(F("==============\n"));
}

// Hàm xử lý lệnh thiết lập lịch
void processSetCommand(String cmd) {
  // Format: SET1=06:00:5 (slot:hour:minute:duration)
  int equalPos = cmd.indexOf('=');
  if (equalPos == -1) {
    Serial.println(F("Error: Format SET1=06:00:5"));
    return;
  }
  
  byte slot = cmd.substring(3, equalPos).toInt();
  String timeStr = cmd.substring(equalPos + 1);
  
  if (slot < 1 || slot > maxSchedules) {
    Serial.println(F("Error: Slot 1-5"));
    return;
  }
  slot--; // Convert to 0-based index
  
  // Parse time format HH:MM:D
  int colon1 = timeStr.indexOf(':');
  int colon2 = timeStr.lastIndexOf(':');
  
  if (colon1 == -1 || colon2 == -1 || colon1 == colon2) {
    Serial.println(F("Error: Format HH:MM:D"));
    return;
  }
  
  byte hour = timeStr.substring(0, colon1).toInt();
  byte minute = timeStr.substring(colon1 + 1, colon2).toInt();
  byte duration = timeStr.substring(colon2 + 1).toInt();
  
  if (hour > 23 || minute > 59 || duration == 0 || duration > 60) {
    Serial.println(F("Error: Invalid time/duration"));
    return;
  }
  
  schedule[slot].hour = hour;
  schedule[slot].minute = minute;
  schedule[slot].duration = duration;
  schedule[slot].enabled = true;
  
  Serial.print(F("Set slot "));
  Serial.print(slot + 1);
  Serial.print(F(": "));
  printTwoDigits(Serial, hour);
  Serial.print(':');
  printTwoDigits(Serial, minute);
  Serial.print(F(" - "));
  Serial.print(duration);
  Serial.println(F("s"));
}

void processOnOffCommand(String cmd, bool enable) {
  byte slot = cmd.substring(enable ? 2 : 3).toInt();
  
  if (slot < 1 || slot > maxSchedules) {
    Serial.println(F("Error: Slot 1-5"));
    return;
  }
  slot--; 
  
  schedule[slot].enabled = enable;
  
  Serial.print(F("Slot "));
  Serial.print(slot + 1);
  Serial.println(enable ? F(" ENABLED") : F(" DISABLED"));
}

void processDelCommand(String cmd) {
  byte slot = cmd.substring(3).toInt();
  
  if (slot < 1 || slot > maxSchedules) {
    Serial.println(F("Error: Slot 1-5"));
    return;
  }
  slot--; 
  
  schedule[slot].hour = 0;
  schedule[slot].minute = 0;
  schedule[slot].duration = 0;
  schedule[slot].enabled = false;
  
  Serial.print(F("Deleted slot "));
  Serial.println(slot + 1);
}

void saveScheduleToSD() {
  SD.remove("schedule.txt");
  logFile = SD.open("schedule.txt", FILE_WRITE);
  if (logFile) {
    for (byte i = 0; i < maxSchedules; i++) {
      if (schedule[i].enabled) {
        logFile.print(schedule[i].hour);
        logFile.print(',');
        logFile.print(schedule[i].minute);
        logFile.print(',');
        logFile.print(schedule[i].duration);
        logFile.print(',');
        logFile.println(schedule[i].enabled ? '1' : '0');
      }
    }
    logFile.close();
    Serial.println(F("Schedule saved to SD!"));
  } else {
    Serial.println(F("Save error!"));
  }
}

void loadScheduleFromSD() {
  logFile = SD.open("schedule.txt");
  if (logFile) {
    byte index = 0;
    while (logFile.available() && index < maxSchedules) {
      String line = logFile.readStringUntil('\n');
      line.trim();
      
      int comma1 = line.indexOf(',');
      int comma2 = line.indexOf(',', comma1 + 1);
      int comma3 = line.indexOf(',', comma2 + 1);
      
      if (comma1 > 0 && comma2 > 0 && comma3 > 0) {
        schedule[index].hour = line.substring(0, comma1).toInt();
        schedule[index].minute = line.substring(comma1 + 1, comma2).toInt();
        schedule[index].duration = line.substring(comma2 + 1, comma3).toInt();
        schedule[index].enabled = line.substring(comma3 + 1).toInt() == 1;
        index++;
      }
    }
    logFile.close();
    Serial.println(F("Schedule loaded from SD!"));
  }
}

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        processSerialCommand(serialBuffer);
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
    }
  }
}

void processSerialCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  
  if (cmd == "L") {
    readLog();
  } else if (cmd == "S") {
    showStatus();
  } else if (cmd == "V") {
    displaySchedule();
  } else if (cmd == "H") {
    Serial.println(F("\n=== COMMANDS ==="));
    Serial.println(F("L - View log"));
    Serial.println(F("S - View status"));
    Serial.println(F("V - View schedule"));
    Serial.println(F("H - Help"));
    Serial.println(F("SET1=06:00:5 - Set slot 1 to 6AM, 5 seconds"));
    Serial.println(F("ON1 - Enable slot 1"));
    Serial.println(F("OFF1 - Disable slot 1"));
    Serial.println(F("DEL1 - Delete slot 1"));
    Serial.println(F("SAVE - Save schedule to SD"));
    Serial.println(F("SHOW - Show current schedule"));
    Serial.println(F("================\n"));
  } else if (cmd.startsWith("SET")) {
    processSetCommand(cmd);
  } else if (cmd.startsWith("ON")) {
    processOnOffCommand(cmd, true);
  } else if (cmd.startsWith("OFF")) {
    processOnOffCommand(cmd, false);
  } else if (cmd.startsWith("DEL")) {
    processDelCommand(cmd);
  } else if (cmd == "SAVE") {
    saveScheduleToSD();
  } else if (cmd == "SHOW") {
    displaySchedule();
  } else {
    Serial.println(F("Unknown command. Type H for help."));
  }
}