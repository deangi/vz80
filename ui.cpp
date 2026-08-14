#include "ui.h"
#include "SD_FTP_Server/src/SD_FTP_Server.h"
#include "config.h"
#include "appconfig.h"
#include "platform.h"
#include "telnet.h"
#include "ftp.h"
#include "host_time.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <SD_MMC.h>
#include <string.h>

#ifndef TFT_BL
#define TFT_BL 45
#endif

#define COL_BG       0x0009
#define COL_TITLE    0x001F
#define COL_ITEM     0x4208
#define COL_ITEM_HI  0x0320
#define COL_TEXT     TFT_WHITE
#define COL_DIM      0x9CD3
#define COL_DANGER   0xC000

#define MENU_VISIBLE 4
#define Htb          26
#define Hmb          30
#define Hbt          30
#define Hspc         6
#define ITEM_Y0      (Htb + Hspc)
#define ITEM_H       (Hmb + Hspc)
#define NAV_Y        (TFT_H - Hbt)

#define HIT_NONE -1
#define HIT_BACK -2
#define HIT_UP   -3
#define HIT_DOWN -4

enum Screen {
  SC_CLOSED, SC_MAIN, SC_DRIVES, SC_DRIVE, SC_DISK_PICKER,
  SC_WIFI_PICKER, SC_VZ80_PICKER,
  SC_CONFIRM_COPY, SC_CONFIRM_DRIVE_REBOOT, SC_INFO, SC_BRIGHT,
  SC_CONFIRM_RESET
};

extern AppConfig cfg;

static Screen g_screen = SC_CLOSED;
static Screen g_after_cancel = SC_MAIN;
static bool g_dirty = false;
static bool g_reboot = false;
static bool g_keyboard = false;
static bool g_esp_restart = false;
static int g_sel = 0;
static int g_scroll = 0;
static uint8_t g_bright = 255;

#define MAX_VARIANTS 16
static char g_variants[MAX_VARIANTS][44];
static int g_variant_count = 0;

#define MAX_FILES 64
static char g_files[MAX_FILES][44];
static int g_file_count = 0;

#define MAX_ITEMS 68
static char g_title[40];
static char g_items[MAX_ITEMS][44];
static int g_count = 0;

static char g_pending_src[72];
static char g_pending_dst[32];
static char g_pending_name[44];
static UiDriveMountFn g_drive_mount_fn = nullptr;

static const char* const DRIVE_NAMES[4] = { "A", "B", "C", "D" };

static const String* drive_path(int d) {
  switch (d) {
    case 0: return &cfg.disk_a;
    case 1: return &cfg.disk_b;
    case 2: return &cfg.disk_c;
    case 3: return &cfg.disk_d;
    default: return &cfg.disk_a;
  }
}

static String* mutable_drive_path(int d) {
  switch (d) {
    case 0: return &cfg.disk_a;
    case 1: return &cfg.disk_b;
    case 2: return &cfg.disk_c;
    case 3: return &cfg.disk_d;
    default: return &cfg.disk_a;
  }
}

static bool supported_disk_ext(const char* base) {
  const char* dot = strrchr(base, '.');
  return dot && !strcasecmp(dot, ".dsk");
}

static void scan_disk_files() {
  SD_FTP_StorageGuard guard;
  g_file_count = 0;
  fs::File root = SD_MMC.open("/");
  if (!root) return;

  for (fs::File f = root.openNextFile(); f && g_file_count < MAX_FILES;
       f = root.openNextFile()) {
    if (!f.isDirectory()) {
      const char* n = f.name();
      const char* slash = strrchr(n, '/');
      const char* base = slash ? slash + 1 : n;
      if (supported_disk_ext(base)) {
        strncpy(g_files[g_file_count], base, sizeof(g_files[g_file_count]) - 1);
        g_files[g_file_count][sizeof(g_files[g_file_count]) - 1] = 0;
        g_file_count++;
      }
    }
    f.close();
  }
  root.close();
}

void ui_init() {
  g_screen = SC_CLOSED;
  g_dirty = false;
  g_reboot = false;
  g_keyboard = false;
  g_esp_restart = false;
  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, g_bright);
}

void ui_set_drive_mount_callback(UiDriveMountFn fn) {
  g_drive_mount_fn = fn;
}

bool ui_is_open()             { return g_screen != SC_CLOSED; }
bool ui_consume_reboot()      { bool r = g_reboot; g_reboot = false; return r; }
bool ui_consume_keyboard()    { bool r = g_keyboard; g_keyboard = false; return r; }
bool ui_consume_esp_restart() { bool r = g_esp_restart; g_esp_restart = false; return r; }

static void scan_variants(const char* prefix) {
  g_variant_count = config_list_variants(prefix, g_variants, MAX_VARIANTS);
}

static void rebuild() {
  g_count = 0;
  switch (g_screen) {
    case SC_MAIN:
      strcpy(g_title, "vZ80 Settings");
      strcpy(g_items[g_count++], "Drives");
      strcpy(g_items[g_count++], "Reboot Emulator");
      strcpy(g_items[g_count++], "WiFi Config");
      strcpy(g_items[g_count++], "vZ80 Config");
      strcpy(g_items[g_count++], "System Info");
      strcpy(g_items[g_count++], "Brightness");
      strcpy(g_items[g_count++], "Keyboard");
      strcpy(g_items[g_count++], "Reset ESP32");
      break;
    case SC_DRIVES:
      strcpy(g_title, "Drives");
      for (int d = 0; d < 4; d++) {
        const String* p = drive_path(d);
        snprintf(g_items[g_count++], 44, "%s: %s",
                 DRIVE_NAMES[d], p->length() ? p->c_str() : "(empty)");
      }
      break;
    case SC_DRIVE: {
      const String* p = drive_path(g_sel);
      snprintf(g_title, sizeof(g_title), "Drive %s", DRIVE_NAMES[g_sel]);
      strcpy(g_items[g_count++], p->length() ? "Change Image" : "Mount Image");
      if (p->length()) strcpy(g_items[g_count++], "Dismount");
      break;
    }
    case SC_DISK_PICKER:
      snprintf(g_title, sizeof(g_title), "Mount into %s", DRIVE_NAMES[g_sel]);
      for (int i = 0; i < g_file_count && g_count < MAX_ITEMS; i++) {
        strncpy(g_items[g_count], g_files[i], sizeof(g_items[g_count]) - 1);
        g_items[g_count][sizeof(g_items[g_count]) - 1] = 0;
        g_count++;
      }
      if (g_count == 0) strcpy(g_items[g_count++], "(no disk images)");
      break;
    case SC_WIFI_PICKER:
      strcpy(g_title, "Select WiFi Config");
      for (int i = 0; i < g_variant_count && g_count < MAX_ITEMS; i++)
        snprintf(g_items[g_count++], 44, "wificonfig-%s.ini", g_variants[i]);
      if (g_count == 0) strcpy(g_items[g_count++], "(no variants)");
      break;
    case SC_VZ80_PICKER:
      strcpy(g_title, "Select vZ80 Config");
      for (int i = 0; i < g_variant_count && g_count < MAX_ITEMS; i++)
        snprintf(g_items[g_count++], 44, "z80config-%s.ini", g_variants[i]);
      if (g_count == 0) strcpy(g_items[g_count++], "(no variants)");
      break;
    case SC_CONFIRM_COPY:
      snprintf(g_title, sizeof(g_title), "Use %s?", g_pending_name);
      strcpy(g_items[g_count++], "Apply and Reset ESP32");
      strcpy(g_items[g_count++], "Cancel");
      break;
    case SC_CONFIRM_DRIVE_REBOOT:
      strcpy(g_title, "Reboot after drive change?");
      strcpy(g_items[g_count++], "Reboot Z80");
      strcpy(g_items[g_count++], "Continue Running");
      break;
    case SC_INFO:
      strcpy(g_title, "System Info");
      break;
    case SC_BRIGHT:
      snprintf(g_title, sizeof(g_title), "Brightness  %d%%", (g_bright * 100) / 255);
      strcpy(g_items[g_count++], "-  Dimmer");
      strcpy(g_items[g_count++], "+  Brighter");
      break;
    case SC_CONFIRM_RESET:
      strcpy(g_title, "Reset ESP32 now?");
      strcpy(g_items[g_count++], "Reset ESP32");
      strcpy(g_items[g_count++], "Cancel");
      break;
    default:
      break;
  }

  if (g_scroll > g_count - MENU_VISIBLE) g_scroll = g_count - MENU_VISIBLE;
  if (g_scroll < 0) g_scroll = 0;
}

static void go(Screen s) {
  g_screen = s;
  g_scroll = 0;
  rebuild();
  g_dirty = true;
}

void ui_open() { go(SC_MAIN); }

static int list_hit(int x, int y) {
  const int hit_y0 = ITEM_Y0 - Hspc / 2;
  const int hit_y1 = hit_y0 + MENU_VISIBLE * ITEM_H;
  if (y >= hit_y0 && y < hit_y1 && x >= 6 && x < 314) {
    int slot = (y - hit_y0) / ITEM_H;
    return slot;
  }
  if (y >= NAV_Y) {
    if (g_count > MENU_VISIBLE) {
      if (x < 156) return HIT_BACK;
      if (x < 236) return HIT_UP;
      return HIT_DOWN;
    }
    return HIT_BACK;
  }
  return HIT_NONE;
}

static void back() {
  switch (g_screen) {
    case SC_MAIN: g_screen = SC_CLOSED; g_dirty = true; break;
    case SC_DRIVES:
      go(SC_MAIN);
      break;
    case SC_DRIVE:
    case SC_DISK_PICKER:
      go(SC_DRIVES);
      break;
    case SC_WIFI_PICKER:
    case SC_VZ80_PICKER:
    case SC_INFO:
    case SC_BRIGHT:
    case SC_CONFIRM_RESET:
      go(SC_MAIN);
      break;
    case SC_CONFIRM_COPY:
      go(g_after_cancel);
      break;
    case SC_CONFIRM_DRIVE_REBOOT:
      go(SC_DRIVES);
      break;
    default:
      go(SC_MAIN);
      break;
  }
}

static void prepare_config_copy(const char* prefix, const char* variant,
                                const char* dst, Screen after_cancel) {
  snprintf(g_pending_src, sizeof(g_pending_src), "/%s%s.ini", prefix, variant);
  strncpy(g_pending_dst, dst, sizeof(g_pending_dst) - 1);
  g_pending_dst[sizeof(g_pending_dst) - 1] = 0;
  snprintf(g_pending_name, sizeof(g_pending_name), "%s%s.ini", prefix, variant);
  g_after_cancel = after_cancel;
  go(SC_CONFIRM_COPY);
}

static void activate(int idx) {
  if (idx < 0 || idx >= g_count) return;

  switch (g_screen) {
    case SC_MAIN:
      if      (idx == 0) go(SC_DRIVES);
      else if (idx == 1) { g_reboot = true; g_screen = SC_CLOSED; g_dirty = true; }
      else if (idx == 2) { scan_variants("wificonfig-"); go(SC_WIFI_PICKER); }
      else if (idx == 3) { scan_variants("z80config-"); go(SC_VZ80_PICKER); }
      else if (idx == 4) go(SC_INFO);
      else if (idx == 5) go(SC_BRIGHT);
      else if (idx == 6) { g_keyboard = true; g_screen = SC_CLOSED; g_dirty = true; }
      else if (idx == 7) go(SC_CONFIRM_RESET);
      break;
    case SC_DRIVES:
      if (idx >= 0 && idx < 4) {
        g_sel = idx;
        go(SC_DRIVE);
      }
      break;
    case SC_DRIVE:
      if (idx == 0) {
        scan_disk_files();
        go(SC_DISK_PICKER);
      } else if (idx == 1) {
        bool ok = g_drive_mount_fn ? g_drive_mount_fn((uint8_t)g_sel, "") : false;
        if (ok) *mutable_drive_path(g_sel) = "";
        LOG("ui: dismount %s: %s", DRIVE_NAMES[g_sel], ok ? "ok" : "FAIL");
        if (ok) go(SC_CONFIRM_DRIVE_REBOOT);
        else go(SC_DRIVES);
      }
      break;
    case SC_DISK_PICKER:
      if (g_file_count > 0 && idx < g_file_count) {
        char path[64];
        snprintf(path, sizeof(path), "/%s", g_files[idx]);
        bool ok = g_drive_mount_fn ? g_drive_mount_fn((uint8_t)g_sel, path) : false;
        if (ok) *mutable_drive_path(g_sel) = path;
        LOG("ui: mount %s: %s -> %s", DRIVE_NAMES[g_sel], path, ok ? "ok" : "FAIL");
        if (ok) go(SC_CONFIRM_DRIVE_REBOOT);
        else go(SC_DRIVES);
      }
      break;
    case SC_WIFI_PICKER:
      if (g_variant_count > 0 && idx < g_variant_count)
        prepare_config_copy("wificonfig-", g_variants[idx], WIFI_CFG_PATH, SC_WIFI_PICKER);
      break;
    case SC_VZ80_PICKER:
      if (g_variant_count > 0 && idx < g_variant_count)
        prepare_config_copy("z80config-", g_variants[idx], VZ80_CFG_PATH, SC_VZ80_PICKER);
      break;
    case SC_CONFIRM_COPY:
      if (idx == 0 && config_copy_file(g_pending_src, g_pending_dst)) {
        g_esp_restart = true;
        g_screen = SC_CLOSED;
      } else {
        go(g_after_cancel);
      }
      g_dirty = true;
      break;
    case SC_CONFIRM_DRIVE_REBOOT:
      if (idx == 0) {
        g_reboot = true;
        g_screen = SC_CLOSED;
        g_dirty = true;
      } else {
        go(SC_DRIVES);
      }
      break;
    case SC_CONFIRM_RESET:
      if (idx == 0) { g_esp_restart = true; g_screen = SC_CLOSED; g_dirty = true; }
      else go(SC_MAIN);
      break;
    case SC_BRIGHT:
      if (idx == 0 && g_bright > 16) g_bright -= 16;
      if (idx == 1 && g_bright < 239) g_bright += 16;
      ledcWrite(TFT_BL, g_bright);
      rebuild();
      g_dirty = true;
      break;
    default:
      break;
  }
}

bool ui_handle_tap(int x, int y) {
  if (g_screen == SC_CLOSED) return false;
  int h = list_hit(x, y);
  if (h == HIT_NONE) return true;
  if (h == HIT_BACK) { back(); return true; }
  if (h == HIT_UP) {
    if (g_scroll > 0) { g_scroll--; g_dirty = true; }
    return true;
  }
  if (h == HIT_DOWN) {
    if (g_scroll + MENU_VISIBLE < g_count) { g_scroll++; g_dirty = true; }
    return true;
  }
  activate(g_scroll + h);
  return true;
}

static void draw_item(TFT_eSPI& tft, int slot, int idx) {
  int y = ITEM_Y0 + slot * ITEM_H;
  uint16_t bg = (idx == g_sel) ? COL_ITEM_HI : COL_ITEM;
  if (g_screen == SC_DRIVES && idx >= 0 && idx < 4 && drive_path(idx)->length()) bg = COL_ITEM_HI;
  if (g_screen == SC_CONFIRM_RESET && idx == 0) bg = COL_DANGER;

  tft.fillRoundRect(6, y, 308, Hmb, 4, bg);
  tft.setTextColor(COL_TEXT, bg);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(g_items[idx], 18, y + Hmb / 2, 2);
  tft.setTextDatum(TL_DATUM);
}

static void draw_nav(TFT_eSPI& tft) {
  tft.fillRect(0, NAV_Y, TFT_W, TFT_H - NAV_Y, COL_BG);
  tft.fillRoundRect(6, NAV_Y, 144, Hbt, 4, COL_TITLE);
  tft.setTextColor(TFT_WHITE, COL_TITLE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Back", 78, NAV_Y + Hbt / 2, 2);

  if (g_count > MENU_VISIBLE) {
    tft.fillRoundRect(160, NAV_Y, 72, Hbt, 4, g_scroll > 0 ? COL_TITLE : COL_ITEM);
    tft.drawString("Up", 196, NAV_Y + Hbt / 2, 2);
    tft.fillRoundRect(242, NAV_Y, 72, Hbt, 4,
                      g_scroll + MENU_VISIBLE < g_count ? COL_TITLE : COL_ITEM);
    tft.drawString("Down", 278, NAV_Y + Hbt / 2, 2);
  }
  tft.setTextDatum(TL_DATUM);
}

static void draw_info(TFT_eSPI& tft) {
  tft.fillRect(0, ITEM_Y0, TFT_W, NAV_Y - ITEM_Y0, COL_BG);
  tft.setTextFont(1);
  tft.setTextColor(TFT_WHITE, COL_BG);
  int y = ITEM_Y0 + 2;
  char line[96];

  auto row = [&](const char* s, uint16_t col = TFT_WHITE) {
    tft.setTextColor(col, COL_BG);
    tft.drawString(s, 8, y, 1);
    y += 12;
  };

  snprintf(line, sizeof(line), "Firmware: %s  build %s", APP_VERSION, APP_BUILD_DATE);
  row(line);
  snprintf(line, sizeof(line), "Profile: %s", cfg.title.c_str());
  row(line);
  snprintf(line, sizeof(line), "WiFi: %s", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "not connected");
  row(line, WiFi.status() == WL_CONNECTED ? TFT_GREEN : TFT_YELLOW);
  if (!cfg.ntp_enabled) {
    row("NTP: disabled", COL_DIM);
  } else if (!host_time_synced()) {
    row("NTP: waiting for sync...", TFT_YELLOW);
  } else {
    char utc[32];
    if (host_time_format_utc(utc, sizeof(utc)))
      snprintf(line, sizeof(line), "UTC: %s", utc);
    else
      snprintf(line, sizeof(line), "UTC: (unavailable)");
    row(line, TFT_CYAN);
  }
  snprintf(line, sizeof(line), "Telnet: %s port %u",
           telnet_connected() ? "connected" : (telnet_listening() ? "listening" : "off"),
           telnet_port());
  row(line, telnet_connected() ? TFT_YELLOW : (telnet_listening() ? TFT_GREEN : COL_DIM));
  snprintf(line, sizeof(line), "FTP: %s port %u user %s",
           ftp_connected() ? "connected" : (ftp_listening() ? "listening" : "off"),
           ftp_port(), cfg.ftp_user.c_str());
  row(line, ftp_connected() ? TFT_YELLOW : (ftp_listening() ? TFT_GREEN : COL_DIM));
  snprintf(line, sizeof(line), "A:%s", drive_path(0)->length() ? drive_path(0)->c_str() : "(empty)");
  row(line);
  snprintf(line, sizeof(line), "B:%s", drive_path(1)->length() ? drive_path(1)->c_str() : "(empty)");
  row(line);
  snprintf(line, sizeof(line), "C:%s", drive_path(2)->length() ? drive_path(2)->c_str() : "(empty)");
  row(line);
  snprintf(line, sizeof(line), "D:%s", drive_path(3)->length() ? drive_path(3)->c_str() : "(empty)");
  row(line);
}

void ui_draw(TFT_eSPI& tft) {
  if (g_screen == SC_CLOSED || !g_dirty) return;
  g_dirty = false;

  tft.fillScreen(COL_BG);
  tft.fillRect(0, 0, TFT_W, 24, COL_TITLE);
  tft.setTextColor(TFT_WHITE, COL_TITLE);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(g_title, 8, 12, 2);
  tft.setTextDatum(TL_DATUM);

  if (g_screen == SC_INFO) {
    draw_info(tft);
  } else {
    tft.fillRect(0, ITEM_Y0, TFT_W, NAV_Y - ITEM_Y0, COL_BG);
    for (int i = 0; i < MENU_VISIBLE; i++) {
      int idx = g_scroll + i;
      if (idx < g_count) draw_item(tft, i, idx);
    }
  }
  draw_nav(tft);
}
