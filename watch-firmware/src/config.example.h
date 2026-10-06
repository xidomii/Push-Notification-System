#pragma once

// ============================================================
//  SmartServe Watch — Konfiguration (Vorlage)
//  Nach config.h kopieren und ausfuellen. config.h ist gitignored.
// ============================================================

// --- WLAN (Handy-Hotspot; Pi + Watch haengen beide dran) ---
#define WIFI_SSID       "DEIN_HOTSPOT"
#define WIFI_PASSWORD   "DEIN_HOTSPOT_PW"

// --- MQTT Broker (Raspberry Pi am Hotspot) ---
// Pi-IP am Hotspot ist DHCP-dynamisch -> primaer per mDNS aufloesen.
// ESP32 (ESPmDNS) kann smartserve.local aufloesen.
#define PI_HOSTNAME     "smartserve"   // mDNS-Name -> smartserve.local
// Fallback-IP, falls mDNS scheitert (Pi-IP vom Hotspot ablesen, hostname -I).
#define MQTT_HOST       "192.168.1.50"
#define MQTT_PORT       1883

// --- Gruppe, deren Nachrichten diese Watch empfaengt ---
// Abonniertes Topic: smartserve/groups/{GROUP_ID}
#define GROUP_ID        1

// --- Timing ---
#define MQTT_RETRY_MS   3000

// --- Physische Taster (MS4: accept/decline/done) ---
// Aktiv LOW (Taster gegen GND, interner Pull-up). Pins meiden SPI (18,23,15,2,4).
// GPIO 32/33/25 = frei am WROOM-32. -1 setzen, falls Taster noch nicht verdrahtet.
#define BTN_ACCEPT      32
#define BTN_DECLINE     33
#define BTN_DONE        25
