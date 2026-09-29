# SmartServe — Watch Firmware

Client-Firmware für die selbstgebaute Smartwatch. Empfängt Nachrichten vom
SmartServe-Backend über MQTT und zeigt sie am Display an.

**Aktueller Stand: Meilenstein 2 (Empfang + Anzeige am Display) — auf Hardware bestätigt.**

## Hardware
- **MCU:** ESP32 WROOM-32 (klassisch, Board `esp32dev`)
- **Display:** 1.28" rund, GC9A01, 240×240, SPI (kein Touch)
- **Eingabe:** 3 Taster (accept/decline/done) — erst ab MS4

### Verdrahtung GC9A01 → ESP32 WROOM-32
| Display | ESP32 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCL (SCLK) | GPIO18 |
| SDA (MOSI) | GPIO23 |
| RES (RST) | GPIO4 |
| DC | GPIO2 |
| CS | GPIO15 |
| BLK | 3V3 |

## Stack
- Framework: Arduino (PlatformIO)
- MQTT: PubSubClient · JSON: ArduinoJson · Display: TFT_eSPI (GC9A01)
- (ab Stufe d optional: LVGL-GUI)

## MQTT-Contract (muss zum Backend passen, siehe `../backend/mqtt.py`)
| Richtung | Topic | Payload | ab |
|---|---|---|---|
| empfangen | `smartserve/groups/{GROUP_ID}` | `type:task` / `type:task_status` | **MS1/MS2** |
| empfangen | `smartserve/device/{MAC}` | direktes Feedback | MS4 |
| senden | `smartserve/heartbeat` | `{mac}` alle 30s | MS3 |
| senden | `smartserve/ack/{notification_id}` | `{mac, action}` accept\|decline\|done | MS4 |

## Setup
1. PlatformIO installieren: `pip install platformio` (oder VSCode-Extension "PlatformIO IDE").
2. `src/config.h` aus `src/config.example.h` erstellen: WLAN (Pi-AP "SmartServe"),
   `MQTT_HOST` = feste Pi-IP `10.42.0.1` (ESP kann kein mDNS), `GROUP_ID`.
3. Display gemäß Tabelle verdrahten. Pins stehen in `platformio.ini`.

## Build / Flash / Test
```bash
pio run                        # kompilieren
pio run -t upload              # flashen
pio device monitor -b 115200   # serielle Ausgabe
```
Testnachricht vom Pi senden:
```bash
mosquitto_pub -h localhost -t smartserve/groups/1 -m '{"type":"task","group_name":"Kueche","message":"Hallo Watch"}'
```
Erwartet: Serial `[RX] ...` **und** Display zeigt „Kueche" + „Hallo Watch".
Boot-Anzeige: „SmartServe" → „WLAN..." → „Broker..." → „Bereit" (grün).

## Meilenstein-Mapping (Dominik)
| MS | Datum | Inhalt | Stand |
|----|-------|--------|-------|
| **MS1** | 18.10.2026 | Empfang Testnachrichten vom Broker | **erledigt (HW bestätigt)** |
| **MS2** | 18.12.2026 | JSON auswerten + Anzeige am Display | **erledigt (HW bestätigt)** |
| MS3 | 18.01.2027 | Heartbeat / Statusmeldung | offen |
| MS4 | 21.02.2027 | Integration Hardware + Gesamttest (Ack-Buttons) | offen |
| MS5 | 18.03.2027 | Fehlerbehebung + Doku | offen |

## Status
MS1 + MS2 auf echter Hardware getestet (ESP32 WROOM-32 + GC9A01): Watch empfängt
und zeigt Nachrichten an. Heartbeat (MS3) und Ack-Buttons (MS4) folgen.
