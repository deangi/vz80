#include "bt_keyboard.h"

#include <string.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp_gap_bt_api.h>
#include <esp_gap_ble_api.h>
#include <esp_gattc_api.h>
#include <esp_event.h>
#include <esp_hidh.h>
#include <esp_hidh_gattc.h>

// NB: btInUse() override lives in vZ80.ino (main sketch). Putting it in a
// src/ subfolder is unreliable because Arduino packs those into a separate
// archive and the linker may still resolve the weak default from the core.

BtKeyboard gBtKbd;

// -----------------------------------------------------------------------------
// HID Usage (Keyboard/Keypad page 0x07) -> ASCII
// Index = usage code, value = ASCII (or 0 if no mapping).
// Columns: unshifted, shifted.
// Boot-protocol keyboard reports usages in this page only.
// -----------------------------------------------------------------------------
struct KeyMap { char base; char shift; };

static const KeyMap kMap[0x68] = {
    /* 00 */ {0,0},{0,0},{0,0},{0,0},
    /* 04..1D a..z */
    {'a','A'},{'b','B'},{'c','C'},{'d','D'},{'e','E'},{'f','F'},{'g','G'},{'h','H'},
    {'i','I'},{'j','J'},{'k','K'},{'l','L'},{'m','M'},{'n','N'},{'o','O'},{'p','P'},
    {'q','Q'},{'r','R'},{'s','S'},{'t','T'},{'u','U'},{'v','V'},{'w','W'},{'x','X'},
    {'y','Y'},{'z','Z'},
    /* 1E..27 1..0 top row */
    {'1','!'},{'2','@'},{'3','#'},{'4','$'},{'5','%'},
    {'6','^'},{'7','&'},{'8','*'},{'9','('},{'0',')'},
    /* 28 Enter, 29 Esc, 2A BS, 2B Tab, 2C Space */
    {'\r','\r'},{0x1B,0x1B},{0x08,0x08},{'\t','\t'},{' ',' '},
    /* 2D..38 punctuation */
    {'-','_'},{'=','+'},{'[','{'},{']','}'},{'\\','|'},
    /* 33 non-US # / ~ (we treat as ';'/':') */
    {';',':'},{';',':'},{'\'','"'},{'`','~'},{',','<'},{'.','>'},{'/','?'},
    /* 39 CapsLock (no ascii) */
    {0,0},
    /* 3A..45 F1..F12 — no mapping */
    {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
    /* 46..52 PrtSc, ScrLk, Pause, Ins, Home, PgUp, Del, End, PgDn, arrows */
    {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0x7F,0x7F},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
    /* 53 NumLock, 54..63 keypad */
    {0,0},
    {'/','/'},{'*','*'},{'-','-'},{'+','+'},{'\r','\r'},
    {'1','1'},{'2','2'},{'3','3'},{'4','4'},{'5','5'},
    {'6','6'},{'7','7'},{'8','8'},{'9','9'},{'0','0'},
    {'.','.'},
};

static constexpr uint8_t HID_MOD_LCTRL  = 0x01;
static constexpr uint8_t HID_MOD_LSHIFT = 0x02;
static constexpr uint8_t HID_MOD_LALT   = 0x04;
static constexpr uint8_t HID_MOD_RCTRL  = 0x10;
static constexpr uint8_t HID_MOD_RSHIFT = 0x20;
static constexpr uint8_t HID_MOD_RALT   = 0x40;

static inline bool modShift(uint8_t m) { return m & (HID_MOD_LSHIFT | HID_MOD_RSHIFT); }
static inline bool modCtrl (uint8_t m) { return m & (HID_MOD_LCTRL  | HID_MOD_RCTRL ); }

// Convert a single HID usage + modifier byte into 0 (no emit) or an ASCII byte.
static char usageToAscii(uint8_t usage, uint8_t mods) {
    if (usage >= sizeof(kMap) / sizeof(kMap[0])) return 0;
    char c = modShift(mods) ? kMap[usage].shift : kMap[usage].base;
    if (c == 0) return 0;
    if (modCtrl(mods) && c >= 'a' && c <= 'z') c = (c - 'a') + 1;     // ^A..^Z
    if (modCtrl(mods) && c >= 'A' && c <= 'Z') c = (c - 'A') + 1;
    return c;
}

// -----------------------------------------------------------------------------
// Pairing window timer
// -----------------------------------------------------------------------------
static constexpr uint32_t kPairingWindowMs = 20000;
static uint32_t pairingDeadline_ = 0;

// Seen-MAC dedup for adv logging — keeps the serial console from being
// drowned by repeat ads from the same nearby device. Reset at scan start.
// 128 slots covers crowded RF environments (iPhone + AirPods + Watch soup).
static constexpr int kSeenMax = 128;
static uint8_t seenBdas_[kSeenMax][6];
static int     seenCount_ = 0;
static bool    seenOverflowLogged_ = false;

static void resetSeenAdvs() { seenCount_ = 0; seenOverflowLogged_ = false; }

// Returns true only for the first sighting of a MAC within the current scan
// window. If the buffer is full we suppress the log (return false) so the
// serial doesn't flood — a one-time overflow notice goes out instead.
static bool markSeenAdv(const uint8_t* bda) {
    for (int i = 0; i < seenCount_; ++i) {
        if (memcmp(seenBdas_[i], bda, 6) == 0) return false;
    }
    if (seenCount_ >= kSeenMax) {
        if (!seenOverflowLogged_) {
            Serial.printf("[BT] seen-MAC buffer full (%d) — suppressing further advs\n",
                          kSeenMax);
            seenOverflowLogged_ = true;
        }
        return false;
    }
    memcpy(seenBdas_[seenCount_++], bda, 6);
    return true;
}

static void stopAllScans() {
    esp_ble_gap_stop_scanning();
}

static void startScans() {
    // BLE-only build: no BR/EDR inquiry. Scan start happens in the
    // SET_SCAN_PARAMS_COMPLETE event fired by the stack after set_scan_params.
    esp_ble_scan_params_t scan = {};
    scan.scan_type          = BLE_SCAN_TYPE_ACTIVE;
    scan.own_addr_type      = BLE_ADDR_TYPE_PUBLIC;
    scan.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    scan.scan_interval      = 0x50;
    scan.scan_window        = 0x30;
    scan.scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE;  // we dedup in s/w by MAC
    resetSeenAdvs();
    esp_err_t e = esp_ble_gap_set_scan_params(&scan);
    Serial.printf("[BT] set_scan_params -> 0x%x\n", e);
}

// -----------------------------------------------------------------------------
// BR/EDR GAP callback
// -----------------------------------------------------------------------------
static bool isHidCoD(uint32_t cod) {
    // Major device class 0x05 (Peripheral) == HID family.
    uint8_t major = (cod >> 8) & 0x1F;
    return major == 0x05;
}

static void getDeviceName(esp_bt_gap_cb_param_t* p, char* out, size_t n) {
    out[0] = 0;
    for (int i = 0; i < p->disc_res.num_prop; ++i) {
        esp_bt_gap_dev_prop_t* pr = &p->disc_res.prop[i];
        if (pr->type == ESP_BT_GAP_DEV_PROP_BDNAME) {
            size_t len = pr->len < n - 1 ? pr->len : n - 1;
            memcpy(out, pr->val, len);
            out[len] = 0;
            return;
        } else if (pr->type == ESP_BT_GAP_DEV_PROP_EIR) {
            uint8_t nameLen = 0;
            uint8_t* name = esp_bt_gap_resolve_eir_data(
                (uint8_t*)pr->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &nameLen);
            if (!name) {
                name = esp_bt_gap_resolve_eir_data(
                    (uint8_t*)pr->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &nameLen);
            }
            if (name && nameLen) {
                size_t len = nameLen < n - 1 ? nameLen : n - 1;
                memcpy(out, name, len);
                out[len] = 0;
                return;
            }
        }
    }
}

static void gapBredrCb(esp_bt_gap_cb_event_t ev, esp_bt_gap_cb_param_t* p) {
    switch (ev) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        uint32_t cod = 0;
        char name[32] = {0};
        for (int i = 0; i < p->disc_res.num_prop; ++i) {
            if (p->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_COD) {
                cod = *(uint32_t*)p->disc_res.prop[i].val;
            }
        }
        if (!isHidCoD(cod)) return;
        getDeviceName(p, name, sizeof(name));
        Serial.printf("[BT] BR/EDR HID device: %s cod=0x%06x\n", name, (unsigned)cod);

        esp_bt_gap_cancel_discovery();
        // Open HID over BR/EDR. addr_type is ignored for BR/EDR.
        esp_hidh_dev_open(p->disc_res.bda, ESP_HID_TRANSPORT_BT, 0);
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        Serial.printf("[BT] BR/EDR discovery state=%d\n", p->disc_st_chg.state);
        break;
    case ESP_BT_GAP_CFM_REQ_EVT:
        Serial.printf("[BT] SSP confirm %u -> accept\n", p->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        Serial.printf("[BT] SSP passkey %u\n", p->key_notif.passkey);
        break;
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (p->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
            Serial.printf("[BT] BR/EDR auth OK %s\n", p->auth_cmpl.device_name);
        else
            Serial.printf("[BT] BR/EDR auth FAIL 0x%x\n", p->auth_cmpl.stat);
        break;
    default:
        break;
    }
}

// -----------------------------------------------------------------------------
// BLE GAP callback
// -----------------------------------------------------------------------------
static bool bleAdvIsHidKeyboard(uint8_t* adv, uint8_t adv_len) {
    uint8_t app_len = 0;
    uint8_t* app = esp_ble_resolve_adv_data(adv, ESP_BLE_AD_TYPE_APPEARANCE, &app_len);
    if (app && app_len >= 2) {
        uint16_t appearance = app[0] | (app[1] << 8);
        // HID category = 0x03C0..0x03FF; 0x03C1 keyboard, 0x03C2 mouse, 0x03C3 combo
        if (appearance == 0x03C1 || appearance == 0x03C3) return true;
    }
    // Also accept if HID service UUID 0x1812 in complete or partial lists.
    uint8_t u_len = 0;
    uint8_t* uuid = esp_ble_resolve_adv_data(adv, ESP_BLE_AD_TYPE_16SRV_CMPL, &u_len);
    if (!uuid) uuid = esp_ble_resolve_adv_data(adv, ESP_BLE_AD_TYPE_16SRV_PART, &u_len);
    if (uuid) {
        for (int i = 0; i + 1 < u_len; i += 2) {
            uint16_t u = uuid[i] | (uuid[i+1] << 8);
            if (u == 0x1812) return true;
        }
    }
    return false;
}

static void gapBleCb(esp_gap_ble_cb_event_t ev, esp_ble_gap_cb_param_t* p) {
    switch (ev) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
        esp_err_t e = esp_ble_gap_start_scanning(20);  // 20s, match pairing window
        Serial.printf("[BT] start_scanning(20) -> 0x%x\n", e);
        break;
    }
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        Serial.printf("[BT] scan start status = 0x%x\n",
                      p->scan_start_cmpl.status);
        break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        Serial.printf("[BT] scan stop status = 0x%x\n",
                      p->scan_stop_cmpl.status);
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        auto* r = &p->scan_rst;
        if (r->search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) {
            Serial.println("[BT] BLE scan window ended");
            break;
        }
        if (r->search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;

        // Skip repeat ads from the same MAC. We still want to see HID matches
        // every time, so the dedup gate is below the HID-detect logic.
        bool firstSighting = markSeenAdv(r->bda);

        // Log every advertisement so we can see what's out there, even if it
        // doesn't match the HID filter.
        uint8_t name_len = 0;
        uint8_t* name = esp_ble_resolve_adv_data(
            r->ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &name_len);
        if (!name) name = esp_ble_resolve_adv_data(
            r->ble_adv, ESP_BLE_AD_TYPE_NAME_SHORT, &name_len);
        char nm[32] = "<noname>";
        if (name && name_len) {
            size_t n = name_len < sizeof(nm) - 1 ? name_len : sizeof(nm) - 1;
            memcpy(nm, name, n); nm[n] = 0;
        }
        uint8_t app_len = 0;
        uint8_t* app = esp_ble_resolve_adv_data(
            r->ble_adv, ESP_BLE_AD_TYPE_APPEARANCE, &app_len);
        uint16_t appearance = (app && app_len >= 2) ? (app[0] | (app[1] << 8)) : 0;
        bool isHid = bleAdvIsHidKeyboard(
            r->ble_adv, r->adv_data_len + r->scan_rsp_len);

        if (firstSighting || isHid) {
            Serial.printf("[BT] adv %02x:%02x:%02x:%02x:%02x:%02x rssi=%d app=0x%04x hid=%d name=%s\n",
                r->bda[0], r->bda[1], r->bda[2], r->bda[3], r->bda[4], r->bda[5],
                r->rssi, appearance, isHid ? 1 : 0, nm);

            // Hexdump high-RSSI advertisements (devices within ~1m). Useful
            // for identifying a close-by keyboard whose ads omit name+HID.
            if (r->rssi >= -50) {
                int total = r->adv_data_len + r->scan_rsp_len;
                Serial.printf("[BT]   raw[%d/%d]:", r->adv_data_len, r->scan_rsp_len);
                for (int i = 0; i < total && i < 62; ++i) {
                    Serial.printf(" %02x", r->ble_adv[i]);
                }
                Serial.println();
            }
        }

        if (!isHid) break;
        Serial.printf("[BT] BLE HID device: %s (connecting)\n", nm);
        esp_ble_gap_stop_scanning();
        esp_hidh_dev_open(r->bda, ESP_HID_TRANSPORT_BLE,
                          (esp_ble_addr_type_t)r->ble_addr_type);
        break;
    }
    case ESP_GAP_BLE_SEC_REQ_EVT:
        // Peripheral is asking us to enable encryption; grant.
        esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_PASSKEY_NOTIF_EVT:
        Serial.printf("[BT] BLE passkey notif: %06u\n",
                      (unsigned)p->ble_security.key_notif.passkey);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        if (p->ble_security.auth_cmpl.success)
            Serial.println("[BT] BLE auth OK");
        else
            Serial.printf("[BT] BLE auth FAIL reason=0x%x\n",
                          p->ble_security.auth_cmpl.fail_reason);
        break;
    default:
        break;
    }
}

// -----------------------------------------------------------------------------
// HIDH callback
// -----------------------------------------------------------------------------
static void hidhCallback(void* /*arg*/, esp_event_base_t base,
                         int32_t id, void* data) {
    auto* param = (esp_hidh_event_data_t*)data;
    switch ((esp_hidh_event_t)id) {
    case ESP_HIDH_OPEN_EVENT: {
        const uint8_t* bda = esp_hidh_dev_bda_get(param->open.dev);
        const char* name = esp_hidh_dev_name_get(param->open.dev);
        if (param->open.status == ESP_OK) {
            Serial.printf("[BT] OPEN ok: %s (%02x:%02x:%02x:%02x:%02x:%02x)\n",
                          name ? name : "?",
                          bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            gBtKbd.onHidOpen(true, name ? name : "kbd");
        } else {
            Serial.printf("[BT] OPEN fail 0x%x\n", param->open.status);
            gBtKbd.onHidOpen(false, nullptr);
        }
        break;
    }
    case ESP_HIDH_INPUT_EVENT:
        gBtKbd.onHidInput(param->input.data, param->input.length);
        break;
    case ESP_HIDH_CLOSE_EVENT:
        Serial.println("[BT] CLOSE");
        gBtKbd.onHidClose();
        break;
    default:
        break;
    }
}

// -----------------------------------------------------------------------------
// BtKeyboard implementation
// -----------------------------------------------------------------------------
// Lazy init: begin() just records rx and flags ready. The heavy stack
// bring-up (controller + bluedroid + HID host) happens on first
// startPairing() so we don't eat heap (or risk crashing) at boot.
static bool stackUp_ = false;

#define BT_CP(n) do { \
    Serial.printf("[BT] cp %d heap=%u largest=%u\n", \
        (n), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap()); \
    Serial.flush(); \
} while (0)

static bool bringStackUp() {
    BT_CP(1);
    esp_bt_controller_status_t st = esp_bt_controller_get_status();
    Serial.printf("[BT] controller status = %d\n", (int)st);

    if (st == ESP_BT_CONTROLLER_STATUS_IDLE) {
        // Classic BT memory was already released at boot in vZ80.ino setup()
        // so WiFi could have heap headroom. Only BLE memory remains reserved.
        esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        cfg.mode = ESP_BT_MODE_BLE;
        esp_err_t e = esp_bt_controller_init(&cfg);
        if (e != ESP_OK) { Serial.printf("[BT] controller init fail 0x%x\n", e); return false; }
        st = esp_bt_controller_get_status();
    }
    BT_CP(2);
    if (st == ESP_BT_CONTROLLER_STATUS_INITED) {
        esp_err_t e = esp_bt_controller_enable(ESP_BT_MODE_BLE);
        if (e != ESP_OK) { Serial.printf("[BT] controller enable fail 0x%x\n", e); return false; }
    }
    BT_CP(3);
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        if (esp_bluedroid_init() != ESP_OK) { Serial.println("[BT] bluedroid init fail"); return false; }
    }
    BT_CP(4);
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED) {
        if (esp_bluedroid_enable() != ESP_OK) { Serial.println("[BT] bluedroid enable fail"); return false; }
    }
    BT_CP(5);

    esp_bt_dev_set_device_name("vZ80");

    esp_ble_auth_req_t  ble_auth = ESP_LE_AUTH_REQ_SC_MITM_BOND;
    esp_ble_io_cap_t    ble_iocap = ESP_IO_CAP_NONE;
    uint8_t             key_size = 16;
    uint8_t             init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t             rsp_key  = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &ble_auth, sizeof(ble_auth));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,      &ble_iocap, sizeof(ble_iocap));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE,    &key_size, sizeof(key_size));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY,    &init_key, sizeof(init_key));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,     &rsp_key,  sizeof(rsp_key));

    esp_ble_gap_register_callback(gapBleCb);
    esp_ble_gattc_register_callback(
        (esp_gattc_cb_t)esp_hidh_gattc_event_handler);
    BT_CP(6);

    Serial.println("[BT] about to esp_hidh_init");
    Serial.flush();

    esp_hidh_config_t hidh_cfg = {
        .callback          = hidhCallback,
        .event_stack_size  = 4096,  // 2048 caused hidh_init to hang
        .callback_arg      = nullptr,
    };
    esp_err_t hr = esp_hidh_init(&hidh_cfg);
    Serial.printf("[BT] esp_hidh_init returned 0x%x\n", hr);
    Serial.flush();
    if (hr != ESP_OK) {
        // Leave Bluedroid up; a partial teardown at low heap can itself
        // crash. Re-try from a cold boot if needed.
        return false;
    }
    BT_CP(7);

    Serial.println("[BT] stack up");
    return true;
}

bool BtKeyboard::begin(StreamBufferHandle_t rx) {
    rx_ = rx;
    state_ = State::Idle;    // "ready to pair" - actual stack comes up lazily
    return true;
}

void BtKeyboard::startPairing() {
    if (state_ == State::Off) { Serial.println("[BT] not ready"); return; }
    if (state_ == State::Connected) { Serial.println("[BT] already connected"); return; }
    if (!stackUp_) {
        Serial.println("[BT] bringing stack up (lazy)");
        if (!bringStackUp()) {
            Serial.println("[BT] stack bring-up failed");
            state_ = State::Off;
            return;
        }
        stackUp_ = true;
    }
    Serial.println("[BT] starting 20s discovery window");
    state_ = State::Pairing;
    pairingDeadline_ = millis() + kPairingWindowMs;
    startScans();
}

void BtKeyboard::disconnect() {
    stopAllScans();
    // esp_hidh_dev_close would require the device handle; skipped for now.
    state_ = State::Idle;
}

void BtKeyboard::onPairingTimeout() {
    if (state_ == State::Pairing) {
        stopAllScans();
        state_ = State::Idle;
        Serial.println("[BT] pairing window closed, no HID keyboard found");
    }
}

void BtKeyboard::onHidOpen(bool ok, const char* name) {
    if (ok) {
        strncpy(devName_, name ? name : "kbd", sizeof(devName_) - 1);
        devName_[sizeof(devName_) - 1] = 0;
        state_ = State::Connected;
        memset(prevKeys_, 0, sizeof(prevKeys_));
        prevMods_ = 0;
    } else {
        state_ = State::Idle;
    }
}

void BtKeyboard::onHidClose() {
    devName_[0] = 0;
    state_ = State::Idle;
}

void BtKeyboard::pushChar(uint8_t c) {
    if (rx_) xStreamBufferSend(rx_, &c, 1, 0);
}

void BtKeyboard::onHidInput(const uint8_t* data, uint16_t len) {
    // Boot-protocol keyboard report: 8 bytes
    //   [0]=modifiers, [1]=reserved, [2..7]=up to 6 usage codes
    // BLE HoGP reports sometimes prefix a report ID; accept 8 or 9 byte forms.
    const uint8_t* r = data;
    if (len == 9) { r++; len = 8; }
    if (len < 8) return;

    uint8_t mods = r[0];

    // Key-down = usage appears in current report but not in previous.
    for (int i = 2; i < 8; ++i) {
        uint8_t u = r[i];
        if (u == 0 || u == 1) continue;  // 1 = ErrorRollOver
        bool wasDown = false;
        for (int j = 2; j < 8; ++j) if (prevKeys_[j - 2] == u) { wasDown = true; break; }
        if (wasDown) continue;
        char c = usageToAscii(u, mods);
        if (c) pushChar((uint8_t)c);
    }

    for (int i = 0; i < 6; ++i) prevKeys_[i] = r[2 + i];
    prevMods_ = mods;
}

const char* BtKeyboard::statusText() const {
    switch (state_) {
    case State::Off:        return "BT-";
    case State::Idle:       return "BT";
    case State::Pairing:    return "BT?";
    case State::Connecting: return "BT.";
    case State::Connected:  return "BT+";
    }
    return "BT";
}

// External hook so loop() can expire the pairing window without adding a task.
void BtKeyboardTick() {
    if (gBtKbd.state() == BtKeyboard::State::Pairing &&
        (int32_t)(millis() - pairingDeadline_) >= 0) {
        gBtKbd.onPairingTimeout();
    }
}
