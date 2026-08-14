#pragma once
#include <Arduino.h>

struct AppConfig {
  // [system] in /z80config.ini
  String title;

  // Optional iCOM / disk PROM image loaded into Z80 RAM at boot.
  // Empty = built-in trap stubs only. Host still overlays SELDSK..WRITE
  // stubs at 0xF02B after load so SD .dsk I/O keeps working.
  String   prom_path;
  uint16_t prom_addr = 0xF000;

  // [wifi] in /wificonfig.ini
  String wifi_ssid;
  String wifi_password;
  String wifi_hostname;

  // [ntp] in /wificonfig.ini
  bool   ntp_enabled = true;
  String ntp_server;

  // [telnet] in /wificonfig.ini
  bool   telnet_enabled = true;
  int    telnet_port    = 23;

  // [ftp] in /wificonfig.ini
  // Passive-mode FTP server exposing the SD card root. The server uses
  // ftp_port for the control channel and ftp_port+1 for passive data.
  bool   ftp_enabled  = true;
  int    ftp_port     = 21;
  String ftp_user;
  String ftp_password;

  // [console] in /z80config.ini
  // terminal selects the TFT console escape-sequence personality:
  // "vt100" or "adm3a". Default is ADM-3A (WordStar / CP/M apps).
  String terminal;

  // Bytes injected into the console input queue after each Z80 boot/reset.
  // Parsed from escaped text, e.g. "dir\r" or "^C".
  static const size_t BOOT_INPUT_MAX = 256;
  uint8_t boot_input[BOOT_INPUT_MAX];
  size_t  boot_input_len = 0;

  // [disks] in /z80config.ini
  String disk_a;       // CP/M drive A
  String disk_b;       // CP/M drive B
  String disk_c;       // CP/M drive C
  String disk_d;       // CP/M drive D
  char   boot_drive = 'a';
};

// Global config instance owned by vZ80.ino.
extern AppConfig cfg;

// SD card
bool sd_mount();

// Config file IO. Network settings live in /wificonfig.ini; emulator
// profile settings live in /z80config.ini. Variants are named
// wificonfig-NAME.ini and z80config-NAME.ini.
bool config_load_wifi(AppConfig& cfg);                       // true if /wificonfig.ini existed
bool config_load_vz80(AppConfig& cfg);                       // true if /z80config.ini existed
bool config_write_default_wifi(const AppConfig& cfg);
bool config_write_default_vz80(const AppConfig& cfg);
void config_apply_compiled_defaults(AppConfig& cfg);

// Load cfg.prom_path into ram[cfg.prom_addr]. Always returns true unless
// ram is invalid. Missing/unreadable PROM falls back to the built-in stubs
// (caller still calls installStubs).
bool config_load_prom(const AppConfig& cfg, uint8_t* ram, size_t ram_size);

// SD-to-SD byte copy used by the variant picker. Truncates dst.
bool config_copy_file(const char* src, const char* dst);

// List variants. Stores the middle NAME between prefix and ".ini".
int  config_list_variants(const char* prefix, char names[][44], int max);

// Create or verify a disk image. New files are filled with `fill`.
// 0xE5 is the CP/M 2.2 unused-directory / FORMAT byte.
bool ensure_disk_image(const char* path, uint32_t bytes,
                       bool create_if_missing, const char* label,
                       uint8_t fill = 0xE5);

// Logging helper
void config_print(const AppConfig& cfg);
