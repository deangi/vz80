#pragma once
#include <Arduino.h>

#define LOG(fmt, ...)   do { Serial.printf("[vZ80] " fmt "\r\n", ##__VA_ARGS__); } while (0)
#define LOGE(fmt, ...)  do { Serial.printf("[vZ80 ERR] " fmt "\r\n", ##__VA_ARGS__); } while (0)
