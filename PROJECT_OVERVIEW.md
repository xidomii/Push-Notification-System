# SmartServe — Projektübersicht

Lokales Push-Benachrichtigungssystem: ein Admin sendet über ein Web-Dashboard
Nachrichten/Aufgaben an selbstgebaute Smartwatches. Übertragung per MQTT, alles im
lokalen Netz, ohne Cloud.

## Die 3 Komponenten (wer läuft wo)

```
   ┌─────────────────────────┐        MQTT         ┌──────────────────┐
   │  RASPBERRY PI            │   (Mosquitto 1883)  │  SMARTWATCH      │
   │  = Server-Appliance      │◄───────────────────►│  = ESP32 + GC9A01│
   │                          │                     │  (watch-firmware)│
   │  • Mosquitto (Broker)    │                     └──────────────────┘
   │  • Flask-Backend (:5000) │
   │  • SQLite-DB             │        HTTP
   │  • WLAN-AP "SmartServe"  │◄───────────────┐
   └─────────────────────────┘                │
                                       ┌───────────────┐
                                       │  ADMIN-BROWSER │
                                       │  (Dashboard)   │
                                       └───────────────┘
```

| Komponente | Läuft auf | Ordner |
|---|---|---|
| Broker (Mosquitto) | Raspberry Pi | (System-Paket, kein Repo-Code) |
| Backend (Flask + REST + MQTT) | Raspberry Pi | `backend/` |
| Admin-Web-UI | im Browser (vom Backend serviert) | `frontend/` |
| Watch-Firmware | ESP32 WROOM-32 | `watch-firmware/` |
| Test-/Emulator-Clients | Laptop oder Pi (manuell) | `test-clients/` |

## Ordner-Landkarte

```
SmartServe/
├── backend/              # Server (läuft am Pi)
│   ├── app.py            #   Flask-App-Factory, startet DB + MQTT
│   ├── models.py         #   DB-Modelle: Device, Group, Notification
│   ├── mqtt.py           #   Broker-Anbindung DES BACKENDS (publish + heartbeat/ack)
│   └── routes/
│       ├── admin.py      #   liefert die HTML-Seiten aus
│       └── api.py        #   REST-API /api/*
├── frontend/             # Admin-Dashboard (Browser)
│   ├── templates/        #   dashboard/devices/groups/notifications .html
│   └── static/           #   je Seite eigenes style.css + script.js
├── watch-firmware/       # ESP32-Firmware der Smartwatch (C++/PlatformIO)
│   ├── src/main.cpp      #   Empfang + Anzeige am GC9A01-Display
│   ├── src/config.h      #   WLAN/Broker/Gruppe (gitignored — Zugangsdaten)
│   ├── src/config.example.h  # Vorlage dazu
│   └── platformio.ini    #   Board + Display-Pins + Libraries
├── test-clients/         # Test-/Emulator-Skripte (PC-seitig, nicht am Pi nötig)
│   ├── sender.py         #   CLI: Testnachricht senden
│   ├── receiver.py       #   einfacher Subscriber (Watch-Ersatz zum Mitlesen)
│   └── device_client.py  #   VOLLER Watch-Emulator (accept/decline/done + Heartbeat)
├── PI_SETUP.md           # Schritt-für-Schritt: Pi einrichten (Broker+Backend+AP)
├── PROJECT_OVERVIEW.md   # diese Datei
├── README.md             # Projekt-Readme (Features, API, MQTT)
├── CLAUDE.md             # Kurz-Doku fürs Repo
└── requirements.txt      # Python-Abhängigkeiten des Backends
```

## Wichtig: zwei „mqtt" nicht verwechseln
- **`backend/mqtt.py`** = fester Bestandteil des Servers. Publiziert Nachrichten und
  verarbeitet Heartbeat/Ack. Läuft immer mit dem Backend am Pi.
- **`test-clients/`** (Ordner) = lose Hilfs-/Testskripte für den PC (Sender, Emulator).
  Nur manuell zum Testen. Läuft NICHT am Pi im Normalbetrieb.

## Datenfluss (MQTT-Topics)
| Topic | Richtung | Zweck |
|---|---|---|
| `smartserve/groups/{id}` | Backend → Watches | neue Aufgabe / Statusupdate |
| `smartserve/device/{MAC}` | Backend → eine Watch | direktes Feedback |
| `smartserve/heartbeat` | Watch → Backend | „online"-Meldung (alle 30s) |
| `smartserve/ack/{id}` | Watch → Backend | accept / decline / done |

## Netzwerk
- Pi ist WLAN-Access-Point **"SmartServe"** (WPA2), Pi = **10.42.0.1**.
- Watch + Admin-Laptop treten diesem WLAN bei.
- Watch nutzt feste Broker-IP `10.42.0.1` (ESP kann kein mDNS).
- Admin-Dashboard: `http://10.42.0.1:5000`.

## Diplomarbeit-Dokumente (NICHT im Repo, unter ~/Documents/Diplomarbeit/)
| Datei | Inhalt |
|---|---|
| `Vorlage_DA_V2.1_2026.docx` | das eigentliche DA-Dokument (Kapitel) |
| `ABA_Ausfuellhilfe_v1.docx` | Anmeldung/Themenstellung (eingereicht) |
| `DA_Arbeitsprotokoll.docx` | fortlaufendes Änderungs-/Arbeitsprotokoll |
| `DA_Entwuerfe_Kap1-2.docx` | Textentwürfe Kapitel 1–2 |
| `DA_Pi_Einrichtung.docx` | As-Built-Doku der Pi-Einrichtung (für Anhang) |

## Meilenstein-Stand (Dominik, Software)
- **MS1** Empfang vom Broker — erledigt (HW bestätigt)
- **MS2** JSON auswerten + Display-Anzeige — erledigt (HW bestätigt)
- MS3 Heartbeat/Status · MS4 Ack-Buttons/Integration · MS5 Doku — offen
