#pragma once

#include <Arduino.h>
#include "RgbLed.h"

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

  RgbLed::RgbLed(int redpin, int greenpin, int bluepin, int offis1 = 0)
  {
    _redpin = redpin;
    _greenpin = greenpin;
    _bluepin = bluepin;
    _state = 0;
    _offis1 = offis1; // set to 0 if led operates such that HIGH=on, set to 1 if LOW=on
    Init();
  }

  void RgbLed::Init()
  {
    pinMode(_redpin,OUTPUT); // HIGH = off
    digitalWrite(_redpin,HIGH);
    pinMode(_greenpin,OUTPUT);
    digitalWrite(_greenpin,HIGH);
    pinMode(_bluepin,OUTPUT);
    digitalWrite(_bluepin,HIGH);
    _state = 0;
  }

  void RgbLed::Set(int bitmask)
  {
    _state = bitmask & 7;
    digitalWrite(_redpin   ,((bitmask>>2)&1) ^ _offis1);
    digitalWrite(_greenpin ,((bitmask>>1)&1) ^ _offis1);
    digitalWrite(_bluepin  ,(bitmask&1)      ^ _offis1);
  }

  int RgbLed::Get() { return _state; } // return bit mask of last setting


