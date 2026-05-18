import paho.mqtt.client as mqtt
import sys
from datetime import datetime

BROKER = "192.168.X.X"   # <-- IP des Brokers (Arch-Laptop) hier eintragen
PORT   = 1883
TOPIC  = "smartserve/#"


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[MQTT] Verbunden mit {BROKER}:{PORT}")
        print(f"[MQTT] Lausche auf Topic: {TOPIC}\n")
        client.subscribe(TOPIC)
    else:
        codes = {
            1: "Falsche Protokollversion",
            2: "Client-ID abgelehnt",
            3: "Broker nicht verfügbar",
            4: "Falscher Benutzername/Passwort",
            5: "Nicht autorisiert",
        }
        print(f"[FEHLER] Verbindung abgelehnt: {codes.get(rc, f'rc={rc}')}")
        sys.exit(1)


def on_disconnect(client, userdata, rc):
    if rc != 0:
        print("[MQTT] Unerwartete Trennung. Versuche neu zu verbinden...")
    else:
        print("[MQTT] Verbindung getrennt.")


def on_message(client, userdata, msg):
    timestamp = datetime.now().strftime("%H:%M:%S")
    payload   = msg.payload.decode(errors="replace")
    print(f"[{timestamp}] {msg.topic}: {payload}")


client = mqtt.Client()
client.on_connect    = on_connect
client.on_disconnect = on_disconnect
client.on_message    = on_message

print(f"[MQTT] Verbinde mit {BROKER}:{PORT} ...")

try:
    client.connect(BROKER, PORT, keepalive=60)
except Exception as e:
    print(f"[FEHLER] Broker nicht erreichbar: {e}")
    print("Tipps:")
    print("  - Broker-IP in receiver.py korrekt?")
    print("  - Beide im selben Netz (Hotspot)?")
    print("  - Mosquitto läuft auf dem Broker-Laptop?")
    print("  - Port 1883 nicht durch Firewall geblockt?")
    sys.exit(1)

try:
    client.loop_forever()
except KeyboardInterrupt:
    client.disconnect()
    print("\n[MQTT] Beendet.")
