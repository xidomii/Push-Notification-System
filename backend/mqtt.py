import paho.mqtt.client as mqtt
import json
from datetime import datetime, timezone

BROKER = "localhost"
PORT   = 1883

_client    = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
_connected = False
_app       = None


def _on_connect(client, userdata, flags, rc):
    global _connected
    if rc == 0:
        _connected = True
        client.subscribe("smartserve/heartbeat")
        print(f"[MQTT] Broker verbunden ({BROKER}:{PORT})")
    else:
        print(f"[MQTT] Verbindung fehlgeschlagen (rc={rc})")


def _on_disconnect(client, userdata, rc):
    global _connected
    _connected = False
    if rc != 0:
        print("[MQTT] Unerwartete Trennung vom Broker.")


def _on_message(client, userdata, msg):
    if msg.topic != "smartserve/heartbeat":
        return
    if not _app:
        return
    try:
        data = json.loads(msg.payload.decode())
        mac  = (data.get("mac") or "").strip().upper().replace("-", ":")
        if not mac:
            return
        with _app.app_context():
            from models import db, Device
            device = Device.query.filter_by(mac=mac).first()
            if device:
                device.status    = "online"
                device.last_seen = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
                db.session.commit()
                print(f"[MQTT] Heartbeat: {device.name} ({mac}) → online")
            else:
                print(f"[MQTT] Heartbeat: unbekannte MAC {mac} — ignoriert")
    except Exception as e:
        print(f"[MQTT] Heartbeat-Fehler: {e}")


_client.on_connect    = _on_connect
_client.on_disconnect = _on_disconnect
_client.on_message    = _on_message


def connect(app):
    global _app
    _app = app
    try:
        _client.connect(BROKER, PORT, keepalive=60)
        _client.loop_start()
    except Exception as e:
        print(f"[MQTT] Broker nicht erreichbar: {e} — MQTT deaktiviert.")


def publish(group_id, group_name, message, timestamp):
    if not _connected:
        print("[MQTT] Nicht verbunden — Nachricht nicht gesendet.")
        return
    payload = json.dumps({
        "group_id":   group_id,
        "group_name": group_name,
        "message":    message,
        "timestamp":  timestamp,
    })
    topic = f"smartserve/groups/{group_id}"
    _client.publish(topic, payload, qos=1)
    print(f"[MQTT] Publiziert → {topic}: {message}")
