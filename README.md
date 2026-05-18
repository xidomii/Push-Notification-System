# SmartServe – Push Notification Admin System

SmartServe is a web-based admin dashboard for managing devices and notification groups. It allows administrators to register network devices (by name and MAC address), organize them into groups, and manage assignments — all persisted in a local SQLite database.

---

## Features

- **Device Management** — Register, list, and delete devices with name and MAC address
- **Group Management** — Create notification groups, assign/unassign devices, rename and delete groups
- **Live Dashboard** — Overview of total devices and groups
- **REST API** — Full JSON API for all device and group operations
- **SQLite persistence** — Zero-config local database, auto-created on first run

---

## Tech Stack

| Layer    | Technology                        |
|----------|-----------------------------------|
| Backend  | Python 3, Flask, Flask-SQLAlchemy |
| Database | SQLite (via SQLAlchemy ORM)       |
| Frontend | Vanilla HTML / CSS / JavaScript   |
| CORS     | Flask-CORS                        |

---

## Project Structure

```
SmartServe/
├── backend/
│   ├── app.py              # App factory, DB init, blueprint registration
│   ├── models.py           # SQLAlchemy models: Device, Group, device_group
│   ├── routes/
│   │   ├── admin.py        # Page routes (/dashboard, /devices, /groups)
│   │   └── api.py          # REST API routes (/api/devices, /api/groups)
│   └── templates/          # Jinja2 templates (dashboard, notifications)
├── frontend/
│   ├── templates/
│   │   ├── devices.html    # Device management page
│   │   └── groups.html     # Group management page
│   └── static/
│       ├── devices/
│       │   ├── script.js   # Devices page JS (fetch, render, add, delete)
│       │   └── style.css
│       └── groups/
│           ├── script.js   # Groups page JS (fetch, render, create, assign)
│           └── style.css
├── requirements.txt
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

### 4. Run the server

```bash
cd backend
python app.py
```

The app starts at **http://localhost:5000**.  
The SQLite database (`smartserve.db`) is created automatically on first run.

---

## Pages

| Route          | Description                          |
|----------------|--------------------------------------|
| `/dashboard`   | Overview: device count, group count  |
| `/devices`     | Register and manage devices          |
| `/groups`      | Create groups and assign devices     |

---

## REST API

### Devices

| Method | Endpoint              | Description              | Body                      |
|--------|-----------------------|--------------------------|---------------------------|
| GET    | `/api/devices`        | List all devices         | —                         |
| POST   | `/api/devices`        | Register a new device    | `{ name, mac }`           |
| DELETE | `/api/devices/:id`    | Delete a device          | —                         |

### Groups

| Method | Endpoint                      | Description                  | Body                        |
|--------|-------------------------------|------------------------------|-----------------------------|
| GET    | `/api/groups`                 | List all groups              | —                           |
| POST   | `/api/groups`                 | Create a new group           | `{ name }`                  |
| PUT    | `/api/groups/:id`             | Rename a group               | `{ name }`                  |
| DELETE | `/api/groups/:id`             | Delete a group               | —                           |
| PUT    | `/api/groups/:id/devices`     | Set device assignments       | `{ device_ids: [1, 2, …] }` |

### Example: Register a device

```bash
curl -X POST http://localhost:5000/api/devices \
  -H "Content-Type: application/json" \
  -d '{"name": "Küchen-Terminal", "mac": "AA:BB:CC:DD:EE:FF"}'
```

### Example: Create a group

```bash
curl -X POST http://localhost:5000/api/groups \
  -H "Content-Type: application/json" \
  -d '{"name": "Küche"}'
```

### Example: Assign devices to a group

```bash
curl -X PUT http://localhost:5000/api/groups/1/devices \
  -H "Content-Type: application/json" \
  -d '{"device_ids": [1, 2, 3]}'
```

---

## Data Model

```
Device
  id        INTEGER  PRIMARY KEY
  name      TEXT     NOT NULL
  mac       TEXT     UNIQUE NOT NULL   (format: AA:BB:CC:DD:EE:FF)
  status    TEXT     DEFAULT 'offline'
  last_seen TEXT

Group
  id        INTEGER  PRIMARY KEY
  name      TEXT     UNIQUE NOT NULL

device_group  (many-to-many join table)
  device_id → Device.id
  group_id  → Group.id
```

---

## Notes

- MAC addresses are stored in uppercase and must be in `XX:XX:XX:XX:XX:XX` format.
- A device can belong to multiple groups.
- Deleting a device removes it from all groups automatically (cascade via SQLAlchemy).
- The database file is excluded from version control (`.gitignore`).
