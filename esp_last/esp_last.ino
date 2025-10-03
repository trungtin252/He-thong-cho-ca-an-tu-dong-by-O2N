#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>

// ==================== Config ====================
SoftwareSerial arduinoSerial(D1, D2); // RX=D1(GPIO5), TX=D2(GPIO4)

const char* ssid = "Trung Tin 2";
const char* password = "0706942875@@";

// Server endpoints
const char* logServer = "http://webhook.site/7a334544-4be3-48bf-a84a-79e26b07ac90";
// const char* commandServer = "http://webhook.site/2bbcfe9c-14bf-40a6-ad16-8bd2fa89ffff"; // temporarily disabled

// Timing
unsigned long lastHeartbeat = 0;
unsigned long lastCommandCheck = 0;

// Buffers
String lineBuffer = "";
String scheduleBuffer = "";
String logBuffer = "";
String currentLogDate = "";
bool isReceivingSchedules = false;
bool isReceivingLogs = false;

// ==================== Setup ====================
void setup() {
  Serial.begin(9600);
  arduinoSerial.begin(9600);

  // Connect WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  Serial.println("ESP8266 ready");
}

// ==================== Loop ====================
void loop() {
  handleArduinoData();
  handleSerialCommand();

  // Heartbeat every 5 minutes
  if (millis() - lastHeartbeat > 300000) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }

  // Check server commands every 10 seconds (temporarily disabled)
  /*
  if (millis() - lastCommandCheck > 10000) {
    checkServerCommand();
    lastCommandCheck = millis();
  }
  */

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

  if (data.startsWith("SCHEDULES:")) {
    isReceivingSchedules = true;
    scheduleBuffer = "";
    Serial.println("Receiving schedules");
  }
  else if (data == "END_SCHEDULES") {
    if (isReceivingSchedules) {
      sendScheduleToServer(scheduleBuffer);
      isReceivingSchedules = false;
    }
  }
  else if (data.startsWith("LOGS:")) {
    isReceivingLogs = true;
    currentLogDate = data.substring(5);
    logBuffer = "";
    Serial.println("Receiving logs for: " + currentLogDate);
  }
  else if (data == "END" || data == "END_LOGS") {
    if (isReceivingLogs) {
      sendLogsToServer(currentLogDate, logBuffer);
      isReceivingLogs = false;
    }
  }
  else {
    if (isReceivingSchedules) {
      scheduleBuffer += data + "|";
    }
    else if (isReceivingLogs) {
      logBuffer += data + "\n";
    }
  }
}

// ==================== Serial Commands ====================
void handleSerialCommand() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;

    Serial.println("Serial command received: " + input);
    handleCommandJson(input);
  }
}

// ==================== Server Commands ====================
void checkServerCommand() {
  // temporarily disabled
  /*
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  http.begin(client, commandServer);
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("Server command: " + payload);
    handleCommandJson(payload);
  } else {
    Serial.println("Error fetching server command: " + String(httpCode));
  }
  http.end();
  */
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
    Serial.println("Requested logs from Arduino");
  }
  else if (command == "get_schedule") {
    arduinoSerial.println("GET_SCHEDULE");
    Serial.println("Requested schedule from Arduino");
  }
  else if (command == "feed") {
    float amount = doc["amount"];
    int amountInt = (int)(amount * 10); // Convert to 0.1kg units
    arduinoSerial.println("FEED:" + String(amountInt));
    Serial.println("Feed command sent: " + String(amount) + "kg");
  }
  else if (command == "update_schedule") {
    String scheduleData = doc["schedule"];
    arduinoSerial.println("SCHEDULE:" + scheduleData);
    Serial.println("Schedule update sent");
  }
  else if (command == "status") {
    sendSystemStatus();
  } 
  else if (command == "delete_log") {
    arduinoSerial.println("DEL_LOG");
    Serial.println("Delete log request sent");
  }
  // === Lệnh mới thêm ===
  else if (command == "get_time") {
    
    arduinoSerial.println("GET_TIME");
    Serial.println("Requested RTC time from Arduino");
  }
  else if (command == "set_time") {
    String datetime = doc["datetime"];  
    // datetime format: "2025-10-02 15:45:00"
    arduinoSerial.println("SETTIME:" + datetime);
    Serial.println("Set RTC time request sent: " + datetime);
  }
  // =====================
  else {
    Serial.println("Unknown command");
  }
}


// ==================== Send Data to Server ====================
void sendScheduleToServer(String scheduleBuffer) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  http.begin(client, logServer);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> doc;
  doc["type"] = "schedule";
  doc["device"] = "feeding_system";
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
  } else {
    Serial.printf("Error sending schedule: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}

void sendLogsToServer(String currentLogDate, String logBuffer) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  http.begin(client, logServer);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<1024> doc;
  doc["type"] = "logs";
  doc["date"] = currentLogDate;
  doc["data"] = logBuffer;
  doc["device"] = "feeding_system";
  doc["timestamp"] = millis();

  String jsonString;
  serializeJson(doc, jsonString);
  int httpResponseCode = http.POST(jsonString);
  if (httpResponseCode > 0) {
    Serial.printf("Logs sent! Response: %d\n", httpResponseCode);
    String response = http.getString();
    Serial.println("Server response: " + response);
  } else {
    Serial.printf("Error sending logs: %s\n", http.errorToString(httpResponseCode).c_str());
  }

  http.end();
}

// ==================== Status & Heartbeat ====================
void sendSystemStatus() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  http.begin(client, logServer);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["type"] = "status";
  doc["device"] = "feeding_system";
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["uptime"] = millis();
  doc["ip"] = WiFi.localIP().toString();

  String jsonString;
  serializeJson(doc, jsonString);
  http.POST(jsonString);
  http.end();

  Serial.println("Status sent");
}

void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  http.begin(client, logServer);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<128> doc;
  doc["type"] = "heartbeat";
  doc["device"] = "feeding_system";
  doc["timestamp"] = millis();

  String jsonString;
  serializeJson(doc, jsonString);

  int httpResponseCode = http.POST(jsonString);
  if (httpResponseCode > 0) {
    Serial.println("Heartbeat sent");
  }

  http.end();
}
