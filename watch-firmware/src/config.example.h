#pragma once

// ============================================================
//  SmartServe Watch — Konfiguration (Vorlage)
//  Nach config.h kopieren und ausfuellen. config.h ist gitignored.
// ============================================================

// --- WLAN (Pi-Access-Point) ---
#define WIFI_SSID       "SmartServe"
#define WIFI_PASSWORD   "DEIN_AP_PASSWORT"

// --- MQTT Broker (Raspberry Pi am AP = feste IP) ---
// ESP kann kein mDNS -> feste IP statt smartserve.local verwenden.
#define MQTT_HOST       "10.42.0.1"
#define MQTT_PORT       1883

// --- Gruppe, deren Nachrichten diese Watch empfaengt ---
// Abonniertes Topic: smartserve/groups/{GROUP_ID}
#define GROUP_ID        1

// --- Timing ---
#define MQTT_RETRY_MS   3000
