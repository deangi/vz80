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
// M5: Touch control panel (top 48 px) + mount modal.  (current)
//     Buttons: RUN/STOP | BOOT | CLR | BT | MNT | SCRL
//     MNT opens a full-screen overlay; the Z80 task is suspended while
//     the modal is up so BDOS can't catch a half-swapped disk.
//
// Serial -> Z80 rx bridge stays in place through M5 for testing; it is
// removed in M6 when the Bluetooth keyboard takes over input.
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
#include <esp_bt.h>
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
#include "src/input/bt_keyboard.h"
#include "src/network/wifi_sta.h"
#include "src/network/telnet_server.h"
#include "src/network/ftp_server.h"

extern void BtKeyboardTick();  // defined in bt_keyboard.cpp

// Override Arduino-ESP32's weak btInUse() so initArduino() does NOT call
// esp_bt_controller_mem_release(BTDM) before setup() runs. Must live in
// the main .ino so the strong definition is in the sketch's own archive
// and wins the link over the core's weak default.
extern "C" bool btInUse() { return true; }

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
static DiskImage   diskA;
static DiskImage   diskB;
static TouchPanel  touchPanel;
static MountModal  mountModal;

static StreamBufferHandle_t txStream = nullptr;  // Z80 -> display
static StreamBufferHandle_t rxStream = nullptr;  // keyboard -> Z80

static TaskHandle_t z80TaskHandle = nullptr;
static bool         z80Running    = false;

static char cfgDriveA[64] = "altair48k.dsk";
static char cfgDriveB[64] = "";

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

    const char* a = doc["drives"]["A"] | "";
    const char* b = doc["drives"]["B"] | "";
    if (*a) copyCfgStr(cfgDriveA, sizeof(cfgDriveA), a);
    if (*b) copyCfgStr(cfgDriveB, sizeof(cfgDriveB), b);

    copyCfgStr(cfgWifiSsid, sizeof(cfgWifiSsid), doc["wifi"]["ssid"]     | "");
    copyCfgStr(cfgWifiPass, sizeof(cfgWifiPass), doc["wifi"]["password"] | "");
    copyCfgStr(cfgFtpUser,  sizeof(cfgFtpUser),  doc["ftp"]["user"]      | "cpm");
    copyCfgStr(cfgFtpPass,  sizeof(cfgFtpPass),  doc["ftp"]["password"]  | "cpm");
    cfgFtpEnabled    = doc["ftp"]["enabled"]    | true;
    cfgTelnetEnabled = doc["telnet"]["enabled"] | true;
    cfgTelnetPort    = doc["telnet"]["port"]    | 23;

    Serial.printf("cfg A:=%s B:=%s\n", cfgDriveA, cfgDriveB);
    Serial.printf("cfg wifi=%s ftp=%s telnet=%s:%u\n",
                  cfgWifiSsid[0] ? cfgWifiSsid : "(off)",
                  cfgFtpUser,
                  cfgTelnetEnabled ? "on" : "off", cfgTelnetPort);
    return true;
}

// Mount a drive from path (no leading slash). Returns true on success.
static bool mountDrive(uint8_t drive, const char* name) {
    DiskImage* img = (drive == 0) ? &diskA : (drive == 1) ? &diskB : nullptr;
    if (!img) return false;
    if (!name || !*name) { bios.unmount(drive); img->close(); return true; }
    char path[80];
    snprintf(path, sizeof(path), "/%s", name);
    img->close();
    if (!img->open(path)) {
        Serial.printf("mount %c: FAIL %s\n", 'A' + drive, path);
        return false;
    }
    bios.mount(drive, img);
    Serial.printf("%c: <- %s (%u trk x %u sec)\n",
                  'A' + drive, path, img->tracks(), img->sectorsPerTrack());
    return true;
}

// Cold-boot CP/M: read trk0 sec1 of A: to 0x0080, set PC, install stubs.
static bool coldBootCpm() {
    if (!diskA.isOpen() && !mountDrive(0, cfgDriveA)) return false;
    if (cfgDriveB[0] && !diskB.isOpen()) mountDrive(1, cfgDriveB);

    bios.resetState();
    bios.installStubs(cpu.ram());

    uint8_t boot[128];
    if (!diskA.readSector(0, 1, boot)) { Serial.println("trk0 sec1 FAIL"); return false; }
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
        touchPanel.setRunning(false);
    }
}

static void z80Resume() {
    if (z80TaskHandle && !z80Running) {
        vTaskResume(z80TaskHandle);
        z80Running = true;
        touchPanel.setRunning(true);
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
    // drain any stale serial/BT input so old keystrokes don't hit the BIOS
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

// -----------------------------------------------------------------------------
// Serial -> Z80 rx bridge (temporary; removed in M6 when BT keyboard lands).
// Arduino Serial Monitor "Newline" sends LF; CP/M wants CR, so remap.
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
    Serial.println("\n=== vZ80 booting (M5) ===");
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

    txStream = xStreamBufferCreate(256, 1);
    rxStream = xStreamBufferCreate(256, 1);
    if (!txStream || !rxStream) fatal("STREAMS");

    cpu.begin(txStream, rxStream);
    cpu.setBios(&bios);

    if (!loadConfig())  fatal("CONFIG");
    if (!coldBootCpm()) fatal("BOOT");

    if (!touchPanel.begin(&lcd)) fatal("TOUCH");

    // BT keyboard: "begin" only records the rx stream and flags ready.
    // The actual Bluedroid/HID stack isn't brought up until the user taps
    // BT (startPairing). This keeps boot stable regardless of BT state.
    gBtKbd.begin(rxStream);
    console.puts("[BT] tap BT button to pair a keyboard\n");
    console.render();

    // Release Bluedroid Classic BT heap NOW (we only ever use BLE for HID).
    // Arduino's btInUse()=true override skips the boot-time release, and
    // bringStackUp() doesn't run until the user presses BT — by then WiFi
    // has already failed because ~30 KB of heap is locked away. Releasing
    // before WiFi.begin() gives WiFi the headroom it needs to associate.
    {
        esp_err_t e = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
        Serial.printf("[bt] classic mem release -> 0x%x  free=%u\n",
            (unsigned)e, (unsigned)ESP.getFreeHeap());
    }

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
    // for BT/WiFi. Bluedroid tasks require core 0 and behave badly when
    // a CPU-bound task is hammering the same core during bring-up.
    BaseType_t ok = xTaskCreatePinnedToCore(
        z80Task, "z80", 8192, &cpu, 1, &z80TaskHandle, 1);
    if (ok != pdPASS) fatal("Z80 TASK");
    z80Running = true;

    Serial.printf("[heap] post-setup free=%u  largest=%u\n",
        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    Serial.println("core0=BT reserved  core1=Z80+UI/display");
}

// -----------------------------------------------------------------------------
void loop() {
    pollBootButton();
    BtKeyboardTick();
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

    // Refresh network status footer once a second.
    static uint32_t nextNetStatus_ms = 0;
    if ((int32_t)(millis() - nextNetStatus_ms) >= 0) {
        nextNetStatus_ms = millis() + 1000;
        char line[64];
        if (!cfgWifiSsid[0]) {
            touchPanel.setStatus("net: off", TFT_DARKGREY, TFT_BLACK);
        } else if (!WifiSta::connected()) {
            snprintf(line, sizeof(line), "wifi: %s ...", cfgWifiSsid);
            touchPanel.setStatus(line, TFT_ORANGE, TFT_BLACK);
        } else {
            const char* tel = !cfgTelnetEnabled ? "tel:off"
                            : gTelnet.clientConnected() ? "tel+" : "tel:.";
            const char* ftp = !ENABLE_FTP   ? "ftp:off"
                            : Ftp::running() ? "ftp+"
                                             : "ftp:-";
            snprintf(line, sizeof(line), "%s  %s  %s",
                     WifiSta::localIP().toString().c_str(),
                     ftp, tel);
            touchPanel.setStatus(line, TFT_GREEN, TFT_BLACK);
        }
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
        case TouchPanel::Action::RUN_TOGGLE:
            if (z80Running) z80Suspend(); else z80Resume();
            break;
        case TouchPanel::Action::BOOT:   doBoot();         break;
        case TouchPanel::Action::CLEAR:  console.clear(); console.render(); break;
        case TouchPanel::Action::SCROLL: doScroll();       break;
        case TouchPanel::Action::MOUNT:  handleMount();    break;
        case TouchPanel::Action::BT: {
            using S = BtKeyboard::State;
            S st = gBtKbd.state();
            if (st == S::Off) {
                console.puts("[BT] stack not up (M6 disabled?)\n");
                console.render();
            } else if (st == S::Connected) {
                console.puts("[BT] already connected\n");
                console.render();
            } else if (st == S::Pairing) {
                console.puts("[BT] pairing window already open\n");
                console.render();
            } else {
                console.puts("[BT] pairing 20s - make kbd discoverable\n");
                console.render();
                Serial.println("[BT] button pressed, suspending Z80 for BT bringup");
                Serial.flush();
                bool wasRunning = z80Running;
                if (wasRunning) z80Suspend();
                gBtKbd.startPairing();
                if (wasRunning) z80Resume();
                Serial.println("[BT] Z80 resumed");
                Serial.flush();
            }
            break;
        }
        case TouchPanel::Action::NONE:   break;
    }

    delay(10);
}