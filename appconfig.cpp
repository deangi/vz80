#include "appconfig.h"
#include "config.h"
#include "platform.h"
#include "secrets.h"
#include "SD_FTP_Server/src/SD_FTP_Server.h"

#include <SD_MMC.h>
#include <string.h>

// -------- helpers --------

static String trim(const String& s) {
  int a = 0, b = (int)s.length();
  while (a < b && isspace((uint8_t)s[a])) a++;
  while (b > a && isspace((uint8_t)s[b - 1])) b--;
  return s.substring(a, b);
}

static String to_lower(const String& s) {
  String t = s;
  t.toLowerCase();
  return t;
}

static bool truthy(const String& v) {
  return v.equalsIgnoreCase("true") || v == "1"
      || v.equalsIgnoreCase("yes")  || v.equalsIgnoreCase("on");
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static String strip_inline_comment(const String& val) {
  bool in_quote = false;
  char quote = 0;
  bool escaped = false;
  for (int i = 0; i < (int)val.length(); i++) {
    char c = val[i];
    if (escaped) { escaped = false; continue; }
    if (c == '\\') { escaped = true; continue; }
    if (in_quote) {
      if (c == quote) in_quote = false;
      continue;
    }
    if (c == '"' || c == '\'') {
      in_quote = true;
      quote = c;
      continue;
    }
    if (c == ';' || c == '#') return trim(val.substring(0, i));
  }
  return trim(val);
}

static String unquote_config_value(const String& val) {
  if (val.length() >= 2) {
    char q = val[0];
    if ((q == '"' || q == '\'') && val[val.length() - 1] == q)
      return val.substring(1, val.length() - 1);
  }
  return val;
}

static void config_set_boot_input(AppConfig& cfg, const String& encoded) {
  cfg.boot_input_len = 0;
  String s = unquote_config_value(encoded);

  for (int i = 0; i < (int)s.length() &&
                  cfg.boot_input_len < AppConfig::BOOT_INPUT_MAX; i++) {
    uint8_t out = (uint8_t)s[i];

    if (s[i] == '^' && i + 1 < (int)s.length()) {
      char c = s[++i];
      if (c == '?') out = 0x7f;
      else          out = ((uint8_t)c) & 0x1f;
    } else if (s[i] == '\\' && i + 1 < (int)s.length()) {
      char c = s[++i];
      switch (c) {
        case 'r': out = '\r'; break;
        case 'n': out = '\n'; break;
        case 't': out = '\t'; break;
        case 'b': out = '\b'; break;
        case 'f': out = '\f'; break;
        case 'e': out = 0x1b; break;
        case 's': out = ' ';  break;
        case '\\': out = '\\'; break;
        case '"': out = '"'; break;
        case '\'': out = '\''; break;
        case 'x': {
          int v = 0;
          int digits = 0;
          while (i + 1 < (int)s.length() && digits < 2) {
            int h = hex_value(s[i + 1]);
            if (h < 0) break;
            v = (v << 4) | h;
            i++;
            digits++;
          }
          out = (uint8_t)v;
          break;
        }
        default:
          if (c >= '0' && c <= '7') {
            int v = c - '0';
            int digits = 1;
            while (i + 1 < (int)s.length() && digits < 3 &&
                   s[i + 1] >= '0' && s[i + 1] <= '7') {
              v = (v << 3) | (s[i + 1] - '0');
              i++;
              digits++;
            }
            out = (uint8_t)v;
          } else {
            out = (uint8_t)c;
          }
          break;
      }
    }

    cfg.boot_input[cfg.boot_input_len++] = out;
  }
}

static String escaped_bytes(const uint8_t* bytes, size_t len) {
  String out;
  char tmp[6];
  for (size_t i = 0; i < len; i++) {
    uint8_t c = bytes[i];
    switch (c) {
      case '\r': out += "\\r"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case 0x1b: out += "\\e"; break;
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      default:
        if (c >= 32 && c < 127) out += (char)c;
        else {
          snprintf(tmp, sizeof(tmp), "\\x%02X", c);
          out += tmp;
        }
        break;
    }
  }
  return out;
}

// -------- SD --------

bool sd_mount() {
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0, SD_MMC_D1, SD_MMC_D2, SD_MMC_D3);
  if (!SD_MMC.begin("/sdcard", false /*1bit*/, false /*format*/, 20000 /*freq*/)) {
    LOGE("SD_MMC.begin() failed");
    return false;
  }
  uint8_t type = SD_MMC.cardType();
  if (type == CARD_NONE) {
    LOGE("No SD card detected");
    return false;
  }
  const char* tname = (type == CARD_MMC)  ? "MMC"
                    : (type == CARD_SD)   ? "SDSC"
                    : (type == CARD_SDHC) ? "SDHC"
                                          : "?";
  uint64_t mb = SD_MMC.cardSize() / (1024ULL * 1024ULL);
  LOG("SD mounted: type=%s size=%llu MB", tname, (unsigned long long)mb);
  return true;
}

// -------- defaults --------

void config_apply_compiled_defaults(AppConfig& cfg) {
  cfg.title         = APP_TITLE;

  cfg.wifi_ssid     = WIFI_SSID;
  cfg.wifi_password = WIFI_PASS;
  cfg.wifi_hostname = WIFI_HOSTNAME;

  cfg.telnet_enabled = true;
  cfg.telnet_port    = TELNET_PORT;

  cfg.ftp_enabled  = true;
  cfg.ftp_port     = FTP_PORT;
  cfg.ftp_user     = FTP_DEFAULT_USER;
  cfg.ftp_password = FTP_DEFAULT_PASS;

  cfg.boot_input_len = 0;
  cfg.terminal       = "vt100";

  cfg.disk_a        = DEFAULT_A_IMG;
  cfg.disk_b        = "";
  cfg.disk_c        = DEFAULT_C_IMG;
  cfg.disk_d        = "";
  cfg.boot_drive    = 'a';
}

// -------- parser --------

enum ConfigDomain : uint8_t { CONFIG_NETWORK, CONFIG_EMULATOR };

static void parse_line(AppConfig& cfg, String& section, const String& raw,
                       ConfigDomain domain) {
  String t = trim(raw);
  if (t.length() == 0) return;
  if (t.startsWith(";") || t.startsWith("#")) return;
  if (t.startsWith("[") && t.endsWith("]")) {
    section = to_lower(t.substring(1, t.length() - 1));
    return;
  }
  int eq = t.indexOf('=');
  if (eq < 0) return;

  String key = to_lower(trim(t.substring(0, eq)));
  String val = strip_inline_comment(t.substring(eq + 1));

  bool network_section = section == "wifi" || section == "telnet" || section == "ftp";
  if ((domain == CONFIG_NETWORK) != network_section) return;

  if (section == "system") {
    if (key == "title") cfg.title = val;
  } else if (section == "wifi") {
    if      (key == "ssid")     cfg.wifi_ssid     = val;
    else if (key == "password") cfg.wifi_password = val;
    else if (key == "hostname") cfg.wifi_hostname = val;
  } else if (section == "telnet") {
    if      (key == "enabled")  cfg.telnet_enabled = truthy(val);
    else if (key == "port")     cfg.telnet_port    = val.toInt();
  } else if (section == "ftp") {
    if      (key == "enabled")  cfg.ftp_enabled  = truthy(val);
    else if (key == "port")     cfg.ftp_port     = val.toInt();
    else if (key == "user")     cfg.ftp_user     = val;
    else if (key == "password") cfg.ftp_password = val;
  } else if (section == "console") {
    if      (key == "terminal") cfg.terminal = to_lower(unquote_config_value(val));
    else if (key == "boot_text" || key == "boot_input" ||
             key == "typeahead" || key == "boot_keys")
      config_set_boot_input(cfg, val);
  } else if (section == "disks") {
    if      (key == "a")        cfg.disk_a = val;
    else if (key == "b")        cfg.disk_b = val;
    else if (key == "c")        cfg.disk_c = val;
    else if (key == "d")        cfg.disk_d = val;
    else if (key == "boot")     cfg.boot_drive = val.length() ? (char)tolower((uint8_t)val[0]) : 'a';
  }
}

static void recover_config_backup(const char* path) {
  if (SD_MMC.exists(path)) return;
  char backup[192];
  if (snprintf(backup, sizeof(backup), "%s.bak", path) >= (int)sizeof(backup)) return;
  if (SD_MMC.exists(backup)) {
    if (SD_MMC.rename(backup, path))
      LOG("Restored interrupted config update: %s", path);
    else
      LOGE("Could not restore config backup %s", backup);
  }
}

static bool parse_config_file(AppConfig& cfg, const char* path, ConfigDomain domain) {
  SD_FTP_StorageGuard guard;
  recover_config_backup(path);
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  String section;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    parse_line(cfg, section, line, domain);
  }
  f.close();
  return true;
}

bool config_load_wifi(AppConfig& cfg) {
  cfg.wifi_ssid     = "";
  cfg.wifi_password = "";
  cfg.wifi_hostname = "";

  bool existed = parse_config_file(cfg, WIFI_CFG_PATH, CONFIG_NETWORK);
  if (!existed) {
    LOG("%s not found, writing defaults", WIFI_CFG_PATH);
    cfg.wifi_ssid     = WIFI_SSID;
    cfg.wifi_password = WIFI_PASS;
    cfg.wifi_hostname = WIFI_HOSTNAME;
    config_write_default_wifi(cfg);
    return false;
  }

  if (cfg.wifi_ssid.length() == 0)     cfg.wifi_ssid     = WIFI_SSID;
  if (cfg.wifi_password.length() == 0) cfg.wifi_password = WIFI_PASS;
  if (cfg.wifi_hostname.length() == 0) cfg.wifi_hostname = WIFI_HOSTNAME;
  if (cfg.telnet_port <= 0)            cfg.telnet_port    = TELNET_PORT;
  if (cfg.ftp_port <= 0)               cfg.ftp_port       = FTP_PORT;
  if (cfg.ftp_user.length() == 0)      cfg.ftp_user       = FTP_DEFAULT_USER;
  if (cfg.ftp_password.length() == 0)  cfg.ftp_password   = FTP_DEFAULT_PASS;
  return true;
}

bool config_load_vz80(AppConfig& cfg) {
  cfg.title  = APP_TITLE;
  cfg.disk_a = "";
  cfg.disk_b = "";
  cfg.disk_c = "";
  cfg.disk_d = "";
  cfg.boot_input_len = 0;
  cfg.terminal = "vt100";

  bool existed = parse_config_file(cfg, VZ80_CFG_PATH, CONFIG_EMULATOR);
  if (!existed) {
    LOG("%s not found, writing defaults", VZ80_CFG_PATH);
    cfg.title      = APP_TITLE;
    cfg.disk_a     = DEFAULT_A_IMG;
    cfg.disk_b     = "";
    cfg.disk_c     = DEFAULT_C_IMG;
    cfg.disk_d     = "";
    cfg.terminal   = "vt100";
    cfg.boot_drive = 'a';
    config_write_default_vz80(cfg);
    return false;
  }
  if (cfg.title.length() == 0) cfg.title = APP_TITLE;
  if (!(cfg.terminal.equalsIgnoreCase("adm3a") ||
        cfg.terminal.equalsIgnoreCase("vt100"))) {
    LOGE("[console] unknown terminal=\"%s\" - using vt100", cfg.terminal.c_str());
    cfg.terminal = "vt100";
  }
  return true;
}

bool config_write_default_wifi(const AppConfig& cfg) {
  SD_FTP_StorageGuard guard;
  File f = SD_MMC.open(WIFI_CFG_PATH, FILE_WRITE);
  if (!f) { LOGE("Could not open %s for write", WIFI_CFG_PATH); return false; }
  f.println("; vZ80 network configuration");
  f.println("; Copy this to wificonfig-NAME.ini to create a named variant");
  f.println("; (then pick it from the Settings -> WiFi Config menu).");
  f.println();
  f.println("[wifi]");
  f.println("; Leave ssid/password blank to use the values compiled into secrets.h.");
  f.println("ssid     = ");
  f.println("password = ");
  f.printf ("hostname = %s\r\n", cfg.wifi_hostname.c_str());
  f.println();
  f.println("[telnet]");
  f.println("; Raw guest console over Telnet.");
  f.printf ("enabled = %s\r\n", cfg.telnet_enabled ? "true" : "false");
  f.printf ("port    = %d\r\n", cfg.telnet_port);
  f.println();
  f.println("[ftp]");
  f.println("; Passive-mode FTP server exposing the SD card root.");
  f.println("; port = control port. Passive data port = port+1.");
  f.printf ("enabled  = %s\r\n", cfg.ftp_enabled ? "true" : "false");
  f.printf ("port     = %d\r\n", cfg.ftp_port);
  f.printf ("user     = %s\r\n", cfg.ftp_user.c_str());
  f.printf ("password = %s\r\n", cfg.ftp_password.c_str());
  f.close();
  LOG("Wrote default %s", WIFI_CFG_PATH);
  return true;
}

bool config_write_default_vz80(const AppConfig& cfg) {
  SD_FTP_StorageGuard guard;
  File f = SD_MMC.open(VZ80_CFG_PATH, FILE_WRITE);
  if (!f) { LOGE("Could not open %s for write", VZ80_CFG_PATH); return false; }
  f.println("; vZ80 emulator configuration");
  f.println("; Copy this to z80config-NAME.ini to create a named variant");
  f.println("; (then pick it from the Settings -> vZ80 Config menu).");
  f.println();
  f.println("[system]");
  f.println("; title is the selected emulator profile name shown in the UI.");
  f.printf("title = %s\r\n", cfg.title.c_str());
  f.println();
  f.println("[console]");
  f.println("; terminal selects the TFT console escape parser: vt100 or adm3a.");
  f.printf("terminal = %s\r\n", cfg.terminal.length() ? cfg.terminal.c_str() : "vt100");
  f.println("; boot_text is injected into the console input queue after each");
  f.println("; Z80 boot/reset. Escapes: \\r \\n \\t \\e \\xHH \\ooo ^C ^[ ^?.");
  f.printf("boot_text = \"%s\"\r\n", escaped_bytes(cfg.boot_input, cfg.boot_input_len).c_str());
  f.println();
  f.println("[disks]");
  f.println("; a, b, c, d = standard CP/M floppy disk images.");
  f.println("; Hard-disk images are not supported in this version.");
  f.println("; Leave a slot blank to dismount it at boot.");
  f.printf("a    = %s\r\n", cfg.disk_a.c_str());
  f.printf("b    = %s\r\n", cfg.disk_b.c_str());
  f.printf("c    = %s\r\n", cfg.disk_c.c_str());
  f.printf("d    = %s\r\n", cfg.disk_d.c_str());
  f.printf("boot = %c\r\n", cfg.boot_drive);
  f.close();
  LOG("Wrote default %s", VZ80_CFG_PATH);
  return true;
}

bool config_copy_file(const char* src, const char* dst) {
  SD_FTP_StorageGuard guard;
  char temp[192];
  char backup[192];
  if (snprintf(temp, sizeof(temp), "%s.tmp", dst) >= (int)sizeof(temp) ||
      snprintf(backup, sizeof(backup), "%s.bak", dst) >= (int)sizeof(backup)) {
    LOGE("config_copy_file: destination path too long: %s", dst);
    return false;
  }

  File s = SD_MMC.open(src, FILE_READ);
  if (!s) { LOGE("config_copy_file: can't open %s", src); return false; }
  uint32_t srcSize = (uint32_t)s.size();

  if (SD_MMC.exists(temp)) SD_MMC.remove(temp);
  File d = SD_MMC.open(temp, FILE_WRITE);
  if (!d) { LOGE("config_copy_file: can't open %s for write", temp); s.close(); return false; }

  uint8_t buf[512];
  size_t total = 0;
  bool copy_ok = true;
  while (s.available()) {
    int n = s.read(buf, sizeof(buf));
    if (n <= 0) { copy_ok = false; break; }
    int w = d.write(buf, n);
    if (w != n) {
      LOGE("config_copy_file: short write (%d/%d) at %u", w, n, (unsigned)total);
      copy_ok = false;
      break;
    }
    total += n;
  }
  s.close();
  d.flush();
  d.close();

  File v = SD_MMC.open(temp, FILE_READ);
  uint32_t verifySize = v ? (uint32_t)v.size() : 0;
  if (v) v.close();
  if (!copy_ok || total != srcSize || verifySize != srcSize) {
    LOGE("config_copy_file: temporary copy failed src=%u written=%u on-disk=%u",
         (unsigned)srcSize, (unsigned)total, (unsigned)verifySize);
    SD_MMC.remove(temp);
    return false;
  }

  if (SD_MMC.exists(backup)) SD_MMC.remove(backup);
  bool had_dst = SD_MMC.exists(dst);
  if (had_dst && !SD_MMC.rename(dst, backup)) {
    LOGE("config_copy_file: can't preserve %s as %s", dst, backup);
    SD_MMC.remove(temp);
    return false;
  }
  if (!SD_MMC.rename(temp, dst)) {
    LOGE("config_copy_file: can't activate %s", dst);
    if (had_dst && !SD_MMC.rename(backup, dst))
      LOGE("config_copy_file: FAILED to restore %s from %s", dst, backup);
    SD_MMC.remove(temp);
    return false;
  }
  if (had_dst) SD_MMC.remove(backup);
  LOG("config_copy_file: atomically replaced %s from %s (%u bytes)",
      dst, src, (unsigned)srcSize);
  return true;
}

int config_list_variants(const char* prefix, char names[][44], int max) {
  SD_FTP_StorageGuard guard;
  if (max <= 0) return 0;
  int count = 0;

  fs::File root = SD_MMC.open("/");
  if (!root) return 0;

  size_t plen = strlen(prefix);
  for (fs::File f = root.openNextFile(); f && count < max; f = root.openNextFile()) {
    if (!f.isDirectory()) {
      const char* fullname = f.name();
      const char* slash = strrchr(fullname, '/');
      const char* base  = slash ? slash + 1 : fullname;
      size_t blen = strlen(base);

      if (strncmp(base, prefix, plen) == 0 &&
          blen > plen + 4 &&
          strcasecmp(base + blen - 4, ".ini") == 0) {
        size_t midlen = blen - plen - 4;
        if (midlen > 0 && midlen < 43) {
          memcpy(names[count], base + plen, midlen);
          names[count][midlen] = 0;
          count++;
        }
      }
    }
    f.close();
  }
  root.close();
  return count;
}

// -------- disk image creation --------

bool ensure_disk_image(const char* path, uint32_t bytes,
                       bool create_if_missing, const char* label) {
  SD_FTP_StorageGuard guard;
  if (!path || !*path) return false;
  if (SD_MMC.exists(path)) {
    File f = SD_MMC.open(path, FILE_READ);
    if (!f) {
      LOGE("[%s] exists but cannot open %s", label, path);
      return false;
    }
    uint32_t sz = (uint32_t)f.size();
    f.close();
    if (sz == bytes) {
      LOG("[%s] %s OK (%u bytes)", label, path, (unsigned)sz);
      return true;
    }
    LOGE("[%s] %s wrong size: %u (expected %u) - leaving file alone",
         label, path, (unsigned)sz, (unsigned)bytes);
    return false;
  }
  if (!create_if_missing) {
    LOG("[%s] %s missing (not auto-created)", label, path);
    return false;
  }
  LOG("[%s] creating zeroed %s (%u bytes) - this can take a while ...",
      label, path, (unsigned)bytes);
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) {
    LOGE("[%s] could not create %s", label, path);
    return false;
  }
  static uint8_t buf[4096] = {0};
  uint32_t remaining = bytes;
  uint32_t t0 = millis();
  uint32_t lastReport = t0;
  while (remaining) {
    size_t n = remaining > sizeof(buf) ? sizeof(buf) : remaining;
    size_t w = f.write(buf, n);
    if (w != n) {
      LOGE("[%s] write failed at offset %u", label, (unsigned)(bytes - remaining));
      f.close();
      return false;
    }
    remaining -= n;
    if (millis() - lastReport >= 2000) {
      uint32_t done = bytes - remaining;
      LOG("[%s]  ... %u / %u bytes (%u%%)",
          label, (unsigned)done, (unsigned)bytes, (unsigned)(done * 100ULL / bytes));
      lastReport = millis();
    }
  }
  f.close();
  LOG("[%s] created %s in %u ms", label, path, (unsigned)(millis() - t0));
  return true;
}

// -------- printer --------

void config_print(const AppConfig& cfg) {
  LOG("---- /wificonfig.ini + /z80config.ini effective values ----");
  LOG("[system]  title=\"%s\"", cfg.title.c_str());
  LOG("[wifi]    ssid=\"%s\" hostname=\"%s\" (password=%d chars)",
      cfg.wifi_ssid.c_str(), cfg.wifi_hostname.c_str(),
      (int)cfg.wifi_password.length());
  LOG("[telnet]  enabled=%s port=%d",
      cfg.telnet_enabled ? "true" : "false", cfg.telnet_port);
  LOG("[ftp]     enabled=%s port=%d user=\"%s\" (password=%d chars)",
      cfg.ftp_enabled ? "true" : "false", cfg.ftp_port,
      cfg.ftp_user.c_str(), (int)cfg.ftp_password.length());
  LOG("[console] terminal=%s boot_text=\"%s\" (%u bytes)",
      cfg.terminal.c_str(),
      escaped_bytes(cfg.boot_input, cfg.boot_input_len).c_str(),
      (unsigned)cfg.boot_input_len);
  LOG("[disks]   a=\"%s\"  b=\"%s\"",
      cfg.disk_a.c_str(), cfg.disk_b.c_str());
  LOG("          c=\"%s\"  d=\"%s\"  boot=%c",
      cfg.disk_c.c_str(), cfg.disk_d.c_str(), cfg.boot_drive);
  LOG("--------------------------------------");
}
