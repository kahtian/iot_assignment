# IoT Assignment - Smart Home Plant Care System 🌱

## Modules:

### 1.💡 Smart LED Grow Light Module (`smart_led.ino`)

This Arduino sketch, running on an ESP32, controls a smart LED grow light. It features:
- **WiFi Connectivity**: Connects to the local network.
- **Firebase Integration**: Communicates with Google Firebase for remote control and data logging.
- **Operating Modes**:
    - **Auto Mode**: Adjusts LED brightness dynamically based on ambient light detected by an LDR sensor.
    - **Manual Mode**: Allows for remote control of LED brightness via Firebase.
    - **Scheduled Mode**: Supports time-based scheduling with customizable LED brightness.
- **Data Logging**: Sends LDR readings, voltage, lux, and brightness percentage to Firebase.

### 2. 💧 Smart Watering System (ESP32 - `soul_esp` & Raspberry Pi - `soil_pi.py`)

This module combines an ESP32-based soil moisture sensor with a Raspberry Pi-controlled pump to automate plant watering.

- **ESP32 (`soul_esp`)**:
    - **Soil Moisture Sensing**: Reads and logs soil moisture data to Firebase.
    - **WiFi & Firebase Integration**: Connects to WiFi, logs data to Firebase, and retrieves pump control settings.
    - **MQTT Publisher**: Publishes soil moisture readings to `esp32/soil_moisture` and sends "ON" or "OFF" commands for the pump to `esp32/control_command`.
    - **Flexible Pump Control**: Supports manual, schedule-based, and automatic (threshold-based) pump operation, with settings configurable via Firebase.

- **Raspberry Pi (`soil_pi.py`)**:
    - **MQTT Subscriber**: Subscribes to the `esp32/control_command` topic.
    - **GPIO Pump Control**: Activates or deactivates the water pump via a relay connected to its GPIO, based on commands received from the ESP32.

### 3. 🪣 Water Level Monitoring Module (`water_lvl_monitoring.ino`)

This Arduino sketch, also running on an ESP32, is responsible for monitoring the water reservoir level:
- **Ultrasonic Sensor**: Uses an ultrasonic sensor to measure the distance to the water surface.
- **Volume Calculation**: Translates distance readings into water volume (mL) and percentage using a multi-point calibration lookup table.
- **Firebase Integration**: Publishes water level data (distance, volume, percentage) to Firebase for logging and real-time monitoring.
- **Alerting System**:
    - **Thresholds**: Monitors water levels against configurable low and high thresholds (fetched from Firebase).
    - **Buzzer**: Activates a buzzer for audible alerts if water levels fall outside the acceptable range, with a "silent mode" option configurable via Firebase.

### 4. 🌡️ Smart Fan and Window (?) Module (`temp_humi.ino`)

This Arduino sketch, running on an ESP32, manages ambient temperature and humidity for a plant enclosure:
- **DHT Sensor**: Uses a DHT22 sensor to read temperature and humidity.
- **Device Control**: Automatically adjusts a fan's speed and a window's opening based on temperature readings.
    - **Fan Control**: A fan connected to the ESP32's PWM pin is controlled by a duty cycle. The fan speed increases as the temperature rises above 27.0°C.
    - **Window Control**: A servo motor attached to pin 12 controls a window. The window begins to open at 27.0°C and is fully open at 32.0°C.
- **Firebase Integration**: Connects to WiFi and sends real-time data to Firebase.
    - **Data Logging**: Sends temperature, humidity, fan speed (PWM), and window angle data to a fixed path for the latest reading (/temp_humi_reading) and a historical log with a unique timestamp (/temp_humi_logs/).
