import paho.mqtt.client as mqtt
import sys

BROKER = "localhost"
PORT   = 1883
TOPIC  = "smartserve/notify"


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[MQTT] Verbunden mit Broker ({BROKER}:{PORT})")
        print(f"[MQTT] Topic: {TOPIC}")
        print("Nachricht eingeben und Enter drücken. 'exit' zum Beenden.\n")
    else:
        print(f"[MQTT] Verbindung fehlgeschlagen (rc={rc})")
        sys.exit(1)


def on_disconnect(client, userdata, rc):
    print("[MQTT] Verbindung getrennt.")


client = mqtt.Client()
client.on_connect    = on_connect
client.on_disconnect = on_disconnect

try:
    client.connect(BROKER, PORT, keepalive=60)
except Exception as e:
    print(f"[FEHLER] Broker nicht erreichbar: {e}")
    print(f"Stelle sicher dass Mosquitto läuft: mosquitto -c /tmp/mqtt.conf -v")
    sys.exit(1)

client.loop_start()

try:
    while True:
        msg = input("> ")
        if msg.strip().lower() == "exit":
            break
        if msg.strip():
            result = client.publish(TOPIC, msg)
            if result.rc == mqtt.MQTT_ERR_SUCCESS:
                print(f"[GESENDET] {msg}")
            else:
                print(f"[FEHLER] Senden fehlgeschlagen (rc={result.rc})")
except KeyboardInterrupt:
    pass
finally:
    client.loop_stop()
    client.disconnect()
    print("[MQTT] Getrennt.")
