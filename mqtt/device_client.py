import paho.mqtt.client as mqtt
import json
import time
import threading
from datetime import datetime

BROKER = "192.168.X.X"   # <-- IP des Broker-Laptops eintragen
PORT   = 1883
MAC    = "AA:BB:CC:DD:EE:FF"  # <-- eigene MAC-Adresse eintragen
HEARTBEAT_INTERVAL = 30  # Sekunden


def send_heartbeat(client):
    while True:
        payload = json.dumps({"mac": MAC})
        client.publish("smartserve/heartbeat", payload, qos=1)
        print(f"[{datetime.now().strftime('%H:%M:%S')}] Heartbeat gesendet ({MAC})")
        time.sleep(HEARTBEAT_INTERVAL)


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[MQTT] Verbunden mit {BROKER}:{PORT}")
        client.subscribe("smartserve/#")
        t = threading.Thread(target=send_heartbeat, args=(client,), daemon=True)
        t.start()
    else:
        codes = {
            1: "Falsche Protokollversion",
            2: "Client-ID abgelehnt",
            3: "Broker nicht verfügbar",
            4: "Falscher Benutzername/Passwort",
            5: "Nicht autorisiert",
        }
        print(f"[FEHLER] Verbindung abgelehnt: {codes.get(rc, f'rc={rc}')}")


def on_disconnect(client, userdata, rc):
    if rc != 0:
        print("[MQTT] Unerwartete Trennung.")


def on_message(client, userdata, msg):
    if msg.topic == "smartserve/heartbeat":
        return
    timestamp = datetime.now().strftime("%H:%M:%S")
    try:
        data  = json.loads(msg.payload.decode())
        group = data.get("group_name", "?")
        text  = data.get("message", "")
        print(f"[{timestamp}] Nachricht von Gruppe '{group}': {text}")
    except Exception:
        print(f"[{timestamp}] {msg.topic}: {msg.payload.decode()}")


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
client.on_connect    = on_connect
client.on_disconnect = on_disconnect
client.on_message    = on_message

print(f"[MQTT] Verbinde mit {BROKER}:{PORT} ...")
try:
    client.connect(BROKER, PORT, keepalive=60)
    client.loop_forever()
except KeyboardInterrupt:
    client.disconnect()
    print("\n[MQTT] Beendet.")
