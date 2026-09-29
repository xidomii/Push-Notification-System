# SmartServe — Raspberry Pi Setup (Broker + Backend)

Der Raspberry Pi ist der zentrale Host: er betreibt **Mosquitto (MQTT-Broker)** und
das **Flask-Backend** (Server-Appliance). Der Laptop ist nur noch Admin-Browser.

**Warum beides auf dem Pi (Variante B):** Backend↔Broker laufen über `localhost`
(kein Netz-Hop, keine IP-Kopplung zwischen zwei Geräten), ein always-on Gerät statt
"Laptop muss an sein", und der ESP zeigt bereits auf `smartserve.local`.

## Was auf den Pi gehört
- Raspberry Pi OS Lite (64-bit), headless, SSH aktiv
- `mosquitto` + `mosquitto-clients` — der Broker
- `python3-venv`, `python3-pip`, `git`
- `avahi-daemon` — mDNS `smartserve.local` (bei RPi OS meist schon dabei)
- Repo `backend/` + `frontend/` + `requirements.txt`
- Python-venv mit Deps **+ gunicorn**
- SQLite-DB `backend/smartserve.db` (wird automatisch erstellt)
- Mosquitto-Config `/etc/mosquitto/conf.d/smartserve.conf`
- systemd-Service fürs Backend (autostart)

**NICHT auf den Pi:** `mqtt/` (Testclients), `watch-firmware/` (läuft am ESP32).

> **Wichtig:** Backend mit **nur 1 gunicorn-Worker** starten. Mehrere Worker = mehrere
> MQTT-Clients (`mqtt.connect()` läuft je Prozess) → doppelte Heartbeat-/Ack-Verarbeitung
> + SQLite-Locks.

## 0) SD/SSD flashen mit Raspberry Pi Imager (headless + SSH)

OS = **Raspberry Pi OS Lite (64-bit)**. Vor dem Schreiben: **⚙ Zahnrad** (`Strg+Shift+X`)
→ "OS-Einstellungen bearbeiten":

**Reiter „Allgemein":**
- Hostname: `smartserve` → erreichbar als `smartserve.local`
- Benutzername + Passwort: z.B. `pi` + starkes Passwort (dieser User = `User=` im systemd-Service)
- WLAN: SSID + Passwort + **Land: `AT`**
- Ländereinstellungen: Zeitzone `Europe/Vienna`, Tastatur `de`

**Reiter „Dienste":**
- **SSH aktivieren** ✓ — Passwort-Auth (einfach) ODER nur Public-Key (sicherer, empfohlen)

Key-Auth (empfohlen), Key am Laptop erzeugen falls nicht vorhanden:
```bash
ssh-keygen -t ed25519       # erzeugt ~/.ssh/id_ed25519(.pub)
cat ~/.ssh/id_ed25519.pub   # Inhalt in den Imager (Public-Key-Feld) einfügen
```

Speichern → Schreiben. Nach Boot (1–2 min):
```bash
ssh pi@smartserve.local
```

## Setup-Befehle (der Reihe nach, auf dem Pi)

```bash
# 1) System aktuell + Hostname smartserve (-> smartserve.local via mDNS)
sudo apt update && sudo apt full-upgrade -y
sudo raspi-config nonint do_hostname smartserve
sudo apt install -y avahi-daemon

# 2) Broker
sudo apt install -y mosquitto mosquitto-clients
echo -e "listener 1883\nallow_anonymous true" | sudo tee /etc/mosquitto/conf.d/smartserve.conf
sudo systemctl enable --now mosquitto

# 3) Backend holen
sudo apt install -y python3-venv python3-pip git
git clone https://github.com/xidomii/Push-Notification-System.git ~/SmartServe

# 4) venv + Abhängigkeiten (+ gunicorn)
cd ~/SmartServe
python3 -m venv venv
./venv/bin/pip install -r requirements.txt gunicorn

# 5) Test manuell (Broker lokal, backend/mqtt.py BROKER="localhost" passt)
cd backend
../venv/bin/gunicorn --workers 1 --bind 0.0.0.0:5000 app:app
#   -> http://smartserve.local:5000 im Browser, Strg+C zum Stoppen
```

## systemd-Service (autostart)

`/etc/systemd/system/smartserve.service`:
```ini
[Unit]
Description=SmartServe Backend (Flask + MQTT)
After=network-online.target mosquitto.service
Wants=network-online.target

[Service]
User=pi
WorkingDirectory=/home/pi/SmartServe/backend
ExecStart=/home/pi/SmartServe/venv/bin/gunicorn --workers 1 --bind 0.0.0.0:5000 app:app
Restart=on-failure

[Install]
WantedBy=multi-user.target
```
```bash
sudo systemctl daemon-reload
sudo systemctl enable --now smartserve
sudo systemctl status smartserve
```

## Pi als WLAN-Access-Point (empfohlen fürs finale Setup)

Der Pi spannt sein eigenes WLAN auf; Watch + Admin-Laptop treten bei. Kein externer
Router, unabhängig vom Schul-/Heimnetz — passt zur "lokales System"-Idee. Bei RPi OS
Lite (Bookworm) via NetworkManager (`nmcli`), kein hostapd/dnsmasq nötig.

> **Achtung:** Sobald der AP aktiv ist, wird `wlan0` zum Access Point → eine bestehende
> SSH-Verbindung übers Heim-WLAN bricht ab. Danach ins Pi-WLAN "SmartServe" verbinden und
> `ssh pi@10.42.0.1` (oder `ssh pi@smartserve.local`). Am besten per Ethernet einrichten.

```bash
# 1) WLAN-Land setzen (Regulierung, sonst kein AP)
sudo raspi-config nonint do_wifi_country AT

# 2) AP-Profil anlegen
sudo nmcli connection add type wifi ifname wlan0 con-name smartserve-ap autoconnect yes ssid SmartServe

# 3) 2,4-GHz-AP (ESP kann nur 2,4 GHz) + eigenes Subnetz mit DHCP (Pi = 10.42.0.1)
sudo nmcli connection modify smartserve-ap 802-11-wireless.mode ap 802-11-wireless.band bg ipv4.method shared

# 4) WLAN-Passwort (min. 8 Zeichen)
sudo nmcli connection modify smartserve-ap wifi-sec.key-mgmt wpa-psk wifi-sec.psk "geheim1234"

# 5) AP starten (SSH übers Heim-WLAN bricht jetzt ab)
sudo nmcli connection up smartserve-ap

# 6) Neu verbinden: Client ins WLAN "SmartServe", dann
ssh pi@10.42.0.1

# 7) Prüfen
nmcli connection show --active
```

ESP `config.h` `MQTT_HOST` bleibt `smartserve.local` (avahi läuft auch im AP-Netz).
Broker + Backend laufen am Pi auf `localhost` → vom AP unberührt.

## Verifikation
```bash
mosquitto_sub -h localhost -t 'smartserve/#' -v
mosquitto_pub -h smartserve.local -t smartserve/groups/1 -m '{"type":"task","message":"Test"}'
```

## Hinweise
- `User=pi` und Pfade `/home/pi/...` an den tatsächlichen Pi-User anpassen.
- USB-SSD statt SD-Karte wegen SQLite-Schreiblast.
- ESP `watch-firmware/src/config.h`: `MQTT_HOST` steht bereits auf `smartserve.local` → nichts zu ändern.
- Broker läuft lokal am Pi → `backend/mqtt.py` `BROKER = "localhost"` bleibt korrekt.
