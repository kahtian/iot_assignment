#include <WiFi.h>
#include <DHT.h>
#include <ESP32Servo.h>

#define DHT_TYPE DHT22
#define DHT_PIN 21
#define LEDWIFI_PIN 25
#define FAN_PIN 5
#define SERVO_PIN 12

DHT dht(DHT_PIN, DHT_TYPE);
Servo windowServo;
bool ledwifi_state;
int currentServoAngle = 0; // Track current servo position

const char* wifi_ssid = "B100M-T8";
const char* wifi_password = "12345678";

// PWM config for fan
const int pwmFreq = 25000;
const int pwmResolution = 8;

// Temperature and servo angle mapping
const float MIN_TEMP = 27.0;    // Temperature threshold to start opening window
const float MAX_TEMP = 32.0;    // Temperature for maximum window opening
const int MIN_ANGLE = 0;        // Minimum servo angle (closed)
const int MAX_ANGLE = 180;      // Maximum servo angle (fully open)

WiFiClient espClient;

void setup() {
  Serial.begin(115200);
  pinMode(LEDWIFI_PIN, OUTPUT);

  // Setup servo motor FIRST
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
 
  Serial.println("Enhanced Temperature Control Started!");
  Serial.println("Window will open proportionally with temperature increase");
  
  // Test servo
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
  delay(2000);
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

  // Calculate fan speed (existing logic)
  int fanSpeed = 0;
  if (t >= MIN_TEMP) {
    fanSpeed = map(t * 10, MIN_TEMP * 10, 30 * 10, 100, 255); // Using *10 for better precision
    fanSpeed = constrain(fanSpeed, 100, 255);
  }

  // Calculate servo angle proportionally
  int targetServoAngle = 0;
  if (t >= MIN_TEMP) {
    // Map temperature to servo angle proportionally
    targetServoAngle = map(t * 10, MIN_TEMP * 10, MAX_TEMP * 10, MIN_ANGLE, MAX_ANGLE);
    targetServoAngle = constrain(targetServoAngle, MIN_ANGLE, MAX_ANGLE);
  } else {
    targetServoAngle = MIN_ANGLE; // Close window when temperature is below threshold
  }

  // Move servo smoothly to target angle
  moveServoToAngle(targetServoAngle);

  // Set fan speed
  ledcWrite(FAN_PIN, fanSpeed);

  // Display status
  Serial.print("Fan Speed (PWM): ");
  Serial.println(fanSpeed);
  Serial.print("Window Angle: ");
  Serial.print(currentServoAngle);
  Serial.print("° (Target: ");
  Serial.print(targetServoAngle);
  Serial.println("°)");
  
  // Show percentage of window opening
  int windowPercentage = map(currentServoAngle, MIN_ANGLE, MAX_ANGLE, 0, 100);
  Serial.print("Window Opening: ");
  Serial.print(windowPercentage);
  Serial.println("%");
  Serial.println("========================");
}

void moveServoToAngle(int targetAngle) {
  if (currentServoAngle == targetAngle) {
    return; // Already at target position
  }

  Serial.print("Moving window from ");
  Serial.print(currentServoAngle);
  Serial.print("° to ");
  Serial.print(targetAngle);
  Serial.println("°");

  // Move servo smoothly in steps
  int step = (targetAngle > currentServoAngle) ? 5 : -5; // 5-degree steps
  
  while (currentServoAngle != targetAngle) {
    if (abs(targetAngle - currentServoAngle) < abs(step)) {
      currentServoAngle = targetAngle; // Final step
    } else {
      currentServoAngle += step;
    }
    
    windowServo.write(currentServoAngle);
    Serial.print("Moving to: ");
    Serial.println(currentServoAngle);
    delay(50); // Small delay for smooth movement
  }
  
  Serial.println("Window position updated!");
}

void testServo() {
  Serial.println("=== SERVO TEST START ===");
  Serial.println("Testing proportional movement...");
  
  // Test gradual movement
  for (int angle = 0; angle <= 180; angle += 30) {
    Serial.print("Moving to: ");
    Serial.print(angle);
    Serial.println("°");
    
    moveServoToAngle(angle);
    delay(1000);
  }
  
  // Return to start position
  moveServoToAngle(0);
  
  Serial.println("=== SERVO TEST END ===");
  delay(2000);
}
