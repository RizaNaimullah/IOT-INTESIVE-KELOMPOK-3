#include <WiFi.h>
#include <Firebase_ESP_Client.h>

// ===============================
// WIFI
// ===============================
#define WIFI_SSID       "Redmi1121"
#define WIFI_PASSWORD   "daoa12345"

// ===============================
// FIREBASE
// ===============================
#define API_KEY         "AIzaSyASwuRCw2jYdLL4dfw-7JMXHsLdyUrRDbw"
#define DATABASE_URL    "https://training-internal-riza-default-rtdb.firebaseio.com/"

// ===============================
// Firebase Objects
// ===============================
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ===============================
// Timer
// ===============================
unsigned long previousMillis = 0;
const unsigned long interval = 3000; // 3 detik


void setup() {

  Serial.begin(115200);

  // ===============================
  // CONNECT WIFI
  // ===============================
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Menghubungkan ke WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("WiFi Terhubung!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());


  // ===============================
  // FIREBASE CONFIG
  // ===============================
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  // Anonymous authentication
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase Authentication berhasil!");
  } 
  else {
    Serial.print("Authentication Error: ");
    Serial.println(config.signer.signupError.message.c_str());
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("Firebase siap!");
}


void loop() {

  if (Firebase.ready() &&
      (millis() - previousMillis >= interval)) {

    previousMillis = millis();

    // ===============================
    // DUMMY SENSOR
    // ===============================

    // Dummy suhu 25 - 35 °C
    float suhu = random(250, 351) / 10.0;

    // Dummy kelembapan 50 - 90 %
    float kelembapan = random(500, 901) / 10.0;

    // Status berdasarkan suhu
    String status;

    if (suhu < 30) {
      status = "NORMAL";
    }
    else if (suhu < 33) {
      status = "WARNING";
    }
    else {
      status = "DANGER";
    }


    // ===============================
    // KIRIM SUHU
    // ===============================

    if (Firebase.RTDB.setFloat(
          &fbdo,
          "/IoT/suhu",
          suhu)) {

      Serial.print("Suhu berhasil dikirim: ");
      Serial.println(suhu);

    } 
    else {

      Serial.print("Gagal kirim suhu: ");
      Serial.println(fbdo.errorReason());
    }


    // ===============================
    // KIRIM KELEMBAPAN
    // ===============================

    if (Firebase.RTDB.setFloat(
          &fbdo,
          "/IoT/kelembapan",
          kelembapan)) {

      Serial.print("Kelembapan berhasil dikirim: ");
      Serial.println(kelembapan);

    } 
    else {

      Serial.print("Gagal kirim kelembapan: ");
      Serial.println(fbdo.errorReason());
    }


    // ===============================
    // KIRIM STATUS
    // ===============================

    if (Firebase.RTDB.setString(
          &fbdo,
          "/IoT/status",
          status)) {

      Serial.print("Status: ");
      Serial.println(status);

    } 
    else {

      Serial.print("Gagal kirim status: ");
      Serial.println(fbdo.errorReason());
    }


    // ===============================
    // KIRIM LAST UPDATE
    // ===============================

    String waktu = String(millis() / 1000) + " detik";

    if (Firebase.RTDB.setString(
          &fbdo,
          "/IoT/last_update",
          waktu)) {

      Serial.print("Last Update: ");
      Serial.println(waktu);

    } 
    else {

      Serial.print("Gagal kirim waktu: ");
      Serial.println(fbdo.errorReason());
    }


    // ===============================
    // PEMBATAS
    // ===============================

    Serial.println("-----------------------------");
  }
}
