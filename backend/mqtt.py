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
        client.subscribe("smartserve/ack/#")
        print(f"[MQTT] Broker verbunden ({BROKER}:{PORT})")
    else:
        print(f"[MQTT] Verbindung fehlgeschlagen (rc={rc})")


def _on_disconnect(client, userdata, rc):
    global _connected
    _connected = False
    if rc != 0:
        print("[MQTT] Unerwartete Trennung vom Broker.")


def _on_message(client, userdata, msg):
    if msg.topic == "smartserve/heartbeat":
        _handle_heartbeat(msg)
    elif msg.topic.startswith("smartserve/ack/"):
        _handle_ack(msg)


def _handle_heartbeat(msg):
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
            else:
                print(f"[MQTT] Heartbeat: unbekannte MAC {mac} — ignoriert")
    except Exception as e:
        print(f"[MQTT] Heartbeat-Fehler: {e}")


def _handle_ack(msg):
    # topic: smartserve/ack/<notification_id>
    if not _app:
        return
    try:
        notification_id = int(msg.topic.split("/")[-1])
        data   = json.loads(msg.payload.decode())
        mac    = (data.get("mac") or "").strip().upper().replace("-", ":")
        action = (data.get("action") or "").strip().lower()

        if action not in ("accept", "decline", "done") or not mac:
            return

        with _app.app_context():
            from models import db, Notification, NotificationAck, Device

            notification = Notification.query.get(notification_id)
            if not notification:
                print(f"[MQTT] Ack: unbekannte notification_id {notification_id}")
                return

            device      = Device.query.filter_by(mac=mac).first()
            device_name = device.name if device else mac

            ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

            # ── ACCEPT ────────────────────────────────────────────────────────
            if action == "accept":
                existing_accept = NotificationAck.query.filter_by(
                    notification_id=notification_id, action="accept"
                ).first()

                if existing_accept:
                    if existing_accept.mac == mac:
                        # Same device accepting again
                        _publish_direct(mac, notification, "already_yours", device_name)
                        print(f"[MQTT] Ack: {device_name} hat #{notification_id} doppelt akzeptiert")
                    else:
                        # Different device — task already taken
                        other_device = Device.query.filter_by(mac=existing_accept.mac).first()
                        other_name   = other_device.name if other_device else existing_accept.mac
                        _publish_direct(mac, notification, "accepted_by_other", other_name)
                        print(f"[MQTT] Ack: {device_name} zu spät — {other_name} hat #{notification_id} bereits")
                    return

                # First accept — save and broadcast
                ack = NotificationAck(notification_id=notification_id, mac=mac,
                                      action="accept", timestamp=ts)
                db.session.add(ack)
                notification.task_status = "ongoing"
                db.session.commit()
                print(f"[MQTT] Ack: {device_name} → accept #{notification_id} → ongoing")
                _publish_group_status(notification, mac, "accepted", device_name)

            # ── DECLINE ───────────────────────────────────────────────────────
            elif action == "decline":
                # Don't allow declining if already accepted by someone (including self)
                existing_accept = NotificationAck.query.filter_by(
                    notification_id=notification_id, action="accept"
                ).first()
                if existing_accept and existing_accept.mac == mac:
                    # Can't decline your own accepted task — use 'done' instead
                    _publish_direct(mac, notification, "decline_blocked", device_name)
                    print(f"[MQTT] Ack: {device_name} versuchte abzulehnen, hat aber bereits akzeptiert")
                    return

                ack = NotificationAck.query.filter_by(
                    notification_id=notification_id, mac=mac
                ).first()
                if not ack:
                    ack = NotificationAck(notification_id=notification_id, mac=mac,
                                          action="decline", timestamp=ts)
                    db.session.add(ack)
                else:
                    ack.action, ack.timestamp = "decline", ts
                db.session.commit()
                print(f"[MQTT] Ack: {device_name} → decline #{notification_id}")
                _publish_group_status(notification, mac, "declined", device_name)

            # ── DONE ──────────────────────────────────────────────────────────
            elif action == "done":
                # Only the device that accepted can mark it done
                existing_accept = NotificationAck.query.filter_by(
                    notification_id=notification_id, action="accept"
                ).first()
                if not existing_accept or existing_accept.mac != mac:
                    _publish_direct(mac, notification, "done_not_yours", device_name)
                    print(f"[MQTT] Ack: {device_name} versuchte #{notification_id} abzuschließen — nicht zugewiesen")
                    return

                notification.task_status = "done"
                # Update ack record to "done"
                existing_accept.action    = "done"
                existing_accept.timestamp = ts
                db.session.commit()
                print(f"[MQTT] Ack: {device_name} → done #{notification_id}")
                _publish_group_status(notification, mac, "done", device_name)

    except Exception as e:
        print(f"[MQTT] Ack-Fehler: {e}")


def _publish_group_status(notification, mac, status, device_name=None):
    """Broadcast a task_status update to every client in the group."""
    if not _connected:
        return
    payload = json.dumps({
        "notification_id": notification.id,
        "type":            "task_status",
        "status":          status,
        "task_status":     notification.task_status,
        "by_mac":          mac,
        "by_name":         device_name or mac,
        "message":         notification.message,
    })
    topic = f"smartserve/groups/{notification.group_id}"
    _client.publish(topic, payload, qos=1)
    print(f"[MQTT] Status-Update → {topic}: {status} by {mac}")


def _publish_direct(mac, notification, status, extra_name=None):
    """Send a private feedback message directly to one device via its MAC topic."""
    if not _connected:
        return
    payload = json.dumps({
        "notification_id": notification.id,
        "type":            "task_status",
        "status":          status,       # "already_yours" | "accepted_by_other" | "decline_blocked" | "done_not_yours"
        "task_status":     notification.task_status,
        "by_mac":          mac,
        "by_name":         extra_name or mac,
        "message":         notification.message,
    })
    topic = f"smartserve/device/{mac}"
    _client.publish(topic, payload, qos=1)
    print(f"[MQTT] Direkt → {topic}: {status}")


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


def publish(notification_id, group_id, group_name, message, timestamp):
    if not _connected:
        print("[MQTT] Nicht verbunden — Nachricht nicht gesendet.")
        return
    payload = json.dumps({
        "notification_id": notification_id,
        "group_id":        group_id,
        "group_name":      group_name,
        "message":         message,
        "timestamp":       timestamp,
        "type":            "task",
        "task_status":     "unassigned",
    })
    topic = f"smartserve/groups/{group_id}"
    _client.publish(topic, payload, qos=1)
    print(f"[MQTT] Publiziert → {topic}: {message}")
