#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

// Single-client telnet server that bridges a remote terminal to the CP/M
// console streams. On client connect we negotiate the standard server-side
// raw-mode trio: WILL ECHO, WILL SUPPRESS-GO-AHEAD, DO BINARY (chr-at-a-time,
// no local echo, 8-bit clean). Bytes received from the client are pushed
// into rxStream (alongside Serial/BT input). Bytes the loop reads from
// txStream should be relayed via writeByte() so the client sees console
// output. tick() is non-blocking; call it from the main loop.

class TelnetServer {
public:
    bool begin(uint16_t port, StreamBufferHandle_t rxStream);
    void tick();

    // Returns true if a client is connected.
    bool clientConnected() const;

    // IP address of the connected client; 0.0.0.0 if none.
    IPAddress clientIP() const;

    // Send a single byte to the connected client (no-op if none). IAC
    // (0xFF) gets escaped per RFC 854.
    void writeByte(uint8_t b);
    void writeBytes(const uint8_t* data, size_t len);

    // Drop any active client (e.g., before WiFi tear-down).
    void disconnect();

private:
    void doNegotiation();
    void readFromClient();

    void* server_      = nullptr;   // WiFiServer*
    void* client_      = nullptr;   // WiFiClient*
    bool  haveClient_  = false;
    StreamBufferHandle_t rx_ = nullptr;

    // IAC parsing state for inbound bytes
    enum class IacState : uint8_t { Data, Iac, Will, Wont, Do, Dont, Sb, SbIac };
    IacState iacState_ = IacState::Data;
};

extern TelnetServer gTelnet;
