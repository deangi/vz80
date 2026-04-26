#pragma once

#include <Arduino.h>
//===================================================================
// Arduino / ESP32 Yellow Board - RGB LED controller
//
// Dean Gienger, March 19, 2026
//
//  3 bit LED color control, 1=on, 0=off - 3 lsb
//  bit2=Red, bit1=Green, bit0=Blue    100=red, 010=green 001=blue
//  000 = black
//  001 = blue
//  010 = green
//  011 = blue+green = cyan
//  100 = red
//  101 = red+blue = purple
//  110 = red+green = yellow
//  111 = white (r+g+b)
//===================================================================

// define bit masks
#define RGBCOLOR_RED   0b001
#define RGBCOLOR_GREEN 0b010
#define RGBCOLOR_BLUE  0b100

class RgbLed {
public:
  RgbLed(int redpin, int greenpin, int bluepin, int offis1);
  ~RgbLed() {}
  void Init();
  void Set(int bitmask); // bit2=Red, bit1=Green, bit0=Blue
  int Get(); // return bit mask

private:
  int _redpin;
  int _greenpin;
  int _bluepin;
  int _state;
  int _offis1;
};