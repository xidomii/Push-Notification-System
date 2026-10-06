// ============================================================
//  SmartServe — Watch GUI (LVGL)
//  Build:  pio run -e esp32dev-lvgl -t upload
//
//  Screens (GC9A01, 240x240 rund):
//    - Boot       : SmartServe -> WLAN... -> Broker... -> Bereit
//    - Watchface  : Statuspunkt + VERBUNDEN + Uhrzeit + Datum + Brand
//    - Aufgabe    : Gruppen-Chip + Nachricht + Zeit  (reine Anzeige)
//
//  Ablauf: kommt eine Nachricht -> Aufgaben-Screen fuer 10 s -> zurueck
//  zum Watchface. Kein accept/decline/done (bewusst entfernt). Neue
//  Nachricht waehrend der Anzeige setzt die 10 s neu.
//
//  Zeit: Backend publisht retained `smartserve/time` ({"epoch":..}) =
//  lokale Wanduhr (Europe/Vienna, per NTP). Watch rechnet HH:MM:SS + Datum.
//
//  Broker: per mDNS (smartserve.local, ESP32 ESPmDNS), Fallback = MQTT_HOST.
//
//  Board: ESP32 WROOM-32 · Display: GC9A01 (SPI, Pins in platformio.ini)
//
//  HINWEIS Umlaute: eingebaute Montserrat-Fonts = nur ASCII. Dynamische
//  Texte werden auf ASCII gefaltet (ae/oe/ue/ss). Latin-1-Font = spaeter.
// ============================================================

#include <WiFi.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "config.h"

// ── SmartServe-Palette (RGB) ───────────────────────────────────────
#define COL_BG      0x0e0f11
#define COL_PRI     0xe8eaf0
#define COL_SEC     0x8a90a3
#define COL_MUTED   0x4b5162
#define COL_ACCENT  0x3d5afe
#define COL_CYAN    0x22d3ee
#define COL_GREEN   0x00e07a
#define COL_DANGER  0xff4d4f

// Anzeigedauer einer Aufgabe, dann zurueck zum Watchface
#define TASK_SHOW_MS  10000

// ── Hardware / LVGL-Treiber ────────────────────────────────────────
TFT_eSPI     tft = TFT_eSPI();
WiFiClient   net;
PubSubClient mqtt(net);
String       g_mac, subGroup;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t         buf1[240 * 40];      // ~19 KB Teilpuffer
static lv_disp_drv_t      disp_drv;

// ── Screens / Zustand ──────────────────────────────────────────────
enum Screen { SCR_BOOT, SCR_FACE, SCR_TASK };
static Screen curScreen = SCR_BOOT;

// Echte Zeit (vom Backend via MQTT). Ohne Sync -> Platzhalter.
static bool     haveTime      = false;
static uint32_t timeBaseLocal = 0;   // lokale Epoch-Sekunden beim Sync
static uint32_t timeMs0       = 0;   // millis() beim Sync
static const char *WDAY[7] = { "SO", "MO", "DI", "MI", "DO", "FR", "SA" };

// LVGL-Objekte
static lv_obj_t *ring;
static lv_obj_t *pBoot, *lblBoot, *bootDot;
static lv_obj_t *pFace, *connWifi, *lblConn, *lblHM, *lblDate, *lblBr1, *lblBr2;
static lv_obj_t *pTask, *chip, *chipDot, *lblGroup, *lblMsg, *lblTTime;

static lv_timer_t *taskTimer = nullptr;

static void show(Screen s);

// ────────────────────────────────────────────── LVGL -> Display
static void flush_cb(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *color) {
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  // LVGL hat die Bytes bereits getauscht (LV_COLOR_16_SWAP=1) -> hier NICHT nochmal.
  tft.pushColors((uint16_t *)color, w * h, false);
  tft.endWrite();
  lv_disp_flush_ready(d);
}

// ────────────────────────────────────────────── Umlaut -> ASCII falten
static void foldAscii(const char *src, char *dst, size_t cap) {
  size_t o = 0;
  for (size_t i = 0; src[i] && o < cap - 2; i++) {
    unsigned char c = src[i];
    if (c == 0xC3 && src[i + 1]) {
      unsigned char n = src[++i];
      const char *r = nullptr;
      switch (n) {
        case 0xA4: case 0x84: r = (n==0x84)?"Ae":"ae"; break;  // ä Ä
        case 0xB6: case 0x96: r = (n==0x96)?"Oe":"oe"; break;  // ö Ö
        case 0xBC: case 0x9C: r = (n==0x9C)?"Ue":"ue"; break;  // ü Ü
        case 0x9F:            r = "ss";                 break;  // ß
      }
      if (r) { while (*r && o < cap - 1) dst[o++] = *r++; }
    } else if (c < 0x80) {
      dst[o++] = c;
    }
  }
  dst[o] = 0;
}

// ────────────────────────────────────────────── Zeit rechnen
static void civil(long z, int *mon, int *day) {   // Tage seit 1970 -> Monat/Tag
  z += 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp  = (5 * doy + 2) / 153;
  *day = doy - (153 * mp + 2) / 5 + 1;
  *mon = mp < 10 ? mp + 3 : mp - 9;
}

static void clockCompute(char *hm, char *ss, char *date) {
  if (!haveTime) { strcpy(hm, "--:--"); strcpy(ss, "--"); date[0] = 0; return; }
  uint32_t cur  = timeBaseLocal + (millis() - timeMs0) / 1000;
  uint32_t secs = cur % 86400UL;
  long     days = cur / 86400UL;
  sprintf(hm, "%02u:%02u", (unsigned)(secs / 3600), (unsigned)((secs % 3600) / 60));
  sprintf(ss, "%02u", (unsigned)(secs % 60));
  int mon, day; civil(days, &mon, &day);
  int dow = (int)((days + 4) % 7);          // 1970-01-01 = Donnerstag
  sprintf(date, "%s " LV_SYMBOL_BULLET " %02d.%02d", WDAY[dow], day, mon);
}

static void clockStampHM(char *out) {
  char hm[6], ss[3], d[16];
  clockCompute(hm, ss, d);
  strncpy(out, hm, 6);
}

// ────────────────────────────────────────────── Status-Ring (Bogen)
static void setRing(Screen s) {
  int frac;  uint32_t col;
  switch (s) {
    case SCR_TASK: frac = 72; col = COL_CYAN;   break;
    case SCR_FACE: frac = 33; col = COL_ACCENT; break;
    default:       frac = 10; col = COL_ACCENT; break;
  }
  lv_arc_set_value(ring, frac);
  lv_obj_set_style_arc_color(ring, lv_color_hex(col), LV_PART_INDICATOR);
}

// ────────────────────────────────────────────── Screen-Wechsel
static void show(Screen s) {
  curScreen = s;
  lv_obj_t *panels[] = { pBoot, pFace, pTask };
  Screen    ids[]    = { SCR_BOOT, SCR_FACE, SCR_TASK };
  for (int i = 0; i < 3; i++) {
    if (ids[i] == s) lv_obj_clear_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
    else             lv_obj_add_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_HIDDEN);
  setRing(s);
}

// ────────────────────────────────────────────── UI-Bausteine
static lv_obj_t *mkPanel() {
  lv_obj_t *p = lv_obj_create(lv_scr_act());
  lv_obj_set_size(p, 240, 240);
  lv_obj_center(p);
  lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(p, 0, 0);
  lv_obj_set_style_pad_all(p, 0, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  return p;
}

static lv_obj_t *mkLabel(lv_obj_t *par, const lv_font_t *font, uint32_t col, const char *txt) {
  lv_obj_t *l = lv_label_create(par);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  lv_label_set_text(l, txt);
  return l;
}

static lv_obj_t *mkDot(lv_obj_t *par, int d, uint32_t col) {
  lv_obj_t *o = lv_obj_create(par);
  lv_obj_set_size(o, d, d);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static void buildUI() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // ── Status-Ring am Rand ───────────────────────────────
  ring = lv_arc_create(scr);
  lv_obj_set_size(ring, 232, 232);
  lv_obj_center(ring);
  lv_arc_set_rotation(ring, 270);
  lv_arc_set_bg_angles(ring, 0, 360);
  lv_arc_set_range(ring, 0, 100);
  lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(ring, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(ring, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(ring, lv_color_hex(0x1a1d23), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);

  // ── BOOT ──────────────────────────────────────────────
  pBoot   = mkPanel();
  bootDot = mkDot(pBoot, 8, COL_ACCENT);
  lv_obj_align(bootDot, LV_ALIGN_CENTER, 0, -18);
  lblBoot = mkLabel(pBoot, &lv_font_montserrat_14, COL_SEC, "SmartServe");
  lv_obj_align(lblBoot, LV_ALIGN_CENTER, 0, 10);

  // ── WATCHFACE ─────────────────────────────────────────
  pFace    = mkPanel();
  connWifi = mkLabel(pFace, &lv_font_montserrat_14, COL_GREEN, LV_SYMBOL_WIFI);
  lv_obj_align(connWifi, LV_ALIGN_TOP_MID, -48, 32);
  lblConn = mkLabel(pFace, &lv_font_montserrat_12, COL_SEC, "VERBUNDEN");
  lv_obj_align(lblConn, LV_ALIGN_TOP_MID, 10, 34);

  lblHM = mkLabel(pFace, &lv_font_montserrat_40, COL_PRI, "--:--");
  lv_obj_align(lblHM, LV_ALIGN_CENTER, 0, -8);

  lblDate = mkLabel(pFace, &lv_font_montserrat_14, COL_SEC, "");
  lv_obj_align(lblDate, LV_ALIGN_CENTER, 0, 30);

  lblBr1 = mkLabel(pFace, &lv_font_montserrat_12, COL_MUTED, "SMART");
  lblBr2 = mkLabel(pFace, &lv_font_montserrat_12, COL_SEC,   "SERVE");
  lv_obj_align(lblBr1, LV_ALIGN_BOTTOM_MID, -17, -34);
  lv_obj_align_to(lblBr2, lblBr1, LV_ALIGN_OUT_RIGHT_MID, 0, 0);

  // ── TASK (reine Anzeige) ──────────────────────────────
  pTask = mkPanel();
  chip  = lv_obj_create(pTask);
  lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(chip, LV_ALIGN_CENTER, 0, -52);
  lv_obj_set_style_radius(chip, 20, 0);
  lv_obj_set_style_bg_color(chip, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_10, 0);
  lv_obj_set_style_border_color(chip, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_border_width(chip, 1, 0);
  lv_obj_set_style_border_opa(chip, LV_OPA_40, 0);
  lv_obj_set_style_pad_hor(chip, 12, 0);
  lv_obj_set_style_pad_ver(chip, 5, 0);
  lv_obj_set_style_pad_column(chip, 7, 0);
  lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  chipDot  = mkDot(chip, 6, COL_CYAN);
  lblGroup = mkLabel(chip, &lv_font_montserrat_12, COL_CYAN, "SMARTSERVE");

  lblMsg = mkLabel(pTask, &lv_font_montserrat_20, COL_PRI, "Bereit");
  lv_label_set_long_mode(lblMsg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lblMsg, 190);
  lv_obj_set_style_text_align(lblMsg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lblMsg, LV_ALIGN_CENTER, 0, 6);

  lblTTime = mkLabel(pTask, &lv_font_montserrat_12, COL_MUTED, "--:--");
  lv_obj_align(lblTTime, LV_ALIGN_BOTTOM_MID, 0, -34);

  show(SCR_BOOT);
}

// ────────────────────────────────────────────── Aufgabe anzeigen
static void taskReturn(lv_timer_t *t) { (void)t; show(SCR_FACE); taskTimer = nullptr; }

static void showTask(const char *groupRaw, const char *msgRaw) {
  char group[24], msg[80], up[24], tm[6];
  foldAscii(groupRaw, group, sizeof(group));
  foldAscii(msgRaw,   msg,   sizeof(msg));

  strncpy(up, group, sizeof(up) - 1); up[sizeof(up)-1] = 0;
  for (char *p = up; *p; ++p) *p = toupper(*p);
  clockStampHM(tm);

  lv_label_set_text(lblGroup, up);
  lv_label_set_text(lblMsg, msg);
  lv_label_set_text(lblTTime, tm);
  show(SCR_TASK);

  // 10-s-Timer (neu setzen, falls weitere Nachricht reinkommt)
  if (taskTimer) lv_timer_del(taskTimer);
  taskTimer = lv_timer_create(taskReturn, TASK_SHOW_MS, NULL);
  lv_timer_set_repeat_count(taskTimer, 1);
}

// ────────────────────────────────────────────── MQTT
static void onMessage(char *topic, byte *payload, unsigned int len) {
  if (strcmp(topic, "smartserve/time") == 0) {       // Zeit-Sync
    StaticJsonDocument<96> t;
    if (!deserializeJson(t, payload, len) && t.containsKey("epoch")) {
      timeBaseLocal = (uint32_t)(t["epoch"].as<long long>());
      timeMs0       = millis();
      haveTime      = true;
    }
    return;
  }

  Serial.printf("[RX] %s\n", topic);
  StaticJsonDocument<512> d;
  if (deserializeJson(d, payload, len)) return;
  const char *type = d["type"] | "";
  if (strcmp(type, "task") == 0 || d.containsKey("message")) {
    const char *group = d["group_name"] | "SmartServe";
    const char *msg   = d["message"]    | "";
    showTask(group, msg);
  }
}

static void setConn(bool on) {
  lv_obj_set_style_text_color(connWifi, lv_color_hex(on ? COL_GREEN : COL_DANGER), 0);
  lv_label_set_text(lblConn, on ? "VERBUNDEN" : "GETRENNT");
}

static void bootStep(const char *t) { lv_label_set_text(lblBoot, t); lv_timer_handler(); }

static void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (curScreen == SCR_BOOT) bootStep("WLAN...");
  else setConn(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); lv_timer_handler(); }
  g_mac    = WiFi.macAddress();
  subGroup = "smartserve/groups/+";        // alle Gruppen (Wildcard), Name kommt im Payload
  MDNS.begin("smartserve-watch");
  Serial.printf("\n[WiFi] IP=%s MAC=%s\n", WiFi.localIP().toString().c_str(), g_mac.c_str());
}

// Pi-Broker: mDNS (smartserve.local) zuerst, sonst Fallback-IP aus config.h
static void resolveBroker() {
  IPAddress ip = MDNS.queryHost(PI_HOSTNAME, 2000);
  if ((uint32_t)ip != 0) {
    mqtt.setServer(ip, MQTT_PORT);
    Serial.printf("[mDNS] %s.local -> %s\n", PI_HOSTNAME, ip.toString().c_str());
  } else {
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    Serial.printf("[mDNS] fehlgeschlagen -> Fallback %s\n", MQTT_HOST);
  }
}

static void ensureMqtt() {
  while (!mqtt.connected()) {
    if (curScreen == SCR_BOOT) bootStep("Broker...");
    else setConn(false);
    resolveBroker();
    String cid = "watch-" + g_mac;
    if (mqtt.connect(cid.c_str())) {
      mqtt.subscribe(subGroup.c_str(), 1);
      mqtt.subscribe("smartserve/time", 1);
      Serial.printf("[MQTT] sub %s + smartserve/time\n", subGroup.c_str());
      setConn(true);
      if (curScreen == SCR_BOOT) { bootStep("Bereit"); delay(400); show(SCR_FACE); }
    } else { delay(MQTT_RETRY_MS); lv_timer_handler(); }
  }
}

// ────────────────────────────────────────────── Arduino
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
  lv_timer_handler();

  ensureWifi();
  mqtt.setCallback(onMessage);           // Broker-IP setzt resolveBroker() in ensureMqtt
  mqtt.setBufferSize(512);
  ensureMqtt();
}

void loop() {
  static uint32_t lastTick = 0, lastClock = 0;
  uint32_t now = millis();
  lv_tick_inc(now - lastTick);
  lastTick = now;

  if (curScreen == SCR_FACE && now - lastClock > 1000) {
    lastClock = now;
    char hm[6], ss[3], date[16];
    clockCompute(hm, ss, date);        // ss ungenutzt (keine Sekundenanzeige)
    lv_label_set_text(lblHM, hm);
    lv_label_set_text(lblDate, date);
  }

  lv_timer_handler();

  ensureWifi();
  ensureMqtt();
  setConn(mqtt.connected());
  mqtt.loop();
  delay(5);
}
