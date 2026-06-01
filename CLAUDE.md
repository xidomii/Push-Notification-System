# SmartServe — CLAUDE.md

## Projekt
Push-Notification-Admin-System. Admin sendet Nachrichten über Web-UI an Gerätegruppen. Backend persistiert in SQLite, publiziert via MQTT an verbundene Geräte.

## Stack
- Backend: Python 3, Flask, Flask-SQLAlchemy, Flask-CORS, paho-mqtt
- DB: SQLite (`backend/smartserve.db`, gitignored)
- Frontend: Vanilla HTML/CSS/JS, IBM Plex Mono/Sans, kein Bootstrap, kein Framework
- Broker: Mosquitto (lokal auf diesem Laptop)

## Starten

```bash
# 1. MQTT Broker
echo -e "listener 1883\nallow_anonymous true" > /tmp/mqtt.conf
mosquitto -c /tmp/mqtt.conf -v

# 2. Backend
cd backend
python app.py          # läuft auf http://localhost:5000
```

## Projektstruktur
```
SmartServe/
├── backend/
│   ├── app.py          # App-Factory, DB-Init, MQTT connect on startup
│   ├── models.py       # Device, Group, Notification (SQLAlchemy)
│   ├── mqtt.py         # paho-mqtt Client: connect() + publish()
│   └── routes/
│       ├── admin.py    # Seiten-Routen via send_from_directory
│       └── api.py      # REST API /api/*
├── frontend/
│   ├── templates/      # Standalone HTML (kein Jinja2 extends)
│   │   ├── dashboard.html
│   │   ├── devices.html
│   │   ├── groups.html
│   │   └── notifications.html
│   └── static/         # CSS + JS pro Seite
│       ├── dashboard/
│       ├── devices/
│       ├── groups/
│       └── notifications/
├── mqtt/
│   ├── sender.py       # CLI-Testsender (localhost)
│   └── receiver.py     # Client für Kollege (Windows), IP eintragen
└── requirements.txt
```

## Seiten
| Route | Datei | Funktion |
|-------|-------|----------|
| `/dashboard` | dashboard.html | Stat-Cards + letzte Notifications |
| `/devices` | devices.html | Geräte registrieren/löschen |
| `/groups` | groups.html | Gruppen + Gerätezuweisung |
| `/notifications` | notifications.html | Nachricht senden + Verlauf |

## REST API
| Method | Endpoint | Body |
|--------|----------|------|
| GET | `/api/devices` | — |
| POST | `/api/devices` | `{name, mac}` |
| DELETE | `/api/devices/:id` | — |
| GET | `/api/groups` | — |
| POST | `/api/groups` | `{name}` |
| PUT | `/api/groups/:id` | `{name}` |
| DELETE | `/api/groups/:id` | — |
| PUT | `/api/groups/:id/devices` | `{device_ids:[]}` |
| GET | `/api/notifications` | — |
| POST | `/api/notifications` | `{message, group_id}` |

## MQTT
- Broker läuft auf diesem Laptop (localhost:1883)
- Backend publiziert nach jedem `POST /api/notifications`
- Topic: `smartserve/groups/{group_id}`
- Payload: `{"group_id", "group_name", "message", "timestamp"}`
- Kollege subscribt mit `mqtt/receiver.py` auf `smartserve/#`
- `BROKER`-IP in `mqtt/receiver.py` anpassen (Hotspot-IP)

## DB-Modell
```
Device:       id, name, mac (unique), status, last_seen
Group:        id, name (unique)
device_group: device_id FK, group_id FK  (many-to-many)
Notification: id, message, timestamp, group_id FK
```

## Bekannte Eigenheiten
- `Group.to_dict()` gibt `device_ids` (int[]) zurück, nicht Objekte
- MQTT-Fehler bei Start = OK, Broker einfach nicht gestartet
- `send_from_directory` statt `render_template` — kein Jinja2 in frontend/templates
- paho-mqtt 2.x: `CallbackAPIVersion.VERSION1` nötig beim Client-Konstruktor
