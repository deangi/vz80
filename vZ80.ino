// vZ80 - Z80/CP/M 2.2 emulator for ESP32 Cheap Yellow Display (CYD)
// Target 1: xxxx ESP32-2432S028R (ILI9341 320x240, XPT2046 touch, SD slot)
// Switched to a different target board:
// Target 2: 2 USB CYD board (ST7789 driver instead of ILI9341), Micro USB and USB-C both
// Use HUGE APP partitioning on the board.
//
// M1: splash + SD                                                      .
// M2: 80x24 console (5x7 font, horizontal scroll viewport)             .
// M3: Z80 emulator (superzazu/z80, MIT) + 64KB RAM + console I/O ports .
// M4: CP/M 2.2 boot from Altair iCOM 3712 .DSK image.                  .
// M5: Touch control panel (top 48 px) + mount modal.
//     MNT opens a full-screen overlay; the Z80 task is suspended while
//     the modal is up so BDOS can't catch a half-swapped disk.
// M6: BT keyboard — REMOVED, insufficient internal RAM (BT + WiFi cannot
//     coexist on this ESP32 board; no PSRAM). Keyboard input is now
//     telnet-only (M7).
// M7: WiFi STA + telnet (port 23) + VT-100 escape parsing on the LCD.
// M8: on-screen keyboard (planned) — local keyboard input without BT.
//
// Serial -> Z80 rx bridge stays in place as a debugging input path; the
// production input path is telnet over WiFi.
//
// Required Arduino libraries:
//   - LovyanGFX       (lovyan03)        V1.2.19+
//   - ArduinoJson     (bblanchon)       V7.4.3
//   - SimpleFTPServer (Renzo Mischianti) V3.0.2  — only if ENABLE_FTP=1

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/stream_buffer.h>

#include "ESP32_SPI_9341.h"
#include "src/display/console.h"
#include "src/z80/z80_cpu.h"
#include "src/storage/disk_image.h"
#include "src/cpm/altair_bios.h"
#include "src/ui/touch_panel.h"
#include "src/ui/mount_modal.h"
#include "src/ui/setup_modal.h"
#include "src/network/wifi_sta.h"
#include "src/network/telnet_server.h"
#include "src/network/ftp_server.h"

// Build identification — bumped on user-visible changes. Shown in the
// SETUP popup so the running build is obvious without checking serial.
static const char VZ80_VERSION[]    = "1.3";
static const char VZ80_BUILD_DATE[] = "2026-04-26";

// Bluetooth removed (was M6). The original ESP32 in the CYD2USB has too
// little internal DRAM to host both WiFi and Bluedroid simultaneously:
// WiFi alone leaves ~17 KB free heap with the largest contiguous block
// at ~16 KB, while esp_bluedroid_init() needs >15 KB contiguous and
// crashes (LoadProhibited) when its internal alloc returns NULL. Even
// tearing WiFi down at runtime doesn't help — the freed heap stays
// fragmented. There's no PSRAM on this board (we probed at boot and the
// QPI handshake fails), so Z80 RAM can't be moved off internal DRAM
// either. Net: BT and WiFi cannot coexist here. Keyboard input now
// comes from telnet over WiFi (src/network/telnet_server.{h,cpp}).
// Future M8: on-screen keyboard for keyboard-less use.

// -----------------------------------------------------------------------------
// Pin assignments (display pins live in ESP32_SPI_9341.h)
// -----------------------------------------------------------------------------
static const uint8_t SD_CS_PIN   = 5;
static const uint8_t SD_SCK_PIN  = 18;
static const uint8_t SD_MISO_PIN = 19;
static const uint8_t SD_MOSI_PIN = 23;

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
static LGFX        lcd;
// SD shares HSPI with the display (bus_shared=true on the panel). Touch
// owns VSPI on its own pins. Do not put SD on VSPI - it will remap VSPI's
// pins and the XPT2046 touch goes silent (demo1 uses this same layout).
static SPIClass    sdSPI(HSPI);
static Console     console;
static Z80CPU      cpu;
static AltairBios  bios;
static DiskImage   disks[4];   // A, B, C, D
static TouchPanel  touchPanel;
static MountModal  mountModal;
static SetupModal  setupModal;

static StreamBufferHandle_t txStream = nullptr;  // Z80 -> display
static StreamBufferHandle_t rxStream = nullptr;  // keyboard -> Z80

static TaskHandle_t z80TaskHandle = nullptr;
static bool         z80Running    = false;

static char cfgDrives[4][64] = { "altair48k.dsk", "", "", "" };
static char cfgAppName[24] = "vZ80";  // shown as title in the top strip

// Network config (M7). Loaded from /config.json; empty SSID disables WiFi.
static char cfgWifiSsid[33] = "";
static char cfgWifiPass[65] = "";
static char cfgFtpUser[33]  = "cpm";
static char cfgFtpPass[33]  = "cpm";
static bool cfgFtpEnabled = true;
static bool cfgTelnetEnabled = true;
static uint16_t cfgTelnetPort = 23;

// -----------------------------------------------------------------------------
// WiFi event hooks. These fire on the system event task (core 0) — keep
// them brief and defer real work to the main loop via flags.
// -----------------------------------------------------------------------------
static volatile bool netStartupPending_ = false;
static volatile bool netTeardownPending_ = false;

static void onWifiConnected()    { netStartupPending_ = true; }
static void onWifiDisconnected() { netTeardownPending_ = true; }

// -----------------------------------------------------------------------------
static bool initSD() {
    sdSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN);
    if (!SD.begin(SD_CS_PIN, sdSPI, 20000000)) return false;
    return SD.cardType() != CARD_NONE;
}

static void fatal(const char* msg) {
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.setTextSize(2);
    lcd.setCursor(8, 8);
    lcd.printf("FATAL: %s", msg);
    Serial.printf("[FATAL] %s\n", msg);
    while (true) { delay(1000); }
}

static void copyCfgStr(char* dst, size_t dst_size, const char* src) {
    if (!src) { dst[0] = 0; return; }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = 0;
}

static bool loadConfig() {
    File f = SD.open("/config.json", FILE_READ);
    if (!f) { Serial.println("config.json missing - defaults"); return true; }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) { Serial.printf("config: %s\n", err.c_str()); return false; }

    static const char* kKeys[4] = { "A", "B", "C", "D" };
    for (int i = 0; i < 4; ++i) {
        const char* p = doc["drives"][kKeys[i]] | "";
        if (*p) copyCfgStr(cfgDrives[i], sizeof(cfgDrives[i]), p);
        else    cfgDrives[i][0] = 0;
    }

    copyCfgStr(cfgWifiSsid, sizeof(cfgWifiSsid), doc["wifi"]["ssid"]     | "");
    copyCfgStr(cfgWifiPass, sizeof(cfgWifiPass), doc["wifi"]["password"] | "");
    copyCfgStr(cfgFtpUser,  sizeof(cfgFtpUser),  doc["ftp"]["user"]      | "cpm");
    copyCfgStr(cfgFtpPass,  sizeof(cfgFtpPass),  doc["ftp"]["password"]  | "cpm");
    cfgFtpEnabled    = doc["ftp"]["enabled"]    | true;
    cfgTelnetEnabled = doc["telnet"]["enabled"] | true;
    cfgTelnetPort    = doc["telnet"]["port"]    | 23;
    copyCfgStr(cfgAppName, sizeof(cfgAppName),  doc["app"]["name"] | "vZ80");

    Serial.printf("cfg A:=%s B:=%s C:=%s D:=%s\n",
                  cfgDrives[0], cfgDrives[1], cfgDrives[2], cfgDrives[3]);
    Serial.printf("cfg wifi=%s ftp=%s telnet=%s:%u\n",
                  cfgWifiSsid[0] ? cfgWifiSsid : "(off)",
                  cfgFtpUser,
                  cfgTelnetEnabled ? "on" : "off", cfgTelnetPort);
    return true;
}

// Mount a drive from path (no leading slash). Returns true on success.
// .dsk files use the iCOM 3712 floppy geometry (77x26x128 = 256,256 bytes).
// .hdd files keep the 26x128 sector layout but derive track count from
// file size — supports larger CP/M hard-disk images on drives C/D.
static bool mountDrive(uint8_t drive, const char* name) {
    if (drive >= 4) return false;
    DiskImage* img = &disks[drive];
    if (!name || !*name) { bios.unmount(drive); img->close(); return true; }
    char path[80];
    snprintf(path, sizeof(path), "/%s", name);
    img->close();

    uint16_t tracks = 77;       // floppy default
    uint8_t  spt    = 26;
    uint16_t bytes  = 128;
    size_t   nlen = strlen(name);
    if (nlen >= 4 && strcasecmp(name + nlen - 4, ".hdd") == 0) {
        File probe = SD.open(path, FILE_READ);
        if (!probe) {
            Serial.printf("mount %c: probe FAIL %s\n", 'A' + drive, path);
            return false;
        }
        size_t sz = probe.size();
        probe.close();
        // 26 sectors x 128 bytes per track = 3328 bytes/track. Round down.
        uint32_t trks = sz / (26u * 128u);
        if (trks < 1)    trks = 1;
        if (trks > 4095) trks = 4095;
        tracks = (uint16_t)trks;
        Serial.printf("[hdd] %s size=%u -> %u trk x 26 x 128\n",
                      name, (unsigned)sz, tracks);
    }

    if (!img->open(path, tracks, spt, bytes, true)) {
        Serial.printf("mount %c: FAIL %s\n", 'A' + drive, path);
        return false;
    }
    bios.mount(drive, img);
    Serial.printf("%c: <- %s (%u trk x %u sec x %u byte)\n",
                  'A' + drive, path, img->tracks(),
                  img->sectorsPerTrack(), img->sectorBytes());
    return true;
}

// Cold-boot CP/M: read trk0 sec1 of A: to 0x0080, set PC, install stubs.
static bool coldBootCpm() {
    if (!disks[0].isOpen() && !mountDrive(0, cfgDrives[0])) return false;
    for (int d = 1; d < 4; ++d) {
        if (cfgDrives[d][0] && !disks[d].isOpen()) {
            mountDrive(d, cfgDrives[d]);  // non-fatal if a 2nd drive fails
        }
    }

    bios.resetState();
    bios.installStubs(cpu.ram());

    uint8_t boot[128];
    if (!disks[0].readSector(0, 1, boot)) { Serial.println("trk0 sec1 FAIL"); return false; }
    cpu.loadProgram(0x0080, boot, sizeof(boot));
    cpu.reset(0x0080);
    return true;
}

// -----------------------------------------------------------------------------
static void z80Task(void* arg) {
    Z80CPU* z = static_cast<Z80CPU*>(arg);
    for (;;) {
        z->runCycles(40000);     // ~10 ms at 4 MHz emulated
        vTaskDelay(1);
    }
}

static void z80Suspend() {
    if (z80TaskHandle && z80Running) {
        vTaskSuspend(z80TaskHandle);
        z80Running = false;
    }
}

static void z80Resume() {
    if (z80TaskHandle && !z80Running) {
        vTaskResume(z80TaskHandle);
        z80Running = true;
    }
}

// -----------------------------------------------------------------------------
// Button action handlers
// -----------------------------------------------------------------------------
static void doBoot() {
    z80Suspend();
    console.clear();
    console.render();
    coldBootCpm();
    // drain any stale serial/telnet input so old keystrokes don't hit the BIOS
    xStreamBufferReset(rxStream);
    xStreamBufferReset(txStream);
    z80Resume();
}

static void doScroll() {
    touchPanel.advanceScroll();
    console.setView(TouchPanel::scrollColFor(touchPanel.scrollIndex()));
    console.markAllDirty();
}

static void handleMount() {
    z80Suspend();
    mountModal.open(&lcd);
    MountModal::Result r;
    while ((r = mountModal.poll()) == MountModal::Result::CONTINUE) {
        delay(15);
    }

    uint8_t d = mountModal.drive();
    if (r == MountModal::Result::OK && mountModal.filename()) {
        mountDrive(d, mountModal.filename());
    } else if (r == MountModal::Result::UNMOUNT) {
        mountDrive(d, nullptr);
    }
    mountModal.close();

    // Redraw main UI.
    lcd.fillScreen(TFT_BLACK);
    touchPanel.render();
    console.markAllDirty();
    console.render();
    z80Resume();
}

static void handleSetup() {
    z80Suspend();
    setupModal.open(&lcd);
    SetupModal::Result r;
    while ((r = setupModal.poll()) == SetupModal::Result::CONTINUE) {
        delay(15);
    }
    setupModal.close();

    // Redraw the main UI before dispatching, so any handler that further
    // suspends/redraws starts from a clean state.
    lcd.fillScreen(TFT_BLACK);
    touchPanel.render();
    console.markAllDirty();
    console.render();

    switch (r) {
    case SetupModal::Result::REBOOT:
        // doBoot suspends Z80 itself; we already did, so just call it
        // — the Resume will be paired correctly inside doBoot.
        z80Resume();   // pair our suspend; doBoot does its own pair
        doBoot();
        return;
    case SetupModal::Result::MOUNT:
        z80Resume();
        handleMount();
        return;
    case SetupModal::Result::CLEAR:
        console.clear();
        console.render();
        break;
    case SetupModal::Result::CANCEL:
    default:
        break;
    }
    z80Resume();
}

// -----------------------------------------------------------------------------
// Serial -> Z80 rx bridge. Useful for debugging via the USB serial port;
// the production input path is telnet over WiFi (BT input was removed —
// see top-of-file note). Arduino Serial Monitor "Newline" sends LF; CP/M
// wants CR, so remap.
// -----------------------------------------------------------------------------
static void drainSerialToZ80() {
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c < 0) break;
        if (c == '\n') c = '\r';
        uint8_t b = (uint8_t)c;
        xStreamBufferSend(rxStream, &b, 1, 0);
    }
}

// -----------------------------------------------------------------------------
// Physical BOOT button on the ESP32 module is GPIO0 (active LOW, pull-up).
// It's a strapping pin, so it can't be held at reset (ROM would enter
// download mode). Instead, poll it at runtime: pressing BOOT while the
// sketch is running triggers touch recalibration.
static const uint8_t PIN_BOOT_BUTTON = 0;
static uint32_t      nextBootPoll_ms = 0;
static bool          prevBootLow     = false;

static void runCalibration() {
    z80Suspend();
    Serial.println("[touch] BOOT pressed -> recalibration");
    lcd.fillScreen(TFT_BLACK);
    touchPanel.calibrateAndSave();
    lcd.fillScreen(TFT_BLACK);
    touchPanel.render();
    console.markAllDirty();
    console.render();
    z80Resume();
}

static void pollBootButton() {
    uint32_t now = millis();
    if ((int32_t)(now - nextBootPoll_ms) < 0) return;
    nextBootPoll_ms = now + 1000;  // ~1 Hz

    bool low = (digitalRead(PIN_BOOT_BUTTON) == LOW);
    if (low && !prevBootLow) runCalibration();
    prevBootLow = low;
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.printf("\n=== vZ80 v%s (%s) booting ===\n",
                  VZ80_VERSION, VZ80_BUILD_DATE);
    Serial.println("press BOOT at any time to recalibrate touch");

    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);

    lcd.init();
    lcd.setBrightness(200);
    lcd.setRotation(3);
    lcd.fillScreen(TFT_BLACK);

    if (!initSD()) fatal("NO SD");
    Serial.printf("SD %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));

    if (!console.begin(&lcd, TFT_WHITE, TFT_BLACK)) fatal("CONSOLE");
    console.puts("vZ80 M5 - booting CP/M ...\n");
    console.render();

    // Stream sizes: rxStream is large enough to absorb a multi-KB telnet
    // paste without dropping bytes (telnet stops draining TCP when rxStream
    // is near-full so TCP-window backpressure throttles the sender).
    // txStream is sized for typical Z80 bursts; the SIO status port reports
    // "tx busy" when it fills, so the Z80 busy-waits instead of losing bytes.
    txStream = xStreamBufferCreate(1024, 1);
    rxStream = xStreamBufferCreate(4096, 1);
    if (!txStream || !rxStream) fatal("STREAMS");

    cpu.begin(txStream, rxStream);
    cpu.setBios(&bios);

    if (!loadConfig())  fatal("CONFIG");
    if (!coldBootCpm()) fatal("BOOT");

    if (!touchPanel.begin(&lcd)) fatal("TOUCH");
    touchPanel.setTitle(cfgAppName);
    setupModal.setVersion(VZ80_VERSION, VZ80_BUILD_DATE);

    // M7: WiFi + telnet + FTP. Empty SSID disables the lot. Both telnet
    // listener and FTP server are started in loop() once STA_GOT_IP fires
    // (WiFiServer.begin() asserts inside lwIP if WiFi isn't initialized,
    // and FTP touches SD which the system event task must not).
    WifiSta::setCallbacks(onWifiConnected, onWifiDisconnected);
    if (cfgWifiSsid[0]) {
        WifiSta::begin(cfgWifiSsid, cfgWifiPass);
    } else {
        console.puts("[net] WiFi disabled (no ssid in config.json)\n");
        console.render();
    }

    // Pin Z80 to core 1 (shares with Arduino loop/UI) and reserve core 0
    // for WiFi/lwIP. (BT removed — see top-of-file note.)
    BaseType_t ok = xTaskCreatePinnedToCore(
        z80Task, "z80", 8192, &cpu, 1, &z80TaskHandle, 1);
    if (ok != pdPASS) fatal("Z80 TASK");
    z80Running = true;

    Serial.printf("[heap] post-setup free=%u  largest=%u\n",
        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    Serial.println("core0=WiFi/lwIP  core1=Z80+UI/display");
}

// -----------------------------------------------------------------------------
void loop() {
    pollBootButton();
    drainSerialToZ80();

    // Deferred network bring-up/tear-down (flagged by WiFi event task).
    if (netStartupPending_) {
        netStartupPending_ = false;
        char msg[64];
        snprintf(msg, sizeof(msg), "[net] WiFi up: %s\n",
                 WifiSta::localIP().toString().c_str());
        console.puts(msg);
        console.render();
        if (cfgTelnetEnabled) gTelnet.begin(cfgTelnetPort, rxStream);
        if (cfgFtpEnabled)    Ftp::begin(cfgFtpUser, cfgFtpPass);

        // WiFi association burst can leave the LCD in a bad state (SPI
        // bus is shared with SD via bus_shared=true). Re-init and redraw
        // to recover from any garbled commands the panel ingested.
        lcd.init();
        lcd.setRotation(3);
        lcd.setBrightness(200);
        lcd.fillScreen(TFT_BLACK);
        touchPanel.render();
        console.markAllDirty();
        console.render();
    }
    if (netTeardownPending_) {
        netTeardownPending_ = false;
        gTelnet.disconnect();
        Ftp::stop();
        console.puts("[net] WiFi down\n");
        console.render();
    }

    gTelnet.tick();

    // Refresh top-strip status (wifi line + bt/telnet line) once a second.
    static uint32_t nextNetStatus_ms = 0;
    if ((int32_t)(millis() - nextNetStatus_ms) >= 0) {
        nextNetStatus_ms = millis() + 1000;
        char wline[40];
        char bline[40];

        // Line 1: WiFi
        if (!cfgWifiSsid[0]) {
            snprintf(wline, sizeof(wline), "wifi: off");
        } else if (!WifiSta::connected()) {
            snprintf(wline, sizeof(wline), "wifi: %s ...", cfgWifiSsid);
        } else {
            snprintf(wline, sizeof(wline), "wifi: %s",
                     WifiSta::localIP().toString().c_str());
        }

        // Line 2: telnet client (BT removed — see top-of-file note).
        if (!cfgTelnetEnabled) {
            snprintf(bline, sizeof(bline), "tel:off");
        } else if (gTelnet.clientConnected()) {
            snprintf(bline, sizeof(bline), "tel:%s",
                     gTelnet.clientIP().toString().c_str());
        } else {
            snprintf(bline, sizeof(bline), "tel:.");
        }

        touchPanel.setStatus(wline, bline);
    }

    // Z80 -> console (display + serial echo + telnet client).
    uint8_t chunk[64];
    size_t n = xStreamBufferReceive(txStream, chunk, sizeof(chunk), 0);
    if (n > 0) {
        for (size_t i = 0; i < n; ++i) {
            console.putc((char)chunk[i]);
            Serial.write(chunk[i]);
        }
        gTelnet.writeBytes(chunk, n);
    }
    console.render();

    // Touch panel actions.
    switch (touchPanel.poll()) {
        case TouchPanel::Action::SETUP:  handleSetup(); break;
        case TouchPanel::Action::SCROLL: doScroll();    break;
        case TouchPanel::Action::NONE:   break;
    }

    delay(10);
}