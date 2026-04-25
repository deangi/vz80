#include "wifi_sta.h"

#include <WiFi.h>
#include <esp_wifi.h>

namespace WifiSta {

static Callback onConnected_    = nullptr;
static Callback onDisconnected_ = nullptr;
static char     ssid_[33] = "";
static char     pass_[65] = "";
static bool     enabled_  = false;

// Throttle disconnect spam — print the first one and then at most once per
// 10s. Without this, an AP that keeps rejecting us floods the console.
static uint32_t lastDiscLog_ms = 0;
static bool     wasConnected_  = false;

static const char* discReasonName(uint8_t r) {
    switch (r) {
    case WIFI_REASON_AUTH_EXPIRE:        return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_FAIL:          return "AUTH_FAIL (wrong password?)";
    case WIFI_REASON_NO_AP_FOUND:        return "NO_AP_FOUND (out of range / wrong SSID)";
    case WIFI_REASON_ASSOC_FAIL:         return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:  return "HANDSHAKE_TIMEOUT (wrong password?)";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_BEACON_TIMEOUT:     return "BEACON_TIMEOUT";
    case WIFI_REASON_ASSOC_LEAVE:        return "ASSOC_LEAVE";
    case WIFI_REASON_AUTH_LEAVE:         return "AUTH_LEAVE";
    case 210: return "NO_AP_FOUND_W_COMPATIBLE_SECURITY (AP security mismatch)";
    case 211: return "NO_AP_FOUND_IN_AUTHMODE_THRESHOLD";
    case 212: return "NO_AP_FOUND_IN_RSSI_THRESHOLD";
    default:                             return "other";
    }
}

static void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.printf("[wifi] connected, IP=%s rssi=%d\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        wasConnected_ = true;
        lastDiscLog_ms = 0;
        if (onConnected_) onConnected_();
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        uint8_t reason = info.wifi_sta_disconnected.reason;
        uint32_t now = millis();
        // First-ever or post-connect disconnect: always log; subsequent
        // ones (during the stack's auto-reconnect retries) at most every 10s.
        if (lastDiscLog_ms == 0 || (int32_t)(now - lastDiscLog_ms) >= 10000) {
            Serial.printf("[wifi] disconnected, reason=%u (%s)\n",
                          reason, discReasonName(reason));
            lastDiscLog_ms = now;
        }
        if (wasConnected_ && onDisconnected_) onDisconnected_();
        wasConnected_ = false;
        // Do NOT call WiFi.reconnect() here — setAutoReconnect(true) is
        // already retrying. A manual call collides with the in-flight
        // attempt and prints "sta is connecting, return error".
        break;
    }
    case ARDUINO_EVENT_WIFI_STA_START:
        Serial.println("[wifi] STA started");
        break;
    default:
        break;
    }
}

void setCallbacks(Callback onConnected, Callback onDisconnected) {
    onConnected_    = onConnected;
    onDisconnected_ = onDisconnected;
}

bool begin(const char* ssid, const char* pass) {
    if (!ssid || !*ssid) {
        Serial.println("[wifi] SSID empty - WiFi disabled");
        enabled_ = false;
        return false;
    }
    strncpy(ssid_, ssid, sizeof(ssid_) - 1); ssid_[sizeof(ssid_) - 1] = 0;
    strncpy(pass_, pass ? pass : "", sizeof(pass_) - 1); pass_[sizeof(pass_) - 1] = 0;

    // Match the user's known-working sketch: mode + begin, then block
    // until connected (or timeout).
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid_, pass_);
    Serial.printf("[wifi] connecting to %s ", ssid_);

    const uint32_t deadline = millis() + 15000;
    while (WiFi.status() != WL_CONNECTED && (int32_t)(millis() - deadline) < 0) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[wifi] connected, IP=%s rssi=%d\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        wasConnected_ = true;
        enabled_ = true;
        // No modem sleep — small TCP packets (telnet keypresses, FTP
        // command replies) sit in lwIP buffer waiting for the radio to
        // wake, which destroys interactive throughput. Display recovers
        // via the post-connect LCD re-init in vZ80.ino instead.
        WiFi.setSleep(WIFI_PS_NONE);
        // Now register the event handler for future disconnect/reconnect.
        WiFi.onEvent(onWifiEvent);
        WiFi.setAutoReconnect(true);
        if (onConnected_) onConnected_();
    } else {
        Serial.printf("[wifi] connect timeout (status=%d) — giving up for now\n",
                      WiFi.status());
        enabled_ = false;
    }
    return enabled_;
}

bool      connected() { return enabled_ && WiFi.status() == WL_CONNECTED; }
IPAddress localIP()   { return WiFi.localIP(); }

}  // namespace WifiSta
