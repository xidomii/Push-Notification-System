# SmartServe — Watch Firmware

Client-Firmware für die selbstgebaute Smartwatch. Empfängt Nachrichten vom
SmartServe-Backend über MQTT und zeigt sie am Display an.

**Aktueller Stand: Meilenstein 2 (Empfang + Anzeige am Display) — auf Hardware bestätigt.**

## Hardware
- **MCU:** ESP32 WROOM-32 (klassisch, Board `esp32dev`)
- **Display:** 1.28" rund, GC9A01, 240×240, SPI (kein Touch)
- **Eingabe:** keine (Aufgabe = reine Anzeige). Accept/decline/done-Workflow entfernt.

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
| empfangen | `smartserve/groups/+` (alle Gruppen) | `type:task` | **MS1/MS2** |
| empfangen | `smartserve/device/{MAC}` | direktes Feedback | MS4 |
| senden | `smartserve/heartbeat` | `{mac}` alle 30s | MS3 |
| senden | `smartserve/ack/{notification_id}` | `{mac, action}` accept\|decline\|done | MS4 |

## Setup
1. PlatformIO installieren: `pip install platformio` (oder VSCode-Extension "PlatformIO IDE").
2. `src/config.h` aus `src/config.example.h` erstellen: WLAN = **Handy-Hotspot**
   (2,4 GHz!), `PI_HOSTNAME="smartserve"` (Pi per mDNS `smartserve.local`),
   `MQTT_HOST` = Fallback-IP (Pi am Hotspot, `hostname -I`), `GROUP_ID`.
3. Display gemäß Tabelle verdrahten. Pins stehen in `platformio.ini`.

## Build-Umgebungen
| Env | Zweck | Datei |
|---|---|---|
| `esp32dev` | MS2-Firmware, direktes Zeichnen (TFT_eSPI) | `src/main.cpp` |
| `esp32dev-lvgl` | LVGL-GUI-Gerüst (Design-Umsetzung) | `src/main_lvgl.cpp` |

## Build / Flash / Test
```bash
pio run -e esp32dev                 # MS2-Firmware kompilieren
pio run -e esp32dev -t upload       # flashen
pio device monitor -b 115200        # serielle Ausgabe
# LVGL-GUI:  pio run -e esp32dev-lvgl [-t upload]
```

## Watch-GUI
- **Design-Referenz / Emulator:** `gui-emulator/index.html` (im Browser öffnen) —
  zeigt die eingefrorene GUI (Watchface / Aufgabe / Liste) auf einem 240×240-Kreis.
- **Umsetzung:** LVGL (`esp32dev-lvgl`, `src/main_lvgl.cpp`, Config `include/lv_conf.h`).
  **Screens am Display umgesetzt (auf HW bestätigt):**
  Boot-Sequenz · Watchface (Statuspunkt + Uhr + Datum + Brand) · Aufgabe
  (Gruppen-Chip + Nachricht + Zeit) · farbiger Status-Ring am Rand.
- **Ablauf Aufgabe:** kommt eine Nachricht → Aufgaben-Screen für **10 s** → zurück zum
  Watchface. **Reine Anzeige, kein accept/decline/done** (bewusst entfernt). Neue
  Nachricht während der Anzeige setzt die 10 s neu. Keine Taster/Buttons mehr.
- **Uhrzeit:** der Pi hängt am Hotspot → holt die Zeit per NTP und publisht sie als
  retained Topic `smartserve/time` (`{"epoch": <lokale Wanduhr Europe/Vienna>}`, alle 10 s).
  Watch rechnet daraus HH:MM:SS + Datum. Ohne Sync → `--:--`.
  → **Backend am Pi muss auf diesen Stand aktualisiert werden** (`backend/mqtt.py`).
- **Limit:** eingebaute Montserrat-Fonts = nur ASCII. Dynamische Backend-Texte mit
  Umlauten werden auf ASCII gefaltet (ä→ae, ö→oe, ü→ue, ß→ss), damit keine leeren
  Kästchen entstehen. Pixelgenaue Umlaute = eigener Latin-1-Font (MS5).
- **Rendering-Fix:** `LV_COLOR_16_SWAP=1` → in `flush_cb` `tft.pushColors(..., false)`
  (sonst doppelter Byte-Swap → verpixeltes/falschfarbiges Bild).
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
