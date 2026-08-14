#include "telnet_shell.h"

#include "SD_FTP_Server/src/SD_FTP_Server.h"
#include "appconfig.h"
#include "config.h"
#include "fifo.h"
#include "host_lib/shell/shell_core.h"
#include "host_lib/shell/shell_media.h"
#include "platform.h"
#include "sd_fs.h"
#include "telnet.h"
#include "vz80_host.h"

#include <Arduino.h>
#include "esp_attr.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef EXT_RAM_BSS_ATTR
#define EXT_RAM_BSS_ATTR
#endif

static constexpr size_t SHELL_LINE_MAX = 255;
static constexpr size_t SHELL_QUEUE_DEPTH = 4;
static constexpr size_t SHELL_OUTPUT_BYTES = 8192;
static constexpr size_t SHELL_PATH_MAX = 128;
static constexpr uint32_t FLOPPY_BYTES = CPM_FLOPPY_BYTES;
static constexpr uint32_t HDD_BYTES    = CPM_HDD_BYTES;
static constexpr uint8_t  DISK_FILL    = 0xE5;  // CP/M 2.2 unused dir / FORMAT

static volatile bool g_active = false;
static char g_input_line[SHELL_LINE_MAX + 1];
static size_t g_input_len = 0;
static char g_commands[SHELL_QUEUE_DEPTH][SHELL_LINE_MAX + 1];
static volatile uint8_t g_command_head = 0;
static volatile uint8_t g_command_tail = 0;
EXT_RAM_BSS_ATTR static uint8_t g_output_storage[SHELL_OUTPUT_BYTES];
EXT_RAM_BSS_ATTR static uint8_t g_file_buffer[4096];
static Fifo g_output;
static bool g_initialized = false;
static char g_cwd[SHELL_PATH_MAX] = "/";

static void output_char(uint8_t value) {
  g_output.push(value);
  if (value == 255) g_output.push(value);
}

static bool output_char_wait(uint8_t value) {
  uint32_t started = millis();
  while (!g_output.push(value)) {
    if (!g_active || millis() - started >= 2000) return false;
    delay(1);
  }
  if (value == 255) {
    started = millis();
    while (!g_output.push(value)) {
      if (!g_active || millis() - started >= 2000) return false;
      delay(1);
    }
  }
  return true;
}

static void output_text(const char* text) {
  if (!text) return;
  while (*text) output_char((uint8_t)*text++);
}

static void output_printf(const char* format, ...) {
  char buffer[384];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  output_text(buffer);
}

static void prompt() {
  output_printf("vz80:%s> ", g_cwd);
}

static bool queue_command(const char* command) {
  uint8_t next = (uint8_t)((g_command_head + 1) % SHELL_QUEUE_DEPTH);
  if (next == g_command_tail) return false;
  strncpy(g_commands[g_command_head], command, SHELL_LINE_MAX);
  g_commands[g_command_head][SHELL_LINE_MAX] = 0;
  g_command_head = next;
  return true;
}

static bool pop_command(char* command, size_t size) {
  if (g_command_head == g_command_tail) return false;
  strncpy(command, g_commands[g_command_tail], size - 1);
  command[size - 1] = 0;
  g_command_tail = (uint8_t)((g_command_tail + 1) % SHELL_QUEUE_DEPTH);
  return true;
}

static void register_vz80_shell();

void telnet_shell_init() {
  if (g_initialized) return;
  g_output.init(g_output_storage, SHELL_OUTPUT_BYTES);
  shell_set_out(output_text);
  register_vz80_shell();
  g_initialized = true;
}

void telnet_shell_enter() {
  telnet_shell_init();
  g_input_len = 0;
  g_input_line[0] = 0;
  g_command_tail = g_command_head;
  g_output.clear();
  strcpy(g_cwd, "/");
  g_active = true;
  LOG("telnet shell: entered by %s", telnet_client_ip());
}

void telnet_shell_disconnect() {
  g_active = false;
  g_input_len = 0;
  g_command_tail = g_command_head;
  if (g_initialized) g_output.clear();
}

bool telnet_shell_active() { return g_active; }

bool telnet_shell_backspace() {
  if (!g_active || g_input_len == 0) return false;
  g_input_line[--g_input_len] = 0;
  return true;
}

bool telnet_shell_input(uint8_t c) {
  if (!g_active) return false;
  if (c == '\r' || c == '\n') {
    g_input_line[g_input_len] = 0;
    if (!queue_command(g_input_line))
      LOGE("telnet shell: command queue full");
    g_input_len = 0;
    g_input_line[0] = 0;
    return false;
  }
  if (c < 0x20 || c > 0x7e || g_input_len >= SHELL_LINE_MAX) return false;
  g_input_line[g_input_len++] = (char)c;
  g_input_line[g_input_len] = 0;
  return true;
}

size_t telnet_shell_output_peek(const uint8_t** data) {
  if (!g_initialized) {
    *data = nullptr;
    return 0;
  }
  return g_output.peek(data);
}

void telnet_shell_output_consume(size_t bytes) {
  g_output.consume(bytes);
}

static bool mounted_path(const char* path) {
  return z80_ftp_path_protected(path);
}

static bool normalize_path(const char* input, char* output, size_t size) {
  if (!input || !*input || !output || size < 2) return false;
  char combined[256];
  int written;
  if (input[0] == '/')
    written = snprintf(combined, sizeof(combined), "%s", input);
  else if (!strcmp(g_cwd, "/"))
    written = snprintf(combined, sizeof(combined), "/%s", input);
  else
    written = snprintf(combined, sizeof(combined), "%s/%s", g_cwd, input);
  if (written < 0 || (size_t)written >= sizeof(combined)) return false;

  char working[256];
  strncpy(working, combined, sizeof(working) - 1);
  working[sizeof(working) - 1] = 0;
  const char* parts[32];
  size_t count = 0;
  char* save = nullptr;
  for (char* part = strtok_r(working, "/", &save);
       part;
       part = strtok_r(nullptr, "/", &save)) {
    if (!strcmp(part, ".") || !*part) continue;
    if (!strcmp(part, "..")) {
      if (count) count--;
      continue;
    }
    if (strchr(part, '\\') || strchr(part, ':') || strchr(part, ';'))
      return false;
    if (count >= sizeof(parts) / sizeof(parts[0])) return false;
    parts[count++] = part;
  }

  size_t used = 0;
  output[used++] = '/';
  for (size_t i = 0; i < count; i++) {
    size_t length = strlen(parts[i]);
    if (used + length + (i + 1 < count ? 1 : 0) >= size) return false;
    memcpy(output + used, parts[i], length);
    used += length;
    if (i + 1 < count) output[used++] = '/';
  }
  output[used] = 0;
  return true;
}

static const char* basename_of(const char* path) {
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

static int split_words(char* line, char* words[], int maximum) {
  int count = 0;
  char* cursor = line;
  while (*cursor && count < maximum) {
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (!*cursor) break;
    char quote = 0;
    if (*cursor == '"' || *cursor == '\'') quote = *cursor++;
    words[count++] = cursor;
    if (quote) {
      while (*cursor && *cursor != quote) cursor++;
    } else {
      while (*cursor && *cursor != ' ' && *cursor != '\t') cursor++;
    }
    if (*cursor) *cursor++ = 0;
  }
  return count;
}

static char* trim_in_place(char* text) {
  while (*text == ' ' || *text == '\t') text++;
  char* end = text + strlen(text);
  while (end > text && (end[-1] == ' ' || end[-1] == '\t')) end--;
  *end = 0;
  return text;
}

static void command_ls(const char* argument) {
  char path[SHELL_PATH_MAX];
  if (!normalize_path(argument && *argument ? argument : g_cwd,
                      path, sizeof(path))) {
    output_text("error: invalid path\r\n");
    return;
  }
  SD_FTP_StorageGuard guard;
  File entry = SD_FS.open(path, "r");
  if (!entry) {
    output_printf("error: cannot open %s\r\n", path);
    return;
  }
  if (!entry.isDirectory()) {
    output_printf("%10lu  %s\r\n", (unsigned long)entry.size(),
                  basename_of(path));
    entry.close();
    return;
  }
  File child;
  while ((child = entry.openNextFile())) {
    const char* name = basename_of(child.name());
    if (child.isDirectory())
      output_printf("     <DIR>  %s/\r\n", name);
    else
      output_printf("%10lu  %s\r\n", (unsigned long)child.size(), name);
    child.close();
  }
  entry.close();
}

static void command_cat(const char* argument) {
  if (!argument) {
    output_text("usage: cat <path>\r\n");
    return;
  }
  char path[SHELL_PATH_MAX];
  if (!normalize_path(argument, path, sizeof(path))) {
    output_text("error: invalid path\r\n");
    return;
  }
  SD_FTP_StorageGuard guard;
  File file = SD_FS.open(path, "r");
  if (!file || file.isDirectory()) {
    output_printf("error: cannot read file: %s\r\n", path);
    if (file) file.close();
    return;
  }

  unsigned scan_lines = 0;
  bool scan_previous_cr = false;
  bool binary = false;
  while (file.available() && scan_lines < 100) {
    int value = file.read();
    if (value < 0) break;
    uint8_t ch = (uint8_t)value;
    if (ch == '\r') {
      scan_lines++;
      scan_previous_cr = true;
    } else if (ch == '\n') {
      if (!scan_previous_cr) scan_lines++;
      scan_previous_cr = false;
    } else {
      scan_previous_cr = false;
      if (ch != '\t' && (ch < 0x20 || ch > 0x7e)) {
        binary = true;
        break;
      }
    }
  }
  if (binary) {
    file.close();
    output_text("error: file is binary\r\n");
    return;
  }
  if (!file.seek(0)) {
    file.close();
    output_printf("error: cannot rewind file: %s\r\n", path);
    return;
  }

  unsigned lines = 0;
  bool previous_cr = false;
  bool output_ok = true;
  while (file.available() && lines < 100 && output_ok) {
    int value = file.read();
    if (value < 0) break;
    uint8_t ch = (uint8_t)value;
    if (ch == '\r') {
      output_ok = output_char_wait('\r') && output_char_wait('\n');
      lines++;
      previous_cr = true;
    } else if (ch == '\n') {
      if (!previous_cr) {
        output_ok = output_char_wait('\r') && output_char_wait('\n');
        lines++;
      }
      previous_cr = false;
    } else {
      previous_cr = false;
      output_ok = output_char_wait(ch);
    }
  }
  file.close();
  if (output_ok && lines >= 100)
    output_text("[output limited to 100 lines]\r\n");
}

static void command_cd(const char* argument) {
  if (!argument) {
    output_text("usage: cd <path>\r\n");
    return;
  }
  char path[SHELL_PATH_MAX];
  if (!normalize_path(argument, path, sizeof(path))) {
    output_text("error: invalid path\r\n");
    return;
  }
  SD_FTP_StorageGuard guard;
  File directory = SD_FS.open(path, "r");
  if (!directory || !directory.isDirectory()) {
    output_printf("error: not a directory: %s\r\n", path);
    if (directory) directory.close();
    return;
  }
  directory.close();
  strcpy(g_cwd, path);
}

static void command_rm(const char* argument) {
  if (!argument) {
    output_text("usage: rm <path>\r\n");
    return;
  }
  char path[SHELL_PATH_MAX];
  if (!normalize_path(argument, path, sizeof(path))) {
    output_text("error: invalid path\r\n");
    return;
  }
  if (mounted_path(path)) {
    output_text("error: file is mounted by the emulator\r\n");
    return;
  }
  SD_FTP_StorageGuard guard;
  File file = SD_FS.open(path, "r");
  if (!file) {
    output_printf("error: file not found: %s\r\n", path);
    return;
  }
  bool directory = file.isDirectory();
  file.close();
  if (directory) {
    output_text("error: rm removes files only\r\n");
    return;
  }
  output_printf(SD_FS.remove(path) ? "removed %s\r\n"
                                    : "error: remove failed: %s\r\n", path);
}

static void command_mv(const char* source_arg, const char* destination_arg) {
  char source[SHELL_PATH_MAX], destination[SHELL_PATH_MAX];
  if (!source_arg || !destination_arg ||
      !normalize_path(source_arg, source, sizeof(source)) ||
      !normalize_path(destination_arg, destination, sizeof(destination))) {
    output_text("usage: mv <source> <destination>\r\n");
    return;
  }
  if (mounted_path(source) || mounted_path(destination)) {
    output_text("error: source or destination is mounted\r\n");
    return;
  }
  SD_FTP_StorageGuard guard;
  if (!SD_FS.exists(source)) {
    output_printf("error: file not found: %s\r\n", source);
    return;
  }
  if (SD_FS.exists(destination)) {
    output_printf("error: destination exists: %s\r\n", destination);
    return;
  }
  output_printf(SD_FS.rename(source, destination) ? "moved %s -> %s\r\n"
                                                   : "error: move failed\r\n",
                source, destination);
}

static void command_cp(const char* source_arg, const char* destination_arg) {
  char source[SHELL_PATH_MAX], destination[SHELL_PATH_MAX];
  if (!source_arg || !destination_arg ||
      !normalize_path(source_arg, source, sizeof(source)) ||
      !normalize_path(destination_arg, destination, sizeof(destination))) {
    output_text("usage: cp <source> <destination>\r\n");
    return;
  }
  if (mounted_path(source) || mounted_path(destination)) {
    output_text("error: source or destination is mounted\r\n");
    return;
  }
  SD_FTP_StorageGuard guard;
  if (SD_FS.exists(destination)) {
    output_printf("error: destination exists: %s\r\n", destination);
    return;
  }
  File source_file = SD_FS.open(source, "r");
  if (!source_file || source_file.isDirectory()) {
    output_printf("error: cannot read file: %s\r\n", source);
    if (source_file) source_file.close();
    return;
  }
  File destination_file = SD_FS.open(destination, "w");
  if (!destination_file) {
    source_file.close();
    output_printf("error: cannot create: %s\r\n", destination);
    return;
  }
  bool ok = true;
  while (source_file.available()) {
    size_t count = source_file.read(g_file_buffer, sizeof(g_file_buffer));
    if (!count) break;
    if (destination_file.write(g_file_buffer, count) != count) {
      ok = false;
      break;
    }
  }
  destination_file.flush();
  destination_file.close();
  source_file.close();
  if (!ok) {
    SD_FS.remove(destination);
    output_text("error: copy failed; partial destination removed\r\n");
  } else {
    output_printf("copied %s -> %s\r\n", source, destination);
  }
}

static int drive_index(const char* unit) {
  if (!unit || !unit[0]) return -1;
  char c = (char)tolower((uint8_t)unit[0]);
  if (c >= 'a' && c <= 'd' && (unit[1] == 0 || unit[1] == ':'))
    return c - 'a';
  return -1;
}

static bool vz80_media_list(int index, MediaUnitInfo* out) {
  if (!out || index < 0 || index >= AltairBios::MAX_DRIVES) return false;
  memset(out, 0, sizeof(*out));
  snprintf(out->name, sizeof(out->name), "%c:", 'A' + index);
  DiskImage& img = disks[index];
  if (!img.isOpen()) return true;
  out->mounted = true;
  strncpy(out->path, img.path(), sizeof(out->path) - 1);
  out->size_bytes = (uint32_t)img.tracks() * img.sectorsPerTrack() * img.sectorBytes();
  const char* kind = (img.sectorsPerTrack() == CPM_HDD_SPT) ? "hdd" : "floppy";
  strncpy(out->extra, kind, sizeof(out->extra) - 1);
  return true;
}

static const char* vz80_mount_usage() {
  return "usage: mount <A|B|C|D> <path> [ro]\r\n";
}

static const char* vz80_create_usage() {
  return "usage: create floppy|hdd <path>\r\n";
}

static bool vz80_media_mount(const char* unit, const char* path, bool readonly,
                             char* err, size_t errlen) {
  int d = drive_index(unit);
  if (d < 0) {
    snprintf(err, errlen, "%s", vz80_mount_usage());
    return false;
  }
  if (!path || !*path) {
    snprintf(err, errlen, "missing image path");
    return false;
  }
  if (!vz80_mount_drive((uint8_t)d, path, !readonly)) {
    snprintf(err, errlen, "mount failed");
    return false;
  }
  err[0] = 0;
  return true;
}

static bool vz80_media_dismount(const char* unit, char* err, size_t errlen) {
  int d = drive_index(unit);
  if (d < 0) {
    snprintf(err, errlen, "%s", vz80_mount_usage());
    return false;
  }
  if (!vz80_mount_drive((uint8_t)d, "", true)) {
    snprintf(err, errlen, "dismount failed");
    return false;
  }
  err[0] = 0;
  return true;
}

static bool vz80_media_create(const char* type, const char* path,
                              char* err, size_t errlen) {
  const bool is_floppy = type && strcasecmp(type, "floppy") == 0;
  const bool is_hdd    = type && strcasecmp(type, "hdd") == 0;
  if (!is_floppy && !is_hdd) {
    snprintf(err, errlen, "%s", vz80_create_usage());
    return false;
  }
  char full[SHELL_PATH_MAX];
  if (!normalize_path(path, full, sizeof(full))) {
    snprintf(err, errlen, "invalid path");
    return false;
  }
  const uint32_t bytes = is_hdd ? HDD_BYTES : FLOPPY_BYTES;
  const char* label = is_hdd ? "hdd" : "floppy";
  if (!ensure_disk_image(full, bytes, true, label, DISK_FILL)) {
    snprintf(err, errlen, "create failed");
    return false;
  }
  snprintf(err, errlen,
           "created %s (%lu bytes, CP/M 2.2 %s, fill 0xE5)\r\n",
           full, (unsigned long)bytes, label);
  return true;
}

static bool vz80_path_protected(const char* path) {
  return mounted_path(path);
}

static bool vz80_restart(char* err, size_t errlen) {
  (void)errlen;
  vz80_request_guest_restart();
  if (err) err[0] = 0;
  return true;
}

static const char* vz80_restart_help() {
  return "remount disks and cold-boot CP/M";
}

static void execute_command(char* line) {
  char* command_start = trim_in_place(line);
  char* words[8];
  int count = split_words(command_start, words, 8);
  if (count == 0) {
    prompt();
    return;
  }
  if (!shell_dispatch(count, words))
    output_printf("unknown command: %s (type help)\r\n", words[0]);
  if (g_active) prompt();
}

static void cmd_help(int, char**) { shell_print_help(); }
static void cmd_pwd(int, char**) { output_printf("%s\r\n", g_cwd); }
static void cmd_cd(int argc, char** argv) {
  command_cd(argc > 1 ? argv[1] : nullptr);
}
static void cmd_ls(int argc, char** argv) {
  command_ls(argc > 1 ? argv[1] : nullptr);
}
static void cmd_cat(int argc, char** argv) {
  command_cat(argc > 1 ? argv[1] : nullptr);
}
static void cmd_rm(int argc, char** argv) {
  command_rm(argc > 1 ? argv[1] : nullptr);
}
static void cmd_mv(int argc, char** argv) {
  command_mv(argc > 1 ? argv[1] : nullptr, argc > 2 ? argv[2] : nullptr);
}
static void cmd_cp(int argc, char** argv) {
  command_cp(argc > 1 ? argv[1] : nullptr, argc > 2 ? argv[2] : nullptr);
}
static void cmd_exit(int, char**) {
  output_text("Returning Telnet to the CP/M console.\r\n");
  g_active = false;
  LOG("telnet shell: returned to CP/M console");
}

static void register_vz80_shell() {
  static const char* help_aliases[] = { "?", nullptr };

  shell_register("help", cmd_help,
                 "help                        show this list",
                 help_aliases, "File commands");
  shell_register("pwd", cmd_pwd,
                 "pwd                         show current SD directory",
                 nullptr, "File commands");
  shell_register("cd", cmd_cd,
                 "cd <path>                   change current directory",
                 nullptr, "File commands");
  shell_register("ls", cmd_ls,
                 "ls [path]                   list a file or directory",
                 nullptr, "File commands");
  shell_register("cat", cmd_cat,
                 "cat <path>                  display the first 100 lines",
                 nullptr, "File commands");
  shell_register("rm", cmd_rm,
                 "rm <path>                   remove a file",
                 nullptr, "File commands");
  shell_register("mv", cmd_mv,
                 "mv <source> <destination>   rename or move a file",
                 nullptr, "File commands");
  shell_register("cp", cmd_cp,
                 "cp <source> <destination>   copy a file",
                 nullptr, "File commands");

  static MediaOps media = {
      vz80_media_list,
      vz80_media_mount,
      vz80_media_dismount,
      vz80_media_create,
      vz80_path_protected,
      vz80_mount_usage,
      vz80_create_usage,
  };
  shell_set_media_ops(&media);
  shell_register_media_commands();

  static GuestControlOps guest = { vz80_restart, vz80_restart_help };
  shell_set_guest_control_ops(&guest);
  shell_register_guest_control_commands();

  shell_register("exit", cmd_exit,
                 "exit                        reconnect Telnet to the CP/M console",
                 nullptr, "Emulator commands");
}

void telnet_shell_poll() {
  if (!g_initialized) return;
  char command[SHELL_LINE_MAX + 1];
  while (g_active && pop_command(command, sizeof(command)))
    execute_command(command);
}
