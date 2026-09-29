# SmartServe – Lokales Push-Benachrichtigungssystem

Ein Admin sendet über ein Web-Dashboard Nachrichten/Aufgaben an selbstgebaute
Smartwatches. Übertragung per MQTT, alles im lokalen Netz, ohne Cloud.

> **Neu hier? → [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md)** erklärt in einer Seite,
> welche Komponente wo läuft und was zu was gehört.
> **Pi aufsetzen? → [PI_SETUP.md](PI_SETUP.md)** (Schritt für Schritt).

---

## Komponenten
| Komponente | Läuft auf | Ordner |
|---|---|---|
| MQTT-Broker (Mosquitto) | Raspberry Pi | System-Paket |
| Backend (Flask + REST + MQTT) | Raspberry Pi | `backend/` |
| Admin-Web-UI | Browser | `frontend/` |
| Smartwatch-Firmware | ESP32 WROOM-32 + GC9A01-Display | `watch-firmware/` |
| Test-/Emulator-Clients | Laptop/Pi (manuell) | `test-clients/` |

Der Pi ist zugleich WLAN-Access-Point **"SmartServe"** (Pi = `10.42.0.1`).
Admin-Dashboard: `http://10.42.0.1:5000`.

## Features
- **Geräteverwaltung** — Registrierung per MAC, Online/Offline via Heartbeat
- **Gruppen** — Geräte zuordnen, umbenennen, löschen
- **Benachrichtigungen** — Nachricht an Gruppe → sofort per MQTT verteilt
- **Aufgaben-Workflow** — Clients können accept / decline / done zurückmelden
- **Dashboard** — Statistik + letzte Benachrichtigungen
- **REST-API** — kompletter JSON-API
- **SQLite** — nullkonfig, wird beim ersten Start erzeugt

## Tech-Stack
| Layer | Technologie |
|---|---|
| Backend | Python 3, Flask, Flask-SQLAlchemy, Flask-CORS, paho-mqtt 2.x |
| DB | SQLite |
| Frontend | Vanilla HTML/CSS/JS, IBM Plex |
| Broker | Mosquitto |
| Watch | ESP32 WROOM-32, Arduino/PlatformIO, TFT_eSPI (GC9A01), PubSubClient, ArduinoJson |
| Deployment | Raspberry Pi (systemd + gunicorn), Pi als WLAN-AP |

## Projektstruktur
```
SmartServe/
├── backend/              # Server (läuft am Pi)
│   ├── app.py            #   Flask-App-Factory, DB-Init, MQTT-Connect
│   ├── models.py         #   Device, Group, Notification (SQLAlchemy)
│   ├── mqtt.py           #   Broker-Anbindung des Backends (publish + heartbeat/ack)
│   └── routes/{admin,api}.py
├── frontend/             # Admin-Dashboard (Browser)
│   ├── templates/        #   dashboard/devices/groups/notifications.html
│   └── static/           #   style.css + script.js pro Seite
├── watch-firmware/       # ESP32-Firmware (C++/PlatformIO) — siehe eigenes README
├── test-clients/         # PC-seitige Test-/Emulatorskripte
│   ├── device_client.py  #   voller Watch-Emulator (accept/decline/done + heartbeat)
│   ├── sender.py         #   CLI-Testsender
│   └── receiver.py       #   einfacher Subscriber (Mitlesen)
├── PI_SETUP.md           # Pi einrichten (Broker + Backend + AP)
├── PROJECT_OVERVIEW.md   # Architektur-Landkarte
├── requirements.txt · CLAUDE.md · README.md
```

## Lokale Entwicklung (ohne Pi)
```bash
python -m venv venv && source venv/bin/activate
pip install -r requirements.txt
# Broker lokal:
echo -e "listener 1883\nallow_anonymous true" > /tmp/mqtt.conf && mosquitto -c /tmp/mqtt.conf -v
# Backend:
cd backend && python app.py        # http://localhost:5000
```
Produktiv-Deployment am Pi (gunicorn + systemd + AP): siehe **PI_SETUP.md**.

## Clients: Watch + Emulator
- **Echte Watch:** `watch-firmware/` auf einen ESP32 flashen (eigenes README dort).
- **Emulator (ohne Hardware):** `test-clients/device_client.py` — bildet die Watch nach
  (subscribe + Heartbeat + accept/decline/done). Vor Start `BROKER` (Pi-IP bzw.
  `localhost` am Pi) und `MAC` setzen; Gerät mit dieser MAC vorher im `/devices`
  registrieren, sonst wird der Heartbeat ignoriert.

## Pages
| Route | Beschreibung |
|---|---|
| `/dashboard` | Statistik + letzte Benachrichtigungen |
| `/devices` | Geräte registrieren, Online/Offline |
| `/groups` | Gruppen + Gerätezuweisung |
| `/notifications` | Nachricht senden + Verlauf |

## REST API
### Devices
| Method | Endpoint | Body |
|---|---|---|
| GET | `/api/devices` | — |
| POST | `/api/devices` | `{name, mac}` |
| DELETE | `/api/devices/:id` | — |
### Groups
| Method | Endpoint | Body |
|---|---|---|
| GET | `/api/groups` | — |
| POST | `/api/groups` | `{name}` |
| PUT | `/api/groups/:id` | `{name}` |
| DELETE | `/api/groups/:id` | — |
| PUT | `/api/groups/:id/devices` | `{device_ids:[…]}` |
### Notifications
| Method | Endpoint | Body |
|---|---|---|
| GET | `/api/notifications` | — |
| POST | `/api/notifications` | `{message, group_id}` (+ MQTT publish) |

## MQTT-Topics
| Topic | Richtung | Zweck |
|---|---|---|
| `smartserve/groups/{id}` | Backend → Clients | neue Aufgabe / Statusupdate |
| `smartserve/device/{MAC}` | Backend → ein Client | direktes Feedback |
| `smartserve/heartbeat` | Client → Backend | „online" (alle 30s) |
| `smartserve/ack/{id}` | Client → Backend | accept / decline / done |

## Datenmodell
```
Device        id, name, mac (unique), status, last_seen
Group         id, name (unique)
device_group  device_id ↔ group_id (many-to-many)
Notification  id, message, timestamp, group_id
```

## Notes
- MAC-Adressen uppercase gespeichert, `:` und `-` akzeptiert
- Gerät online, wenn Heartbeat < 60 s
- Broker beim Start nicht da = nur Warnung, App läuft weiter
- `send_from_directory` für Frontend-Seiten (kein Jinja2)
- paho-mqtt 2.x braucht `CallbackAPIVersion.VERSION1`
- `smartserve.db` ist gitignored
