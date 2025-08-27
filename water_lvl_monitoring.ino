#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>

// Required for Firebase
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "time.h"

// --- WiFi & Firebase Configuration ---
const char* WIFI_SSID = "B100M";
const char* WIFI_PASSWORD = "12345678";

#define API_KEY "AIzaSyDJXRY084sI0LWgpClDVLYYIx98oz-R5sc"
#define DATABASE_URL "https://iott-c526f-default-rtdb.firebaseio.com"

// --- NTP Time Configuration ---
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 28800; // GMT+8
const int DAYLIGHT_OFFSET_SEC = 0;

// --- Firebase Global Objects ---
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// --- Global Variables for Timing & Status ---
unsigned long sendDataPrevMillis = 0;
bool signupOK = false;

// --- Sensor & Measurement Configuration ---
const int TRIG_PIN = 5;
const int ECHO_PIN = 18;
const int BUZZER_PIN = 19;
const float SENSOR_HEIGHT = 18.48;
const int READINGS = 15;
const float SOUND_SPEED = 0.0343;

// --- Multi-Point Calibration Lookup Table (LUT) ---
const int CALIBRATION_POINTS = 7;
float distance_points[CALIBRATION_POINTS] = {7.27, 8.5, 9.96, 12.06, 15.5, 16.88, 18.48}; // Sensor distance (cm)
float volume_points[CALIBRATION_POINTS]   = {600,  500, 400,   300,   200,   100,   0};     // Corresponding volume (mL)

// --- Alerting Thresholds ---
const float LOW_WATER_THRESHOLD = 0.20;
const float HIGH_WATER_THRESHOLD = 0.90;
float MAX_VOLUME = 600;

// --- Function Prototypes ---
void setup_wifi();
void initializeFirebase();
void sendDataToFirebase(float distance, float volume, float percentage);
String getFormattedTimestamp();
void sortArray(float arr[], int size);
float getFilteredDistance();
float getVolumeFromLut(float distance);
void checkWaterLevelAndBuzzer(float volume);

void setup() {
  Serial.begin(115200);

  // Initialize hardware pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  delay(500);

  MAX_VOLUME = volume_points[0];

  setup_wifi();
  initializeFirebase();

  Serial.println("\nSmart Water Level Monitoring System (Firebase Enabled)");
  Serial.println("-----------------------------------------------------------------");
}

void loop() {
  // Use non-blocking timer to send data every 10 seconds
  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 10000 || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();

    float distance = getFilteredDistance();

    if (distance >= 0) {
      float volume = getVolumeFromLut(distance);
      float percentage = (volume / MAX_VOLUME) * 100.0;
      percentage = max(0.0f, min(100.0f, percentage));

      Serial.print("Distance: ");
      Serial.print(distance, 2);
      Serial.print(" cm | Volume: ");
      Serial.print(volume, 2);
      Serial.print(" mL (");
      Serial.print(percentage, 2);
      Serial.println("%)");

      sendDataToFirebase(distance, volume, percentage);
      // This single function now handles all buzzer logic
      checkWaterLevelAndBuzzer(volume);
    } else {
      Serial.println("Error: Could not read from sensor.");
    }
  }
}

void setup_wifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  Serial.println("Time configured via NTP.");
}

void initializeFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase signup OK");
    signupOK = true;
  } else {
    Serial.printf("Signup error: %s\n", config.signer.signupError.message.c_str());
  }

  // The token callback is now handled automatically by the TokenHelper addon
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void sendDataToFirebase(float distance, float volume, float percentage) {
  if (Firebase.ready() && WiFi.status() == WL_CONNECTED) {
    FirebaseJson json;
    String timestamp = getFormattedTimestamp();

    json.set("timestamp", timestamp);
    json.set("distance_cm", String(distance, 2));
    json.set("volume_mL", String(volume, 2));
    json.set("percentage", String(percentage, 2));

    // 1. Send to a historical log with a unique timestamp
    String log_path = "/water_level_logs/" + timestamp;
    Serial.printf("Sending data to log path: %s\n", log_path.c_str());
    if (Firebase.RTDB.setJSON(&fbdo, log_path.c_str(), &json)) {
      Serial.println("-> Log write SUCCESS");
    } else {
      Serial.println("-> Log write FAILED: " + fbdo.errorReason());
    }

    // 2. Send to a fixed path for the latest reading
    String latest_path = "/latest_water_lvl_reading";
    Serial.printf("Updating latest reading at: %s\n", latest_path.c_str());
    if (Firebase.RTDB.setJSON(&fbdo, latest_path.c_str(), &json)) {
        Serial.println("-> Latest reading update SUCCESS");
    } else {
        Serial.println("-> Latest reading update FAILED: " + fbdo.errorReason());
    }
  }
}

/**
 * @brief Checks water level and controls the buzzer based on a master mode setting in Firebase.
 */
void checkWaterLevelAndBuzzer(float volume) {
  // Check the master buzzer mode from Firebase first
  int buzzerMode = 0; // Default to OFF
  if (Firebase.ready() && WiFi.status() == WL_CONNECTED) {
    if (Firebase.RTDB.getInt(&fbdo, "/control/buzzer_mode")) {
      buzzerMode = fbdo.intData();
    }
  }

  // If buzzer mode is 0 (disabled), turn it off and do nothing else.
  if (buzzerMode == 0) {
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  // --- If buzzer_mode is 1, proceed with the original logic ---
  float percentage = volume / MAX_VOLUME;

  // Priority 1: Check for local alerts
  if (percentage < LOW_WATER_THRESHOLD) {
    Serial.println("ALERT: Water level LOW!");
    digitalWrite(BUZZER_PIN, HIGH);
  } else if (percentage > HIGH_WATER_THRESHOLD) {
    Serial.println("ALERT: Water level HIGH!");
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    // Priority 2: If no local alert, check for remote manual control
    int manualBuzzerState = 0; // Default to OFF
    if (Firebase.ready() && WiFi.status() == WL_CONNECTED) {
        if (Firebase.RTDB.getInt(&fbdo, "/control/buzzer")) {
            manualBuzzerState = fbdo.intData();
        }
    }
    
    digitalWrite(BUZZER_PIN, manualBuzzerState == 1 ? HIGH : LOW);
    if (manualBuzzerState == 1) {
      Serial.println("Buzzer ON by remote control.");
    }
  }
}


String getFormattedTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "1970-01-01 00:00:00";
  }
  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d_%H-%M-%S", &timeinfo);
  return String(timeStringBuff);
}


// --- Your Existing Helper Functions (Unchanged) ---
void sortArray(float arr[], int size) {
  for (int i = 0; i < size - 1; i++) {
    for (int j = 0; j < size - i - 1; j++) {
      if (arr[j] > arr[j + 1]) {
        float temp = arr[j];
        arr[j] = arr[j + 1];
        arr[j + 1] = temp;
      }
    }
  }
}

float getFilteredDistance() {
  float distance_samples[READINGS];
  int valid_readings = 0;
  for (int i = 0; i < READINGS; i++) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH, 30000);
    if (duration > 0) {
      float sample = (duration * SOUND_SPEED) / 2.0;
      if (sample <= SENSOR_HEIGHT && sample > 0) {
        distance_samples[valid_readings] = sample;
        valid_readings++;
      }
    }
    delay(30);
  }
  if (valid_readings > 0) {
    sortArray(distance_samples, valid_readings);
    return distance_samples[valid_readings / 2];
  } else {
    return -1.0;
  }
}

float getVolumeFromLut(float distance) {
  if (distance <= distance_points[0]) {
    return volume_points[0];
  }
  if (distance >= distance_points[CALIBRATION_POINTS - 1]) {
    return volume_points[CALIBRATION_POINTS - 1];
  }
  int i = 0;
  while (distance > distance_points[i + 1]) {
    i++;
  }
  float d1 = distance_points[i];
  float v1 = volume_points[i];
  float d2 = distance_points[i + 1];
  float v2 = volume_points[i + 1];
  float volume = v1 + ((distance - d1) * (v2 - v1)) / (d2 - d1);
  return volume;
}
