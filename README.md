# SmartServe – Push Notification Admin System

Web-based admin dashboard for sending push notifications to device groups via MQTT. Admin registers devices by MAC address, organizes them into groups, and sends messages from the browser — delivered in real-time to all connected clients.

---

## Features

- **Device Management** — Register devices by name and MAC, live online/offline status via heartbeat
- **Group Management** — Create groups, assign devices, rename and delete
- **Notifications** — Send messages to a group from the browser, published to MQTT broker instantly
- **Dashboard** — Stats overview + recent notification history
- **MQTT Integration** — Backend publishes to `smartserve/groups/{id}`, clients subscribe and receive messages
- **Device Heartbeat** — Clients send heartbeat every 30s, backend marks them online/offline automatically
- **REST API** — Full JSON API for all operations
- **SQLite persistence** — Zero-config local DB, auto-created on first run

---

## Tech Stack

| Layer    | Technology                              |
|----------|-----------------------------------------|
| Backend  | Python 3, Flask, Flask-SQLAlchemy       |
| Database | SQLite (via SQLAlchemy ORM)             |
| Frontend | Vanilla HTML / CSS / JS, IBM Plex fonts |
| MQTT     | paho-mqtt 2.x, Mosquitto broker         |
| CORS     | Flask-CORS                              |

---

## Project Structure

```
SmartServe/
├── backend/
│   ├── app.py              # App factory, DB init, MQTT connect on startup
│   ├── models.py           # Device, Group, Notification (SQLAlchemy)
│   ├── mqtt.py             # paho-mqtt client: publish notifications + heartbeat subscriber
│   └── routes/
│       ├── admin.py        # Page routes via send_from_directory
│       └── api.py          # REST API /api/*
├── frontend/
│   ├── templates/          # Standalone HTML pages (no Jinja2)
│   │   ├── dashboard.html
│   │   ├── devices.html
│   │   ├── groups.html
│   │   └── notifications.html
│   └── static/             # CSS + JS per page
│       ├── dashboard/
│       ├── devices/
│       ├── groups/
│       └── notifications/
├── mqtt/
│   ├── device_client.py    # Client script for colleague laptops (heartbeat + receive)
│   ├── sender.py           # CLI test sender
│   └── receiver.py         # Simple subscriber (debug only)
├── requirements.txt
├── CLAUDE.md
└── README.md
```

---

## Setup & Installation

### 1. Clone the repository

```bash
git clone https://github.com/xidomii/Push-Notification-System.git
cd Push-Notification-System
```

### 2. Create and activate a virtual environment

```bash
python -m venv venv
source venv/bin/activate      # Linux / macOS
venv\Scripts\activate         # Windows
```

### 3. Install dependencies

```bash
pip install -r requirements.txt
```

### 4. Start the MQTT broker (broker laptop only)

```bash
echo -e "listener 1883\nallow_anonymous true" > /tmp/mqtt.conf
mosquitto -c /tmp/mqtt.conf -v
```

> Mosquitto must be installed: `sudo pacman -S mosquitto` (Arch) / `winget install mosquitto` (Windows)

### 5. Start the backend

```bash
cd backend
python app.py
```

App starts at **http://localhost:5000**. SQLite DB auto-created on first run. MQTT connects automatically — if broker is down, app still starts with a warning.

---

## Client Setup (colleague laptops)

Each client laptop runs `mqtt/device_client.py`. Edit two lines before starting:

```python
BROKER = "192.168.X.X"        # IP of the broker laptop (same network/hotspot)
MAC    = "AA:BB:CC:DD:EE:FF"  # this laptop's MAC address
```

Find MAC on Windows:
```
ipconfig /all  →  "Physische Adresse"
```

Install and run:
```bash
pip install paho-mqtt
python mqtt/device_client.py
```

The client:
- Connects to the broker
- Sends a heartbeat every 30s → backend marks the device as **online**
- Receives all group notifications in real-time
- After 60s without heartbeat → device shown as **offline**

> The device must be registered in the web UI (`/devices`) with the exact same MAC before the heartbeat is recognized.

---

## Pages

| Route             | Description                                      |
|-------------------|--------------------------------------------------|
| `/dashboard`      | Stats: device/group/notification counts + recent history |
| `/devices`        | Register devices, view online/offline status     |
| `/groups`         | Create groups, assign devices                    |
| `/notifications`  | Send message to a group, view notification log   |

---

## REST API

### Devices

| Method | Endpoint           | Body            | Description           |
|--------|--------------------|-----------------|-----------------------|
| GET    | `/api/devices`     | —               | List all devices      |
| POST   | `/api/devices`     | `{name, mac}`   | Register device       |
| DELETE | `/api/devices/:id` | —               | Delete device         |

### Groups

| Method | Endpoint                  | Body                    | Description            |
|--------|---------------------------|-------------------------|------------------------|
| GET    | `/api/groups`             | —                       | List all groups        |
| POST   | `/api/groups`             | `{name}`                | Create group           |
| PUT    | `/api/groups/:id`         | `{name}`                | Rename group           |
| DELETE | `/api/groups/:id`         | —                       | Delete group           |
| PUT    | `/api/groups/:id/devices` | `{device_ids: [1,2,…]}` | Set device assignments |

### Notifications

| Method | Endpoint              | Body                   | Description                        |
|--------|-----------------------|------------------------|------------------------------------|
| GET    | `/api/notifications`  | —                      | List all notifications             |
| POST   | `/api/notifications`  | `{message, group_id}`  | Send notification (+ MQTT publish) |

### Examples

```bash
# Register a device
curl -X POST http://localhost:5000/api/devices \
  -H "Content-Type: application/json" \
  -d '{"name": "Küchen-Terminal", "mac": "AA:BB:CC:DD:EE:FF"}'

# Send a notification
curl -X POST http://localhost:5000/api/notifications \
  -H "Content-Type: application/json" \
  -d '{"message": "Tisch 3 bitte bedienen", "group_id": 1}'
```

---

## MQTT Architecture

```
[Browser] → POST /api/notifications
                ↓
         [Flask Backend]
                ↓ mqtt.publish()
         [Mosquitto Broker :1883]
                ↓
    smartserve/groups/{group_id}
                ↓
     [device_client.py on each laptop]
```

| Topic                       | Direction        | Payload                                          |
|-----------------------------|------------------|--------------------------------------------------|
| `smartserve/groups/{id}`    | backend → client | `{group_id, group_name, message, timestamp}`     |
| `smartserve/heartbeat`      | client → backend | `{mac}`                                          |

---

## Data Model

```
Device
  id        INTEGER  PRIMARY KEY
  name      TEXT     NOT NULL
  mac       TEXT     UNIQUE NOT NULL   (format: AA:BB:CC:DD:EE:FF)
  status    TEXT     DEFAULT 'offline' (computed dynamically from last_seen)
  last_seen TEXT                       (ISO 8601 UTC timestamp)

Group
  id        INTEGER  PRIMARY KEY
  name      TEXT     UNIQUE NOT NULL

device_group  (many-to-many)
  device_id → Device.id
  group_id  → Group.id

Notification
  id        INTEGER  PRIMARY KEY
  message   TEXT     NOT NULL
  timestamp TEXT     NOT NULL
  group_id  → Group.id
```

---

## Notes

- MAC addresses stored uppercase, both `:` and `-` separators accepted in heartbeat
- Device online if heartbeat received within last 60 seconds
- MQTT broker down on startup = warning only, app still runs
- `send_from_directory` used for all frontend pages — no Jinja2 templating
- paho-mqtt 2.x requires `CallbackAPIVersion.VERSION1` in Client constructor
- DB file (`smartserve.db`) is gitignored
