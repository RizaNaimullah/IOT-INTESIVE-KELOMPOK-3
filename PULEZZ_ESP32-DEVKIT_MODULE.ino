// ============================================================================
// INCLUDE LIBRARY
// ============================================================================
#include <WiFi.h>
#include <esp_now.h>
#include <Firebase_ESP_Client.h>

// ============================================================================
// DEFINISI PIN RELAY & SENSOR TEKANAN HX710B
// ============================================================================
#define RELAY_PUMP_IN1  32      // Pin GPIO 32 terhubung ke Relay 1 (Kontrol Pompa Udara)
#define RELAY_VALVE_IN2 33      // Pin GPIO 33 terhubung ke Relay 2 (Kontrol Solenoid Valve)
#define HX710B_DOUT     12      // Pin GPIO 12 terhubung ke Data Out (DOUT) HX710B
#define HX710B_SCK      13      // Pin GPIO 13 terhubung ke Serial Clock (SCK) HX710B

// ============================================================================
// KREDENSIAL WIFI & FIREBASE REALTIME DATABASE
// ============================================================================
#define WIFI_SSID     "fafa"
#define WIFI_PASSWORD "12345678"
#define API_KEY       "AIzaSyDsb7WbbpiK1E9M2VTHgnNwqXPIfpogCh0"
#define DATABASE_URL  https://project-pulezz-default-rtdb.firebaseio.com/

// ============================================================================
// STRUKTUR DATA PENERIMAAN ESP-NOW
// ============================================================================
typedef struct struct_message {
  float spo2;
  int heartRate;
  bool validData;
  int totalOsaCount;
  int currentHourOsaCount;
  float avgOsaPerHour;
  char osaSeverity[12];
} struct_message;

struct_message receivedData;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ============================================================================
// PARAMETER TEKANAN & PEMICU BANTAL
// ============================================================================
const long ZERO_OFFSET = 8388607;
const float SCALE_FACTOR = 142500.0;
const float TARGET_PRESSURE = 5.0;    // Tekanan untuk menaikkan kepala 15 derajat
const float NORMAL_PRESSURE = 0.5;    // Tekanan batas bawah (posisi bantal normal/kempes)
const float SAFETY_PRESSURE = 12.0;   // Batas tekanan maksimum pengaman

float base_spo2 = 98.0;               // Baseline SpO2 untuk acuan desaturasi
bool trigger_pillow = false;          // Sinyal perintah inflasi bantal

// ============================================================================
// FUNGSI PEMBACAAN SENSOR TEKANAN UDARA HX710B
// ============================================================================
float getPressurekPa() {
  long count = 0;
  while (digitalRead(HX710B_DOUT)); 
  for (int i = 0; i < 24; i++) {
    digitalWrite(HX710B_SCK, HIGH);
    count = count << 1;
    digitalWrite(HX710B_SCK, LOW);
    if (digitalRead(HX710B_DOUT)) count++;
  }
  digitalWrite(HX710B_SCK, HIGH);
  digitalWrite(HX710B_SCK, LOW);
  
  if (count & 0x800000) count |= 0xFF000000;
  return (float)(count - ZERO_OFFSET) / SCALE_FACTOR;
}

// ============================================================================
// CALLBACK FUNCTION ESP-NOW DENGAN LOGIKA HYSTERESIS
// ============================================================================
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&receivedData, incomingData, sizeof(receivedData));
  
  if (receivedData.validData && receivedData.spo2 > 70) {
    
    // Jika bantal SEDANG DALAM POSISI NORMAL (Belum Trigger)
    if (!trigger_pillow) {
      bool isSpO2Drop = ((base_spo2 - receivedData.spo2) >= 4.0); 
      bool isAbsoluteLow = (receivedData.spo2 <= 94.0);

      if (isSpO2Drop || isAbsoluteLow) {
        trigger_pillow = true; // Aktifkan mode OSA
      } else if (receivedData.spo2 >= 96.0) {
        // Pembaruan baseline dinamis jika napas sedang sangat stabil
        base_spo2 = receivedData.spo2; 
      }
    } 
    // Jika bantal SEDANG MEMOMPA / MODE OSA (Sudah Trigger)
    else {
      // Tahan status OSA tetap TRUE sampai SpO2 benar-benar pulih ke 96%
      if (receivedData.spo2 >= 96.0) {
        trigger_pillow = false;        // Matikan mode OSA
        base_spo2 = receivedData.spo2; // Reset baseline ke nilai pulih
      }
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PUMP_IN1, OUTPUT);
  pinMode(RELAY_VALVE_IN2, OUTPUT);
  digitalWrite(RELAY_PUMP_IN1, HIGH); 
  digitalWrite(RELAY_VALVE_IN2, HIGH); 

  pinMode(HX710B_DOUT, INPUT);
  pinMode(HX710B_SCK, OUTPUT);

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(OnDataRecv);
  }

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  Firebase.signUp(&config, &auth, "", "");
  Firebase.begin(&config, &auth);
}

void loop() {
  float current_pressure = getPressurekPa();

  // 1. SAFETY OVERPRESSURE
  if (current_pressure > SAFETY_PRESSURE) {
    digitalWrite(RELAY_PUMP_IN1, HIGH); // Mati total Pompa
    digitalWrite(RELAY_VALVE_IN2, LOW);  // Buka Katup Solenoid darurat
    return;
  }

  // 2. KONTROL PNEUMATIK DENGAN TARGET POSISI NORMAL
  if (trigger_pillow) {
    // KONDISI OSA: Naikkan bantal ke 15 derajat (TARGET_PRESSURE)
    if (current_pressure < TARGET_PRESSURE) {
      digitalWrite(RELAY_VALVE_IN2, HIGH); // Tutup katup pembuangan
      digitalWrite(RELAY_PUMP_IN1, LOW);   // Nyalakan pompa udara
    } else {
      digitalWrite(RELAY_PUMP_IN1, HIGH);  // Pompa mati (Tekanan tercapai)
      digitalWrite(RELAY_VALVE_IN2, HIGH); // Katup tetap tertutup untuk menahan udara
    }
  } else {
    // KONDISI NORMAL: Turunkan bantal ke posisi tidur normal (NORMAL_PRESSURE)
    if (current_pressure > NORMAL_PRESSURE) {
      digitalWrite(RELAY_PUMP_IN1, HIGH);  // Pastikan pompa mati
      digitalWrite(RELAY_VALVE_IN2, LOW);  // Buka katup solenoid untuk buang udara
    } else {
      // Jika sudah mencapai posisi normal, matikan katup untuk menghemat daya
      digitalWrite(RELAY_PUMP_IN1, HIGH);  
      digitalWrite(RELAY_VALVE_IN2, HIGH); // Tutup katup
    }
  }

  // 3. PENGIRIMAN KE FIREBASE
  if (Firebase.ready()) {
    FirebaseJson json;
    json.set("spo2", receivedData.spo2);
    json.set("heartRate", receivedData.heartRate);
    json.set("validData", receivedData.validData);
    json.set("trigger_pillow", trigger_pillow);
    json.set("totalOsaCount", receivedData.totalOsaCount);
    json.set("currentHourOsaCount", receivedData.currentHourOsaCount);
    json.set("avgOsaPerHour", receivedData.avgOsaPerHour);
    json.set("osaSeverity", receivedData.osaSeverity);
    json.set("pillowPressure_kPa", current_pressure);

    Firebase.RTDB.setJSON(&fbdo, "/gelang_monitoring/latest", &json);
  }

  delay(500); 
}
