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

**NICHT auf den Pi:** `test-clients/` (Testclients), `watch-firmware/` (läuft am ESP32).

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
sudo systemctl enable mosquitto
sudo systemctl restart mosquitto      # WICHTIG: restart, nicht nur enable --now!
# Pruefen dass er auf allen Interfaces lauscht (0.0.0.0:1883, NICHT nur 127.0.0.1):
ss -tlnp | grep 1883

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

## Pi als Hotspot-Client + NTP (aktuelles Setup)

**Kein eigener AP mehr.** Der Pi verbindet sich beim Booten mit dem Handy-Hotspot →
Internetzugang → Uhrzeit per NTP. Watch + Admin-Laptop hängen am selben Hotspot.
Der Pi stellt die echte Zeit über MQTT (`smartserve/time`, retained) für die Watch bereit.

> **Wichtig:**
> - Hotspot muss **2,4 GHz** anbieten (klassischer ESP32 kann kein 5 GHz).
> - Hotspot **vor** dem Pi-Boot einschalten (sonst kein Autoconnect).
> - Pi-IP am Hotspot ist **DHCP-dynamisch** → Watch findet den Pi per mDNS
>   (`smartserve.local`, avahi am Pi). Fallback: feste IP in `watch-firmware/src/config.h`
>   (`MQTT_HOST`), vom Hotspot ablesen mit `hostname -I`.

```bash
# 0) Alten AP deaktivieren (falls vorhanden)
sudo nmcli con down smartserve-ap 2>/dev/null
sudo nmcli con modify smartserve-ap connection.autoconnect no 2>/dev/null
# optional ganz weg:  sudo nmcli con delete smartserve-ap

# 1) WLAN-Land setzen (Regulierung)
sudo raspi-config nonint do_wifi_country AT

# 2) Hotspot-Profil anlegen (Autoconnect, hohe Prioritaet)
sudo nmcli con add type wifi ifname wlan0 con-name hotspot ssid "<HOTSPOT_SSID>"
sudo nmcli con modify hotspot wifi-sec.key-mgmt wpa-psk wifi-sec.psk "<HOTSPOT_PW>"
sudo nmcli con modify hotspot connection.autoconnect yes connection.autoconnect-priority 100
sudo nmcli con up hotspot

# 3) Zeit + NTP
sudo timedatectl set-timezone Europe/Vienna
sudo timedatectl set-ntp true
timedatectl status          # -> "System clock synchronized: yes", "NTP service: active"

# 4) Broker auf allen Interfaces (nicht an AP-IP gebunden)
#    /etc/mosquitto/conf.d/smartserve.conf:  listener 1883 0.0.0.0
grep -R listener /etc/mosquitto/conf.d/ ; sudo systemctl restart mosquitto

# 5) mDNS-Responder (fuer smartserve.local)
systemctl status avahi-daemon --no-pager

# 6) Backend neu starten (publisht jetzt echte NTP-Zeit als smartserve/time)
sudo systemctl restart smartserve.service
hostname -I                 # Pi-IP am Hotspot -> als Fallback in config.h eintragen
```

ESP `config.h`: `WIFI_SSID`/`WIFI_PASSWORD` = Hotspot; `PI_HOSTNAME="smartserve"`
(mDNS); `MQTT_HOST` = Fallback-IP. Broker + Backend laufen am Pi auf `localhost`.

> Hinweis Client-Isolation: manche Handy-Hotspots trennen Clients voneinander
> (dann findet die Watch den Pi nicht). Android erlaubt Client-zu-Client meist;
> falls nicht → in den Hotspot-Einstellungen Isolation deaktivieren.

## Verifikation
```bash
mosquitto_sub -h localhost -t 'smartserve/#' -v
mosquitto_pub -h smartserve.local -t smartserve/groups/1 -m '{"type":"task","message":"Test"}'
```

## Hinweise
- `User=pi` und Pfade `/home/pi/...` an den tatsächlichen Pi-User anpassen.
- USB-SSD statt SD-Karte wegen SQLite-Schreiblast.
- ESP `watch-firmware/src/config.h`: `WIFI_SSID`/`WIFI_PASSWORD` = Hotspot, `PI_HOSTNAME="smartserve"` (mDNS), `MQTT_HOST` = Fallback-IP.
- Broker läuft lokal am Pi → `backend/mqtt.py` `BROKER = "localhost"` bleibt korrekt.
- Zeit: `backend/mqtt.py` publisht retained `smartserve/time` (lokale NTP-Zeit) → Watch-Uhr.
