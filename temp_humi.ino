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
// IMPORTANT: Replace with your network credentials and Firebase project details
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
bool signupOK = false;

// --- Sensor & Device Pin Configuration ---
#define DHT_TYPE DHT22
#define DHT_PIN 21
#define LEDWIFI_PIN 25
#define FAN_PIN 5
#define SERVO_PIN 12

DHT dht(DHT_PIN, DHT_TYPE);
Servo windowServo;
bool ledwifi_state;
int currentServoAngle = 0;

// --- PWM Config for Fan ---
const int pwmFreq = 25000;
const int pwmResolution = 8;

// --- Temperature & Servo Angle Mapping ---
const float MIN_TEMP = 27.0; // Temperature to start opening window
const float MAX_TEMP = 32.0; // Temperature for maximum window opening
const int MIN_ANGLE = 0;     // Minimum servo angle (closed)
const int MAX_ANGLE = 180;   // Maximum servo angle (fully open)

// --- Function Prototypes ---
void setup_wifi();
void initializeFirebase();
void sendDataToFirebase(float temp, float hum, int fanSpeed, int windowAngle);
String getFormattedTimestamp();
void controlDevicesAndSendData();
void moveServoToAngle(int targetAngle);

void setup() {
    Serial.begin(115200);
    pinMode(LEDWIFI_PIN, OUTPUT);

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

    Serial.println("Enhanced Temperature Control with Firebase Logging Started!");
}

void loop() {
    // This non-blocking timer runs the main logic every 10 seconds
    if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 10000 || sendDataPrevMillis == 0)) {
        sendDataPrevMillis = millis();
        controlDevicesAndSendData();
    }
}

void setup_wifi() {
    ledwifi_state = 0;
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        ledwifi_state = !ledwifi_state;
        digitalWrite(LEDWIFI_PIN, ledwifi_state);
        delay(500);
    }
    digitalWrite(LEDWIFI_PIN, HIGH);
    Serial.println("\nWiFi connected.");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Configure time via NTP for timestamps
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    Serial.println("Time configured via NTP.");
}

void initializeFirebase() {
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;

    if (Firebase.signUp(&config, &auth, "", "")) {
        Serial.println("Firebase anonymous signup OK");
        signupOK = true;
    } else {
        Serial.printf("Signup error: %s\n", config.signer.signupError.message.c_str());
    }
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
}

void controlDevicesAndSendData() {
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    if (isnan(h) || isnan(t)) {
        Serial.println(F("Failed to read from DHT sensor!"));
        return;
    }

    Serial.println("----------------------------------------");
    Serial.print(getFormattedTimestamp());
    Serial.print(" | Humidity: ");
    Serial.print(h, 1);
    Serial.print("% | Temperature: ");
    Serial.print(t, 1);
    Serial.println("°C");

    // Calculate fan speed
    int fanSpeed = 0;
    if (t >= MIN_TEMP) {
        fanSpeed = map(t * 10, MIN_TEMP * 10, 30 * 10, 100, 255);
        fanSpeed = constrain(fanSpeed, 100, 255);
    }

    // Calculate target servo angle
    int targetServoAngle = 0;
    if (t >= MIN_TEMP) {
        targetServoAngle = map(t * 10, MIN_TEMP * 10, MAX_TEMP * 10, MIN_ANGLE, MAX_ANGLE);
        targetServoAngle = constrain(targetServoAngle, MIN_ANGLE, MAX_ANGLE);
    }

    // Move servo to the target angle (if needed)
    moveServoToAngle(targetServoAngle);

    // Set fan speed
    ledcWrite(FAN_PIN, fanSpeed);

    // Display current status
    Serial.print("Fan Speed (PWM): ");
    Serial.print(fanSpeed);
    Serial.print(" | Window Angle: ");
    Serial.println(currentServoAngle);

    // Send all data to Firebase
    sendDataToFirebase(t, h, fanSpeed, currentServoAngle);
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
        if (Firebase.RTDB.setJSON(&fbdo, log_path.c_str(), &json)) {
            Serial.println("-> Firebase log write SUCCESS");
        } else {
            Serial.println("-> Firebase log write FAILED: " + fbdo.errorReason());
        }

        // 2. Send to a fixed path for the latest reading
        String latest_path = "/temp_humi_reading";
        if (Firebase.RTDB.setJSON(&fbdo, latest_path.c_str(), &json)) {
            Serial.println("-> Firebase latest reading update SUCCESS");
        } else {
            Serial.println("-> Firebase latest reading update FAILED: " + fbdo.errorReason());
        }
    }
}

String getFormattedTimestamp() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return "1970-01-01_00-00-00";
    }
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d_%H-%M-%S", &timeinfo);
    return String(timeStringBuff);
}

void moveServoToAngle(int targetAngle) {
    if (currentServoAngle == targetAngle) {
        return; // Already at target
    }
    // Simple, direct move. Your smooth movement logic can be swapped back in here if preferred.
    windowServo.write(targetAngle);
    currentServoAngle = targetAngle;
    delay(500); // Give servo time to move
}
