import paho.mqtt.client as mqtt
import json
import time
import threading
from datetime import datetime

BROKER             = "192.168.X.X"   # <-- IP des Broker-Laptops eintragen
PORT               = 1883
MAC                = "AA:BB:CC:DD:EE:FF"  # <-- eigene MAC-Adresse eintragen
HEARTBEAT_INTERVAL = 30  # Sekunden

# { notification_id: {"message": str, "group": str, "task_status": str} }
_pending_tasks = {}
_pending_lock  = threading.Lock()

_mqtt_client = None


def send_heartbeat(client):
    while True:
        payload = json.dumps({"mac": MAC})
        client.publish("smartserve/heartbeat", payload, qos=1)
        time.sleep(HEARTBEAT_INTERVAL)


def on_connect(client, userdata, flags, rc):
    global _mqtt_client
    if rc == 0:
        _mqtt_client = client
        print(f"[MQTT] Verbunden mit {BROKER}:{PORT}")
        client.subscribe("smartserve/#")
        client.subscribe(f"smartserve/device/{MAC}")   # direct feedback channel
        threading.Thread(target=send_heartbeat, args=(client,), daemon=True).start()
        threading.Thread(target=input_loop, daemon=True).start()
    else:
        codes = {
            1: "Falsche Protokollversion", 2: "Client-ID abgelehnt",
            3: "Broker nicht verfügbar",   4: "Falscher Benutzername/Passwort",
            5: "Nicht autorisiert",
        }
        print(f"[FEHLER] Verbindung abgelehnt: {codes.get(rc, f'rc={rc}')}")


def on_disconnect(client, userdata, rc):
    if rc != 0:
        print("[MQTT] Unerwartete Trennung.")


def on_message(client, userdata, msg):
    if msg.topic == "smartserve/heartbeat":
        return

    ts = datetime.now().strftime("%H:%M:%S")

    try:
        data = json.loads(msg.payload.decode())
    except Exception:
        print(f"[{ts}] {msg.topic}: {msg.payload.decode()}")
        return

    msg_type        = data.get("type")
    notification_id = data.get("notification_id")

    # ── New task arriving ────────────────────────────────────────────────────
    if msg_type == "task":
        group   = data.get("group_name", "?")
        message = data.get("message", "")
        print(f"\n[{ts}] ╔═ NEUE AUFGABE #{notification_id} [{group}]")
        print(f"       ║  {message}")
        print(f"       ║  Status: unassigned")
        print(f"       ╚═ a {notification_id}  akzeptieren  |  d {notification_id}  ablehnen")
        print("> ", end="", flush=True)
        with _pending_lock:
            _pending_tasks[notification_id] = {
                "message": message, "group": group, "task_status": "unassigned"
            }

    # ── Status updates ───────────────────────────────────────────────────────
    elif msg_type == "task_status":
        status      = data.get("status")       # the event that triggered this
        task_status = data.get("task_status")  # the new DB state
        by_mac      = (data.get("by_mac") or "").upper()
        by_name     = data.get("by_name", by_mac)
        message     = data.get("message", "")
        is_mine     = by_mac == MAC.upper()

        # Update local task_status
        with _pending_lock:
            if notification_id in _pending_tasks:
                _pending_tasks[notification_id]["task_status"] = task_status or \
                    _pending_tasks[notification_id]["task_status"]

        if status == "accepted":
            if is_mine:
                print(f"\n[{ts}] ✔ Du hast Aufgabe #{notification_id} übernommen → ongoing")
                print(f"       Tippe  done {notification_id}  wenn erledigt")
            else:
                print(f"\n[{ts}] ℹ Aufgabe #{notification_id} von '{by_name}' übernommen → ongoing")
                print(f"       (kein Handlungsbedarf)")
                # Remove from pending — someone else has it
                with _pending_lock:
                    _pending_tasks.pop(notification_id, None)

        elif status == "declined":
            if is_mine:
                print(f"\n[{ts}] ✘ Du hast Aufgabe #{notification_id} abgelehnt")
                with _pending_lock:
                    _pending_tasks.pop(notification_id, None)
            else:
                print(f"\n[{ts}] ℹ '{by_name}' hat Aufgabe #{notification_id} abgelehnt")

        elif status == "done":
            if is_mine:
                print(f"\n[{ts}] ✔✔ Aufgabe #{notification_id} als erledigt markiert → done")
            else:
                print(f"\n[{ts}] ℹ Aufgabe #{notification_id} von '{by_name}' abgeschlossen → done")
            with _pending_lock:
                _pending_tasks.pop(notification_id, None)

        elif status == "already_yours":
            # Direct feedback: this device already accepted this task
            print(f"\n[{ts}] ⚠ Aufgabe #{notification_id} ist bereits dir zugewiesen!")
            print(f"       Tippe  done {notification_id}  wenn du fertig bist")

        elif status == "accepted_by_other":
            # Direct feedback: someone else has it (by_name = their name)
            print(f"\n[{ts}] ✖ Aufgabe #{notification_id} wurde bereits von '{by_name}' übernommen")
            with _pending_lock:
                _pending_tasks.pop(notification_id, None)

        elif status == "decline_blocked":
            print(f"\n[{ts}] ⚠ Du kannst Aufgabe #{notification_id} nicht mehr ablehnen — du hast sie bereits akzeptiert")
            print(f"       Tippe  done {notification_id}  wenn du fertig bist")

        elif status == "done_not_yours":
            print(f"\n[{ts}] ✖ Aufgabe #{notification_id} ist dir nicht zugewiesen — du kannst sie nicht abschließen")

        print("> ", end="", flush=True)


def send_ack(notification_id, action):
    if _mqtt_client is None:
        print("[FEHLER] Nicht verbunden.")
        return
    payload = json.dumps({"mac": MAC, "action": action})
    _mqtt_client.publish(f"smartserve/ack/{notification_id}", payload, qos=1)


def input_loop():
    print("\nBefehle:  a <id>  akzeptieren  |  d <id>  ablehnen  |  done <id>  erledigt  |  l  auflisten\n")
    while True:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if not line:
            continue

        parts = line.split()
        cmd   = parts[0].lower()

        if cmd == "l":
            with _pending_lock:
                if not _pending_tasks:
                    print("  Keine Aufgaben.")
                else:
                    for nid, info in _pending_tasks.items():
                        print(f"  #{nid} [{info['task_status']}] [{info['group']}] {info['message']}")
            continue

        if cmd in ("a", "d", "done") and len(parts) == 2:
            try:
                nid = int(parts[1])
                if cmd == "a":
                    action = "accept"
                elif cmd == "d":
                    action = "decline"
                else:
                    action = "done"
                send_ack(nid, action)
            except ValueError:
                print("  Ungültige ID.")
            continue

        print("  Unbekannter Befehl. Beispiele:  a 3  |  d 3  |  done 3  |  l")


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
