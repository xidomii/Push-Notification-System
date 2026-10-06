// ============================================================
//  SmartServe — Smartwatch Client Firmware
//  MS2: Empfang vom Broker + Anzeige am GC9A01-Display.
//
//    - WLAN verbinden (Pi-AP "SmartServe")
//    - MQTT-Broker verbinden (Pi = 10.42.0.1)
//    - Gruppen-Topic abonnieren
//    - JSON-Payload auswerten (group_name, message)
//    - Nachricht am runden 240x240-Display anzeigen
//
//  Board: ESP32-S3 SuperMini · Display: GC9A01 (SPI, Pins in platformio.ini)
//  Heartbeat (MS3) + Ack-Buttons (MS4) folgen spaeter.
// ============================================================

#include <WiFi.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "config.h"

TFT_eSPI     tft = TFT_eSPI();
WiFiClient   net;
PubSubClient mqtt(net);

String g_mac;
String subGroup;

// ---------------------------------------------------------------- Anzeige
void showStatus(const char* text, uint16_t color) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(text, 120, 120, 4);
}

// einfacher Wortumbruch fuer die runde Anzeige
void showMessage(const String& group, const String& msg) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  // Gruppe oben
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(group, 120, 55, 2);

  // Nachricht mittig, in Zeilen zu max ~16 Zeichen
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  const int maxChars = 16;
  int y = 105;
  String rest = msg;
  int lines = 0;
  while (rest.length() > 0 && lines < 4) {
    String line = rest.substring(0, maxChars);
    // an letztem Leerzeichen umbrechen falls moeglich
    if (rest.length() > (unsigned)maxChars) {
      int sp = line.lastIndexOf(' ');
      if (sp > 4) line = rest.substring(0, sp);
    }
    tft.drawString(line, 120, y, 4);
    rest = rest.substring(line.length());
    rest.trim();
    y += 30;
    lines++;
  }
}

// ---------------------------------------------------------------- Empfang
void onMessage(char* topic, byte* payload, unsigned int len) {
  Serial.printf("\n[RX] %s: ", topic);
  for (unsigned int i = 0; i < len; i++) Serial.write(payload[i]);
  Serial.println();

  StaticJsonDocument<512> d;
  if (deserializeJson(d, payload, len)) return;   // kein JSON -> ignorieren

  const char* type = d["type"] | "";
  // nur neue Aufgaben/Nachrichten anzeigen
  if (strcmp(type, "task") == 0 || d.containsKey("message")) {
    String group = String((const char*)(d["group_name"] | "SmartServe"));
    String msg   = String((const char*)(d["message"]    | ""));
    showMessage(group, msg);
  }
}

// ---------------------------------------------------------------- WLAN
void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("[WiFi] verbinde mit %s ...\n", WIFI_SSID);
  showStatus("WLAN...", TFT_DARKGREY);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  g_mac    = WiFi.macAddress();
  subGroup = "smartserve/groups/" + String(GROUP_ID);
  MDNS.begin("smartserve-watch");
  Serial.printf("\n[WiFi] verbunden. IP=%s MAC=%s\n",
                WiFi.localIP().toString().c_str(), g_mac.c_str());
}

// Pi-Broker finden: mDNS (smartserve.local) zuerst, sonst Fallback-IP aus config.h
void resolveBroker() {
  IPAddress ip = MDNS.queryHost(PI_HOSTNAME, 2000);
  if ((uint32_t)ip != 0) {
    mqtt.setServer(ip, MQTT_PORT);
    Serial.printf("[mDNS] %s.local -> %s\n", PI_HOSTNAME, ip.toString().c_str());
  } else {
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    Serial.printf("[mDNS] fehlgeschlagen -> Fallback %s\n", MQTT_HOST);
  }
}

// ---------------------------------------------------------------- MQTT
void ensureMqtt() {
  while (!mqtt.connected()) {
    showStatus("Broker...", TFT_DARKGREY);
    resolveBroker();
    String cid = "watch-" + g_mac;
    if (mqtt.connect(cid.c_str())) {
      mqtt.subscribe(subGroup.c_str(), 1);
      Serial.printf("[MQTT] verbunden. abonniert: %s\n", subGroup.c_str());
      showStatus("Bereit", TFT_GREEN);
    } else {
      Serial.printf("[MQTT] fehlgeschlagen rc=%d, retry\n", mqtt.state());
      delay(MQTT_RETRY_MS);
    }
  }
}

// ---------------------------------------------------------------- Arduino
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== SmartServe Watch — MS2 (Empfang + Display) ===");

  tft.init();
  tft.setRotation(0);
  showStatus("SmartServe", TFT_WHITE);

  ensureWifi();
  mqtt.setCallback(onMessage);           // Broker-IP setzt resolveBroker() in ensureMqtt
  mqtt.setBufferSize(512);
  ensureMqtt();
}

void loop() {
  ensureWifi();
  ensureMqtt();
  mqtt.loop();
}
