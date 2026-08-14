#pragma once

#include <stddef.h>
#include <stdint.h>

void telnet_shell_init();
void telnet_shell_enter();
void telnet_shell_disconnect();
bool telnet_shell_active();

bool telnet_shell_input(uint8_t c);
bool telnet_shell_backspace();

// Call from the Arduino loop (core 1). SD and emulator mutations happen here.
void telnet_shell_poll();

size_t telnet_shell_output_peek(const uint8_t** data);
void telnet_shell_output_consume(size_t bytes);
