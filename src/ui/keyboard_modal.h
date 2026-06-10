#pragma once
#include <stdint.h>

class TFT_eSPI;

// On-screen US-QWERTY keyboard adapted from the donor vZ80 sketch for the
// Freenove/TFT_eSPI display stack. It overlays the bottom 144 px.
class KeyboardModal {
public:
  enum class Result : uint8_t {
    NONE,
    KEY,
    CLOSE
  };

  void open();
  void close();
  bool isOpen() const { return open_; }

  void draw(TFT_eSPI& tft);
  Result handleTap(int x, int y, uint8_t* out);

  static constexpr int KBD_Y0 = 96;
  static constexpr int KBD_H  = 144;

private:
  bool open_ = false;
  bool shift_ = false;
  bool ctrl_ = false;
  bool dirty_ = true;

  int hitTest(int x, int y) const;
  uint8_t resolveKey(int idx) const;
};
