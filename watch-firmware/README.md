# SmartServe — Watch Firmware

Client-Firmware für die selbstgebaute Smartwatch (ESP32-S3 SuperMini).
Empfängt Aufgaben/Nachrichten vom SmartServe-Backend über MQTT.

**Aktueller Stand: Meilenstein 1 (Empfang).** Weitere Stufen sind bewusst noch
nicht umgesetzt — siehe Meilenstein-Mapping unten.

## Hardware (Zielgerät)
- **MCU:** ESP32-S3 SuperMini
- **Display:** 1.28" rund, GC9A01, 240×240, SPI (kein Touch) — erst ab MS2
- **Eingabe:** 3 Taster (accept/decline/done) — erst ab MS4

Für MS1 genügt das nackte Entwicklungsboard (ESP32-S3 SuperMini), kein Display nötig.

## Stack
- Framework: Arduino (PlatformIO)
- MQTT: PubSubClient
- (ab MS2: TFT_eSPI für GC9A01, ArduinoJson; ab Stufe d: LVGL-GUI)

## MQTT-Contract (muss zum Backend passen, siehe `../backend/mqtt.py`)
| Richtung | Topic | Payload | ab |
|---|---|---|---|
| empfangen | `smartserve/groups/{GROUP_ID}` | `type:task` / `type:task_status` | **MS1** |
| empfangen | `smartserve/device/{MAC}` | direktes Feedback | MS4 |
| senden | `smartserve/heartbeat` | `{mac}` alle 30s | MS3 |
| senden | `smartserve/ack/{notification_id}` | `{mac, action}` accept\|decline\|done | MS4 |

`WiFi.macAddress()` liefert Großbuchstaben + Doppelpunkt — passt zur Backend-Normalisierung.

## Setup
1. PlatformIO installieren: `pip install platformio` (oder VSCode-Extension "PlatformIO IDE").
2. `src/config.h` aus `src/config.example.h` erstellen: WLAN, Broker-Host (Pi, `smartserve.local`), `GROUP_ID`.

## Build / Flash / Test (MS1)
```bash
pio run                        # kompilieren
pio run -t upload              # auf Entwicklungsboard flashen
pio device monitor -b 115200   # serielle Ausgabe
```
MS1-Nachweis: eine Testnachricht an das Topic senden und im Monitor sehen:
```bash
mosquitto_pub -h smartserve.local -t smartserve/groups/1 -m '{"type":"task","notification_id":1,"group_name":"Kueche","message":"Testnachricht"}'
```
→ Erwartet: `[RX] Topic: smartserve/groups/1` + Payload im Serial-Monitor.

## Meilenstein-Mapping (Dominik)
| MS | Datum | Inhalt | Firmware-Stufe |
|----|-------|--------|----------------|
| **MS1** | 18.10.2026 | **Empfang Testnachrichten vom Broker (Dev-Board)** | **erledigt (Code), ungetestet auf HW** |
| MS2 | 18.12.2026 | JSON auswerten + Anzeige am Display | offen |
| MS3 | 18.01.2027 | Heartbeat / Statusmeldung | offen |
| MS4 | 21.02.2027 | Integration Hardware + Gesamttest (Ack-Buttons) | offen |
| MS5 | 18.03.2027 | Fehlerbehebung + Doku | offen |

Die weiter fortgeschrittene Implementierung (Display, Heartbeat, Ack) liegt bereits
als Referenz in `reference/main_full_stage_d.cpp.txt` — wird ab MS2 schrittweise
in `src/` überführt. Für MS1 bewusst NICHT aktiv.

## Status
MS1-Code vollständig, **noch nicht auf Hardware getestet** (Board/Display ausständig).
