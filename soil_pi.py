from paho.mqtt import client as mqtt
from datetime import datetime
import RPi.GPIO as GPIO

# === GPIO Setup ===
RELAY_PIN = 5
GPIO.setmode(GPIO.BCM)
GPIO.setup(RELAY_PIN, GPIO.OUT)
GPIO.output(RELAY_PIN, GPIO.LOW)  # Start OFF

# === MQTT Configuration ===
broker = "localhost"
pump_topic = "esp32/control_command"  # <-- Updated topic from ESP32

# === MQTT Callback Handlers ===
def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT Broker")
    client.subscribe(pump_topic)

def on_message(client, userdata, msg):
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    try:
        command = msg.payload.decode().strip().upper()
        print(f"{now} | Control received from ESP: {command}")

        if command == "ON":
            GPIO.output(RELAY_PIN, GPIO.HIGH)
            print("Relay ON - Water Pump Activated")
        elif command == "OFF":
            GPIO.output(RELAY_PIN, GPIO.LOW)
            print("Relay OFF - Water Pump Deactivated")
        else:
            print(f"{now} | Unknown command: {command}")

    except Exception as e:
        print(f"{now} | Error handling MQTT message: {e}")

# === Main MQTT Client Setup ===
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

try:
    client.connect(broker, 1883, 60)
    print("MQTT loop running...")
    client.loop_forever()

except KeyboardInterrupt:
    print("\nStopping program")
    GPIO.output(RELAY_PIN, GPIO.LOW)
    GPIO.cleanup()

