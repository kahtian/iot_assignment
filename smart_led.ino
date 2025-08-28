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
unsigned long scheduleCheckPrevMillis = 0;
const int SCHEDULE_CHECK_INTERVAL = 30000; // Check for schedules every 30 seconds

bool signupOK = false;
bool isManualMode = false;      // Controlled by remote, but overridden by schedule
bool scheduleActive = false;    // True if a schedule is currently running
int lastBrightnessValue = -1;
int scheduleBrightness = 0;   // To store the brightness from the active schedule

// --- LED & LDR Configuration ---
#define LDR_PIN         32   // ADC1 channel 4
#define LED_PIN         2    // PWM-capable LED pin

// Calibration values
const int MIN_LUX = 20;
const int MAX_LUX = 1000;
const int MIN_BRIGHTNESS = 10;
const int MAX_BRIGHTNESS = 100;

// --- Function Prototypes ---
// void setup_wifi();
// void initializeFirebase();
// void sendLEDDataToFirebase(int rawLDR, float voltage, float lux, int brightness);
// String getFormattedTimestamp();
// void setLEDBrightness(int percent);
// void checkSchedules();
// void checkRemoteControl();
// void runAutoMode();
// void printCurrentState();

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  analogReadResolution(12); // Use 12-bit ADC resolution
  
  setup_wifi();
  initializeFirebase();

  Serial.println("\nSmart LED Grow Light Module");
  Serial.println("-----------------------------------------------------------------");
}

void loop() {
  // Highest priority: Check for active schedules periodically.
  if (millis() - scheduleCheckPrevMillis > SCHEDULE_CHECK_INTERVAL) {
    scheduleCheckPrevMillis = millis();
    checkSchedules();
  }

  // If a schedule is NOT active, then check for remote control commands.
  if (!scheduleActive && (millis() - controlCheckPrevMillis > 2000)) {
    controlCheckPrevMillis = millis();
    checkRemoteControl();
  }

  // If neither schedule nor manual mode is active, run auto mode.
  if (!scheduleActive && !isManualMode && Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 10000)) {
    sendDataPrevMillis = millis();
    runAutoMode();
  }

  printCurrentState();
  delay(1000); // Slow down the printing loop
}

void setLEDBrightness(int percent) {
  int pwmValue = map(percent, 0, 100, 0, 255);
  analogWrite(LED_PIN, pwmValue);
}

void checkSchedules() {
  if (!Firebase.ready() || WiFi.status() != WL_CONNECTED) {
    return; // Can't check schedules without connection
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time for schedule check.");
    return;
  }

  int currentDay = timeinfo.tm_wday; // Sunday = 0, Saturday = 6
  int currentTime = timeinfo.tm_hour * 100 + timeinfo.tm_min;
  
  bool foundActiveSchedule = false;

  // Loop through up to 5 possible schedules
  for (int i = 1; i <= 5; i++) {
    String path = "/control/led/schedules/schedule_" + String(i);
    
    // Check if the schedule exists and is enabled
    if (Firebase.RTDB.getBool(&fbdo, path + "/enabled") && fbdo.boolData()) {
      
      // Get schedule details
      String startTimeStr, endTimeStr, daysStr;
      
      if (Firebase.RTDB.getString(&fbdo, path + "/start_time")) startTimeStr = fbdo.stringData();
      else { Serial.println("--> ERROR: Failed to get start_time for " + path); continue; }
      
      if (Firebase.RTDB.getString(&fbdo, path + "/end_time")) endTimeStr = fbdo.stringData();
      else { Serial.println("--> ERROR: Failed to get end_time for " + path); continue; }
      
      if (Firebase.RTDB.getInt(&fbdo, path + "/brightness")) scheduleBrightness = fbdo.intData();
      else { Serial.println("--> ERROR: Failed to get brightness for " + path); continue; }

      if (Firebase.RTDB.getString(&fbdo, path + "/days")) daysStr = fbdo.stringData();
      else { Serial.println("--> ERROR: Failed to get days for " + path); continue; }

      // --- Time & Day Validation ---
      int startTime = atoi(startTimeStr.substring(0, 2).c_str()) * 100 + atoi(startTimeStr.substring(3).c_str());
      int endTime = atoi(endTimeStr.substring(0, 2).c_str()) * 100 + atoi(endTimeStr.substring(3).c_str());
      
      // Check if current day is in the schedule's day list
      if (daysStr.indexOf(String(currentDay)) != -1) {
        // Check if current time is within the schedule's time range
        if (currentTime >= startTime && currentTime < endTime) {
          foundActiveSchedule = true;
          break; // Found an active schedule, no need to check others
        }
      }
    }
  }

  // --- Update System State ---
  if (foundActiveSchedule) {
    if (!scheduleActive) { // Print only on state change
      Serial.println("--> Schedule is now ACTIVE.");
    }
    scheduleActive = true;
    setLEDBrightness(scheduleBrightness);
    lastBrightnessValue = scheduleBrightness;
  } else {
    if (scheduleActive) { // Print only on state change
      Serial.println("--> Schedule is now INACTIVE. Reverting to previous mode.");
      // When schedule ends, revert to the state dictated by Firebase's auto_mode
      if(Firebase.RTDB.getBool(&fbdo, "/control/led/auto_mode")){
          isManualMode = !fbdo.boolData();
      }
    }
    scheduleActive = false;
  }
}

void checkRemoteControl() {
  if (!Firebase.ready() || WiFi.status() != WL_CONNECTED) return;

  // Check for auto_mode flag from Firebase
  if (Firebase.RTDB.getBool(&fbdo, "/control/led/auto_mode")) {
    bool autoModeEnabled = fbdo.boolData();
    // In our new logic, isManualMode is just the opposite of autoModeEnabled
    if (isManualMode == autoModeEnabled) {
      isManualMode = !autoModeEnabled;
      Serial.print("--> Remote control changed mode to: ");
      Serial.println(isManualMode ? "Manual" : "Auto");
    }
  }

  // If in manual mode, check for brightness changes
  if (isManualMode && Firebase.RTDB.getInt(&fbdo, "/control/led/brightness_lvl")) {
    int manualBrightness = constrain(fbdo.intData(), 0, 100);
    if (manualBrightness != lastBrightnessValue) {
      lastBrightnessValue = manualBrightness;
      setLEDBrightness(manualBrightness);
    }
  }
}

void runAutoMode() {
  int rawLDR = analogRead(LDR_PIN);
  if (rawLDR == 0) {
    Serial.println("Error: LDR reading is 0. Check wiring.");
    return;
  }
  float voltage = rawLDR * (3.3 / 4095.0);
  float lux = 500 / voltage;
  int brightness = map(lux, MIN_LUX, MAX_LUX, MAX_BRIGHTNESS, MIN_BRIGHTNESS);
  brightness = constrain(brightness, MIN_BRIGHTNESS, MAX_BRIGHTNESS);

  Serial.print("Raw LDR: ");
  Serial.print(rawLDR);
  Serial.print(" | Voltage: ");
  Serial.print(voltage);
  Serial.print("V | Lux: ");
  Serial.print(lux);
  Serial.print(" | Brightness: ");
  Serial.print(brightness);
  Serial.println("%");

  setLEDBrightness(brightness);
  lastBrightnessValue = brightness; // Keep track of the last auto brightness
  sendLEDDataToFirebase(rawLDR, voltage, lux, brightness);
}

void printCurrentState() {
  static String lastPrintedState = "";
  String currentState = "";

  if (scheduleActive) {
    currentState = "State: Schedule Active (" + String(scheduleBrightness) + "%)";
  } else if (isManualMode) {
    currentState = "State: Manual Mode (" + String(lastBrightnessValue) + "%)";
  } else {
    currentState = "State: Auto Mode";
  }

  if (currentState != lastPrintedState) {
    Serial.println(currentState);
    lastPrintedState = currentState;
  }
}

// --- Helper Functions (WiFi, Firebase Init, etc.) ---

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
    return "1970-01-01 00:00:00";
  }
  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d_%H-%M-%S", &timeinfo);
  return String(timeStringBuff);
}
