#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// --- CẤU HÌNH ---
const char* ssid        = "Wokwi-GUEST";
const char* password    = "";
const char* mqtt_server = "0acb987148084628b41bbeed64717bab.s1.eu.hivemq.cloud";
const char* mqtt_user   = "danglong_esp8266";
const char* mqtt_pass   = "Long@123";

// Các topic MQTT
const char* mqtt_topic_time    = "iot/door/open_time"; // Gửi thời gian mở cửa (giây)
const char* mqtt_topic_warning = "iot/door/warning";   // Gửi cảnh báo

#define PIR_PIN     13    
#define LED_PIN     2     
#define BUZZER_PIN  12 

WiFiClientSecure espClient;
PubSubClient client(espClient);

// --- CÁC BIẾN LOGIC ---
bool isDoorOpen = false;
unsigned long openStartTime = 0;
bool warningSent = false;

// Cấu hình thời gian cảnh báo: 10 phút = 10 * 60 * 1000 = 600000 mili-giây
const unsigned long MAX_OPEN_TIME = 600000; 

// --- HÀM BỔ TRỢ ---
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
    String clientId = "ESP32-" + String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("Connected!");
    } else {
      Serial.printf("Failed, rc=%d. Try again in 5s\n", client.state());
      delay(5000);
    }
  }
}

// --- LUỒNG CHÍNH ---
void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  setup_wifi();
  client.setServer(mqtt_server, 8883);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  bool currentSensorState = digitalRead(PIR_PIN);

  // 1. NGAY LÚC CỬA BẮT ĐẦU MỞ (PIR chuyển từ LOW sang HIGH)
  if (currentSensorState == HIGH && !isDoorOpen) {
    isDoorOpen = true;
    openStartTime = millis(); // Bắt đầu bấm giờ
    warningSent = false;

    Serial.println("🚪 Cửa vừa MỞ!");
    digitalWrite(LED_PIN, HIGH);
    
    // Còi kêu tiếng bíp ngắn (âm cao) báo hiệu mở cửa
    tone(BUZZER_PIN, 1500, 150); 
  } 
  
  // 2. TRONG LÚC CỬA ĐANG TRẠNG THÁI MỞ (Kiểm tra mở quá 10 phút)
  else if (currentSensorState == HIGH && isDoorOpen) {
    unsigned long currentOpenDuration = millis() - openStartTime;

    // Nếu thời gian mở vượt quá 10 phút (600,000 ms)
    if (currentOpenDuration > MAX_OPEN_TIME) {
      // Hú còi liên tục
      tone(BUZZER_PIN, 2000);
      
      // Gửi thông tin cảnh báo lên MQTT (chỉ gửi 1 lần)
      if (!warningSent) {
        Serial.println("⚠️ CẢNH BÁO: Cửa mở quá 10 phút!");
        client.publish(mqtt_topic_warning, "warning_door_open_>10m");
        warningSent = true;
      }
    }
  } 
  
  // 3. KHI CỬA ĐÓNG LẠI (PIR chuyển từ HIGH về LOW)
  else if (currentSensorState == LOW && isDoorOpen) {
    isDoorOpen = false;
    
    // Tắt LED và tắt còi (nếu đang hú báo động)
    digitalWrite(LED_PIN, LOW); 
    noTone(BUZZER_PIN);         
    
    // Còi kêu tiếng bíp ngắn (âm thấp) báo hiệu đóng cửa
    tone(BUZZER_PIN, 800, 150);

    // Tính tổng thời gian cửa đã mở (đổi ra giây)
    unsigned long totalOpenTimeMs = millis() - openStartTime;
    unsigned long totalOpenTimeSec = totalOpenTimeMs / 1000;
    
    Serial.printf("✅ Cửa ĐÓNG. Đã mở trong: %lu giây\n", totalOpenTimeSec);
    
    // Gửi thời gian đã mở lên MQTT
    char timeStr[20];
    snprintf(timeStr, sizeof(timeStr), "%lu", totalOpenTimeSec);
    client.publish(mqtt_topic_time, timeStr);

    // Báo an toàn lên MQTT nếu trước đó có cảnh báo
    if (warningSent) {
      client.publish(mqtt_topic_warning, "door_closed_safe");
      warningSent = false;
    }
  }

  delay(50); // Tăng độ nhạy, chống nhiễu
}