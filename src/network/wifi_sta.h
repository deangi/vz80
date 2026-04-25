#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// Lightweight STA wrapper. Empty SSID = disabled (begin() is a no-op).
// Calls onConnected/onDisconnected from the system event task on core 0,
// so handlers must be brief and post work to a queue or task — they must
// not touch LovyanGFX/SD/SPI directly.

namespace WifiSta {

using Callback = void (*)();

void setCallbacks(Callback onConnected, Callback onDisconnected);

// Kicks off connection. Reconnect is automatic on disconnect events.
bool begin(const char* ssid, const char* pass);

bool connected();
IPAddress localIP();

}  // namespace WifiSta
