#pragma once

// ============================================================
//  SmartServe Watch — Konfiguration (Meilenstein 1)
//  Diese Datei anpassen, NICHT committen (WLAN-Zugangsdaten).
// ============================================================

// --- WLAN ---
#define WIFI_SSID       "DEIN_WLAN"
#define WIFI_PASSWORD   "DEIN_PASSWORT"

// --- MQTT Broker (Raspberry Pi) ---
// Feste IP ODER mDNS-Hostname des Pi. mDNS bevorzugt (ueberlebt IP-Wechsel).
#define MQTT_HOST       "smartserve.local"   // z.B. "192.168.1.50"
#define MQTT_PORT       1883

// --- Gruppe, deren Nachrichten diese Watch empfaengt ---
// Abonniertes Topic: smartserve/groups/{GROUP_ID}
#define GROUP_ID        1

// --- Timing ---
#define MQTT_RETRY_MS   3000
