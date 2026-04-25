#include "ftp_server.h"

#if ENABLE_FTP

#include <SD.h>
// NOTE: STORAGE_SD must be set in SimpleFTPServer's FtpServerKey.h
// (line ~63: DEFAULT_STORAGE_TYPE_ESP32). Arduino IDE compiles library
// sources separately so #defining it here has no effect on the library.
#include <SimpleFTPServer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace Ftp {

static FtpServer*    server_     = nullptr;
static TaskHandle_t  taskHandle_ = nullptr;
static volatile bool running_    = false;
static volatile bool stopReq_    = false;

static void ftpTask(void* /*arg*/) {
    while (!stopReq_) {
        if (server_) server_->handleFTP();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    running_ = false;
    Serial.println("[ftp] task exit");
    vTaskDelete(nullptr);
}

bool begin(const char* user, const char* pass) {
    if (running_) return true;
    if (!server_) server_ = new FtpServer();

    server_->begin(user, pass);
    Serial.printf("[ftp] listening, user=%s\n", user ? user : "(null)");

    stopReq_ = false;
    running_ = true;
    BaseType_t ok = xTaskCreatePinnedToCore(
        ftpTask, "ftp", 4096, nullptr, 1, &taskHandle_, 1);
    if (ok != pdPASS) {
        Serial.println("[ftp] task spawn FAIL");
        running_ = false;
        return false;
    }
    return true;
}

void stop() {
    if (!running_) return;
    stopReq_ = true;
}

bool running() { return running_; }

}  // namespace Ftp

#else  // !ENABLE_FTP — inert stubs so callers don't need #ifdefs

namespace Ftp {
bool begin(const char* /*user*/, const char* /*pass*/) { return false; }
void stop()    {}
bool running() { return false; }
}  // namespace Ftp

#endif  // ENABLE_FTP
