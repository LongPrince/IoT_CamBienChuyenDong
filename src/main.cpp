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

const char* mqtt_topic_data    = "iot/door/data"; 
const char* mqtt_topic_control = "iot/door/control"; // Topic mới để nhận lệnh điều khiển

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

unsigned long totalDailyOpenTime = 0;
int doorCycleCount = 0;               
unsigned long lastResetMillis = 0;    

// BIẾN ĐIỀU KHIỂN THỦ CÔNG
bool isManualMode = false; 

// --- HÀM GỬI JSON LÊN NODE-RED ---
void sendStatusToNodeRed(String eventType, String alertMsg) {
  JsonDocument doc;
  
  if (eventType != "door_closed") {
    doc["event"] = eventType;
  }
  
  doc["total_time_day"] = totalDailyOpenTime;
  doc["cycle_count"] = doorCycleCount;
  doc["alert"] = alertMsg;
  doc["maintenance"] = (doorCycleCount >= 5) ? "REQUIRED" : "OK";

  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(mqtt_topic_data, buffer);
  Serial.print("Data sent: ");
  Serial.println(buffer);
}

// --- HÀM NHẬN LỆNH TỪ NODE-RED ---
void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("Nhận lệnh từ MQTT: ");
  Serial.println(message);

  if (String(topic) == mqtt_topic_control) {
    if (message == "MANUAL_ON") {
      isManualMode = true;
      isDoorOpen = false;         // Reset trạng thái auto
      warningSent = false;
      digitalWrite(LED_PIN, HIGH); // Ép bật đèn LED (Mở cửa)
      noTone(BUZZER_PIN);          // Tắt còi nếu đang kêu
      Serial.println("Chế độ: THỦ CÔNG (Mở cửa tự do, vô hiệu hóa PIR)");
      sendStatusToNodeRed("manual_mode", "Manual door open");
    } 
    else if (message == "AUTO") {
      isManualMode = false;
      digitalWrite(LED_PIN, LOW);  // Tắt đèn LED, chờ PIR
      Serial.println("Chế độ: TỰ ĐỘNG (Quay lại dùng cảm biến PIR)");
      sendStatusToNodeRed("auto_mode", "Auto mode restored");
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
      // ĐĂNG KÝ NHẬN LỆNH KHI KẾT NỐI THÀNH CÔNG
      client.subscribe(mqtt_topic_control); 
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
  client.setCallback(callback); // Cài đặt hàm callback nhận dữ liệu
  lastResetMillis = millis();
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  if (millis() - lastResetMillis > 86400000) {
    totalDailyOpenTime = 0;
    doorCycleCount = 0;
    lastResetMillis = millis();
    sendStatusToNodeRed("daily_reset", "System reset for new day");
  }

  // NẾU ĐANG Ở CHẾ ĐỘ THỦ CÔNG -> BỎ QUA TOÀN BỘ LOGIC CẢM BIẾN DƯỚI ĐÂY
  if (isManualMode) {
    delay(100);
    return; // Kết thúc vòng lặp loop tại đây, không đọc PIR nữa
  }

  // --- LOGIC TỰ ĐỘNG BẰNG PIR DƯỚI NÀY CHỈ CHẠY KHI TẮT NÚT MANUAL ---
  bool currentSensorState = digitalRead(PIR_PIN);

  if (currentSensorState == HIGH && !isDoorOpen) {
    isDoorOpen = true;
    openStartTime = millis();
    warningSent = false;
    digitalWrite(LED_PIN, HIGH);
    tone(BUZZER_PIN, 1500, 150); 
  } 
  
  else if (currentSensorState == HIGH && isDoorOpen) {
    unsigned long currentOpenDuration = millis() - openStartTime;
    if (currentOpenDuration > MAX_OPEN_TIME && !warningSent) {
      tone(BUZZER_PIN, 2000);
      warningSent = true;
      sendStatusToNodeRed("warning", "Door left open > 10m");
    }
  } 
  
  else if (currentSensorState == LOW && isDoorOpen) {
    isDoorOpen = false;
    digitalWrite(LED_PIN, LOW); 
    noTone(BUZZER_PIN);         
    tone(BUZZER_PIN, 800, 150);

    unsigned long sessionTime = (millis() - openStartTime) / 1000;
    totalDailyOpenTime += sessionTime; 
    doorCycleCount++;                  

    sendStatusToNodeRed("door_closed", warningSent ? "closed_after_timeout" : "normal");
  }

  delay(50);
}