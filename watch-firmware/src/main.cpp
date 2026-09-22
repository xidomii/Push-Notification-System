// ============================================================
//  SmartServe — Smartwatch Client Firmware
//  MEILENSTEIN 1 (Dominik, 18.10.2026):
//    "Client-Software empfaengt Testnachrichten vom Broker
//     (Prototyp am Entwicklungsboard)"
//
//  Umfang MS1 bewusst minimal:
//    - WLAN verbinden
//    - mit MQTT-Broker verbinden (Raspberry Pi)
//    - Gruppen-Topic abonnieren
//    - empfangene Nachrichten ueber Serial ausgeben
//
//  NICHT Teil von MS1 (spaetere Stufen, siehe reference/):
//    - Display-Anzeige / JSON-Auswertung .... MS2
//    - Heartbeat / Statusmeldung ............ MS3
//    - Ack-Buttons / Hardware-Integration ... MS4
//
//  Board: ESP32-S3 SuperMini (Entwicklungsboard, ohne Display fuer MS1)
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include "config.h"

WiFiClient   net;
PubSubClient mqtt(net);

String g_mac;
String subGroup;

// ---------------------------------------------------------------- Empfang
void onMessage(char* topic, byte* payload, unsigned int len) {
  Serial.printf("\n[RX] Topic: %s\n", topic);
  Serial.print("[RX] Payload: ");
  for (unsigned int i = 0; i < len; i++) Serial.write(payload[i]);
  Serial.println();
}

// ---------------------------------------------------------------- WLAN
void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("[WiFi] verbinde mit %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  g_mac    = WiFi.macAddress();
  subGroup = "smartserve/groups/" + String(GROUP_ID);
  Serial.printf("\n[WiFi] verbunden. IP=%s MAC=%s\n",
                WiFi.localIP().toString().c_str(), g_mac.c_str());
}

// ---------------------------------------------------------------- MQTT
void ensureMqtt() {
  while (!mqtt.connected()) {
    Serial.printf("[MQTT] verbinde mit %s:%d ...\n", MQTT_HOST, MQTT_PORT);
    String cid = "watch-" + g_mac;
    if (mqtt.connect(cid.c_str())) {
      mqtt.subscribe(subGroup.c_str(), 1);
      Serial.printf("[MQTT] verbunden. abonniert: %s\n", subGroup.c_str());
    } else {
      Serial.printf("[MQTT] fehlgeschlagen rc=%d, retry in %dms\n",
                    mqtt.state(), MQTT_RETRY_MS);
      delay(MQTT_RETRY_MS);
    }
  }
}

// ---------------------------------------------------------------- Arduino
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== SmartServe Watch — MS1 (Empfang) ===");

  ensureWifi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(512);
  ensureMqtt();
}

void loop() {
  ensureWifi();
  ensureMqtt();
  mqtt.loop();
}
