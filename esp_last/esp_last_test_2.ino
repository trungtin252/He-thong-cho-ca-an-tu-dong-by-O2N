#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <FS.h>

// ==================== Config ====================
SoftwareSerial arduinoSerial(D1, D2);
LiquidCrystal_I2C lcd(0x27, 16, 2);

const int RESET_BUTTON_PIN = D5;
unsigned long resetButtonPressTime = 0;
bool resetButtonPressed = false;

enum Mode { DEV_MODE, OPERATION_MODE };
Mode currentMode = DEV_MODE;

struct DeviceConfig {
  char ssid[32];
  char password[64];
  char reciveServer[128];
  char commandServer[128];
  char deviceId[32];
  bool configured;
};

DeviceConfig config;
ESP8266WebServer server(80);

// Timing
unsigned long lastHeartbeat = 0;
unsigned long lastLCDUpdate = 0;
unsigned long lastCommandFetch = 0;
unsigned long lastScheduleCheck = 0;
unsigned long lastWiFiCheck = 0;


#define ARDUINO_LOG_SIZE 50
String arduinoLog[ARDUINO_LOG_SIZE];
int arduinoLogIndex = 0;
int arduinoLogCount = 0;

// Feeding
bool isFeeding = false;
float currentFeedingWeight = 0.0;
String feedingMessage = "";
unsigned long feedDoneTime = 0;
bool feedingJustDone = false;
String lastLCDContent = "";
bool arduinoReady = false;

// Schedule structure
struct Schedule {
  byte hour;
  byte minute;
  float amount;
  bool enabled;
};

Schedule schedules[5];
byte scheduleCount = 0;

// Max time config
float maxTimePerKg = 1.8;

// Time from Arduino RTC
struct DateTime {
  int year;
  byte month;
  byte day;
  byte hour;
  byte minute;
  byte second;
};

DateTime currentTime;
bool timeValid = false;
unsigned long lastTimeReceived = 0;

// ==================== EEPROM Functions ====================
void loadConfig() {
  EEPROM.begin(512);
  EEPROM.get(0, config);
  EEPROM.end();

  if (config.configured != true) {
    memset(&config, 0, sizeof(config));
    config.configured = false;
    strcpy(config.deviceId, "FEED001");
  }
}

void saveConfig() {
  config.configured = true;
  EEPROM.begin(512);
  EEPROM.put(0, config);
  EEPROM.commit();
  EEPROM.end();
  Serial.println("Config saved!");
}

void clearConfig() {
  memset(&config, 0, sizeof(config));
  config.configured = false;
  saveConfig();
}

// ==================== SPIFFS Storage Functions ====================
void initSPIFFS() {
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS initialization failed!");
    SPIFFS.format();
    SPIFFS.begin();
  }
}

void loadSchedule() {
  scheduleCount = 0;
  
  if (!SPIFFS.exists("/schedule.txt")) {
    Serial.println("No schedule file, creating default");
    schedules[0] = {8, 0, 2.5, false};
    schedules[1] = {12, 0, 1.5, false};
    schedules[2] = {18, 0, 2.0, false};
    schedules[3] = {0, 0, 0, false};
    schedules[4] = {0, 0, 0, false};
    scheduleCount = 5;
    saveSchedule();
    return;
  }

  File f = SPIFFS.open("/schedule.txt", "r");
  if (f) {
    while (f.available() && scheduleCount < 5) {
      String line = f.readStringUntil('\n');
      line.trim();
      
      int c1 = line.indexOf(',');
      int c2 = line.lastIndexOf(',');
      
      if (c1 > 0 && c2 > c1) {
        String timeStr = line.substring(0, c1);
        float amount = line.substring(c1 + 1, c2).toFloat();
        bool enabled = line.substring(c2 + 1).toInt() == 1;
        
        int colon = timeStr.indexOf(':');
        if (colon > 0) {
          schedules[scheduleCount].hour = timeStr.substring(0, colon).toInt();
          schedules[scheduleCount].minute = timeStr.substring(colon + 1).toInt();
          schedules[scheduleCount].amount = amount;
          schedules[scheduleCount].enabled = enabled;
          scheduleCount++;
        }
      }
    }
    f.close();
    Serial.printf("Loaded %d schedules\n", scheduleCount);
  }
}

void saveSchedule() {
  File f = SPIFFS.open("/schedule.txt", "w");
  if (f) {
    for (byte i = 0; i < scheduleCount; i++) {
      if (schedules[i].hour < 10) f.print('0');
      f.print(schedules[i].hour);
      f.print(':');
      if (schedules[i].minute < 10) f.print('0');
      f.print(schedules[i].minute);
      f.print(',');
      f.print(schedules[i].amount, 1);
      f.print(',');
      f.println(schedules[i].enabled ? '1' : '0');
    }
    f.close();
    Serial.println("Schedule saved");
  }
}

void loadMaxTime() {
  if (!SPIFFS.exists("/maxtime.txt")) {
    maxTimePerKg = 1.8;
    saveMaxTime();
    return;
  }
  
  File f = SPIFFS.open("/maxtime.txt", "r");
  if (f) {
    String data = f.readStringUntil('\n');
    data.trim();
    float value = data.toFloat();
    if (value > 0.5 && value < 10) {
      maxTimePerKg = value;
      Serial.printf("Loaded maxTimePerKg: %.1f\n", maxTimePerKg);
    }
    f.close();
  }
}

void saveMaxTime() {
  File f = SPIFFS.open("/maxtime.txt", "w");
  if (f) {
    f.println(maxTimePerKg, 1);
    f.close();
    Serial.printf("Saved maxTimePerKg: %.1f\n", maxTimePerKg);
  }
}

void writeLog(String type, float amount, bool success) {
  if (!timeValid) return;
  
  File f = SPIFFS.open("/logs.txt", "a");
  if (f) {
    char buf[100];
    sprintf(buf, "%04d/%02d/%02d %02d:%02d:%02d,%s,%.1fKG,%s\n",
            currentTime.year, currentTime.month, currentTime.day,
            currentTime.hour, currentTime.minute, currentTime.second,
            type.c_str(), amount, success ? "SUCCESS" : "FAIL");
    f.print(buf);
    f.close();
    Serial.print("Log written: ");
    Serial.print(buf);
  }
}

String getTodayLogs() {
  if (!timeValid) return "No time available";
  
  String todayStr = String(currentTime.year) + "/" + 
                    String(currentTime.month) + "/" + 
                    String(currentTime.day);
  
  String result = "LOGS:" + todayStr + "\n";
  
  if (!SPIFFS.exists("/logs.txt")) {
    return result + "No logs found\n";
  }
  
  File f = SPIFFS.open("/logs.txt", "r");
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.startsWith(todayStr)) {
        result += line + "\n";
      }
    }
    f.close();
  }
  
  return result;
}

void deleteLogs() {
  if (SPIFFS.exists("/logs.txt")) {
    SPIFFS.remove("/logs.txt");
    Serial.println("Logs deleted");
  }
}

// ==================== Time Functions ====================
String getTimeString() {
  if (!timeValid) return "Loading...";
  
  char buf[20];
  sprintf(buf, "%02d:%02d:%02d", currentTime.hour, currentTime.minute, currentTime.second);
  return String(buf);
}

String getDateString() {
  if (!timeValid) return "N/A";
  
  char buf[20];
  sprintf(buf, "%04d-%02d-%02d", currentTime.year, currentTime.month, currentTime.day);
  return String(buf);
}

String getDateTimeString() {
  if (!timeValid) return "N/A";
  
  char buf[20];
  sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
          currentTime.year, currentTime.month, currentTime.day,
          currentTime.hour, currentTime.minute, currentTime.second);
  return String(buf);
}

void parseTimeFromArduino(String timeStr) {
  // Format: 2025-01-15 10:30:45
  if (timeStr.length() < 19) return;
  
  currentTime.year = timeStr.substring(0, 4).toInt();
  currentTime.month = timeStr.substring(5, 7).toInt();
  currentTime.day = timeStr.substring(8, 10).toInt();
  currentTime.hour = timeStr.substring(11, 13).toInt();
  currentTime.minute = timeStr.substring(14, 16).toInt();
  currentTime.second = timeStr.substring(17, 19).toInt();
  
  timeValid = true;
  lastTimeReceived = millis();
}

// ==================== Schedule Check ====================
void checkAutoFeed() {
  if (!timeValid || isFeeding) return;
  
  // Kiểm tra nếu không nhận được time update quá 10 giây thì request lại
  if (millis() - lastTimeReceived > 10000) {
    arduinoSerial.println("GET_TIME");
    lastTimeReceived = millis();
    return;
  }
  
  byte h = currentTime.hour;
  byte m = currentTime.minute;
  byte s = currentTime.second;
  
  // Chỉ check khi giây từ 0-5 để tránh trigger nhiều lần
  if (s > 5) return;
  
  static byte lastCheckedMinute = 255;
  
  // Tránh check trùng trong cùng 1 phút
  if (lastCheckedMinute == m) return;
  
  for (byte i = 0; i < scheduleCount; i++) {
    if (schedules[i].enabled && 
        schedules[i].hour == h && 
        schedules[i].minute == m) {
      
      lastCheckedMinute = m;
      
      Serial.printf("Auto feed triggered: %.1f kg at %02d:%02d\n", 
                    schedules[i].amount, h, m);
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("AUTO FEEDING");
      lcd.setCursor(0, 1);
      lcd.print(schedules[i].amount, 1);
      lcd.print(" kg");
      
      arduinoSerial.print("FEED:");
      arduinoSerial.println(schedules[i].amount, 1);
      
      delay(2000);
      break;
    }
  }
  
  // Reset check sau 1 phút
  if (lastCheckedMinute != 255 && lastCheckedMinute != m) {
    lastCheckedMinute = 255;
  }
}

// ==================== LCD Functions ====================
void initLCD() {
  Wire.begin(D4, D3);
  lcd.init();
  lcd.backlight();
  lcd.clear();
}

void updateLCD() {
  if (millis() - lastLCDUpdate < 500) return;
  lastLCDUpdate = millis();

  String newContent = "";

  if (feedingJustDone) {
    if (millis() - feedDoneTime > 3000) {
      feedingJustDone = false;
      feedingMessage = "";
      currentFeedingWeight = 0.0;
    }
  }

  if (isFeeding || feedingJustDone) {
    if (feedingMessage.startsWith("ERROR")) {
      newContent = "ERROR:" + feedingMessage;
    } else if (feedingMessage == "DONE") {
      newContent = "DONE:" + String(currentFeedingWeight, 2) + "kg";
    } else if (feedingMessage == "STOPPED") {
      newContent = "STOPPED";
    } else {
      newContent = "FEEDING:" + String(currentFeedingWeight, 2) + "kg";
    }
  } else {
    String line1 = "";
    String line2 = "";

    if (currentMode == DEV_MODE) {
      line1 = "MODE: DEV";
      line2 = "192.168.4.1";
    } else {
      line1 = "ID:" + String(config.deviceId);
      line2 = getTimeString();
    }

    newContent = line1 + "|" + line2;
  }

  if (newContent != lastLCDContent) {
    lastLCDContent = newContent;
    lcd.clear();

    if (isFeeding || feedingJustDone) {
      if (feedingMessage.startsWith("ERROR")) {
        lcd.setCursor(0, 0);
        lcd.print("ERROR!");
        lcd.setCursor(0, 1);
        String errorType = feedingMessage.substring(6);
        lcd.print(errorType.substring(0, min(16, (int)errorType.length())));
      } else if (feedingMessage == "DONE") {
        lcd.setCursor(0, 0);
        lcd.print("DONE!");
        lcd.setCursor(0, 1);
        lcd.print(String(currentFeedingWeight, 2));
        lcd.print(" kg");
      } else if (feedingMessage == "STOPPED") {
        lcd.setCursor(0, 0);
        lcd.print("STOPPED!");
      } else {
        lcd.setCursor(0, 0);
        lcd.print("FEEDING...");
        lcd.setCursor(0, 1);
        lcd.print(String(currentFeedingWeight, 2));
        lcd.print(" kg");
      }
    } else {
      int sepPos = newContent.indexOf('|');
      if (sepPos > 0) {
        lcd.setCursor(0, 0);
        lcd.print(newContent.substring(0, sepPos));
        lcd.setCursor(0, 1);
        lcd.print(newContent.substring(sepPos + 1));
      }
    }
  }
}

// ==================== Web Server HTML ====================
const char* devModeHTML = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>Feeding System - Config</title>
  <style>
    body { font-family: Arial; margin: 20px; background: #f0f0f0; }
    .container { max-width: 800px; margin: auto; background: white; padding: 20px; border-radius: 10px; }
    h1 { color: #333; }
    h2 { color: #555; font-size: 18px; margin-top: 20px; }
    input, button { width: 100%; padding: 10px; margin: 5px 0; box-sizing: border-box; }
    button { background: #4CAF50; color: white; border: none; cursor: pointer; font-size: 16px; border-radius: 5px; }
    button:hover { background: #45a049; }
    .section { margin: 20px 0; padding: 15px; background: #f9f9f9; border-radius: 5px; }
    .command-btn { background: #2196F3; }
    .command-btn:hover { background: #0b7dda; }
    .danger-btn { background: #f44336; }
    .danger-btn:hover { background: #da190b; }
    .warning-btn { background: #ff9800; }
    .warning-btn:hover { background: #e68900; }
    #response { background: #e8e8e8; padding: 10px; min-height: 100px; max-height: 300px; overflow-y: auto; font-family: monospace; font-size: 12px; white-space: pre-wrap; }
    .time-input { display: flex; gap: 5px; }
    .time-input input { width: auto; flex: 1; }
    .schedule-item { background: white; padding: 10px; margin: 5px 0; border-radius: 5px; border: 1px solid #ddd; display: flex; justify-content: space-between; align-items: center; }
    .schedule-item button { width: auto; padding: 5px 15px; }
    #scheduleList { max-height: 300px; overflow-y: auto; }
    .inline-btn { width: auto; display: inline-block; margin: 0 5px; padding: 8px 15px; }
    .time-display { font-size: 28px; text-align: center; padding: 20px; background: #e3f2fd; border-radius: 5px; font-weight: bold; color: #1976d2; }
  </style>
</head>
<body>
  <div class='container'>
    <h1>Feeding System Config</h1>
    
    <div class='section'>
      <h2>RTC Time (from Arduino DS3231)</h2>
      <div id='currentTime' class='time-display'>Loading...</div>
      <div class='time-input'>
        <input type='datetime-local' id='newTime' step='1'>
        <button class='warning-btn' onclick='setTime()'>Set RTC Time</button>
      </div>
    </div>
    
    <div class='section'>
      <h2>Feeding Schedule</h2>
      <div id='scheduleList'></div>
      <hr>
      <h3>Add New Schedule</h3>
      <div class='time-input'>
        <input type='time' id='newScheduleTime'>
        <input type='number' id='newScheduleAmount' placeholder='Amount (kg)' step='0.1' min='0.1' max='10'>
      </div>
      <button onclick='addSchedule()'>Add Schedule</button>
      <button class='warning-btn' onclick='saveSchedules()'>Save All Schedules</button>
    </div>
    
    <div class='section'>
      <h2>WiFi Configuration</h2>
      <input type='text' id='ssid' placeholder='WiFi SSID'>
      <input type='password' id='password' placeholder='WiFi Password'>
    </div>
    
    <div class='section'>
      <h2>Server Configuration</h2>
      <input type='text' id='reciveServer' placeholder='Log Server URL'>
      <input type='text' id='commandServer' placeholder='Command Server URL'>
      <input type='text' id='deviceId' placeholder='Device ID' value='FEED001'>
    </div>
    
    <button onclick='saveConfig()'>Save Config & Start Operation Mode</button>

    <div class='section'>
      <h2>Manual Feeding</h2>
      <div class='time-input'>
        <input type='number' id='feedAmount' placeholder='Amount (kg)' step='0.1' min='0.1' max='10' value='1.0'>
        <button class='warning-btn' onclick='manualFeed()'>Feed Now</button>
      </div>
    </div>

    <div class='section'>
      <h2>Max Feed Time Settings</h2>
      <p style='font-size: 12px; color: #666;'>Maximum time allowed per kg (minutes/kg)</p>
      <div class='time-input'>
        <input type='number' id='maxTimePerKg' step='0.1' min='0.5' max='10' value='1.8'>
        <button class='warning-btn' onclick='getMaxTime()'>Get Current</button>
      </div>
      <button onclick='setMaxTime()'>Update Max Time</button>
    </div>

    <div class='section'>
      <h2>Encoder Configuration</h2>
      <p style='font-size: 12px; color: #666;'>Configure encoder settings for valve control</p>
      <div style='display: flex; gap: 10px; margin: 10px 0;'>
        <button class='warning-btn' onclick='resetEncoder()' style='flex: 1;'>Reset Encoder to 0</button>
        <button class='command-btn' onclick='getEncoderValue()' style='flex: 1;'>Get Current Encoder</button>
      </div>
      <div style='display: flex; gap: 10px; margin: 10px 0;'>
        <input type='number' id='encoderTarget' placeholder='Set encoder target (0-10000)' min='0' max='10000' style='flex: 2;'>
        <button class='warning-btn' onclick='setEncoderTarget()' style='flex: 1;'>Set Target</button>
      </div>
      <div id='encoderResponse' style='background: #e8f5e9; padding: 10px; margin-top: 10px; min-height: 50px; font-family: monospace; font-size: 12px;'></div>
    </div>

    <div class='section'>
      <h2>Send Command to Arduino</h2>
      <input type='text' id='arduinoCommand' placeholder='Enter command (e.g. GET_WEIGHT)'>
      <button class='command-btn' onclick='sendArduinoCommand()'>Send Command</button>
      <div id='commandResponse' style='background: #e8f5e9; padding: 10px; margin-top: 10px; min-height: 50px; font-family: monospace; font-size: 12px;'></div>
    </div>

    <div class='section'>
      <h2>System Logs</h2>
      <button class='command-btn' onclick='getLogs()'>Get Today Logs</button>
      <button class='danger-btn' onclick='deleteLogs()'>Delete All Logs</button>
      <div id='response'></div>
    </div>

    <div class='section'>
      <h2>Arduino Communication Log</h2>
      <button class='command-btn' onclick='getArduinoLog()'>Refresh Log</button>
      <button class='danger-btn' onclick='clearArduinoLog()'>Clear Log</button>
      <div id='arduinoLog' style='background: #e8e8e8; padding: 10px; min-height: 150px; max-height: 300px; overflow-y: auto; font-family: monospace; font-size: 12px; white-space: pre-wrap;'></div>
    </div>
  </div>
  
  <script>
    let schedules = [];
    
    function updateTime() {
      fetch('/gettime').then(r => r.text()).then(t => {
        document.getElementById('currentTime').innerText = t;
      });
    }
    
    function setTime() {
      const datetime = document.getElementById('newTime').value;
      if (!datetime) {
        alert('Please select date and time');
        return;
      }
      
      // Convert from datetime-local format to YYYY-MM-DD HH:MM:SS
      const formatted = datetime.replace('T', ' ') + ':00';
      
      if (confirm('Set RTC time to: ' + formatted + '?')) {
        fetch('/settime?datetime=' + encodeURIComponent(formatted))
          .then(r => r.text())
          .then(t => {
            alert(t);
            updateTime();
          });
      }
    }
    
    function loadSchedules() {
      fetch('/getschedule').then(r => r.json()).then(data => {
        schedules = data.schedules || [];
        renderSchedules();
      });
    }
    
    function saveConfig() {
      var data = {
        ssid: document.getElementById('ssid').value,
        password: document.getElementById('password').value,
        reciveServer: document.getElementById('reciveServer').value,
        commandServer: document.getElementById('commandServer').value,
        deviceId: document.getElementById('deviceId').value
      };
      
      if (!data.ssid || !data.deviceId) {
        alert('SSID and Device ID are required');
        return;
      }
      
      fetch('/save', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(data)
      }).then(r => r.text()).then(t => {
        alert(t);
        if (t.includes('Success')) {
          setTimeout(() => location.reload(), 2000);
        }
      });
    }
    
    function addSchedule() {
      const time = document.getElementById('newScheduleTime').value;
      const amount = parseFloat(document.getElementById('newScheduleAmount').value);
      
      if (!time || !amount) {
        alert('Please enter time and amount');
        return;
      }
      
      if (schedules.length >= 5) {
        alert('Maximum 5 schedules allowed');
        return;
      }
      
      schedules.push({ time: time, amount: amount, enabled: true });
      schedules.sort((a, b) => a.time.localeCompare(b.time));
      renderSchedules();
      
      document.getElementById('newScheduleTime').value = '';
      document.getElementById('newScheduleAmount').value = '';
    }
    
    function deleteSchedule(index) {
      if (confirm('Delete this schedule?')) {
        schedules.splice(index, 1);
        renderSchedules();
      }
    }
    
    function toggleSchedule(index) {
      schedules[index].enabled = !schedules[index].enabled;
      renderSchedules();
    }
    
    function renderSchedules() {
      const list = document.getElementById('scheduleList');
      if (schedules.length === 0) {
        list.innerHTML = '<p style="color:#999">No schedules. Add one below.</p>';
        return;
      }
      
      list.innerHTML = schedules.map((s, i) => {
        const enabled = s.enabled !== false;
        const statusColor = enabled ? '#4CAF50' : '#999';
        const statusText = enabled ? 'Active' : 'Disabled';
        return `<div class='schedule-item'>
          <span><strong>${s.time}</strong> - ${s.amount} kg <span style="color:${statusColor}">${statusText}</span></span>
          <div>
            <button class='warning-btn inline-btn' onclick='toggleSchedule(${i})'>${enabled ? 'Disable' : 'Enable'}</button>
            <button class='danger-btn inline-btn' onclick='deleteSchedule(${i})'>Delete</button>
          </div>
        </div>`;
      }).join('');
    }
    
    function saveSchedules() {
      const scheduleStr = schedules.map(s => {
        const enabled = s.enabled !== false ? '1' : '0';
        return s.time + ',' + s.amount + ',' + enabled;
      }).join('|');
      
      fetch('/saveschedule', {
        method: 'POST',
        headers: {'Content-Type': 'text/plain'},
        body: scheduleStr
      }).then(r => r.text()).then(t => {
        alert(t);
        loadSchedules();
      });
    }
    
    function manualFeed() {
      const amount = parseFloat(document.getElementById('feedAmount').value);
      if (!amount || amount <= 0 || amount > 10) {
        alert('Invalid amount (0.1 - 10 kg)');
        return;
      }
      
      if (confirm('Feed ' + amount + ' kg now?')) {
        fetch('/feed?amount=' + amount).then(r => r.text()).then(t => {
          alert(t);
        });
      }
    }
    
    function getMaxTime() {
      fetch('/getmaxtime').then(r => r.text()).then(t => {
        document.getElementById('maxTimePerKg').value = t;
      });
    }
    
    function setMaxTime() {
      const value = parseFloat(document.getElementById('maxTimePerKg').value);
      if (!value || value < 0.5 || value > 10) {
        alert('Value must be between 0.5 and 10 minutes/kg');
        return;
      }
      
      fetch('/setmaxtime?value=' + value).then(r => r.text()).then(t => {
        alert(t);
      });
    }
    
    function getLogs() {
      fetch('/getlogs').then(r => r.text()).then(t => {
        document.getElementById('response').innerText = t;
      });
    }
    
    function deleteLogs() {
      if (confirm('Delete all logs?')) {
        fetch('/deletelogs').then(r => r.text()).then(t => {
          alert(t);
          document.getElementById('response').innerText = '';
        });
      }
    }

    function getArduinoLog() {
      fetch('/getarduinolog').then(r => r.text()).then(t => {
        document.getElementById('arduinoLog').innerText = t;
      });
    }
    
    function clearArduinoLog() {
      if (confirm('Clear Arduino log?')) {
        fetch('/cleararduinolog').then(r => r.text()).then(t => {
          alert(t);
          document.getElementById('arduinoLog').innerText = '';
        });
      }
    }

    function resetEncoder() {
      if (confirm('Reset encoder to 0? This will reset the current encoder position.')) {
        fetch('/resetencoder')
          .then(r => r.text())
          .then(t => {
            document.getElementById('encoderResponse').innerText = 'Reset Encoder: ' + t;
          });
      }
    }
    
    function getEncoderValue() {
      fetch('/getencoder')
        .then(r => r.text())
        .then(t => {
          document.getElementById('encoderResponse').innerText = 'Current Encoder: ' + t;
        });
    }
    
    function setEncoderTarget() {
      const target = document.getElementById('encoderTarget').value;
      if (!target || target < 0 || target > 10000) {
        alert('Please enter a valid target (0-10000)');
        return;
      }
      
      if (confirm('Set encoder target to ' + target + '?')) {
        fetch('/setencodertarget?target=' + target)
          .then(r => r.text())
          .then(t => {
            document.getElementById('encoderResponse').innerText = 'Set Target: ' + t;
          });
      }
    }

    function sendArduinoCommand() {
      const command = document.getElementById('arduinoCommand').value.trim();
      if (!command) {
        alert('Please enter a command');
        return;
      }
      
      fetch('/sendcommand?cmd=' + encodeURIComponent(command))
        .then(r => r.text())
        .then(t => {
          document.getElementById('commandResponse').innerText = 'Sent: ' + command + '\nResponse: ' + t;
        });
    }
    
    // Auto update arduino log every 2 seconds
    setInterval(getArduinoLog, 1000);
    
    // Initial load
    getArduinoLog();
    
    // Auto update time every second
    setInterval(updateTime, 1000);
    
    // Initial load
    updateTime();
    loadSchedules();
    getMaxTime();
  </script>
</body>
</html>
)";

// ==================== Web Server Handlers ====================
void handleRoot() {
  server.send(200, "text/html", devModeHTML);
}

void handleSave() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, server.arg("plain"));

    strlcpy(config.ssid, doc["ssid"], sizeof(config.ssid));
    strlcpy(config.password, doc["password"], sizeof(config.password));
    strlcpy(config.reciveServer, doc["reciveServer"], sizeof(config.reciveServer));
    strlcpy(config.commandServer, doc["commandServer"], sizeof(config.commandServer));
    strlcpy(config.deviceId, doc["deviceId"], sizeof(config.deviceId));

    saveConfig();
    server.send(200, "text/plain", "Success! Restarting...");
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleGetTime() {
  server.send(200, "text/plain", getDateTimeString());
}

void handleSetTime() {
  if (server.hasArg("datetime")) {
    String datetime = server.arg("datetime");
    arduinoSerial.println("SETTIME:" + datetime);
    delay(500);
    arduinoSerial.println("GET_TIME");
    server.send(200, "text/plain", "RTC time updated");
  } else {
    server.send(400, "text/plain", "Missing datetime");
  }
}

void handleGetSchedule() {
  StaticJsonDocument<1024> doc;
  JsonArray arr = doc.createNestedArray("schedules");
  
  for (byte i = 0; i < scheduleCount; i++) {
    JsonObject obj = arr.createNestedObject();
    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", schedules[i].hour, schedules[i].minute);
    obj["time"] = timeStr;
    obj["amount"] = schedules[i].amount;
    obj["enabled"] = schedules[i].enabled;
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleSaveSchedule() {
  if (server.hasArg("plain")) {
    String data = server.arg("plain");
    scheduleCount = 0;
    
    int start = 0;
    int end = data.indexOf('|');
    
    while (scheduleCount < 5 && (end >= 0 || start < data.length())) {
      String item = (end >= 0) ? data.substring(start, end) : data.substring(start);
      item.trim();
      
      int c1 = item.indexOf(',');
      int c2 = item.lastIndexOf(',');
      
      if (c1 > 0 && c2 > c1) {
        String timeStr = item.substring(0, c1);
        float amount = item.substring(c1 + 1, c2).toFloat();
        bool enabled = item.substring(c2 + 1).toInt() == 1;
        
        int colon = timeStr.indexOf(':');
        if (colon > 0) {
          schedules[scheduleCount].hour = timeStr.substring(0, colon).toInt();
          schedules[scheduleCount].minute = timeStr.substring(colon + 1).toInt();
          schedules[scheduleCount].amount = amount;
          schedules[scheduleCount].enabled = enabled;
          scheduleCount++;
        }
      }
      
      if (end < 0) break;
      start = end + 1;
      end = data.indexOf('|', start);
    }
    
    saveSchedule();
    server.send(200, "text/plain", "Schedule saved!");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleFeed() {
  if (server.hasArg("amount")) {
    float amount = server.arg("amount").toFloat();
    if (amount > 0 && amount <= 10) {
      arduinoSerial.print("FEED:");
      arduinoSerial.println(amount, 1);
      server.send(200, "text/plain", "Feeding " + String(amount) + " kg...");
    } else {
      server.send(400, "text/plain", "Invalid amount");
    }
  } else {
    server.send(400, "text/plain", "Missing amount");
  }
}

void handleGetMaxTime() {
  server.send(200, "text/plain", String(maxTimePerKg, 1));
}

void handleSetMaxTime() {
  if (server.hasArg("value")) {
    float value = server.arg("value").toFloat();
    if (value >= 0.5 && value <= 10) {
      maxTimePerKg = value;
      saveMaxTime();
      server.send(200, "text/plain", "Max time updated: " + String(maxTimePerKg, 1) + " min/kg");
    } else {
      server.send(400, "text/plain", "Invalid value (0.5-10)");
    }
  } else {
    server.send(400, "text/plain", "Missing value");
  }
}

void handleGetLogs() {
  server.send(200, "text/plain", getTodayLogs());
}

void handleDeleteLogs() {
  deleteLogs();
  server.send(200, "text/plain", "Logs deleted");
}

void handleReset() {
  clearConfig();
  SPIFFS.format();
  server.send(200, "text/plain", "Factory reset! Restarting...");
  delay(1000);
  ESP.restart();
}

// ==================== Reset Button ====================
void handleResetButton() {
  int buttonState = digitalRead(RESET_BUTTON_PIN);

  if (buttonState == LOW) {
    if (!resetButtonPressed) {
      resetButtonPressed = true;
      resetButtonPressTime = millis();
    } else {
      unsigned long holdTime = millis() - resetButtonPressTime;

      if (holdTime < 5000 && !isFeeding && !feedingJustDone) {
        int secondsLeft = 5 - (holdTime / 1000);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Release to keep");
        lcd.setCursor(0, 1);
        lcd.print("Reset in: ");
        lcd.print(secondsLeft);
        lcd.print("s");
        lastLCDContent = "RESET_COUNTDOWN";
      }

      if (holdTime >= 5000) {
        performFactoryReset();
      }
    }
  } else {
    if (resetButtonPressed) {
      unsigned long holdTime = millis() - resetButtonPressTime;
      if (holdTime < 5000) {
        if (!isFeeding && !feedingJustDone) {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Reset cancelled");
          delay(1000);
        }
        lastLCDContent = "";
      }
      resetButtonPressed = false;
    }
  }
}

void performFactoryReset() {
  Serial.println("=== FACTORY RESET ===");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Factory Reset");
  lcd.setCursor(0, 1);
  lcd.print("Please wait...");

  clearConfig();
  SPIFFS.format();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Reset Complete!");
  lcd.setCursor(0, 1);
  lcd.print("Restarting...");

  delay(2000);
  ESP.restart();
}

// ==================== Arduino Communication ====================
void handleArduinoData() {
  static String lineBuffer = "";
  
  while (arduinoSerial.available()) {
    char c = arduinoSerial.read();
    if (c == '\n') {
      lineBuffer.trim();
      if (lineBuffer.length() > 0) {
        processArduinoLine(lineBuffer);
      }
      lineBuffer = "";
    } else {
      lineBuffer += c;
    }
  }
}

void processArduinoLine(String data) {
  Serial.println("Arduino: " + data);


  // Thêm vào log buffer
  arduinoLog[arduinoLogIndex] = data;
  arduinoLogIndex = (arduinoLogIndex + 1) % ARDUINO_LOG_SIZE;
  if (arduinoLogCount < ARDUINO_LOG_SIZE) {
    arduinoLogCount++;
  }

  if (data == "Arduino_OK" && !arduinoReady) {
    arduinoReady = true;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Arduino OK");
    delay(1000);
  } 
  else if (data.startsWith("TIME:")) {
    parseTimeFromArduino(data.substring(5));
  }
  else if (data.startsWith("ERROR_RTC_FIND")) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RTC ERROR!");
    lcd.setCursor(0, 1);
    lcd.print("Check DS3231");
  }
  else if (data.startsWith("ERROR_RTC_POWER_LOST")) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RTC Power Lost");
    lcd.setCursor(0, 1);
    lcd.print("Set time!");
    delay(3000);
  }
  else if (data.startsWith("FEEDING,")) {
    isFeeding = true;
    feedingJustDone = false;
    
    int comma1 = data.indexOf(',');
    int comma2 = data.indexOf(',', comma1 + 1);
    
    if (comma1 > 0 && comma2 > comma1) {
      currentFeedingWeight = data.substring(comma1 + 1, comma2).toFloat();
    }
  } 
  else if (data.startsWith("RELEASING,")) {
    int comma = data.indexOf(',');
    if (comma > 0) {
      float remaining = data.substring(comma + 1).toFloat();
      feedingMessage = "RELEASE:" + String(remaining, 2);
    }
  } 
  else if (data.startsWith("FEED_DONE,")) {
    isFeeding = false;
    feedingJustDone = true;
    feedDoneTime = millis();
    feedingMessage = "DONE";
    
    int comma = data.indexOf(',');
    if (comma > 0) {
      String amountStr = data.substring(comma + 1);
      amountStr.replace("KG", "");
      amountStr.trim();
      float amount = amountStr.toFloat();
      currentFeedingWeight = amount;
      
      // Write log
      writeLog("AUTO", amount, true);
      
      // Send to server
      if (currentMode == OPERATION_MODE) {
        sendFeedLogToServer(amount, true);
      }
    }
  } 
  else if (data == "FEED_START") {
    isFeeding = true;
    feedingJustDone = false;
  }
  else if (data == "FEED_STOPPED") {
    isFeeding = false;
    feedingJustDone = true;
    feedDoneTime = millis();
    feedingMessage = "STOPPED";
    writeLog("MANUAL_STOP", currentFeedingWeight, false);
  } else if (data.startsWith("FEED_FAIL,")) {
    int comma = data.indexOf(',');
    float amount = 0;
    if (comma > 0) {
      amount = data.substring(comma + 1).toFloat();
    }

    feedingMessage = "ERROR:FEED_FAIL";
    isFeeding = false;
    feedingJustDone = true;
    feedDoneTime = millis();

    // 📝 Ghi log thất bại
    writeLog("FAIL", amount, false);

    // 📤 Gửi lên server (nếu cần)
    if (currentMode == OPERATION_MODE) {
      sendFeedLogToServer(amount, false);
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("FEED FAIL");
    lcd.setCursor(0, 1);
    lcd.print("Released:");
    lcd.print(amount, 2);
    lcd.print("kg");
    delay(2000);
  }
  else if (data.startsWith("RELEASE_TIMEOUT,")) {
    int comma = data.indexOf(',');
    float amount = 0;
    if (comma > 0) {
      amount = data.substring(comma + 1).toFloat();
    }

    feedingMessage = "ERROR:TIMEOUT";
    isFeeding = false;
    feedingJustDone = true;
    feedDoneTime = millis();

    // 📝 Ghi log lỗi timeout
    writeLog("TIMEOUT", amount, false);

    if (currentMode == OPERATION_MODE) {
      sendFeedLogToServer(amount, false);
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RELEASE TIMEOUT");
    lcd.setCursor(0, 1);
    lcd.print("Released:");
    lcd.print(amount, 2);
    lcd.print("kg");
    delay(2000);
  }
  
}

// ==================== Server Communication ====================
void sendFeedLogToServer(float amount, bool success) {
  if (WiFi.status() != WL_CONNECTED || strlen(config.reciveServer) == 0) return;
  
  WiFiClient client;
  HTTPClient http;
  
  http.begin(client, config.reciveServer);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<256> doc;
  doc["type"] = "feed_log";
  doc["device"] = config.deviceId;
  doc["amount"] = amount;
  doc["success"] = success;
  doc["datetime"] = getDateTimeString();
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  http.POST(jsonString);
  http.end();
}

void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED || strlen(config.reciveServer) == 0) return;
  
  WiFiClient client;
  HTTPClient http;
  
  http.begin(client, config.reciveServer);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<256> doc;
  doc["type"] = "heartbeat";
  doc["device"] = config.deviceId;
  doc["datetime"] = getDateTimeString();
  doc["uptime"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["rssi"] = WiFi.RSSI();
  
  String jsonString;
  serializeJson(doc, jsonString);
  http.POST(jsonString);
  http.end();
}

void fetchCommandsFromServer() {
  if (WiFi.status() != WL_CONNECTED || strlen(config.commandServer) == 0) return;
  
  WiFiClient client;
  HTTPClient http;
  
  http.begin(client, config.commandServer);
  int code = http.GET();
  
  if (code == 200) {
    String payload = http.getString();
    handleServerCommand(payload);
  }
  
  http.end();
}

void handleServerCommand(String json) {
  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, json)) return;
  
  String cmd = doc["command"];
  
  if (cmd == "feed") {
    float amount = doc["amount"];
    arduinoSerial.print("FEED:");
    arduinoSerial.println(amount, 1);
  } else if (cmd == "get_logs") {
    sendLogsToServer();
  } else if (cmd == "get_schedule") {
    sendScheduleToServer();
  } else if (cmd == "update_schedule") {
    String scheduleData = doc["schedule"];
    updateScheduleFromServer(scheduleData);
  } else if (cmd == "set_time") {
    String datetime = doc["datetime"];
    arduinoSerial.println("SETTIME:" + datetime);
  } else if (cmd == "get_status") {
    sendStatusToServer();
  }
}

void sendLogsToServer() {
  if (WiFi.status() != WL_CONNECTED || strlen(config.reciveServer) == 0) return;
  
  WiFiClient client;
  HTTPClient http;
  
  http.begin(client, config.reciveServer);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<1024> doc;
  doc["type"] = "logs";
  doc["device"] = config.deviceId;
  doc["date"] = getDateString();
  doc["data"] = getTodayLogs();
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  http.POST(jsonString);
  http.end();
}

void sendScheduleToServer() {
  if (WiFi.status() != WL_CONNECTED || strlen(config.reciveServer) == 0) return;
  
  WiFiClient client;
  HTTPClient http;
  
  http.begin(client, config.reciveServer);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<512> doc;
  doc["type"] = "schedule";
  doc["device"] = config.deviceId;
  
  JsonArray arr = doc.createNestedArray("schedules");
  for (byte i = 0; i < scheduleCount; i++) {
    JsonObject obj = arr.createNestedObject();
    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", schedules[i].hour, schedules[i].minute);
    obj["time"] = timeStr;
    obj["amount"] = schedules[i].amount;
    obj["enabled"] = schedules[i].enabled;
  }
  
  String jsonString;
  serializeJson(doc, jsonString);
  http.POST(jsonString);
  http.end();
}

void sendStatusToServer() {
  if (WiFi.status() != WL_CONNECTED || strlen(config.reciveServer) == 0) return;
  
  WiFiClient client;
  HTTPClient http;
  
  http.begin(client, config.reciveServer);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<512> doc;
  doc["type"] = "status";
  doc["device"] = config.deviceId;
  doc["datetime"] = getDateTimeString();
  doc["uptime"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["mode"] = (currentMode == DEV_MODE) ? "DEV" : "OPERATION";
  doc["feeding"] = isFeeding;
  doc["time_valid"] = timeValid;
  
  String jsonString;
  serializeJson(doc, jsonString);
  http.POST(jsonString);
  http.end();
}

void updateScheduleFromServer(String data) {
  scheduleCount = 0;
  int start = 0;
  int end = data.indexOf('|');
  
  while (scheduleCount < 5 && (end >= 0 || start < data.length())) {
    String item = (end >= 0) ? data.substring(start, end) : data.substring(start);
    item.trim();
    
    int c1 = item.indexOf(',');
    int c2 = item.lastIndexOf(',');
    
    if (c1 > 0 && c2 > c1) {
      String timeStr = item.substring(0, c1);
      float amount = item.substring(c1 + 1, c2).toFloat();
      bool enabled = item.substring(c2 + 1).toInt() == 1;
      
      int colon = timeStr.indexOf(':');
      if (colon > 0) {
        schedules[scheduleCount].hour = timeStr.substring(0, colon).toInt();
        schedules[scheduleCount].minute = timeStr.substring(colon + 1).toInt();
        schedules[scheduleCount].amount = amount;
        schedules[scheduleCount].enabled = enabled;
        scheduleCount++;
      }
    }
    
    if (end < 0) break;
    start = end + 1;
    end = data.indexOf('|', start);
  }
  
  saveSchedule();
}

// ==================== Setup ====================
void setup() {
  Serial.begin(9600);
  arduinoSerial.begin(9600);

  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  initLCD();
  initSPIFFS();
  loadConfig();
  loadSchedule();
  loadMaxTime();

  lcd.setCursor(0, 0);
  lcd.print("Starting...");
  delay(2000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Waiting Arduino");
  
  unsigned long bootStart = millis();
  unsigned long lastCheck = millis();
  
  while (!arduinoReady && millis() - bootStart < 30000) {
    handleArduinoData();
    
    // Request Arduino status every 3 seconds
    if (millis() - lastCheck >= 3000) {
      arduinoSerial.println("CHECK");
      lastCheck = millis();
    }
    
    delay(100);
  }

  if (!arduinoReady) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Arduino ERROR!");
    lcd.setCursor(0, 1);
    lcd.print("Check connection");
    while (1) delay(1000);
  }

  // Request time from Arduino
  arduinoSerial.println("GET_TIME");
  delay(500);

  if (config.configured && strlen(config.ssid) > 0) {
    currentMode = OPERATION_MODE;
    startOperationMode();
  } else {
    currentMode = DEV_MODE;
    startDevMode();
  }

  updateLCD();
}

void startDevMode() {
  Serial.println("DEV MODE");
  
  WiFi.mode(WIFI_AP);
  String mac = WiFi.softAPmacAddress();
  mac.replace(":", "");
  String ssid = "Feeding_" + mac.substring(6);
  WiFi.softAP(ssid.c_str(), "admin@123");
  
  Serial.println("AP: " + ssid);
  Serial.println("IP: 192.168.4.1");
  
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/gettime", handleGetTime);
  server.on("/settime", handleSetTime);
  server.on("/getschedule", handleGetSchedule);
  server.on("/saveschedule", HTTP_POST, handleSaveSchedule);
  server.on("/feed", handleFeed);
  server.on("/getmaxtime", handleGetMaxTime);
  server.on("/setmaxtime", handleSetMaxTime);
  server.on("/getlogs", handleGetLogs);
  server.on("/deletelogs", handleDeleteLogs);
  server.on("/reset", handleReset);
  server.on("/getarduinolog", HTTP_GET, []() {
    String logText = "";
    int startIndex = (arduinoLogCount >= ARDUINO_LOG_SIZE) ? arduinoLogIndex : 0;
    
    for (int i = 0; i < arduinoLogCount; i++) {
      int idx = (startIndex + i) % ARDUINO_LOG_SIZE;
      logText += arduinoLog[idx] + "\n";
    }
    
    server.send(200, "text/plain", logText);
  });
  
  server.on("/cleararduinolog", HTTP_GET, []() {
    arduinoLogIndex = 0;
    arduinoLogCount = 0;
    server.send(200, "text/plain", "Arduino log cleared");
  });

  server.on("/sendcommand", HTTP_GET, []() {
    if (server.hasArg("cmd")) {
      String cmd = server.arg("cmd");
      arduinoSerial.println(cmd);
      server.send(200, "text/plain", "Command sent to Arduino");
    } else {
      server.send(400, "text/plain", "Missing command");
    }
  });

  server.on("/resetencoder", HTTP_GET, []() {
    arduinoSerial.println("RESET_ENCODER");
    server.send(200, "text/plain", "Encoder reset command sent");
  });

  server.on("/getencoder", HTTP_GET, []() {
    arduinoSerial.println("GET_ENCODER");
    server.send(200, "text/plain", "Encoder value requested");
  });

  server.on("/setencodertarget", HTTP_GET, []() {
    if (server.hasArg("target")) {
      String target = server.arg("target");
      arduinoSerial.println("SET_ENCODER_TARGET:" + target);
      server.send(200, "text/plain", "Encoder target set to: " + target);
    } else {
      server.send(400, "text/plain", "Missing target value");
    }
  });
  server.begin();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DEV MODE");
  lcd.setCursor(0, 1);
  lcd.print("192.168.4.1");
}

void startOperationMode() {
  Serial.println("OPERATION MODE");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid, config.password);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    lcd.setCursor(attempts % 16, 1);
    lcd.print(".");
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi failed! -> DEV MODE");
    currentMode = DEV_MODE;
    startDevMode();
    return;
  }
  
  Serial.println("WiFi OK: " + WiFi.localIP().toString());
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  delay(2000);
  
  lastHeartbeat = millis();
}

// ==================== Loop ====================
void loop() {
  if (currentMode == DEV_MODE) {
    server.handleClient();
    
    if (millis() - lastScheduleCheck > 2000) {
      checkAutoFeed();
      lastScheduleCheck = millis();
    }
  }
  
  handleArduinoData();
  handleResetButton();
  updateLCD();
  
  if (currentMode == OPERATION_MODE) {

    checkAndReconnectWiFi();

    // Check schedule every 10 seconds
    if (millis() - lastScheduleCheck > 2000) {
      checkAutoFeed();
      lastScheduleCheck = millis();
    }
    
    // Heartbeat every minute
    if (millis() - lastHeartbeat > 60000) {
      sendHeartbeat();
      lastHeartbeat = millis();
    }
    
    // Fetch commands every 5 seconds
    if (millis() - lastCommandFetch > 2000) {
      fetchCommandsFromServer();
      lastCommandFetch = millis();
    }
  }
  
  delay(50);
}


void checkAndReconnectWiFi() {
  if (millis() - lastWiFiCheck < 10000) return; // Kiểm tra mỗi 10 giây
  lastWiFiCheck = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected! Attempting to reconnect...");
    
    // Hiển thị trạng thái mất kết nối trên LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Disconnected");
    lcd.setCursor(0, 1);
    lcd.print("Reconnecting...");

    // Thử kết nối lại
    WiFi.disconnect();
    WiFi.begin(config.ssid, config.password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) { // Thử 10 lần, mỗi lần 500ms
      delay(500);
      Serial.print(".");
      lcd.setCursor(attempts % 16, 1);
      lcd.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi reconnected: " + WiFi.localIP().toString());
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("WiFi Connected");
      lcd.setCursor(0, 1);
      lcd.print(WiFi.localIP());
      delay(2000);
      lastLCDContent = ""; // Reset nội dung LCD để cập nhật lại
    } else {
      Serial.println("\nWiFi reconnect failed!");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("WiFi Failed!");
      lcd.setCursor(0, 1);
      lcd.print("Check Network");
      delay(2000);
      lastLCDContent = "";
    }
  }
}