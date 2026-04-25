#pragma once
#include <Arduino.h>

// Wraps SimpleFTPServer (Renzo Mischianti, lib name "SimpleFTPServer") so
// the rest of the sketch doesn't pull its headers. Library must be added
// via Arduino Library Manager — search "SimpleFTPServer".
//
// SD must already be initialized before begin().
//
// Set ENABLE_FTP to 0 to compile the FTP server out entirely. The Ftp::*
// API stays linkable but the calls become inert no-ops; SimpleFTPServer
// is not pulled in, saving ~10 KB flash and ~4 KB RAM.

#ifndef ENABLE_FTP
#define ENABLE_FTP 0
#endif

namespace Ftp {

bool begin(const char* user, const char* pass);
void stop();
bool running();

}  // namespace Ftp
