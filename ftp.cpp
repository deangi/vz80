#include "ftp.h"
#include "platform.h"
#include "SD_FTP_Server/src/SD_FTP_Server.h"

// The app owns a project-wide "enabled" gate for FTP. The library itself
// doesn't model that
// gate (callers just don't begin() if they don't want it running), so we
// remember the flag here for the status-pill query.
static bool s_enabled = false;

static void ftp_log_info(const char* m) { LOG ("%s", m); }
static void ftp_log_err (const char* m) { LOGE("%s", m); }
extern bool z80_ftp_path_protected(const char* path);

static bool ftp_path_protected(const char* path) {
  return z80_ftp_path_protected(path);
}

void ftp_begin(uint16_t port, bool enabled, const char* user, const char* pass) {
  s_enabled = enabled;
  if (!enabled) { LOG("ftp: disabled in config"); return; }
  SD_FTP_Server::Config cfg;
  cfg.port       = port;
  cfg.user       = user ? user : "";
  cfg.pass       = pass ? pass : "";
  cfg.vfs_root   = "/sdcard";
  cfg.log_fn     = ftp_log_info;
  cfg.log_err_fn = ftp_log_err;
  cfg.path_protected_fn = ftp_path_protected;
  SDFTPServer.begin(cfg);
}

void     ftp_poll()      { if (s_enabled) SDFTPServer.poll(); }
bool     ftp_enabled()   { return s_enabled; }
bool     ftp_listening() { return s_enabled && SDFTPServer.listening(); }
bool     ftp_connected() { return s_enabled && SDFTPServer.connected(); }
uint16_t ftp_port()      { return SDFTPServer.port(); }
