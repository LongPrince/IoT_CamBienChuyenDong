#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- CẤU HÌNH ---
const char* ssid        = "Wokwi-GUEST";
const char* password    = "";
const char* mqtt_server = "*";
const char* mqtt_user   = "*";
const char* mqtt_pass   = "*";

const char* mqtt_topic_data    = "iot/door/data"; 
const char* mqtt_topic_control = "iot/door/control"; 

#define PIR_PIN     13    
#define LED_PIN     2     
#define BUZZER_PIN  4

WiFiClientSecure espClient;
PubSubClient client(espClient);

// --- CÁC BIẾN LOGIC & THỐNG KÊ ---
bool isDoorOpen = false;
unsigned long openStartTime = 0;
bool warningSent = false;
const unsigned long MAX_OPEN_TIME = 10000; // 10 giây để test nhanh

unsigned long totalDailyOpenTime = 0;
int doorCycleCount = 0;               
unsigned long lastResetMillis = 0;    

// BIẾN ĐIỀU KHIỂN THỦ CÔNG
bool isManualMode = false; 

// --- HÀM GỬI JSON LÊN NODE-RED ---
void sendStatusToNodeRed(String eventType, String alertMsg) {
  JsonDocument doc;
  
  doc["event"] = eventType;
  doc["total_time_day"] = totalDailyOpenTime;
  doc["cycle_count"] = doorCycleCount;
  doc["alert"] = alertMsg;

  // Logic phân loại bảo trì
  if (isManualMode) {
    doc["maintenance"] = "UNDER_MAINTENANCE"; // Đang trong quá trình can thiệp
  } else {
    doc["maintenance"] = (doorCycleCount >= 5) ? "REQUIRED" : "OK";
  }

  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(mqtt_topic_data, buffer);
  
  Serial.print("--- Data sent to MQTT: ");
  Serial.println(buffer);
}

// --- HÀM NHẬN LỆNH TỪ NODE-RED ---
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("\n[MQTT] Lệnh nhận được: ");
  Serial.println(message);

  if (String(topic) == mqtt_topic_control) {
    if (message == "MANUAL_ON") {
      // Nếu đang mở cửa ở chế độ AUTO mà bị ngắt bằng MANUAL, hãy chốt số liệu cũ
      if (isDoorOpen && !isManualMode) {
        unsigned long sessionTime = (millis() - openStartTime) / 1000;
        totalDailyOpenTime += sessionTime;
        doorCycleCount++;
      }

      isManualMode = true;
      isDoorOpen = false; 
      warningSent = false;
      
      digitalWrite(LED_PIN, HIGH); // Ép bật đèn (Cửa mở để bảo trì)
      noTone(BUZZER_PIN);          // Tắt còi báo động cũ
      
      Serial.println(">>> CHẾ ĐỘ THỦ CÔNG: Đang bảo trì...");
      sendStatusToNodeRed("manual_start", "Maintenance in progress");
    } 
    else if (message == "AUTO") {
      isManualMode = false;
      doorCycleCount = 0; // FIX: Reset bộ đếm vì coi như đã bảo trì xong
      
      digitalWrite(LED_PIN, LOW); 
      Serial.println(">>> CHẾ ĐỘ TỰ ĐỘNG: Đã reset bộ đếm.");
      sendStatusToNodeRed("auto_restore", "System cleaned and reset");
    }
  }
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
      client.subscribe(mqtt_topic_control); 
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5s");
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
  client.setCallback(callback);
  
  lastResetMillis = millis();
  Serial.println("--- HỆ THỐNG SẴN SÀNG ---");
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  // Tự động reset thông số sau 24h
  if (millis() - lastResetMillis > 86400000) {
    totalDailyOpenTime = 0;
    doorCycleCount = 0;
    lastResetMillis = millis();
    sendStatusToNodeRed("daily_reset", "New day data cleared");
  }

  // --- NẾU ĐANG MANUAL: KHÔNG CHẠY LOGIC CẢM BIẾN ---
  if (isManualMode) {
    delay(200);
    return; 
  }

  // --- LOGIC TỰ ĐỘNG (PIR) ---
  bool currentSensorState = digitalRead(PIR_PIN);

  // 1. Cửa bắt đầu mở
  if (currentSensorState == HIGH && !isDoorOpen) {
    isDoorOpen = true;
    openStartTime = millis();
    warningSent = false;
    digitalWrite(LED_PIN, HIGH);
    tone(BUZZER_PIN, 1500, 150); 
    Serial.println("Cửa mở (PIR detected)");
  } 
  
  // 2. Kiểm tra thời gian mở quá lâu
  else if (currentSensorState == HIGH && isDoorOpen) {
    unsigned long currentOpenDuration = millis() - openStartTime;
    if (currentOpenDuration > MAX_OPEN_TIME && !warningSent) {
      tone(BUZZER_PIN, 2000);
      warningSent = true;
      sendStatusToNodeRed("warning", "Door left open too long");
    }
  } 
  
  // 3. Cửa đóng lại
  else if (currentSensorState == LOW && isDoorOpen) {
    isDoorOpen = false;
    digitalWrite(LED_PIN, LOW); 
    noTone(BUZZER_PIN);         
    tone(BUZZER_PIN, 800, 150);

    unsigned long sessionTime = (millis() - openStartTime) / 1000;
    totalDailyOpenTime += sessionTime; 
    doorCycleCount++;                  

    sendStatusToNodeRed("door_closed", warningSent ? "closed_after_timeout" : "normal");
    Serial.print("Cửa đóng. Lần mở thứ: ");
    Serial.println(doorCycleCount);
  }

  delay(50);
}