// ============================================================
//  SmartServe — Watch GUI (LVGL, volle Screens)
//  Build:  pio run -e esp32dev-lvgl -t upload
//
//  Setzt das eingefrorene Design aus gui-emulator/index.html am
//  echten Display um (GC9A01, 240x240 rund):
//    - Boot       : SmartServe -> WLAN... -> Broker... -> Bereit
//    - Watchface  : Statuspunkt + VERBUNDEN + Uhrzeit + Datum + Brand
//    - Aufgabe    : Gruppen-Chip + Nachricht + Zeit + runde X/OK-Buttons
//    - Erledigt   : Haken-Splash, dann zurueck zum Watchface
//    - Liste      : letzte Aufgaben mit Statuspunkt
//    - Status-Ring: farbiger Bogen am Displayrand je Screen
//
//  Zeit: das Backend publiziert retained `smartserve/time` ({"epoch":..})
//  = lokale Wanduhr (Europe/Vienna). Watch rechnet daraus HH:MM:SS + Datum.
//  Ohne Zeit-Sync -> "--:--".
//
//  Eingabe = 3 physische Taster (accept / decline / done), Pins in
//  config.h. Ohne Taster laeuft die GUI trotzdem (MQTT -> Aufgabe).
//
//  Board: ESP32 WROOM-32 · Display: GC9A01 (SPI, Pins in platformio.ini)
//
//  HINWEIS Umlaute: die eingebauten Montserrat-Fonts enthalten nur ASCII.
//  Dynamische Backend-Texte werden auf ASCII gefaltet (ae/oe/ue/ss), damit
//  keine leeren Kaestchen entstehen. Pixelgenaue Umlaute = eigener Font (MS5).
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
#define COL_PANEL   0x14161a
#define COL_BORDER  0x272b35
#define COL_PRI     0xe8eaf0
#define COL_SEC     0x8a90a3
#define COL_MUTED   0x4b5162
#define COL_ACCENT  0x3d5afe
#define COL_CYAN    0x22d3ee
#define COL_GREEN   0x00e07a
#define COL_AMBER   0xffb020
#define COL_DANGER  0xff4d4f

// ── Hardware / LVGL-Treiber ────────────────────────────────────────
TFT_eSPI     tft = TFT_eSPI();
WiFiClient   net;
PubSubClient mqtt(net);
String       g_mac, subGroup;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t         buf1[240 * 40];      // ~19 KB Teilpuffer
static lv_disp_drv_t      disp_drv;

// ── Screens / Zustand ──────────────────────────────────────────────
enum Screen { SCR_BOOT, SCR_FACE, SCR_TASK, SCR_DONE, SCR_LIST };
enum TStatus { ST_NEW, ST_ONGOING, ST_DONE, ST_DECLINED };

static Screen curScreen = SCR_BOOT;

// Aufgaben-Liste (Ringpuffer, neueste zuerst)
struct Task { char group[24]; char msg[72]; char time[6]; uint8_t status; };
static Task    tasks[8];
static int     taskCount = 0;

// Echte Zeit (vom Backend via MQTT). Ohne Sync -> Platzhalter.
static bool     haveTime      = false;
static uint32_t timeBaseLocal = 0;   // lokale Epoch-Sekunden beim Sync
static uint32_t timeMs0       = 0;   // millis() beim Sync

static const char *WDAY[7] = { "SO", "MO", "DI", "MI", "DO", "FR", "SA" };

// LVGL-Objekte
static lv_obj_t *ring;
static lv_obj_t *pBoot, *lblBoot, *bootDot;
static lv_obj_t *pFace, *connDot, *lblConn, *lblHM, *lblSS, *lblDate, *lblBr1, *lblBr2;
static lv_obj_t *pTask, *chip, *chipDot, *lblGroup, *lblMsg, *lblTTime, *lblState;
static lv_obj_t *btnNo, *btnOk, *btnDone;
static lv_obj_t *pDone;
static lv_obj_t *pList, *listCont;

static lv_timer_t *doneTimer = nullptr;

static void show(Screen s);
static void renderList();

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
    if (c == 0xC3 && src[i + 1]) {           // UTF-8 Latin-1 Supplement
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
    }                                        // sonstige Nicht-ASCII: weglassen
  }
  dst[o] = 0;
}

// ────────────────────────────────────────────── Zeit rechnen
// Zivildatum aus Tagen seit 1970 (Howard Hinnant)
static void civil(long z, int *mon, int *day) {
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
  int mon, day;
  civil(days, &mon, &day);
  int dow = (int)((days + 4) % 7);           // 1970-01-01 = Donnerstag
  sprintf(date, "%s " LV_SYMBOL_BULLET " %02d.%02d", WDAY[dow], day, mon);
}

static void clockStamp(char *out) {
  char hm[6], ss[3], d[16];
  clockCompute(hm, ss, d);
  strncpy(out, hm, 6);
}

// ────────────────────────────────────────────── Status-Ring (Bogen)
static void setRing(Screen s) {
  int frac;  uint32_t col;
  switch (s) {
    case SCR_TASK: frac = (taskCount && tasks[0].status == ST_ONGOING) ? 72 : 50;
                   col  = COL_AMBER; break;
    case SCR_LIST: frac = 100; col = COL_ACCENT; break;
    case SCR_DONE: frac = 100; col = COL_GREEN;  break;
    case SCR_FACE: frac = 33;  col = COL_ACCENT; break;
    default:       frac = 10;  col = COL_ACCENT; break;
  }
  lv_arc_set_value(ring, frac);
  lv_obj_set_style_arc_color(ring, lv_color_hex(col), LV_PART_INDICATOR);
}

// ────────────────────────────────────────────── Screen-Wechsel
static void show(Screen s) {
  curScreen = s;
  lv_obj_t *panels[] = { pBoot, pFace, pTask, pDone, pList };
  Screen    ids[]    = { SCR_BOOT, SCR_FACE, SCR_TASK, SCR_DONE, SCR_LIST };
  for (int i = 0; i < 5; i++) {
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

// runder Aktions-Button (nur Optik; Eingabe = physische Taster)
static lv_obj_t *mkActBtn(lv_obj_t *par, const char *sym, uint32_t col, int w) {
  lv_obj_t *b = lv_obj_create(par);
  lv_obj_set_size(b, w, 40);
  lv_obj_set_style_radius(b, 14, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(col), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_10, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(col), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_opa(b, LV_OPA_40, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *l = mkLabel(b, &lv_font_montserrat_20, col, sym);
  lv_obj_center(l);
  return b;
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
  pFace   = mkPanel();
  connDot = mkDot(pFace, 7, COL_GREEN);
  lv_obj_align(connDot, LV_ALIGN_TOP_MID, -42, 36);
  lblConn = mkLabel(pFace, &lv_font_montserrat_12, COL_SEC, "VERBUNDEN");
  lv_obj_align(lblConn, LV_ALIGN_TOP_MID, 6, 34);

  lblHM = mkLabel(pFace, &lv_font_montserrat_40, COL_PRI, "--:--");
  lv_obj_align(lblHM, LV_ALIGN_CENTER, -8, -8);
  lblSS = mkLabel(pFace, &lv_font_montserrat_14, COL_ACCENT, "--");
  lv_obj_align_to(lblSS, lblHM, LV_ALIGN_OUT_RIGHT_TOP, 3, 4);

  lblDate = mkLabel(pFace, &lv_font_montserrat_14, COL_SEC, "");
  lv_obj_align(lblDate, LV_ALIGN_CENTER, 0, 30);

  lblBr1 = mkLabel(pFace, &lv_font_montserrat_12, COL_MUTED, "SMART");
  lblBr2 = mkLabel(pFace, &lv_font_montserrat_12, COL_SEC,   "SERVE");
  lv_obj_align(lblBr1, LV_ALIGN_BOTTOM_MID, -17, -34);
  lv_obj_align_to(lblBr2, lblBr1, LV_ALIGN_OUT_RIGHT_MID, 0, 0);

  // ── TASK ──────────────────────────────────────────────
  pTask = mkPanel();
  chip  = lv_obj_create(pTask);
  lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(chip, LV_ALIGN_CENTER, 0, -56);
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
  lv_obj_set_width(lblMsg, 184);
  lv_obj_set_style_text_align(lblMsg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lblMsg, LV_ALIGN_CENTER, 0, -4);

  lblTTime = mkLabel(pTask, &lv_font_montserrat_12, COL_MUTED, "--:--");
  lv_obj_align(lblTTime, LV_ALIGN_CENTER, 0, 36);

  lblState = mkLabel(pTask, &lv_font_montserrat_12, COL_AMBER, "AKTIV");
  lv_obj_align(lblState, LV_ALIGN_CENTER, 0, 36);
  lv_obj_add_flag(lblState, LV_OBJ_FLAG_HIDDEN);

  // runde Aktions-Buttons (Optik wie Emulator; echte Eingabe = Taster)
  btnNo = mkActBtn(pTask, LV_SYMBOL_CLOSE, COL_DANGER, 56);
  lv_obj_align(btnNo, LV_ALIGN_BOTTOM_MID, -34, -22);
  btnOk = mkActBtn(pTask, LV_SYMBOL_OK, COL_GREEN, 56);
  lv_obj_align(btnOk, LV_ALIGN_BOTTOM_MID, 34, -22);
  btnDone = mkActBtn(pTask, LV_SYMBOL_OK "  Erledigt", COL_GREEN, 140);
  lv_obj_align(btnDone, LV_ALIGN_BOTTOM_MID, 0, -22);
  lv_obj_add_flag(btnDone, LV_OBJ_FLAG_HIDDEN);

  // ── DONE ──────────────────────────────────────────────
  pDone = mkPanel();
  lv_obj_t *mark = lv_obj_create(pDone);
  lv_obj_set_size(mark, 74, 74);
  lv_obj_align(mark, LV_ALIGN_CENTER, 0, -16);
  lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(mark, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(mark, lv_color_hex(COL_GREEN), 0);
  lv_obj_set_style_border_width(mark, 2, 0);
  lv_obj_clear_flag(mark, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *ok = mkLabel(mark, &lv_font_montserrat_40, COL_GREEN, LV_SYMBOL_OK);
  lv_obj_center(ok);
  lv_obj_t *cap = mkLabel(pDone, &lv_font_montserrat_14, COL_SEC, "Erledigt");
  lv_obj_align(cap, LV_ALIGN_CENTER, 0, 44);

  // ── LISTE ─────────────────────────────────────────────
  pList = mkPanel();
  lv_obj_t *head = mkLabel(pList, &lv_font_montserrat_12, COL_SEC, "AUFGABEN");
  lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 30);
  listCont = lv_obj_create(pList);
  lv_obj_set_size(listCont, 200, 150);
  lv_obj_align(listCont, LV_ALIGN_TOP_MID, 0, 56);
  lv_obj_set_style_bg_opa(listCont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(listCont, 0, 0);
  lv_obj_set_style_pad_all(listCont, 0, 0);
  lv_obj_set_style_pad_row(listCont, 7, 0);
  lv_obj_set_flex_flow(listCont, LV_FLEX_FLOW_COLUMN);

  show(SCR_BOOT);
}

// ────────────────────────────────────────────── Liste rendern
static uint32_t statusColor(uint8_t s) {
  switch (s) { case ST_NEW: return COL_CYAN; case ST_ONGOING: return COL_AMBER;
               case ST_DONE: return COL_GREEN; default: return COL_DANGER; }
}

static void renderList() {
  lv_obj_clean(listCont);
  if (!taskCount) {
    lv_obj_t *e = mkLabel(listCont, &lv_font_montserrat_12, COL_MUTED, "keine");
    lv_obj_set_width(e, LV_PCT(100));
    lv_obj_set_style_text_align(e, LV_TEXT_ALIGN_CENTER, 0);
    return;
  }
  for (int i = 0; i < taskCount; i++) {
    lv_obj_t *row = lv_obj_create(listCont);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_border_color(row, lv_color_hex(COL_BORDER), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_set_style_pad_ver(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    mkDot(row, 7, statusColor(tasks[i].status));
    mkLabel(row, &lv_font_montserrat_12, COL_CYAN, tasks[i].group);
    lv_obj_t *m = mkLabel(row, &lv_font_montserrat_12, COL_PRI, tasks[i].msg);
    lv_label_set_long_mode(m, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(m, 1);
    lv_obj_set_width(m, 0);
  }
}

// ────────────────────────────────────────────── Aufgabe eintragen
static void pushTask(const char *groupRaw, const char *msgRaw) {
  char group[24], msg[72];
  foldAscii(groupRaw, group, sizeof(group));
  foldAscii(msgRaw,   msg,   sizeof(msg));

  for (int i = min(taskCount, (int)(sizeof(tasks)/sizeof(tasks[0])) - 1); i > 0; i--)
    tasks[i] = tasks[i - 1];
  strncpy(tasks[0].group, group, sizeof(tasks[0].group) - 1);
  tasks[0].group[sizeof(tasks[0].group) - 1] = 0;
  strncpy(tasks[0].msg, msg, sizeof(tasks[0].msg) - 1);
  tasks[0].msg[sizeof(tasks[0].msg) - 1] = 0;
  clockStamp(tasks[0].time);
  tasks[0].status = ST_NEW;
  if (taskCount < (int)(sizeof(tasks)/sizeof(tasks[0]))) taskCount++;

  char up[24]; strncpy(up, group, sizeof(up) - 1); up[sizeof(up)-1] = 0;
  for (char *p = up; *p; ++p) *p = toupper(*p);
  lv_label_set_text(lblGroup, up);
  lv_label_set_text(lblMsg, msg);
  lv_label_set_text(lblTTime, tasks[0].time);
  lv_obj_clear_flag(lblTTime, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(lblState, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btnNo, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btnOk, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(btnDone, LV_OBJ_FLAG_HIDDEN);
  renderList();
  show(SCR_TASK);
}

// ────────────────────────────────────────────── Aktionen
static void actAccept() {
  if (curScreen != SCR_TASK || !taskCount) return;
  tasks[0].status = ST_ONGOING;
  lv_obj_add_flag(btnNo, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(btnOk, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btnDone, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(lblTTime, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(lblState, "AKTIV");
  lv_obj_clear_flag(lblState, LV_OBJ_FLAG_HIDDEN);
  renderList();
  setRing(SCR_TASK);
}

static void doneReturn(lv_timer_t *t) { (void)t; show(SCR_FACE); doneTimer = nullptr; }

static void actDone() {
  if (curScreen != SCR_TASK || !taskCount) return;
  tasks[0].status = ST_DONE;
  renderList();
  show(SCR_DONE);
  if (doneTimer) lv_timer_del(doneTimer);
  doneTimer = lv_timer_create(doneReturn, 1600, NULL);
  lv_timer_set_repeat_count(doneTimer, 1);
}

static void actDecline() {
  if (curScreen == SCR_TASK && taskCount) { tasks[0].status = ST_DECLINED; renderList(); }
  show(SCR_FACE);
}

static void handleButton(int pin) {
  if (pin == BTN_ACCEPT) {
    if (curScreen == SCR_TASK) {
      if (taskCount && tasks[0].status == ST_ONGOING) actDone();
      else actAccept();
    } else if (curScreen == SCR_FACE) show(SCR_LIST);
    else show(SCR_FACE);
  } else if (pin == BTN_DECLINE) {
    actDecline();
  } else if (pin == BTN_DONE) {
    if (curScreen == SCR_TASK) actDone();
    else if (curScreen == SCR_FACE) show(SCR_LIST);
    else show(SCR_FACE);
  }
}

// ────────────────────────────────────────────── Taster lesen (entprellt)
static void initButtons() {
  const int pins[] = { BTN_ACCEPT, BTN_DECLINE, BTN_DONE };
  for (int p : pins) if (p >= 0) pinMode(p, INPUT_PULLUP);
}

static void pollButtons() {
  static uint32_t lastChange[40] = {0};
  static bool     lastState[40]  = {0};
  const int pins[] = { BTN_ACCEPT, BTN_DECLINE, BTN_DONE };
  uint32_t now = millis();
  for (int p : pins) {
    if (p < 0 || p >= 40) continue;
    bool pressed = (digitalRead(p) == LOW);
    if (pressed != lastState[p] && now - lastChange[p] > 40) {
      lastChange[p] = now;
      lastState[p]  = pressed;
      if (pressed) handleButton(p);
    }
  }
}

// ────────────────────────────────────────────── MQTT
static void onMessage(char *topic, byte *payload, unsigned int len) {
  // Zeit-Sync vom Backend
  if (strcmp(topic, "smartserve/time") == 0) {
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
    pushTask(group, msg);
  }
}

static void setConn(bool on) {
  lv_obj_set_style_bg_color(connDot, lv_color_hex(on ? COL_GREEN : COL_DANGER), 0);
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
  subGroup = "smartserve/groups/" + String(GROUP_ID);
  MDNS.begin("smartserve-watch");          // eigener Responder + queryHost nutzbar
  Serial.printf("\n[WiFi] IP=%s MAC=%s\n", WiFi.localIP().toString().c_str(), g_mac.c_str());
}

// Pi-Broker finden: mDNS (smartserve.local) zuerst, sonst Fallback-IP aus config.h
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
    resolveBroker();                       // IP bei jedem Versuch neu aufloesen
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
  Serial.println("\n=== SmartServe Watch — LVGL (volle GUI) ===");

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
  initButtons();
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

  if (curScreen == SCR_FACE && now - lastClock > 500) {
    lastClock = now;
    char hm[6], ss[3], date[16];
    clockCompute(hm, ss, date);
    lv_label_set_text(lblHM, hm);
    lv_label_set_text(lblSS, ss);
    lv_label_set_text(lblDate, date);
  }

  pollButtons();
  lv_timer_handler();

  ensureWifi();
  ensureMqtt();
  setConn(mqtt.connected());
  mqtt.loop();
  delay(5);
}
