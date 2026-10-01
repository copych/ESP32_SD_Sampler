#pragma once
#include <stdint.h>
#include <string.h>
#include "font_mo.h"

// Portable input and OLED primitives, also exercised by host tests.
namespace sampler_gui {
class Button {
public:
    enum Event { NONE, CLICK, HOLD };
    Event poll(bool down, uint32_t now) {
        if (down != raw) { raw = down; changed = now; }
        if (raw != stable && uint32_t(now - changed) >= 20) {
            stable = raw;
            if (stable) { pressed = now; held = false; }
            else if (!held) return CLICK;
        }
        if (stable && !held && uint32_t(now - pressed) >= 700) {
            held = true; return HOLD;
        }
        return NONE;
    }
private:
    bool raw = false, stable = false, held = false;
    uint32_t changed = 0, pressed = 0;
};

class Encoder {
public:
    void begin(uint8_t pins) { previous = pins & 3; sum = 0; }
    int poll(uint8_t pins) {
        static const int8_t delta[16] = {0,1,-1,0,-1,0,0,1,1,0,0,-1,0,-1,1,0};
        pins &= 3;
        if ((pins ^ previous) == 3) sum = 0; // missed/invalid quadrature edge
        else sum += delta[(previous << 2) | pins];
        previous = pins;
        if (sum >= 4) { sum = 0; return 1; }
        if (sum <= -4) { sum = 0; return -1; }
        return 0;
    }
private:
    uint8_t previous = 0;
    int sum = 0;
};

class Screen {
public:
    uint8_t pixels[1024]{};
    void clear() { memset(pixels, 0, sizeof(pixels)); }
    // One 8-pixel row; 21 ASCII characters. UTF-8 sequences become one '?'.
    void text(unsigned row, const char* text, bool inverted = false) {
        if (row >= 8) return;
        uint8_t* out = pixels + row * 128;
        memset(out, inverted ? 0xff : 0, 128);
        unsigned col = 0;
        for (const unsigned char* p = (const unsigned char*)text; *p && col < 21; ++p) {
            if ((*p & 0xc0) == 0x80) continue;
            unsigned c = (*p >= 32 && *p < 127) ? *p : '?';
            for (unsigned x = 0; x < 5; ++x)
                out[col * 6 + x] = font_mo[c - 16][x] ^ (inverted ? 0xff : 0);
            ++col;
        }
    }
    void present() { offset = 0; }
    bool pending() const { return offset < sizeof(pixels); }
    // SH1106 uses two hidden leading columns. Exactly one 16-byte fragment
    // per service call; no full-screen I2C operation in the control loop.
    template<class Send>
    bool step(Send send) {
        if (!pending()) return true;
        const unsigned column = offset % 128 + 2;
        const uint8_t commands[] = {uint8_t(0xb0 | (offset / 128)),
            uint8_t(column & 15), uint8_t(0x10 | (column >> 4))};
        if (!send(0x00, commands, sizeof(commands)) || !send(0x40, pixels + offset, 16)) {
            offset = sizeof(pixels); return false;
        }
        offset += 16;
        return true;
    }
private:
    unsigned offset = sizeof(pixels);
};
}
