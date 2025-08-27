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
unsigned long controlCheckPrevMillis = 0;
bool signupOK = false;
bool isManualMode = false;
bool lastAutoModeState = false; // true
int lastBrightnessValue = -1;

// --- LED & LDR Configuration ---
#define LDR_PIN         32   // ADC1 channel 4
#define LED_PIN         2    // PWM-capable LED pin

// Calibration values
const int MIN_LUX = 20;
const int MAX_LUX = 1000;
const int MIN_BRIGHTNESS = 10;
const int MAX_BRIGHTNESS = 100;

// --- Function Prototypes ---
void setup_wifi();
void initializeFirebase();
void sendLEDDataToFirebase(int rawLDR, float voltage, float lux, int brightness);
String getFormattedTimestamp();

void setup() {
  Serial.begin(115200);
  
  // Initialize hardware pins
  pinMode(LED_PIN, OUTPUT);
  analogReadResolution(12);   // Use 12-bit ADC resolution
  
  setup_wifi();
  initializeFirebase();

  Serial.println("\nAdaptive LED Brightness System (Firebase Enabled)");
  Serial.println("-----------------------------------------------------------------");
}

void loop() {
  // Use non-blocking timer to send data every 10 seconds
  if (!isManualMode && Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 10000)) {
    sendDataPrevMillis = millis();

    int rawLDR = analogRead(LDR_PIN);
    
    // Prevent division by zero if the reading is 0
    if (rawLDR == 0) {
      Serial.println("Error: LDR reading is 0. Check wiring.");
      return; // Skip the rest of the loop
    }
    
    float voltage = rawLDR * (3.3 / 4095.0);
    float lux = 500 / voltage;

    // Invert mapping: brighter environment → lower LED brightness
    int brightness = map(lux, MIN_LUX, MAX_LUX, MAX_BRIGHTNESS, MIN_BRIGHTNESS);
    brightness = constrain(brightness, MIN_BRIGHTNESS, MAX_BRIGHTNESS);

    setLEDBrightness(brightness);

    Serial.print("Raw LDR: ");
    Serial.print(rawLDR);
    Serial.print(" | Voltage: ");
    Serial.print(voltage);
    Serial.print("V | Lux: ");
    Serial.print(lux);
    Serial.print(" | Brightness: ");
    Serial.print(brightness);
    Serial.println("%");

    sendLEDDataToFirebase(rawLDR, voltage, lux, brightness);
  }
  
  // Check for remote control commands every 2 seconds
  if (millis() - controlCheckPrevMillis > 2000) {
    controlCheckPrevMillis = millis();
    checkRemoteControl();
  }
}

void setLEDBrightness(int percent) {
  // Convert percentage to PWM value (0-255)
  int pwmValue = percent * 255 / 100;
  analogWrite(LED_PIN, pwmValue);
}

void checkRemoteControl() {
  if (Firebase.ready() && WiFi.status() == WL_CONNECTED) {
    // Check for auto mode changes
    if (Firebase.RTDB.getBool(&fbdo, "/control/led/auto_mode")) {
      bool autoMode = fbdo.boolData();
      
      // Only process if the value has changed
      if (autoMode != lastAutoModeState) {
        lastAutoModeState = autoMode;
        isManualMode = !autoMode;
        
        if (autoMode) {
          Serial.println("Switched to auto mode.");
          // Reset timer to get an immediate reading
          sendDataPrevMillis = 0;
        } else {
          Serial.println("Switched to manual mode.");
        }
      }
    }

    // Check for manual brightness changes (only in manual mode)
    if (isManualMode && Firebase.RTDB.getInt(&fbdo, "/control/led/brightness_lvl")) {
      int manualBrightness = constrain(fbdo.intData(), 0, 100);
      
      // Only process if the value has changed
      if (manualBrightness != lastBrightnessValue) {
        lastBrightnessValue = manualBrightness;
        setLEDBrightness(manualBrightness);
        Serial.print("Manual brightness set to: ");
        Serial.print(manualBrightness);
        Serial.println("%");
      }
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

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void sendLEDDataToFirebase(int rawLDR, float voltage, float lux, int brightness) {
  if (Firebase.ready() && WiFi.status() == WL_CONNECTED) {
    FirebaseJson json;
    String timestamp = getFormattedTimestamp();

    json.set("timestamp", timestamp);
    json.set("raw_ldr", String(rawLDR));
    json.set("voltage", String(voltage, 2));
    json.set("lux", String(lux, 2));
    json.set("brightness_percent", String(brightness));

    // 1. Send to a historical log with a unique timestamp
    String log_path = "/led_logs/" + timestamp;
    Serial.printf("Sending LED data to log path: %s\n", log_path.c_str());
    if (Firebase.RTDB.setJSON(&fbdo, log_path.c_str(), &json)) {
      Serial.println("-> LED Log write SUCCESS");
    } else {
      Serial.println("-> LED Log write FAILED: " + fbdo.errorReason());
    }

    // 2. Send to a fixed path for the latest reading
    String latest_path = "/latest_led_reading";
    Serial.printf("Updating latest LED reading at: %s\n", latest_path.c_str());
    if (Firebase.RTDB.setJSON(&fbdo, latest_path.c_str(), &json)) {
        Serial.println("-> Latest LED reading update SUCCESS");
    } else {
        Serial.println("-> Latest LED reading update FAILED: " + fbdo.errorReason());
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