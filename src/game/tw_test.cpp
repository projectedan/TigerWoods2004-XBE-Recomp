// Testing only: TW_AUTOPAD=<first second>,<last second>,<period> presses Start then A on pad 1 in turn, so a run can
// reach the menus with nobody at the controller. Nothing happens without the variable.
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "core/input.h"

void TW_MenuPad(int port, input::Pad& pad);  // tw_menu.cpp

static void AutoPad(int port, input::Pad& pad) {
    static float first = -1, last = 0, period = 2;
    static const bool on = [] {
        const char* e = std::getenv("TW_AUTOPAD");
        return e && std::sscanf(e, "%f,%f,%f", &first, &last, &period) == 3;
    }();
    if (!on || port) return;
    static const auto t0 = std::chrono::steady_clock::now();
    float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    if (t < first || t > last) return;
    int step = int((t - first) / period);
    if ((t - first) - step * period > 0.15f) return;
    pad.connected = true;
    if (step & 1) pad.analog[0] = 255;  // A
    else pad.buttons |= 0x10;           // Start
}

void TW_Pad(int port, input::Pad& pad) {
    AutoPad(port, pad);
    TW_MenuPad(port, pad);
}
