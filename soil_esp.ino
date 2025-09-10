// Wi-Fi and MQTT Libraries
#include <WiFi.h>
#include <PubSubClient.h>


// Firebase Client and Time
#include <Firebase_ESP_Client.h>
#include <time.h>


// Your Wi-Fi Credentials
#define WIFI_SSID "B100M"
#define WIFI_PASSWORD "12345678"


// MQTT Configuration
#define MQTT_SERVER "192.168.200.182"
#define MQTT_PORT 1883
const char* moistureTopic = "esp32/soil_moisture";
const char* controlCommandTopic = "esp32/control_command";  // Commands to Pi


// Firebase Configuration
#define API_KEY "AIzaSyDJXRY084sI0LWgpClDVLYYIx98oz-R5sc"
#define DATABASE_URL "https://iott-c526f-default-rtdb.firebaseio.com/"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
FirebaseConfig config;
FirebaseAuth auth;
FirebaseData fbdo;


bool signupOK = false;


// Pins
const int soilPin = 34;


// Time settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset = 8 * 3600;
const int daylightOffset = 0;


// MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);


// Timing
unsigned long lastSend = 0;
const unsigned long interval = 1000;


// Variables to track pump state for logging
String lastPumpCommand = "";


//--------------------------------------------------
void setup_wifi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed!");
  }
}


//--------------------------------------------------
String getTimeNow() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "NTP Error";
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}


//--------------------------------------------------
void logPumpCommandToFirebase(String command, String reason) {
  if (Firebase.ready()) {
    FirebaseJson pumpLog;
    pumpLog.set("command", command);
    pumpLog.set("reason", reason);
    pumpLog.set("timestamp", getTimeNow());
    Firebase.RTDB.pushJSON(&fbdo, "/pump_commands", &pumpLog);
    Serial.println("Pump command logged to Firebase: " + command + " (" + reason + ")");
  }
}


//--------------------------------------------------
void reconnectMQTT() {
  int attempts = 0;
  while (!mqttClient.connected() && attempts < 5) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      attempts++;
      delay(5000);
    }
  }
}


//--------------------------------------------------
void setupFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;


  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase signup OK");
    signupOK = true;
  } else {
    Serial.printf("Firebase signup failed: %s\n", config.signer.signupError.message.c_str());
  }


  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}


//--------------------------------------------------
void sendPumpCommand(String command, String reason) {
  if (!mqttClient.connected()) return;


  // Always send for manual/schedule modes
  if (reason.startsWith("Manual") || reason.startsWith("Schedule")) {
    mqttClient.publish(controlCommandTopic, command.c_str());
    logPumpCommandToFirebase(command, reason);
    Serial.println("Sent pump command: " + command + " (" + reason + ")");
    return;
  }


  // Only suppress duplicates for auto mode
  if (command != lastPumpCommand) {
    mqttClient.publish(controlCommandTopic, command.c_str());
    logPumpCommandToFirebase(command, reason);
    lastPumpCommand = command;
    Serial.println("Sent pump command: " + command + " (" + reason + ")");
  } else {
    Serial.println("Skipped sending duplicate command in Auto Mode: " + command);
  }
}




//--------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 Soil Moisture Sensor Starting ===");


  setup_wifi();
  configTime(gmtOffset, daylightOffset, ntpServer);
  Serial.println("Time synchronized with NTP server");


  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);


  setupFirebase();
 
  Serial.println("System ready - Starting monitoring loop...");
}


//--------------------------------------------------
void loop() {
  // Reconnect WiFi if needed
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    setup_wifi();
    setupFirebase();
  }


  // Reconnect MQTT if needed
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }


  mqttClient.loop();


  if (millis() - lastSend > interval) {
    lastSend = millis();


    // Read soil moisture sensor
    int moisture = analogRead(soilPin);
    String timeNow = getTimeNow();


    Serial.print("Moisture reading: ");
    Serial.println(moisture);


    // Publish moisture data to MQTT
    if (mqttClient.connected()) {
      mqttClient.publish(moistureTopic, String(moisture).c_str());
      Serial.println("Moisture data published to MQTT");
    }


    // Log sensor data to Firebase
    if (Firebase.ready()) {
      FirebaseJson soilLog;
      soilLog.set("value", moisture);
      soilLog.set("timestamp", timeNow);
      Firebase.RTDB.pushJSON(&fbdo, "/soil_log", &soilLog);


      // Get control mode from Firebase
      String mode = "auto";
      if (Firebase.RTDB.getString(&fbdo, "/control/pump_mode/mode")) {
        mode = fbdo.stringData();
      }


      mode.toLowerCase();
      Serial.println("Current mode: " + mode);


      // Get current time for schedule mode
      time_t now;
      struct tm timeinfo;
      time(&now);
      localtime_r(&now, &timeinfo);
      float currentHour = timeinfo.tm_hour + (timeinfo.tm_min / 60.0);


      // Determine pump command based on mode
      if (mode == "manual") {
        String manualState = "OFF";
        if (Firebase.RTDB.getString(&fbdo, "/control/pump_mode/manual_state")) {
          manualState = fbdo.stringData();
        }
        sendPumpCommand(manualState, "Manual Mode");


      } else if (mode == "schedule") {
        bool enabled = false;
        float startHour = 7, endHour = 7;
        float startMin = 0, endMin = 10;


        Firebase.RTDB.getBool(&fbdo, "/control/pump_mode/schedule/enabled") ? enabled = fbdo.boolData() : false;
        Firebase.RTDB.getFloat(&fbdo, "/control/pump_mode/schedule/start_hour") ? startHour = fbdo.floatData() : false;
        Firebase.RTDB.getFloat(&fbdo, "/control/pump_mode/schedule/start_min") ? startMin = fbdo.floatData() : false;
        Firebase.RTDB.getFloat(&fbdo, "/control/pump_mode/schedule/end_hour") ? endHour = fbdo.floatData() : false;
        Firebase.RTDB.getFloat(&fbdo, "/control/pump_mode/schedule/end_min") ? endMin = fbdo.floatData() : false;


        float scheduleStart = startHour + (startMin / 60.0);
        float scheduleEnd = endHour + (endMin / 60.0);


        Serial.printf("Schedule: %02.0f:%02.0f - %02.0f:%02.0f, Current: %02.0f:%02.0f\n",
                     startHour, startMin, endHour, endMin,
                     (float)timeinfo.tm_hour, (float)timeinfo.tm_min);


        if (enabled && currentHour >= scheduleStart && currentHour <= scheduleEnd) {
          sendPumpCommand("ON", "Schedule Mode - Active Period");
        } else {
          sendPumpCommand("OFF", "Schedule Mode - Outside Period");
        }


      } else if (mode == "auto") {
        int dryThreshold = 1000;
        int wetThreshold = 1300;


        Firebase.RTDB.getInt(&fbdo, "/control/pump_mode/auto/dry_threshold") ? dryThreshold = fbdo.intData() : false;
        Firebase.RTDB.getInt(&fbdo, "/control/pump_mode/auto/wet_threshold") ? wetThreshold = fbdo.intData() : false;


        Serial.printf("Auto thresholds - Dry: %d, Wet: %d\n", dryThreshold, wetThreshold);


        if (moisture < dryThreshold) {
          sendPumpCommand("ON", "Auto Mode - Soil Too Dry (" + String(moisture) + " < " + String(dryThreshold) + ")");
        } else if (moisture > wetThreshold) {
          sendPumpCommand("OFF", "Auto Mode - Soil Wet Enough (" + String(moisture) + " > " + String(wetThreshold) + ")");
        }
        // If moisture is between thresholds, don't send command (maintain current state)
      }
    }


    delay(100);
  }
}
