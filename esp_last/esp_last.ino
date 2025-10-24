#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ==================== Config ====================
SoftwareSerial arduinoSerial(D1, D2);  // RX=D1(GPIO5), TX=D2(GPIO4)
LiquidCrystal_I2C lcd(0x27, 16, 2);    // Địa chỉ I2C 0x27, màn hình 16x2

// Reset button
const int RESET_BUTTON_PIN = D5;
unsigned long resetButtonPressTime = 0;
bool resetButtonPressed = false;

// Chế độ hoạt động
enum Mode { DEV_MODE,
            OPERATION_MODE };
Mode currentMode = DEV_MODE;

// Cấu hình lưu trong EEPROM
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
unsigned long startTime;
unsigned long lastCommandFetch = 0;

String currentScheduleData = "";
float currentFeedingWeight = 0.0;

// Buffers
String lineBuffer = "";
String scheduleBuffer = "";
String logBuffer = "";
String currentLogDate = "";
bool isReceivingSchedules = false;
bool isReceivingLogs = false;

// Feeding status
bool isFeeding = false;
float currentFeedAmount = 0.0;
String currentTime = "";
String feedingMessage = "";
unsigned long feedDoneTime = 0;
bool feedingJustDone = false;
String lastLCDContent = "";

bool arduinoReady = false;
bool isReceivingData = false;

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

// ==================== LCD Functions ====================
void initLCD() {
  Wire.begin(D4, D3);  // SDA=D4, SCL=D3
  lcd.init();
  lcd.backlight();
  lcd.clear();
}

void updateLCD() {
  if (millis() - lastLCDUpdate < 500) return;
  lastLCDUpdate = millis();

  String newContent = "";

  // Kiểm tra nếu vừa cho ăn xong, đợi 3s
  if (feedingJustDone) {
    if (millis() - feedDoneTime > 3000) {
      feedingJustDone = false;
      feedingMessage = "";
      currentFeedingWeight = 0.0;
    }
  }

  // Hiển thị feeding (ưu tiên cao nhất)
  if (isFeeding || feedingJustDone) {
    // Kiểm tra nếu có lỗi
    if (feedingMessage.startsWith("ERROR")) {
      newContent = "ERROR:" + feedingMessage;
    } else if (feedingMessage == "DONE") {
      newContent = "DONE:" + String(currentFeedingWeight, 2) + "kg";
    } else {
      newContent = "FEEDING:" + String(currentFeedingWeight, 2) + "kg";
    }
  } else {
    // Màn hình bình thường (khi không feeding)
    String line1 = "";
    String line2 = "";

    if (currentMode == DEV_MODE) {
      line1 = "MODE: DEV";
      line2 = "192.168.4.1";
    } else {
      line1 = "ID:" + String(config.deviceId);
      if (currentTime.length() > 0) {
        line2 = currentTime.substring(0, 16);
      } else {
        line2 = "Loading time...";
      }
    }

    newContent = line1 + "|" + line2;
  }

  // Chỉ cập nhật LCD khi nội dung thay đổi
  if (newContent != lastLCDContent) {
    lastLCDContent = newContent;
    lcd.clear();

    if (isFeeding || feedingJustDone) {
      // Hiển thị trạng thái feeding
      if (feedingMessage.startsWith("ERROR")) {
        lcd.setCursor(0, 0);
        lcd.print("ERROR!");
        lcd.setCursor(0, 1);
        String errorType = feedingMessage.substring(6);
        if (errorType.length() > 16) {
          lcd.print(errorType.substring(0, 16));
        } else {
          lcd.print(errorType);
        }
      } else if (feedingMessage == "DONE") {
        lcd.setCursor(0, 0);
        lcd.print("DONE!");
        lcd.setCursor(0, 1);
        lcd.print(String(currentFeedingWeight, 2));
        lcd.print(" kg");
      } else {
        lcd.setCursor(0, 0);
        lcd.print("FEEDING...");
        lcd.setCursor(0, 1);
        lcd.print(String(currentFeedingWeight, 2));
        lcd.print(" kg");
      }
    } else {
      // Hiển thị màn hình bình thường
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

// ==================== Web Server Pages ====================
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
    input, button, textarea { width: 100%; padding: 10px; margin: 5px 0; box-sizing: border-box; }
    button { background: #4CAF50; color: white; border: none; cursor: pointer; font-size: 16px; border-radius: 5px; }
    button:hover { background: #45a049; }
    .section { margin: 20px 0; padding: 15px; background: #f9f9f9; border-radius: 5px; }
    .command-btn { background: #2196F3; margin: 5px 0; }
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
    .status-badge { padding: 2px 8px; border-radius: 3px; font-size: 12px; margin-left: 10px; }
  </style>
</head>
<body>
  <div class='container'>
    <h1>Feeding System Config</h1>
    
    <div class='section'>
      <h2>RTC Time Settings</h2>
      <div class='time-input'>
        <input type='date' id='dateInput'>
        <input type='time' id='timeInput' step='1'>
      </div>
      <button class='warning-btn' onclick='getCurrentTime()'>Get Current Time</button>
      <button onclick='setDateTime()'>Set RTC Time</button>
    </div>
    
    <div class='section'>
      <h2>Feeding Schedule</h2>
      <div id='scheduleList'></div>
      <hr>
      <h3>Add New Schedule</h3>
      <div class='time-input'>
        <input type='time' id='newScheduleTime' placeholder='HH:MM'>
        <input type='number' id='newScheduleAmount' placeholder='Amount (kg)' step='0.1' min='0.1' max='10'>
      </div>
      <button onclick='addSchedule()'>Add Schedule</button>
      <button class='command-btn' onclick='getSchedule()'>Load from Arduino</button>
      <button class='warning-btn' onclick='sendScheduleToArduino()'>Save to Arduino</button>
    </div>
    
    <div class='section'>
      <h2>WiFi Configuration</h2>
      <input type='text' id='ssid' placeholder='WiFi SSID'>
      <input type='password' id='password' placeholder='WiFi Password'>
    </div>
    
    <div class='section'>
      <h2>Server Configuration</h2>
      <input type='text' id='reciveServer' placeholder='Log Server URL' value=''>
      <input type='text' id='commandServer' placeholder='Command Server URL' value=''>
      <input type='text' id='deviceId' placeholder='Device ID' value=''>
    </div>
    
    <button onclick='saveConfig()'>Save Config & Start</button>

    <div class='section'>
      <h2>Manual Feeding</h2>
      <div class='time-input'>
        <input type='number' id='feedAmount' placeholder='Amount (kg)' step='0.1' min='0.1' max='10' value='1.0'>
        <button class='warning-btn' onclick='manualFeed()'>Feed Now</button>
      </div>
    </div>

    <div class='section'>
      <h2>Max Feed Time Settings</h2>
      <p style='font-size: 12px; color: #666;'>Thời gian tối đa cho phép mỗi kg cám (phút/kg)</p>
      <div class='time-input'>
        <input type='number' id='maxTimePerKg' placeholder='Thời gian (phút/kg)' step='0.1' min='0.5' max='10' value='1.8'>
        <button class='warning-btn' onclick='getMaxTime()'>Get Current</button>
      </div>
      <button onclick='setMaxTime()'>Set Max Time</button>
      <div style='background: #fffacd; padding: 10px; margin-top: 10px; border-radius: 5px; font-size: 12px;'>
        <strong>Info:</strong><br>
        - If exceed this time → timeout and auto stop<br>
        - Default: 1.8 min/kg<br>
        - Recommended: 1.5 - 2.5 min/kg
      </div>
    </div>
    
    <div class='section'>
      <h2>Arduino Commands</h2>
      <button class='command-btn' onclick='sendCmd("GET_SCHEDULE")'>Get Schedule</button>
      <button class='command-btn' onclick='sendCmd("GET_LOG")'>Get Logs</button>
      <button class='command-btn' onclick='sendCmd("GET_TIME")'>Get Time</button>
      <button class='command-btn' onclick='sendCmd("FEED:10")'>Feed 1.0kg</button>
      <button class='danger-btn' onclick='sendCmd("DEL_LOG")'>Delete Logs</button>
      <div id='response'></div>
    </div>
  </div>
  
  <script>
    let schedules = [];
    
    function saveConfig() {
      var data = {
        ssid: document.getElementById('ssid').value,
        password: document.getElementById('password').value,
        reciveServer: document.getElementById('reciveServer').value,
        commandServer: document.getElementById('commandServer').value,
        deviceId: document.getElementById('deviceId').value
      };
      
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
    
    function sendCmd(cmd) {
      fetch('/command?cmd=' + encodeURIComponent(cmd))
        .then(r => r.text())
        .then(t => document.getElementById('response').innerHTML += t + '\n');
    }
    
    function getCurrentTime() {
      sendCmd('GET_TIME');
    }
    
    function setDateTime() {
      const date = document.getElementById('dateInput').value;
      const time = document.getElementById('timeInput').value;
      if (!date || !time) {
        alert('Please enter both date and time');
        return;
      }
      const datetime = date + ' ' + time;
      fetch('/settime?datetime=' + encodeURIComponent(datetime))
        .then(r => r.text())
        .then(t => {
          alert(t);
          document.getElementById('response').innerHTML += 'Set time: ' + datetime + '\n';
        });
    }
    
    function getSchedule() {
      fetch('/getschedule')
        .then(r => r.json())
        .then(data => {
          schedules = data.schedules || [];
          renderSchedules();
          document.getElementById('response').innerHTML += 'Loaded ' + schedules.length + ' schedules\n';
        });
    }
    
    function addSchedule() {
      const time = document.getElementById('newScheduleTime').value;
      const amount = parseFloat(document.getElementById('newScheduleAmount').value);
      
      if (!time || !amount) {
        alert('Please enter time and amount');
        return;
      }
      
      schedules.push({ time: time, amount: amount, enabled: true });
      schedules.sort((a, b) => a.time.localeCompare(b.time));
      renderSchedules();
      
      document.getElementById('newScheduleTime').value = '';
      document.getElementById('newScheduleAmount').value = '';
    }
    
    function deleteSchedule(index) {
      schedules.splice(index, 1);
      renderSchedules();
    }
    
    function renderSchedules() {
      const list = document.getElementById('scheduleList');
      if (schedules.length === 0) {
        list.innerHTML = '<p style="color:#999">No schedules yet. Add one below or load from Arduino.</p>';
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
    
    function toggleSchedule(index) {
      schedules[index].enabled = !schedules[index].enabled;
      renderSchedules();
    }

    function setMaxTime() {
      const value = parseFloat(document.getElementById('maxTimePerKg').value);
      
      if (!value || isNaN(value)) {
        alert('Please enter a valid number');
        return;
      }
      
      if (value < 0.5 || value > 10) {
        alert('Value must be between 0.5 and 10 minutes/kg');
        return;
      }
      
      if (confirm('Set max feed time to ' + value + ' minutes/kg?')) {
        sendCmd('SET_MAXTIME:' + value);
        document.getElementById('response').innerHTML += 'Setting max time to: ' + value + ' min/kg\n';
      }
    }
    
    function manualFeed() {
      const amount = parseFloat(document.getElementById('feedAmount').value);
      
      if (!amount || amount <= 0) {
        alert('Please enter a valid amount (greater than 0)');
        return;
      }
      
      if (amount > 10) {
        alert('Amount too large! Maximum is 10 kg');
        return;
      }
      
      const amountInt = Math.round(amount * 10);
      
      if (confirm('Feed ' + amount + ' kg now?')) {
        sendCmd('FEED:' + amountInt);
        document.getElementById('response').innerHTML += 'Feeding ' + amount + ' kg...\n';
      }
    }
    
    function sendScheduleToArduino() {
      if (schedules.length === 0) {
        alert('No schedules to send');
        return;
      }
      
      const scheduleStr = schedules.map(s => {
        const amount = s.amount;
        const enabled = s.enabled !== false ? '1' : '0';
        return s.time + ',' + amount + ',' + enabled;
      }).join('|');
      
      fetch('/updateschedule', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({ schedule: scheduleStr })
      }).then(r => r.text()).then(t => {
        alert(t);
        document.getElementById('response').innerHTML += 'Schedule sent to Arduino\n';
        
        setTimeout(() => {
          getSchedule();
          document.getElementById('response').innerHTML += 'Schedule updated\n';
        }, 1000);
      });
    }
    
    // Auto-refresh response
    setInterval(() => {
      fetch('/response').then(r => r.text()).then(t => {
        if (t) document.getElementById('response').innerHTML = t;
      });
    }, 1000);
    
    // Set current date/time on load
    const now = new Date();
    document.getElementById('dateInput').value = now.toISOString().split('T')[0];
    document.getElementById('timeInput').value = now.toTimeString().split(' ')[0];
    
    // Initial render
    renderSchedules();
  </script>
</body>
</html>
)";

String arduinoResponse = "";

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

void handleCommand() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    arduinoSerial.println(cmd);
    Serial.println("Dev command sent: " + cmd);
    server.send(200, "text/plain", "Sent: " + cmd);
  } else {
    server.send(400, "text/plain", "Missing cmd parameter");
  }
}

void handleResponse() {
  server.send(200, "text/plain", arduinoResponse);
}

void handleSetTime() {
  if (server.hasArg("datetime")) {
    String datetime = server.arg("datetime");
    arduinoSerial.println("SETTIME:" + datetime);
    Serial.println("Set time request: " + datetime);

    delay(500);
    arduinoSerial.println("GET_TIME");

    server.send(200, "text/plain", "Time set command sent");
  } else {
    server.send(400, "text/plain", "Missing datetime parameter");
  }
}

void handleGetSchedule() {
  arduinoSerial.println("GET_SCHEDULE");
  delay(3000);

  StaticJsonDocument<1024> doc;
  JsonArray schedules = doc.createNestedArray("schedules");

  if (currentScheduleData.length() > 0) {
    int start = 0;
    int end = currentScheduleData.indexOf('|');

    while (end >= 0 || start < currentScheduleData.length()) {
      String item = (end >= 0) ? currentScheduleData.substring(start, end) : currentScheduleData.substring(start);
      item.trim();

      int comma1 = item.indexOf(',');
      int comma2 = item.indexOf(',', comma1 + 1);

      if (comma1 > 0) {
        String time = item.substring(0, comma1);
        float amount = item.substring(comma1 + 1, comma2 > 0 ? comma2 : item.length()).toFloat();
        bool enabled = (comma2 > 0) ? (item.substring(comma2 + 1).toInt() == 1) : true;

        JsonObject schedule = schedules.createNestedObject();
        schedule["time"] = time;
        schedule["amount"] = amount;
        schedule["enabled"] = enabled;
      }

      if (end < 0) break;
      start = end + 1;
      end = currentScheduleData.indexOf('|', start);
    }
  }

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleUpdateSchedule() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, server.arg("plain"));

    String scheduleData = doc["schedule"];
    arduinoSerial.println("SCHEDULE:" + scheduleData);
    Serial.println("Schedule update sent: " + scheduleData);

    delay(1000);
    arduinoSerial.println("GET_SCHEDULE");

    server.send(200, "text/plain", "Schedule sent to Arduino");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleReset() {
  clearConfig();
  server.send(200, "text/plain", "Config cleared! Restarting...");
  delay(1000);
  ESP.restart();
}

// ==================== Reset Button Handler ====================
void handleResetButton() {
  int buttonState = digitalRead(RESET_BUTTON_PIN);

  if (buttonState == LOW) {
    if (!resetButtonPressed) {
      resetButtonPressed = true;
      resetButtonPressTime = millis();
      Serial.println("Reset button pressed...");
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
        Serial.println("Reset cancelled");
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

  Serial.println("Sending RESET to Arduino...");
  arduinoSerial.println("RESET");
  delay(500);

  Serial.println("Clearing EEPROM...");
  clearConfig();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Reset Complete!");
  lcd.setCursor(0, 1);
  lcd.print("Restarting...");

  delay(2000);

  ESP.restart();
}

// ==================== Setup ====================
void setup() {
  Serial.begin(9600);
  arduinoSerial.begin(9600);
  startTime = millis();

  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  initLCD();
  loadConfig();

  lcd.setCursor(0, 0);
  lcd.print("Starting...");
  delay(2000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Waiting Arduino");
  
  unsigned long bootStartTime = millis();
  unsigned long lastCheckTime = millis();
  while (!arduinoReady && millis() - bootStartTime < 50000) {
    handleArduinoData(); 
    
    if (millis() - lastCheckTime >= 30000) {
        arduinoSerial.println("CHECK");
        lastCheckTime = millis();
    }
    
    delay(100);
  }


  if (!arduinoReady) {
    Serial.println("ERROR: Arduino not responding!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Arduino ERROR!");
    lcd.setCursor(0, 1);
    lcd.print("Check connection");
    while (1) delay(1000);
  }


  if (config.configured && strlen(config.ssid) > 0) {
    currentMode = OPERATION_MODE;
    startOperationMode();

    delay(200);
  } else {
    currentMode = DEV_MODE;
    startDevMode();
  }

  updateLCD();
}

void startDevMode() {
  Serial.println("Starting DEV MODE");

  WiFi.mode(WIFI_AP);
  delay(100);

  String mac = WiFi.softAPmacAddress();
  mac.replace(":", "");

  String ssid1 = "O2N_Auto_Feeding_System_" + mac.substring(6);

  WiFi.softAP(ssid1.c_str(), "admin@123");

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/command", handleCommand);
  server.on("/response", handleResponse);
  server.on("/settime", handleSetTime);
  server.on("/getschedule", handleGetSchedule);
  server.on("/updateschedule", HTTP_POST, handleUpdateSchedule);
  server.on("/reset", handleReset);
  server.begin();

  Serial.println("Web server started at http://192.168.4.1");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DEV MODE");
  lcd.setCursor(0, 1);
  lcd.print("192.168.4.1");
}

void startOperationMode() {
  Serial.println("Starting OPERATION MODE");

  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid, config.password);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(attempts % 16, 1);
    lcd.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi failed! Switching to DEV MODE");
    currentMode = DEV_MODE;
    startDevMode();
    return;
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi OK");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  delay(2000);
  arduinoSerial.println("GET_TIME");
  delay(100);
  lastHeartbeat = millis();
}

// ==================== Loop ====================
void loop() {
  if (currentMode == DEV_MODE) {
    server.handleClient();
  }

  handleArduinoData();
  handleResetButton();
  updateLCD();

  if (currentMode == OPERATION_MODE) {
    // Heartbeat every 1 minute
    if (millis() - lastHeartbeat > 60000) {
      sendHeartbeat();
      arduinoSerial.println("GET_TIME");
      lastHeartbeat = millis();
    }

    if (!isReceivingData && millis() - lastCommandFetch > 5000) {
      lastCommandFetch = millis();
      fetchCommandsFromServer();
    }
  }

  delay(50);
}

// ==================== Arduino Data ====================
void handleArduinoData() {
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
  Serial.println("From Arduino: " + data);

  if (currentMode == DEV_MODE) {
    arduinoResponse += data + "\n";
    if (arduinoResponse.length() > 2000) {
      arduinoResponse = arduinoResponse.substring(arduinoResponse.length() - 2000);
    }
  }

  if (data.startsWith("ERROR_")) {
    handleErrorMessage(data);
    return;
  }

  if (data == "Arduino_OK" && !arduinoReady) {
    arduinoReady = true;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Arduino OK");
    delay(1000);
    return;
  }

  if (data.startsWith("FEEDING,")) {
    isFeeding = true;
    feedingJustDone = false;

    int comma1 = data.indexOf(',');
    int comma2 = data.indexOf(',', comma1 + 1);

    if (comma1 > 0 && comma2 > comma1) {
      String fedStr = data.substring(comma1 + 1, comma2);
      currentFeedingWeight = fedStr.toFloat();
    }
  } else if (data.startsWith("RELEASING,")) {
    int comma = data.indexOf(',');
    if (comma > 0) {
      String remainStr = data.substring(comma + 1);
      float remaining = remainStr.toFloat();
      feedingMessage = "RELEASE:" + String(remaining, 2) + "kg";
    }
  } else if (data.startsWith("FEED_DONE") || data.startsWith("DONE")) {
    isFeeding = false;
    feedingJustDone = true;
    feedDoneTime = millis();
    feedingMessage = "DONE";
  } else if (data.startsWith("FEED_ERROR")) {
    isFeeding = false;
    feedingJustDone = true;
    feedDoneTime = millis();

    int comma = data.indexOf(',');
    if (comma > 0) {
      String errorType = data.substring(comma + 1);
      feedingMessage = "ERROR:" + errorType;
    } else {
      feedingMessage = "ERROR:UNKNOWN";
    }
    currentFeedingWeight = 0.0;
  } else if (data.startsWith("TIME:")) {
    currentTime = data.substring(5);
  } else if (data.startsWith("SCHEDULES:")) {
    isReceivingSchedules = true;
    isReceivingData = true;
    scheduleBuffer = "";
    currentScheduleData = "";
  } else if (data == "END_SCHEDULES") {
    if (isReceivingSchedules) {
      currentScheduleData = scheduleBuffer;
      Serial.println("Schedule reception complete: " + currentScheduleData);

      if (currentMode == OPERATION_MODE) {
        sendScheduleToServer(scheduleBuffer);
      }
      isReceivingSchedules = false;
      isReceivingData = false;
    }
  } else if (data.startsWith("LOGS:")) {
    isReceivingLogs = true;
    isReceivingData = true;
    currentLogDate = data.substring(5);
    logBuffer = "";
  } else if (data == "END" || data == "END_LOGS") {
    if (isReceivingLogs && currentMode == OPERATION_MODE) {
      sendLogsToServer(currentLogDate, logBuffer);
      isReceivingLogs = false;
      isReceivingData = false;
    }
  } else {
    if (isReceivingSchedules) {
      scheduleBuffer += data + "|";
    } else if (isReceivingLogs) {
      logBuffer += data + "\n";
    }
  }
}

void fetchCommandsFromServer() {
  if (WiFi.status() != WL_CONNECTED || strlen(config.commandServer) == 0) {
    Serial.println("Cannot fetch commands: No WiFi or commandServer not configured");
    return;
  }

  WiFiClient client;
  HTTPClient http;

  Serial.println("Fetching commands from: " + String(config.commandServer));
  http.begin(client, config.commandServer);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) {
    String payload = http.getString();
    Serial.println("Received command: " + payload);
    handleCommandJson(payload);
  } else {
    Serial.printf("Failed to fetch commands! Response code: %d\n", httpResponseCode);
  }
  http.end();
}

// ==================== Handle JSON Command ====================
void handleCommandJson(String input) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, input);
  if (error) {
    Serial.println("Invalid JSON command");
    return;
  }

  String command = doc["command"];
  if (command == "get_logs") {
    arduinoSerial.println("GET_LOG");
  } else if (command == "get_schedule") {
    arduinoSerial.println("GET_SCHEDULE");
  } else if (command == "feed") {
    float amount = doc["amount"];
    int amountInt = (int)(amount * 10);
    arduinoSerial.println("FEED:" + String(amountInt));
  } else if (command == "update_schedule") {
    String scheduleData = doc["schedule"];
    arduinoSerial.println("SCHEDULE:" + scheduleData);

    delay(1000);
    arduinoSerial.println("GET_SCHEDULE");
  } else if (command == "status") {
    sendSystemStatus();
  } else if (command == "delete_log") {
    arduinoSerial.println("DEL_LOG");
  } else if (command == "get_time") {
    arduinoSerial.println("GET_TIME");
  } else if (command == "set_time") {
    String datetime = doc["datetime"];
    arduinoSerial.println("SETTIME:" + datetime);

    delay(500);
    arduinoSerial.println("GET_TIME");
  }
}

// ==================== Send Data to Server ====================
void sendScheduleToServer(String scheduleBuffer) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  http.begin(client, config.reciveServer);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> doc;
  doc["type"] = "schedule";
  doc["device"] = config.deviceId;
  doc["timestamp"] = millis();

  JsonArray arr = doc.createNestedArray("data");

  int start = 0;
  int end = scheduleBuffer.indexOf('|');
  while (end > 0) {
    arr.add(scheduleBuffer.substring(start, end));
    start = end + 1;
    end = scheduleBuffer.indexOf('|', start);
  }

  String jsonString;
  serializeJson(doc, jsonString);
  int httpResponseCode = http.POST(jsonString);
  if (httpResponseCode > 0) {
    Serial.printf("Schedule sent! Response: %d\n", httpResponseCode);
  }
  http.end();
}

void sendLogsToServer(String currentLogDate, String logBuffer) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  http.begin(client, config.reciveServer);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<1024> doc;
  doc["type"] = "logs";
  doc["date"] = currentLogDate;
  doc["data"] = logBuffer;
  doc["device"] = config.deviceId;
  doc["timestamp"] = millis();

  String jsonString;
  serializeJson(doc, jsonString);
  int httpResponseCode = http.POST(jsonString);
  if (httpResponseCode > 0) {
    Serial.printf("Logs sent! Response: %d\n", httpResponseCode);
  }
  http.end();
}

void sendSystemStatus() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  http.begin(client, config.reciveServer);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["type"] = "status";
  doc["device"] = config.deviceId;
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["uptime"] = millis();
  doc["ip"] = WiFi.localIP().toString();

  String jsonString;
  serializeJson(doc, jsonString);
  http.POST(jsonString);
  http.end();
}

void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  http.begin(client, config.reciveServer);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<128> doc;
  doc["type"] = "heartbeat";
  doc["device"] = config.deviceId;
  doc["timestamp"] = millis();

  String jsonString;
  serializeJson(doc, jsonString);
  http.POST(jsonString);
  http.end();
}

void handleErrorMessage(String errorMsg) {
  Serial.println("System Error detected: " + errorMsg);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SYSTEM ERROR!");
  lcd.setCursor(0, 1);
  lcd.print(errorMsg.substring(6, min(16, (int)errorMsg.length())));

  feedingMessage = "ERROR:" + errorMsg.substring(6);
  feedingJustDone = true;
  feedDoneTime = millis();

  if (currentMode == OPERATION_MODE && WiFi.status() == WL_CONNECTED) {
    sendErrorToServer(errorMsg);
  }
}

void sendErrorToServer(String errorType) {
  if (WiFi.status() != WL_CONNECTED || strlen(config.reciveServer) == 0) {
    Serial.println("Cannot send error: No WiFi or reciveServer not configured");
    return;
  }

  WiFiClient client;
  HTTPClient http;

  http.begin(client, config.reciveServer);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["type"] = "error";
  doc["device"] = config.deviceId;
  doc["error"] = errorType;
  doc["timestamp"] = millis();

  if (currentTime.length() > 0) {
    doc["datetime"] = currentTime;
  }

  String jsonString;
  serializeJson(doc, jsonString);

  Serial.println("Sending error to server: " + jsonString);

  int httpResponseCode = http.POST(jsonString);
  if (httpResponseCode > 0) {
    Serial.printf("Error sent! Response: %d\n", httpResponseCode);
  } else {
    Serial.printf("Failed to send error! Response: %d\n", httpResponseCode);
  }

  http.end();
}