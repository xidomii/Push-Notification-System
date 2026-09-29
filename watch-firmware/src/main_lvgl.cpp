// ============================================================
//  SmartServe — Watch GUI (LVGL-Geruest)
//  Build:  pio run -e esp32dev-lvgl
//
//  Bindet LVGL an TFT_eSPI (GC9A01, 240x240) an und zeigt eine erste
//  Version der GUI im SmartServe-Design. MQTT-Nachrichten aktualisieren
//  die Anzeige. Die vollstaendigen Screens (Watchface / Aufgabe / Liste,
//  Design-Referenz: gui-emulator/) werden ergaenzt, sobald das Display
//  zum visuellen Pruefen da ist.
//
//  Farben = SmartServe-Palette. Fonts = Montserrat (in LVGL enthalten).
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "config.h"

// SmartServe-Palette
#define COL_BG     0x0e0f11
#define COL_SEC    0x7a8099
#define COL_CYAN   0x22d3ee
#define COL_TEXT   0xe8eaf0
#define COL_GREEN  0x00e07a

TFT_eSPI     tft = TFT_eSPI();
WiFiClient   net;
PubSubClient mqtt(net);
String       g_mac, subGroup;

// LVGL Zeichenpuffer (Teilpuffer ~1/6 Screen -> ~19 KB)
static lv_disp_draw_buf_t draw_buf;
static lv_color_t         buf1[240 * 40];
static lv_disp_drv_t      disp_drv;

// UI-Objekte
static lv_obj_t *lblStatus, *lblGroup, *lblMsg;
static uint32_t  lastTick = 0;

// ---------------------------------------------------------------- LVGL -> Display
void flush_cb(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *color) {
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.pushColors((uint16_t *)color, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(d);
}

// ---------------------------------------------------------------- UI aufbauen
void buildUI() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Statuszeile oben
  lblStatus = lv_label_create(scr);
  lv_obj_set_style_text_font(lblStatus, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lblStatus, lv_color_hex(COL_SEC), 0);
  lv_label_set_text(lblStatus, "start");
  lv_obj_align(lblStatus, LV_ALIGN_TOP_MID, 0, 34);

  // Gruppe (cyan)
  lblGroup = lv_label_create(scr);
  lv_obj_set_style_text_font(lblGroup, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lblGroup, lv_color_hex(COL_CYAN), 0);
  lv_label_set_text(lblGroup, "SMARTSERVE");
  lv_obj_align(lblGroup, LV_ALIGN_CENTER, 0, -34);

  // Nachricht (gross, zentriert, umbrechend)
  lblMsg = lv_label_create(scr);
  lv_obj_set_style_text_font(lblMsg, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblMsg, lv_color_hex(COL_TEXT), 0);
  lv_label_set_long_mode(lblMsg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lblMsg, 180);
  lv_obj_set_style_text_align(lblMsg, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(lblMsg, "Bereit");
  lv_obj_align(lblMsg, LV_ALIGN_CENTER, 0, 6);
}

void setStatus(const char *t, uint32_t color) {
  lv_label_set_text(lblStatus, t);
  lv_obj_set_style_text_color(lblStatus, lv_color_hex(color), 0);
}

// ---------------------------------------------------------------- MQTT
void onMessage(char *topic, byte *payload, unsigned int len) {
  Serial.printf("[RX] %s\n", topic);
  StaticJsonDocument<512> d;
  if (deserializeJson(d, payload, len)) return;
  String group = String((const char *)(d["group_name"] | "SmartServe"));
  String msg   = String((const char *)(d["message"] | ""));
  group.toUpperCase();
  lv_label_set_text(lblGroup, group.c_str());
  lv_label_set_text(lblMsg, msg.c_str());
}

void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  setStatus("WLAN...", COL_SEC);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); lv_timer_handler(); }
  g_mac    = WiFi.macAddress();
  subGroup = "smartserve/groups/" + String(GROUP_ID);
  Serial.printf("\n[WiFi] IP=%s\n", WiFi.localIP().toString().c_str());
}

void ensureMqtt() {
  while (!mqtt.connected()) {
    setStatus("Broker...", COL_SEC);
    String cid = "watch-" + g_mac;
    if (mqtt.connect(cid.c_str())) {
      mqtt.subscribe(subGroup.c_str(), 1);
      setStatus("verbunden", COL_GREEN);
      Serial.printf("[MQTT] sub %s\n", subGroup.c_str());
    } else { delay(MQTT_RETRY_MS); lv_timer_handler(); }
  }
}

// ---------------------------------------------------------------- Arduino
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== SmartServe Watch — LVGL ===");

  tft.begin();
  tft.setRotation(0);

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, 240 * 40);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = 240;
  disp_drv.ver_res  = 240;
  disp_drv.flush_cb = flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  buildUI();
  lastTick = millis();

  ensureWifi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(512);
  ensureMqtt();
}

void loop() {
  uint32_t now = millis();
  lv_tick_inc(now - lastTick);
  lastTick = now;
  lv_timer_handler();

  ensureWifi();
  ensureMqtt();
  mqtt.loop();
  delay(5);
}
