#include <WiFi.h>
#include <DHT.h>
#include <ESP32Servo.h>

#define DHT_TYPE DHT22
#define DHT_PIN 21
#define LEDWIFI_PIN 25
#define FAN_PIN 5
#define SERVO_PIN 12  // Changed from 13 to 12

DHT dht(DHT_PIN, DHT_TYPE);
Servo windowServo;
bool ledwifi_state;
bool windowOpen = false; // Track window state

const char* wifi_ssid = "B100M-T8";     // YOUR WIFI SSID
const char* wifi_password = "12345678"; // YOUR WIFI PASSWORD

// PWM config for fan
const int pwmFreq = 25000;   // 25 kHz (above audible range)
const int pwmResolution = 8; // 8-bit (0-255)

WiFiClient espClient;

void setup() {
  Serial.begin(115200);
  pinMode(LEDWIFI_PIN, OUTPUT);

  // *** CRITICAL: Setup servo motor FIRST to avoid timer conflicts ***
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
  windowServo.setPeriodHertz(50);
  windowServo.attach(SERVO_PIN, 500, 2400);
  windowServo.write(0); // Start with window closed
  delay(1000); // Give servo time to initialize properly
  
  // Setup PWM for fan AFTER servo
  ledcAttach(FAN_PIN, pwmFreq, pwmResolution);
  ledcWrite(FAN_PIN, 0); // Ensure fan is OFF at start
  
  setup_wifi();
  dht.begin();
  
  Serial.println("Temperature Humidity Module with Fan and Window Control Started!");
  
  // Test servo immediately after setup
  Serial.println("Testing servo motor...");
  testServo();
}

void setup_wifi() {
  ledwifi_state = 0;
  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi reconnecting...");
    ledwifi_state = !ledwifi_state;
    digitalWrite(LEDWIFI_PIN, ledwifi_state);
    delay(500);
  }

  digitalWrite(LEDWIFI_PIN, HIGH);
  Serial.println("WiFi connected.");
}

void loop() {
  readDHT();
  delay(2000); // read every 2s
}

void readDHT() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }

  Serial.print("Humidity: ");
  Serial.print(h);
  Serial.print("%  Temperature: ");
  Serial.print(t);
  Serial.println("°C");

  int fanSpeed = 0;
  bool shouldOpenWindow = (t >= 27.0);

  Serial.print("Current window state: ");
  Serial.println(windowOpen ? "OPEN" : "CLOSED");
  Serial.print("Should window be open: ");
  Serial.println(shouldOpenWindow ? "YES" : "NO");

  if (shouldOpenWindow) {
    // Temperature is high - turn on fan and open window
    
    // Map temperature (27–30°C) to fan speed (100–255)
    fanSpeed = map(t, 27, 30, 100, 255);
    fanSpeed = constrain(fanSpeed, 100, 255);
    
    // Open window if not already open
    if (!windowOpen) {
      Serial.println(">>> TRIGGERING WINDOW OPEN <<<");
      openWindow();
      windowOpen = true;
    } else {
      Serial.println("Window already open, no action needed");
    }
    
  } else {
    // Temperature is normal - turn off fan and close window
    fanSpeed = 0;
    
    // Close window if currently open
    if (windowOpen) {
      Serial.println(">>> TRIGGERING WINDOW CLOSE <<<");
      closeWindow();
      windowOpen = false;
    } else {
      Serial.println("Window already closed, no action needed");
    }
  }

  ledcWrite(FAN_PIN, fanSpeed);

  Serial.print("Fan Speed (PWM): ");
  Serial.println(fanSpeed);
  Serial.print("Window Status: ");
  Serial.println(windowOpen ? "OPEN" : "CLOSED");
  Serial.println("========================");
}

void testServo() {
  Serial.println("=== SERVO TEST START ===");
  
  // Test basic positions with longer delays
  int testPositions[] = {0, 45, 90, 135, 180, 90, 0};
  
  for (int i = 0; i < 7; i++) {
    Serial.print("Moving to: ");
    Serial.print(testPositions[i]);
    Serial.println("°");
    
    windowServo.write(testPositions[i]);
    delay(1000); // Wait 1 second at each position
    
    Serial.println("Position reached, waiting...");
  }
  
  Serial.println("=== SERVO TEST END ===");
  Serial.println("If servo didn't move, check power supply and wiring!");
  delay(2000);
}

void openWindow() {
  Serial.println("*** OPENING WINDOW FUNCTION CALLED ***");
  Serial.println("Moving servo to open window (0° to 180°)...");
  
  for (int pos = 0; pos <= 180; pos += 10) {
    windowServo.write(pos);
    Serial.print("Opening - Servo position: ");
    Serial.println(pos);
    delay(100);
  }
  Serial.println("*** WINDOW OPENED SUCCESSFULLY ***");
}

void closeWindow() {
  Serial.println("*** CLOSING WINDOW FUNCTION CALLED ***");
  Serial.println("Moving servo to close window (180° to 0°)...");
  
  for (int pos = 180; pos >= 0; pos -= 10) {
    windowServo.write(pos);
    Serial.print("Closing - Servo position: ");
    Serial.println(pos);
    delay(100);
  }
  Serial.println("*** WINDOW CLOSED SUCCESSFULLY ***");
}
