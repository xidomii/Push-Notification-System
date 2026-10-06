# SmartServe — CLAUDE.md

## Projekt
Lokales Push-Benachrichtigungssystem. Admin sendet über Web-UI Nachrichten/Aufgaben an
selbstgebaute Smartwatches (ESP32 + Display). Backend persistiert in SQLite, publiziert
via MQTT. Aufgaben-Workflow: accept/decline/done. **Architektur-Landkarte:
[PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md). Pi-Setup: [PI_SETUP.md](PI_SETUP.md).**

## Wo läuft was
- **Raspberry Pi** = Server-Appliance: Mosquitto-Broker + Flask-Backend + SQLite. Hängt als Client am **Handy-Hotspot** (Internet → NTP-Zeit). Kein eigener AP mehr. Pi-IP am Hotspot DHCP-dynamisch → per mDNS `smartserve.local`.
- **ESP32 WROOM-32 + GC9A01** = Smartwatch (`watch-firmware/`), am selben Hotspot; findet Pi per mDNS (Fallback-IP in config.h).
- **Browser** = Admin-Dashboard (`http://smartserve.local:5000`).

## Stack
- Backend: Python 3, Flask, Flask-SQLAlchemy, Flask-CORS, paho-mqtt 2.x
- DB: SQLite (`backend/smartserve.db`, gitignored)
- Frontend: Vanilla HTML/CSS/JS, IBM Plex, kein Framework
- Watch: Arduino/PlatformIO, TFT_eSPI (GC9A01), PubSubClient, ArduinoJson
- Deployment: gunicorn (1 Worker!) + systemd am Pi

## Starten
- **Produktiv (Pi):** siehe PI_SETUP.md (systemd-Service `smartserve.service`, `mosquitto`).
- **Lokal (Dev):** `mosquitto -c /tmp/mqtt.conf -v` + `cd backend && python app.py` (:5000).

## Projektstruktur
```
backend/            app.py, models.py, mqtt.py, routes/{admin,api}.py
frontend/           templates/ + static/ (je Seite css+js)
watch-firmware/     ESP32-Firmware (src/main.cpp, config.h, platformio.ini)
test-clients/       sender.py, receiver.py, device_client.py (Watch-Emulator)
PI_SETUP.md · PROJECT_OVERVIEW.md · README.md · requirements.txt
```
**Nicht verwechseln:** `backend/mqtt.py` = Broker-Anbindung des Servers.
`test-clients/` = lose PC-Testskripte (nicht Teil des Servers).

## Seiten / REST API
| Route | Funktion |            | Endpoint | Body |
|---|---|---|---|---|
| `/dashboard` | Stats + letzte | GET/POST/DELETE | `/api/devices` | `{name, mac}` |
| `/devices` | Geräte | GET/POST/PUT/DELETE | `/api/groups[/:id]` | `{name}` |
| `/groups` | Gruppen | PUT | `/api/groups/:id/devices` | `{device_ids:[]}` |
| `/notifications` | Senden + Verlauf | GET/POST | `/api/notifications` | `{message, group_id}` |

## MQTT
- Broker am Pi (localhost:1883 aus Backend-Sicht). `backend/mqtt.py`: `BROKER="localhost"`.
- Backend publiziert nach `POST /api/notifications` → Topic `smartserve/groups/{group_id}`.
- Topics: `smartserve/groups/{id}` (Backend→Clients), `smartserve/device/{MAC}` (direkt),
  `smartserve/heartbeat` (Client→Backend), `smartserve/ack/{id}` (accept/decline/done),
  `smartserve/time` (Backend→Clients, **retained**, `{epoch}` = lokale Zeit Europe/Vienna,
  alle 10 s; Watch-Uhr, kein NTP am AP nötig).
- Payload `groups/{id}`: `{notification_id, group_id, group_name, message, timestamp, type, task_status}`.
- Watch (`watch-firmware/`) findet den Broker per mDNS (`smartserve.local`, ESP32 `ESPmDNS`), Fallback = feste IP in `config.h` (`MQTT_HOST`).

## DB-Modell
```
Device:       id, name, mac (unique), status, last_seen
Group:        id, name (unique)
device_group: device_id FK, group_id FK  (many-to-many)
Notification: id, message, timestamp, group_id FK, task_status
```

## Bekannte Eigenheiten
- `Group.to_dict()` gibt `device_ids` (int[]) zurück, nicht Objekte
- MQTT-Fehler bei Start = OK (Broker nicht gestartet)
- `send_from_directory` statt `render_template` — kein Jinja2 in frontend/templates
- paho-mqtt 2.x: `CallbackAPIVersion.VERSION1` nötig
- gunicorn nur mit **1 Worker** (sonst doppelte MQTT-Clients/Heartbeat)
- Mosquitto nach Config-Änderung **restart** (nicht nur enable), sonst nur localhost
