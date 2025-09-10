#include <Arduino.h>
#include <WiFi.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <Firebase_ESP_Client.h>

// Required for Firebase
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "time.h"

// --- WiFi & Firebase Configuration ---
const char* WIFI_SSID = "B100M-T8";
const char* WIFI_PASSWORD = "12345678";
#define API_KEY "AIzaSyDJXRY084sI0LWgpClDVLYYIx98oz-R5sc"
#define DATABASE_URL "https://iott-c526f-default-rtdb.firebaseio.com"

// --- NTP Time Configuration ---
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 28800; // GMT+8 for Kuala Lumpur
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
int lastFanSpeed = -1;
int lastWindowAngle = -1;

// --- Sensor & Device Pin Configuration ---
#define DHT_TYPE DHT22
#define DHT_PIN 21
#define STATUS_LED_PIN 25
#define BUZZER_PIN 26
#define FAN_PIN 5
#define SERVO_PIN 12

DHT dht(DHT_PIN, DHT_TYPE);
Servo windowServo;
int currentServoAngle = 0;

// --- PWM Config for Fan ---
const int pwmFreq = 25000;
const int pwmResolution = 8;

// --- Temperature Thresholds ---
// --- For Alerts ---
const float WARM_TEMP = 29.0;
const float HIGH_TEMP = 31.0;
// --- For Servo Angle Mapping ---
const float MIN_TEMP = 27.0;  
const float MAX_TEMP = 32.0;  
const int MIN_ANGLE = 0;    
const int MAX_ANGLE = 180; 

// --- Function  ---
void setup_wifi();
void initializeFirebase();
void sendDataToFirebase(float temp, float hum, int fanSpeed, int windowAngle);
String getFormattedTimestamp();
void checkRemoteControl();
void runAutoMode();
void moveServoToAngle(int targetAngle);
void setFanSpeed(int speed);
void printCurrentState();
void setTempAlert(float currentTemp);

void setup() {
    Serial.begin(115200);
    // Setup LED and Buzzer
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // Setup servo motor
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    windowServo.setPeriodHertz(50);
    windowServo.attach(SERVO_PIN, 500, 2400);
    windowServo.write(0); // Start with window closed
    currentServoAngle = 0;
    delay(1000);

    // Setup PWM for fan
    ledcAttach(FAN_PIN, pwmFreq, pwmResolution);
    ledcWrite(FAN_PIN, 0);

    setup_wifi();
    dht.begin();
    initializeFirebase(); // Initialize Firebase connection

    Serial.println("\nSmart Temperature Control Module");
    Serial.println("-----------------------------------------------------------------");
}

void loop() {
    // Check for remote control
    if (millis() - controlCheckPrevMillis > 2000) {
        controlCheckPrevMillis = millis();
        checkRemoteControl();
    }

    // If manual mode is not active, run auto mode
    if (!isManualMode && Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 10000)) {
        sendDataPrevMillis = millis();
        runAutoMode();
    }

    printCurrentState();
    delay(1000);
}

void checkRemoteControl() {
    if (!Firebase.ready() || WiFi.status() != WL_CONNECTED) return;

    // Check for auto_mode flag from Firebase
    if (Firebase.RTDB.getBool(&fbdo, "/control/temp/auto_mode")) {
        bool autoModeEnabled = fbdo.boolData();
        if (isManualMode == autoModeEnabled) {
            isManualMode = !autoModeEnabled;
            Serial.print("--> Remote control changed mode to: ");
            Serial.println(isManualMode ? "Manual" : "Auto");
        }
    }

    // If in manual mode, check for fan speed and window angle changes
    if (isManualMode) {
        if (Firebase.RTDB.getInt(&fbdo, "/control/temp/fan_speed")) {
            int manualFanSpeed = constrain(fbdo.intData(), 0, 255);
            if (manualFanSpeed != lastFanSpeed) {
                lastFanSpeed = manualFanSpeed;
                setFanSpeed(manualFanSpeed);
                Serial.print("--> Manual fan speed set to: ");
                Serial.println(manualFanSpeed);
            }
        }

        if (Firebase.RTDB.getInt(&fbdo, "/control/temp/window_angle")) {
            int manualWindowAngle = constrain(fbdo.intData(), MIN_ANGLE, MAX_ANGLE);
            if (manualWindowAngle != lastWindowAngle) {
                lastWindowAngle = manualWindowAngle;
                moveServoToAngle(manualWindowAngle);
                Serial.print("--> Manual window angle set to: ");
                Serial.println(manualWindowAngle);
            }
        }
    }
}

void runAutoMode() {
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (isnan(h) || isnan(t)) {
        Serial.println(F("Error: Failed to read from DHT sensor!"));
        return;
    }

    setTempAlert(t);

    Serial.println("----------------------------------------");
    Serial.print("Temperature: ");
    Serial.print(t, 1);
    Serial.print("°C | Humidity: ");
    Serial.print(h, 1);
    Serial.println("%");

    // Calculate fan speed based on temperature
    int fanSpeed = 0;
    if (t >= MIN_TEMP) {
        fanSpeed = map(t * 10, MIN_TEMP * 10, 30 * 10, 100, 255);
        fanSpeed = constrain(fanSpeed, 100, 255);
    }

    // Calculate target servo angle based on temperature
    int targetServoAngle = 0;
    if (t >= MIN_TEMP) {
        targetServoAngle = map(t * 10, MIN_TEMP * 10, MAX_TEMP * 10, MIN_ANGLE, MAX_ANGLE);
        targetServoAngle = constrain(targetServoAngle, MIN_ANGLE, MAX_ANGLE);
    }

    Serial.print("Auto Fan Speed: ");
    Serial.print(fanSpeed);
    Serial.print(" | Auto Window Angle: ");
    Serial.println(targetServoAngle);

    // Apply the calculated values
    setFanSpeed(fanSpeed);
    moveServoToAngle(targetServoAngle);
    
    // Keep track of the last auto values
    lastFanSpeed = fanSpeed;
    lastWindowAngle = targetServoAngle;

    // Send data to Firebase
    sendDataToFirebase(t, h, fanSpeed, targetServoAngle);
}

void setTempAlert(float currentTemp) {
  if (currentTemp >= HIGH_TEMP) {
    // Pattern for HIGH temperature
    if (millis() % 200 < 100) {
      digitalWrite(STATUS_LED_PIN, HIGH);
    } else {
      digitalWrite(STATUS_LED_PIN, LOW);
    }
    tone(BUZZER_PIN, 1500);
  } else if (currentTemp >= WARM_TEMP) {
    // Pattern for WARM temperature
    if (millis() % 1000 < 500) { 
      digitalWrite(STATUS_LED_PIN, HIGH);
    } else {
      digitalWrite(STATUS_LED_PIN, LOW);
    }
    if (millis() % 3000 < 100) { 
      tone(BUZZER_PIN, 800);
    } else {
      noTone(BUZZER_PIN);
    }
  } else {
    // Normal temperature: No alert
    digitalWrite(STATUS_LED_PIN, LOW);
    noTone(BUZZER_PIN);
  }
}


void setFanSpeed(int speed) {
    ledcWrite(FAN_PIN, speed);
}

void moveServoToAngle(int targetAngle) {
    if (currentServoAngle == targetAngle) {
        return; 
    }
    windowServo.write(targetAngle);
    currentServoAngle = targetAngle;
    delay(500);
}

void printCurrentState() {
    static String lastPrintedState = "";
    String currentState = "";

    if (isManualMode) {
        currentState = "State: Manual Mode (Fan: " + String(lastFanSpeed) + ", Window: " + String(lastWindowAngle) + "°)";
    } else {
        currentState = "State: Auto Mode";
    }

    if (currentState != lastPrintedState) {
        Serial.println(currentState);
        lastPrintedState = currentState;
    }
}

void setup_wifi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
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

void sendDataToFirebase(float temp, float hum, int fanSpeed, int windowAngle) {
    if (Firebase.ready() && WiFi.status() == WL_CONNECTED) {
        FirebaseJson json;
        String timestamp = getFormattedTimestamp();

        json.set("timestamp", timestamp);
        json.set("temperature_C", String(temp, 2));
        json.set("humidity_pct", String(hum, 2));
        json.set("fan_speed_pwm", String(fanSpeed));
        json.set("window_angle_deg", String(windowAngle));
        
        int windowPercentage = map(windowAngle, MIN_ANGLE, MAX_ANGLE, 0, 100);
        json.set("window_open_pct", String(windowPercentage));

        // 1. Send to a historical log with a unique timestamp
        String log_path = "/temp_humi_logs/" + timestamp;
        Serial.printf("Sending temp data to log path: %s\n", log_path.c_str());
        if (Firebase.RTDB.setJSON(&fbdo, log_path.c_str(), &json)) {
            Serial.println("-> Temp Log write SUCCESS");
        } else {
            Serial.println("-> Temp Log write FAILED: " + fbdo.errorReason());
        }

        // 2. Send to a fixed path for the latest reading
        String latest_path = "/latest_temp_reading";
        Serial.printf("Updating latest temp reading at: %s\n", latest_path.c_str());
        if (Firebase.RTDB.setJSON(&fbdo, latest_path.c_str(), &json)) {
            Serial.println("-> Latest temp reading update SUCCESS");
        } else {
            Serial.println("-> Latest temp reading update FAILED: " + fbdo.errorReason());
        }
    }
}

String getFormattedTimestamp() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "1970-01-01 00:00:00";
    }
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d_%H-%M-%S", &timeinfo);
    return String(timeStringBuff);
}
