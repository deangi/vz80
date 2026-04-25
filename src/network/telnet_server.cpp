#include "telnet_server.h"

#include <WiFi.h>

TelnetServer gTelnet;

// Telnet protocol bytes
static constexpr uint8_t IAC  = 0xFF;
static constexpr uint8_t DONT = 0xFE;
static constexpr uint8_t DO   = 0xFD;
static constexpr uint8_t WONT = 0xFC;
static constexpr uint8_t WILL = 0xFB;
static constexpr uint8_t SB   = 0xFA;
static constexpr uint8_t SE   = 0xF0;

static constexpr uint8_t OPT_BINARY = 0x00;
static constexpr uint8_t OPT_ECHO   = 0x01;
static constexpr uint8_t OPT_SGA    = 0x03;  // suppress go-ahead

bool TelnetServer::begin(uint16_t port, StreamBufferHandle_t rxStream) {
    rx_ = rxStream;
    if (server_) {
        Serial.println("[telnet] already listening");
        return true;
    }
    auto* s = new WiFiServer(port);
    s->begin();
    s->setNoDelay(true);
    server_ = s;
    Serial.printf("[telnet] listening on port %u\n", (unsigned)port);
    return true;
}

bool TelnetServer::clientConnected() const {
    if (!haveClient_) return false;
    auto* c = static_cast<WiFiClient*>(client_);
    return c && c->connected();
}

void TelnetServer::disconnect() {
    if (haveClient_) {
        auto* c = static_cast<WiFiClient*>(client_);
        if (c) { c->stop(); delete c; }
        client_ = nullptr;
        haveClient_ = false;
        iacState_ = IacState::Data;
        Serial.println("[telnet] client disconnected (forced)");
    }
}

void TelnetServer::doNegotiation() {
    auto* c = static_cast<WiFiClient*>(client_);
    // Server raw-mode trio: we echo, we suppress GA, we want binary on both
    // directions. Order matters less than getting them all out before any
    // app data.
    const uint8_t init[] = {
        IAC, WILL, OPT_ECHO,
        IAC, WILL, OPT_SGA,
        IAC, WILL, OPT_BINARY,
        IAC, DO,   OPT_BINARY,
    };
    c->write(init, sizeof(init));
}

void TelnetServer::readFromClient() {
    auto* c = static_cast<WiFiClient*>(client_);
    int avail = c->available();
    if (avail > 0) {
        Serial.printf("[telnet] %d bytes from client:", avail);
    }
    while (c->available()) {
        uint8_t b = (uint8_t)c->read();
        Serial.printf(" %02x", b);
        switch (iacState_) {
        case IacState::Data:
            if (b == IAC) iacState_ = IacState::Iac;
            else if (rx_) xStreamBufferSend(rx_, &b, 1, 0);
            break;
        case IacState::Iac:
            if      (b == IAC)  { if (rx_) xStreamBufferSend(rx_, &b, 1, 0); iacState_ = IacState::Data; }
            else if (b == WILL) iacState_ = IacState::Will;
            else if (b == WONT) iacState_ = IacState::Wont;
            else if (b == DO)   iacState_ = IacState::Do;
            else if (b == DONT) iacState_ = IacState::Dont;
            else if (b == SB)   iacState_ = IacState::Sb;
            else                iacState_ = IacState::Data;
            break;
        case IacState::Will: {
            // Client offers to do option <b>. We already said DO BINARY, so
            // any WILL BINARY reply needs no further ack (avoid ping-pong).
            // For everything else, respond DONT to close the negotiation.
            if (b != OPT_BINARY) {
                uint8_t reply[3] = { IAC, DONT, b };
                c->write(reply, 3);
            }
            iacState_ = IacState::Data;
            break;
        }
        case IacState::Do: {
            // Client asks us to do option <b>. We already WILL'd ECHO/SGA/
            // BINARY at connect, so confirmations need no reply. Refuse
            // everything else with WONT.
            bool weDo = (b == OPT_ECHO || b == OPT_SGA || b == OPT_BINARY);
            if (!weDo) {
                uint8_t reply[3] = { IAC, WONT, b };
                c->write(reply, 3);
            }
            iacState_ = IacState::Data;
            break;
        }
        case IacState::Wont:
        case IacState::Dont:
            iacState_ = IacState::Data;
            break;
        case IacState::Sb:
            if (b == IAC) iacState_ = IacState::SbIac;
            break;
        case IacState::SbIac:
            iacState_ = (b == SE) ? IacState::Data : IacState::Sb;
            break;
        }
    }
    if (avail > 0) Serial.println();
}

void TelnetServer::tick() {
    auto* s = static_cast<WiFiServer*>(server_);
    if (!s) return;

    // Accept new connection if we don't have one
    if (!haveClient_) {
        WiFiClient incoming = s->accept();
        if (incoming) {
            auto* c = new WiFiClient(incoming);
            c->setNoDelay(true);
            client_ = c;
            haveClient_ = true;
            iacState_ = IacState::Data;
            Serial.printf("[telnet] client %s connected\n",
                          c->remoteIP().toString().c_str());
            doNegotiation();
        }
    } else {
        auto* c = static_cast<WiFiClient*>(client_);
        if (!c->connected()) {
            Serial.printf("[telnet] client disconnected (was %s)\n",
                          c->remoteIP().toString().c_str());
            c->stop();
            delete c;
            client_ = nullptr;
            haveClient_ = false;
            iacState_ = IacState::Data;
            return;
        }
        readFromClient();
    }

    // If a second client tries to connect while one is active, politely
    // reject so they don't hang.
    if (haveClient_ && s->hasClient()) {
        WiFiClient extra = s->accept();
        if (extra) {
            const char* msg = "vZ80 telnet busy\r\n";
            extra.write((const uint8_t*)msg, strlen(msg));
            extra.stop();
        }
    }
}

void TelnetServer::writeByte(uint8_t b) {
    if (!haveClient_) return;
    auto* c = static_cast<WiFiClient*>(client_);
    if (b == IAC) {
        const uint8_t esc[2] = { IAC, IAC };
        c->write(esc, 2);
    } else {
        c->write(&b, 1);
    }
}

// Retry-write helper: WiFiClient::write returns 0 when lwIP's send buffer
// is full. Keep retrying with brief yields until either everything's sent
// or the connection drops. Caps total wait at ~1s per chunk to avoid
// hanging the main loop indefinitely on a stuck client.
static bool tcpWriteAll(WiFiClient* c, const uint8_t* buf, size_t len) {
    size_t sent = 0;
    int    retries = 100;  // 100 * 10ms = 1s ceiling
    while (sent < len && retries > 0) {
        if (!c->connected()) return false;
        size_t n = c->write(buf + sent, len - sent);
        if (n > 0) {
            sent += n;
        } else {
            delay(10);
            retries--;
        }
    }
    return sent == len;
}

void TelnetServer::writeBytes(const uint8_t* data, size_t len) {
    if (!haveClient_ || !data || !len) return;
    auto* c = static_cast<WiFiClient*>(client_);
    if (!c->connected()) return;

    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == IAC) {
            if (i > start && !tcpWriteAll(c, data + start, i - start)) return;
            const uint8_t esc[2] = { IAC, IAC };
            if (!tcpWriteAll(c, esc, 2)) return;
            start = i + 1;
        }
    }
    if (start < len) tcpWriteAll(c, data + start, len - start);
}
