#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- CẤU HÌNH ---
const char* ssid        = "Wokwi-GUEST";
const char* password    = "";
const char* mqtt_server = "0acb987148084628b41bbeed64717bab.s1.eu.hivemq.cloud";
const char* mqtt_user   = "danglong_esp8266";
const char* mqtt_pass   = "Long@123";

const char* mqtt_topic_data = "iot/door/data"; 

#define PIR_PIN     13    
#define LED_PIN     2     
#define BUZZER_PIN  4

WiFiClientSecure espClient;
PubSubClient client(espClient);

// --- CÁC BIẾN LOGIC & THỐNG KÊ ---
bool isDoorOpen = false;
unsigned long openStartTime = 0;
bool warningSent = false;
const unsigned long MAX_OPEN_TIME = 10000; 

// Biến tích lũy
unsigned long totalDailyOpenTime = 0; // Tổng thời gian mở trong ngày (giây)
int doorCycleCount = 0;               // Tổng số lượt đóng/mở
unsigned long lastResetMillis = 0;    // Dùng để reset sau 24h

// --- HÀM GỬI JSON LÊN NODE-RED ---
// --- HÀM GỬI JSON LÊN NODE-RED ---
void sendStatusToNodeRed(String eventType, String alertMsg) {
  JsonDocument doc;
  
  // SỬA ĐÚNG 1 CHỖ Ở ĐÂY: Chỉ đưa key "event" vào JSON nếu nó KHÔNG PHẢI là "door_closed"
  if (eventType != "door_closed") {
    doc["event"] = eventType;
  }
  
  doc["total_time_day"] = totalDailyOpenTime;
  doc["cycle_count"] = doorCycleCount;
  doc["alert"] = alertMsg;
  
  // Cảnh báo bảo trì nếu quá 100 lần (trong code đang để >= 5)
  if (doorCycleCount >= 5) {
    doc["maintenance"] = "REQUIRED";
  } else {
    doc["maintenance"] = "OK";
  }

  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(mqtt_topic_data, buffer);
  Serial.print("Data sent to Node-RED: ");
  Serial.println(buffer);
}

void setup_wifi() {
  Serial.print("\nConnecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  espClient.setInsecure(); 
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32-Door-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("Connected!");
    } else {
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  setup_wifi();
  client.setServer(mqtt_server, 8883);
  lastResetMillis = millis();
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  // Tự động reset thông số sau mỗi 24 giờ
  if (millis() - lastResetMillis > 86400000) {
    totalDailyOpenTime = 0;
    doorCycleCount = 0;
    lastResetMillis = millis();
    sendStatusToNodeRed("daily_reset", "System reset for new day");
  }

  bool currentSensorState = digitalRead(PIR_PIN);

  // 1. KHI CỬA MỞ: Chỉ xử lý local (LED, Buzzer), KHÔNG gửi MQTT
  if (currentSensorState == HIGH && !isDoorOpen) {
    isDoorOpen = true;
    openStartTime = millis();
    warningSent = false;
    digitalWrite(LED_PIN, HIGH);
    tone(BUZZER_PIN, 1500, 150); 
  } 
  
  // 2. TRONG KHI MỞ: Chỉ gửi MQTT nếu quá 10 phút (Cảnh báo khẩn cấp)
  else if (currentSensorState == HIGH && isDoorOpen) {
    unsigned long currentOpenDuration = millis() - openStartTime;
    if (currentOpenDuration > MAX_OPEN_TIME && !warningSent) {
      tone(BUZZER_PIN, 2000);
      warningSent = true;
      sendStatusToNodeRed("warning", "Door left open > 10m");
    }
  } 
  
  // 3. KHI CỬA ĐÓNG: Cập nhật dữ liệu và GỬI MQTT
  else if (currentSensorState == LOW && isDoorOpen) {
    isDoorOpen = false;
    digitalWrite(LED_PIN, LOW); 
    noTone(BUZZER_PIN);         
    tone(BUZZER_PIN, 800, 150);

    unsigned long sessionTime = (millis() - openStartTime) / 1000;
    totalDailyOpenTime += sessionTime; // Cộng dồn tổng thời gian trong ngày
    doorCycleCount++;                  // Tăng số lượt đóng mở

    // Gửi báo cáo tổng hợp về Node-RED
    sendStatusToNodeRed("door_closed", warningSent ? "closed_after_timeout" : "normal");
  
  }

  delay(50);
}