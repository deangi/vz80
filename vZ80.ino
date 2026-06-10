// board: esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi
// vZ80 - Z80 / CP/M emulator on Freenove ESP32-S3 2.8" Display
// Dean Gienger and Codex and Claude, 9 Jun 2026
// Boots up a version of cpm2.2 from Altair, 48k version from floppy disk
// ESP32S3 Dev Module board, 16Mb Flash, 8Mb PSRAM, 2.8" dispay with capacitive touch screen
// Partition: 16M flash (3Mb app/9.9Mb SPIFAT)
// Tools: CDC On Boot=true
//----------------------------------------------------------------------------

#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <SD_MMC.h>
#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/stream_buffer.h>
#include "Freenove_WS2812_Lib_for_ESP32.h"

#include "config.h"
#include "platform.h"
#include "secrets.h"
#include "appconfig.h"
#include "console.h"
#include "telnet.h"
#include "ftp.h"
#include "touch.h"
#include "ui.h"
#include "src/z80/z80_cpu.h"
#include "src/storage/disk_image.h"
#include "src/cpm/altair_bios.h"
#include "src/ui/keyboard_modal.h"

static TFT_eSPI tft;
static Freenove_ESP32_WS2812 strip(LED_COUNT, LED_PIN, LED_CHANNEL, TYPE_GRB);
AppConfig cfg;

static bool sd_ok = false;
static bool z80_running = false;
static SemaphoreHandle_t g_ui_mutex = nullptr;

static Z80CPU cpu;
static AltairBios bios;
static DiskImage disks[AltairBios::MAX_DRIVES];
static StreamBufferHandle_t txStream = nullptr;  // Z80 -> console/Telnet
static StreamBufferHandle_t rxStream = nullptr;  // Serial/Telnet -> Z80
static KeyboardModal keyboard;

static volatile uint32_t g_last_cycles = 0;
static volatile bool g_boot_ok = false;
static volatile bool g_keyboard_open = false;

enum {
  ROW_PSRAM = 0, ROW_SD, ROW_CFG, ROW_WIFI, ROW_IP, ROW_CPU
};

static void led(uint8_t r, uint8_t g, uint8_t b) {
  strip.setLedColorData(0, r, g, b);
  strip.show();
}

static void tft_banner() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(2);
  tft.setCursor(4, 4);
  tft.printf("%s  %s", APP_TITLE, APP_VERSION);
  tft.setCursor(4, 22);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.printf("build %s", APP_BUILD_DATE);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

static void tft_status(int row, const char* label, const char* value, uint16_t color) {
  int y = 50 + row * 18;
  tft.fillRect(0, y, TFT_W, 18, TFT_BLACK);
  tft.setCursor(4, y);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.print(label);
  tft.setTextColor(color, TFT_BLACK);
  tft.print(value);
}

static void wifi_connect() {
  const char* ssid = cfg.wifi_ssid.c_str();
  const char* pass = cfg.wifi_password.c_str();
  const char* host = cfg.wifi_hostname.length() ? cfg.wifi_hostname.c_str() : WIFI_HOSTNAME;

  if (cfg.wifi_ssid.length() == 0) {
    LOGE("WiFi SSID is empty - set [wifi] ssid= in %s", WIFI_CFG_PATH);
    tft_status(ROW_WIFI, "WiFi:  ", "disabled (no SSID)", TFT_YELLOW);
    tft_status(ROW_IP,   "IP:    ", "(none)", TFT_DARKGREY);
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(host);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, pass);

  LOG("WiFi connecting to \"%s\" (hostname=%s) ...", ssid, host);
  tft_status(ROW_WIFI, "WiFi:  ", "connecting...", TFT_YELLOW);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    LOG("WiFi connected, IP=%s", WiFi.localIP().toString().c_str());
    tft_status(ROW_WIFI, "WiFi:  ", ssid, TFT_GREEN);
    tft_status(ROW_IP,   "IP:    ", WiFi.localIP().toString().c_str(), TFT_GREEN);
  } else {
    LOGE("WiFi connect timed out");
    tft_status(ROW_WIFI, "WiFi:  ", "not connected", TFT_YELLOW);
    tft_status(ROW_IP,   "IP:    ", "(none)", TFT_DARKGREY);
  }
}

static void sd_and_config_init() {
  tft_status(ROW_SD, "SD:    ", "mounting...", TFT_YELLOW);
  if (sd_mount()) {
    char info[32];
    uint64_t mb = SD_MMC.cardSize() / (1024ULL * 1024ULL);
    snprintf(info, sizeof(info), "OK  %llu MB", (unsigned long long)mb);
    tft_status(ROW_SD, "SD:    ", info, TFT_GREEN);
    sd_ok = true;
  } else {
    tft_status(ROW_SD, "SD:    ", "FAILED", TFT_RED);
    sd_ok = false;
  }

  tft_status(ROW_CFG, "Cfg:   ", "(reading)", TFT_YELLOW);
  config_apply_compiled_defaults(cfg);
  if (!sd_ok) {
    tft_status(ROW_CFG, "Cfg:   ", "defaults (no SD)", TFT_YELLOW);
  } else {
    bool wifi_existed = config_load_wifi(cfg);
    bool vz80_existed = config_load_vz80(cfg);
    tft_status(ROW_CFG, "Cfg:   ",
               (wifi_existed && vz80_existed) ? "loaded split cfg" : "wrote default cfg",
               (wifi_existed && vz80_existed) ? TFT_GREEN : TFT_YELLOW);
  }
  config_print(cfg);
}

static int boot_slot() {
  switch (tolower((uint8_t)cfg.boot_drive)) {
    case 'a': return 0;
    case 'b': return 1;
    case 'c': return 2;
    case 'd': return 3;
    default:  return 0;
  }
}

static bool has_ext(const char* path, const char* ext) {
  size_t n = strlen(path);
  size_t e = strlen(ext);
  return n >= e && strcasecmp(path + n - e, ext) == 0;
}

static const String* drive_config(int d) {
  switch (d) {
    case 0: return &cfg.disk_a;
    case 1: return &cfg.disk_b;
    case 2: return &cfg.disk_c;
    case 3: return &cfg.disk_d;
    default: return &cfg.disk_a;
  }
}

static bool mount_drive(uint8_t drive, const char* name) {
  if (drive >= AltairBios::MAX_DRIVES) return false;

  DiskImage* img = &disks[drive];
  if (!name || !*name) {
    bios.unmount(drive);
    img->close();
    return true;
  }

  char path[96];
  if (name[0] == '/') snprintf(path, sizeof(path), "%s", name);
  else                snprintf(path, sizeof(path), "/%s", name);

  img->close();
  uint16_t tracks = 77;
  uint8_t spt = 26;
  uint16_t bytes = 128;

  if (has_ext(path, ".hdd")) {
    fs::File probe = SD_MMC.open(path, FILE_READ);
    if (!probe) {
      LOGE("mount %c: probe FAIL %s", 'A' + drive, path);
      return false;
    }
    size_t sz = probe.size();
    probe.close();
    uint32_t trks = sz / (26u * 128u);
    if (trks < 1) trks = 1;
    if (trks > 4095) trks = 4095;
    tracks = (uint16_t)trks;
    LOG("[hdd] %s size=%u -> %u trk x 26 x 128",
        path, (unsigned)sz, tracks);
  }

  if (!img->open(path, tracks, spt, bytes, true)) {
    LOGE("mount %c: FAIL %s", 'A' + drive, path);
    return false;
  }

  bios.mount(drive, img);
  LOG("%c: <- %s (%u trk x %u sec x %u byte)",
      'A' + drive, path, img->tracks(),
      img->sectorsPerTrack(), img->sectorBytes());
  return true;
}

static bool mount_configured_drives() {
  bool ok = true;
  for (int d = 0; d < AltairBios::MAX_DRIVES; d++) {
    const String* p = drive_config(d);
    if (p->length() == 0) {
      mount_drive(d, "");
      continue;
    }
    bool mounted = mount_drive(d, p->c_str());
    if (!mounted && d == boot_slot()) ok = false;
  }
  return ok;
}

static void inject_boot_text() {
  if (!rxStream || cfg.boot_input_len == 0) return;
  size_t sent = xStreamBufferSend(rxStream, cfg.boot_input, cfg.boot_input_len, 0);
  LOG("Injected %u/%u boot_text byte(s) into Z80 console input",
      (unsigned)sent, (unsigned)cfg.boot_input_len);
}

static void apply_console_terminal_mode() {
  ConsoleTerminalMode mode = cfg.terminal.equalsIgnoreCase("adm3a")
                           ? CONSOLE_TERM_ADM3A
                           : CONSOLE_TERM_VT100;
  console_set_terminal_mode(mode);
  LOG("Console terminal mode: %s", mode == CONSOLE_TERM_ADM3A ? "adm3a" : "vt100");
}

static bool cold_boot_cpm() {
  if (!sd_ok) {
    LOGE("Cannot boot Z80: SD not mounted");
    return false;
  }
  if (!mount_configured_drives()) return false;

  int bs = boot_slot();
  if (!disks[bs].isOpen()) {
    LOGE("Boot drive %c: is not mounted", 'A' + bs);
    return false;
  }

  LOG("Boot disk %c: %s", 'A' + bs, disks[bs].path());

  bios.resetState();
  bios.installStubs(cpu.ram());

  uint8_t boot[128];
  if (!disks[bs].readSector(0, 1, boot)) {
    LOGE("Boot read failed: %s trk0 sec1", disks[bs].path());
    return false;
  }

  cpu.loadProgram(0x0080, boot, sizeof(boot));
  cpu.reset(0x0080);
  inject_boot_text();
  console_init();
  apply_console_terminal_mode();
  console_force_redraw();
  g_boot_ok = true;
  return true;
}

static void reboot_z80() {
  LOG("Reboot Z80");
  g_boot_ok = false;
  console_init();
  apply_console_terminal_mode();
  console_force_redraw();
  xStreamBufferReset(txStream);
  xStreamBufferReset(rxStream);
  cold_boot_cpm();
}

static void enqueue_input(uint8_t c) {
  if (rxStream) xStreamBufferSend(rxStream, &c, 1, 0);
}

static void drain_telnet_input() {
  uint8_t b;
  while (telnet_in_pop(&b)) enqueue_input(b);
}

static void drain_z80_output() {
  uint8_t b;
  int budget = 768;
  while (budget-- > 0 && xStreamBufferReceive(txStream, &b, 1, 0) == 1) {
    console_feed(b);
    telnet_write(b);
    Serial.write(b);
  }
}

static void draw_status_bar() {
  static uint32_t prev_cycles = 0;
  static uint32_t prev_ms = 0;
  const int sy = CON_ROWS * CELL_H;

  tft.drawFastHLine(0, sy, TFT_W, TFT_DARKGREY);

  for (int d = 0; d < AltairBios::MAX_DRIVES; d++) {
    uint16_t col = disks[d].isOpen() ? TFT_GREEN : 0x2945;
    int bx = 6 + d * 32;
    tft.fillRoundRect(bx, sy + 5, 28, 16, 2, col);
    tft.setTextColor(TFT_BLACK, col);
    tft.setTextDatum(MC_DATUM);
    char lbl[3] = { (char)('A' + d), ':', 0 };
    tft.drawString(lbl, bx + 14, sy + 13, 1);
  }
  tft.setTextDatum(TL_DATUM);

  auto draw_pill = [&](int bx, const char* label, uint16_t col) {
    tft.fillRoundRect(bx, sy + 5, 30, 16, 2, col);
    tft.setTextColor(TFT_BLACK, col);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(label, bx + 15, sy + 13, 1);
    tft.setTextDatum(TL_DATUM);
  };

  const uint16_t col_off = 0x2945;
  uint16_t tel_col = !telnet_listening() ? col_off
                   : telnet_connected() ? TFT_YELLOW : TFT_GREEN;
  uint16_t ftp_col = !ftp_listening() ? col_off
                   : ftp_connected() ? TFT_YELLOW : TFT_GREEN;
  draw_pill(138, "TEL", tel_col);
  draw_pill(172, "FTP", ftp_col);

  uint32_t now = millis();
  uint32_t cycles = g_last_cycles;
  float mhz = 0.0f;
  if (prev_ms && now > prev_ms && cycles >= prev_cycles)
    mhz = (float)(cycles - prev_cycles) / (float)(now - prev_ms) / 1000.0f;
  prev_cycles = cycles;
  prev_ms = now;

  tft.fillRect(206, sy + 1, TFT_W - 206, TFT_H - sy - 1, TFT_BLACK);
  tft.setTextColor(WiFi.status() == WL_CONNECTED ? TFT_WHITE : TFT_YELLOW, TFT_BLACK);
  tft.drawString(WiFi.status() == WL_CONNECTED
                   ? WiFi.localIP().toString().c_str()
                   : "not connected",
                 208, sy + 5, 1);
  char line[32];
  snprintf(line, sizeof(line), "%.2f MHz", mhz);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(line, 208, sy + 22, 1);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(cfg.title.c_str(), 6, sy + 22, 1);
}

static void ui_open_locked() {
  xSemaphoreTake(g_ui_mutex, portMAX_DELAY);
  ui_open();
  xSemaphoreGive(g_ui_mutex);
}

static void ui_tap_locked(int x, int y) {
  xSemaphoreTake(g_ui_mutex, portMAX_DELAY);
  ui_handle_tap(x, y);
  xSemaphoreGive(g_ui_mutex);
}

static void keyboard_open() {
  keyboard.open();
  g_keyboard_open = true;
}

static void keyboard_close() {
  keyboard.close();
  g_keyboard_open = false;
  console_force_redraw();
}

static void render_task(void* arg) {
  (void)arg;
  bool was_open = false;
  bool was_keyboard = false;
  uint32_t status_ms = 0;
  for (;;) {
    bool open = ui_is_open();
    bool kbd = g_keyboard_open;
    if ((was_open && !open) || (was_keyboard && !kbd)) {
      tft.fillRect(0, CON_ROWS * CELL_H, TFT_W, TFT_H - CON_ROWS * CELL_H, TFT_BLACK);
      tft.fillRect(0, KeyboardModal::KBD_Y0, TFT_W, KeyboardModal::KBD_H, TFT_BLACK);
      console_force_redraw();
      status_ms = 0;
    }
    was_open = open;
    was_keyboard = kbd;

    if (open) {
      xSemaphoreTake(g_ui_mutex, portMAX_DELAY);
      ui_draw(tft);
      xSemaphoreGive(g_ui_mutex);
    } else {
      drain_z80_output();
      console_render(tft);
      if (kbd) {
        xSemaphoreTake(g_ui_mutex, portMAX_DELAY);
        keyboard.draw(tft);
        xSemaphoreGive(g_ui_mutex);
      } else {
        uint32_t now = millis();
        if (now - status_ms >= 500) {
          status_ms = now;
          draw_status_bar();
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void net_task(void* arg) {
  (void)arg;
  for (;;) {
    telnet_poll();
    ftp_poll();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

static void z80_task(void* arg) {
  (void)arg;
  for (;;) {
    if (z80_running && !ui_is_open() && g_boot_ok) {
      cpu.runCycles(40000);
      g_last_cycles = cpu.cpu()->cyc;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void setup() {
  delay(200);
  Serial.begin(115200);
  for (int i = 0; i < 3; i++) {
    delay(1000);
    Serial.println(i);
  }
  Serial.println();
  LOG("%s %s build %s", APP_TITLE, APP_VERSION, APP_BUILD_DATE);

  strip.begin();
  strip.setBrightness(20);
  led(32, 0, 0);

  tft.init();
  tft.setRotation(1);
  tft_banner();

  char buf[32];
  snprintf(buf, sizeof(buf), "%u KB", (unsigned)(ESP.getPsramSize() / 1024));
  tft_status(ROW_PSRAM, "PSRAM: ", buf, ESP.getPsramSize() ? TFT_GREEN : TFT_RED);
  tft_status(ROW_SD,    "SD:    ", "(pending)", TFT_DARKGREY);
  tft_status(ROW_CFG,   "Cfg:   ", "(pending)", TFT_DARKGREY);
  tft_status(ROW_WIFI,  "WiFi:  ", "(pending)", TFT_DARKGREY);
  tft_status(ROW_IP,    "IP:    ", "(none)", TFT_DARKGREY);
  tft_status(ROW_CPU,   "CPU:   ", "(pending)", TFT_DARKGREY);

  txStream = xStreamBufferCreate(4096, 1);
  rxStream = xStreamBufferCreate(512, 1);
  if (!txStream || !rxStream) {
    tft_status(ROW_CPU, "CPU:   ", "stream alloc FAILED", TFT_RED);
    LOGE("Stream buffer allocation failed");
    return;
  }

  tft_status(ROW_CPU, "CPU:   ", "init...", TFT_YELLOW);
  cpu.begin(txStream, rxStream);
  cpu.setBios(&bios);
  if (!cpu.ram()) {
    tft_status(ROW_CPU, "CPU:   ", "RAM alloc FAILED", TFT_RED);
    LOGE("Z80 RAM allocation failed");
    return;
  }
  tft_status(ROW_CPU, "CPU:   ", "Z80 ready", TFT_GREEN);

  sd_and_config_init();
  wifi_connect();

  telnet_begin(cfg.telnet_port, cfg.telnet_enabled);
  ftp_begin(cfg.ftp_port, cfg.ftp_enabled, cfg.ftp_user.c_str(), cfg.ftp_password.c_str());
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  touch_init();
  ui_init();
  console_init();
  apply_console_terminal_mode();

  g_ui_mutex = xSemaphoreCreateMutex();
  if (!g_ui_mutex) {
    tft_status(ROW_CPU, "CPU:   ", "mutex FAILED", TFT_RED);
    return;
  }

  bool booted = cold_boot_cpm();
  tft_status(ROW_CPU, "CPU:   ", booted ? "booting CP/M" : "boot FAILED",
             booted ? TFT_GREEN : TFT_RED);
  z80_running = booted;
  led(booted ? 0 : 32, booted ? 0 : 0, booted ? 32 : 0);

  xTaskCreatePinnedToCore(render_task, "render", 10240, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(net_task,    "net",     8192, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(z80_task,    "z80",     8192, NULL, 3, NULL, 1);
}

void loop() {
  static bool btn_prev = true;
  static uint32_t last_tap = 0;
  static uint32_t wifi_ms = 0;
  static bool prompt_seen = false;

  int tx, ty;
  if (touch_poll(&tx, &ty)) {
    if (g_keyboard_open) {
      uint8_t b = 0;
      xSemaphoreTake(g_ui_mutex, portMAX_DELAY);
      KeyboardModal::Result kr = keyboard.handleTap(tx, ty, &b);
      xSemaphoreGive(g_ui_mutex);
      if (kr == KeyboardModal::Result::KEY) enqueue_input(b);
      else if (kr == KeyboardModal::Result::CLOSE) keyboard_close();
    } else if (ui_is_open()) {
      ui_tap_locked(tx, ty);
    } else {
      uint32_t now = millis();
      if (now - last_tap < 450) { ui_open_locked(); last_tap = 0; }
      else last_tap = now;
    }
  }

  bool btn_now = digitalRead(BUTTON_PIN);
  if (btn_prev && !btn_now && !ui_is_open()) {
    if (g_keyboard_open) keyboard_close();
    else ui_open_locked();
  }
  btn_prev = btn_now;

  if (ui_consume_reboot()) {
    if (g_keyboard_open) keyboard_close();
    reboot_z80();
    prompt_seen = false;
    led(0, 0, 32);
  }
  if (ui_consume_esp_restart()) {
    LOG("ui: reset ESP32");
    delay(50);
    ESP.restart();
  }
  if (ui_consume_keyboard()) keyboard_open();

  while (Serial.available())
    enqueue_input((uint8_t)Serial.read());
  drain_telnet_input();

  uint32_t now = millis();
  if (now - wifi_ms >= 10000) {
    wifi_ms = now;
    if (cfg.wifi_ssid.length() && WiFi.status() != WL_CONNECTED) {
      LOGE("WiFi link down - reconnecting");
      WiFi.reconnect();
    }
  }

  if (!prompt_seen && console_feed_count() > 0 &&
      millis() - console_last_feed_ms() > 800) {
    prompt_seen = true;
    led(0, 32, 0);
  }

  delay(1);
}
