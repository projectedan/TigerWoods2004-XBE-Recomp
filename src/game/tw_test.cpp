// Testing only: TW_AUTOPAD=<first second>,<last second>,<period> presses Start then A on pad 1 in turn, so a run can
// reach the menus with nobody at the controller. Nothing happens without the variable.
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"

void TW_MenuPad(int port, input::Pad& pad);  // tw_menu.cpp
void TW_GlyphsPad(int port);                 // tw_glyphs.cpp

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

// Testing only: TW_MODECYCLE=<first second>,<period>,<times> steps the window through its modes
static void ModeCycle() {
    static float first = -1, period = 0;
    static int times = 0, done = 0;
    static const bool on = [] {
        const char* e = std::getenv("TW_MODECYCLE");
        return e && std::sscanf(e, "%f,%f,%d", &first, &period, &times) == 3;
    }();
    if (!on || done >= times) return;
    static const auto t0 = std::chrono::steady_clock::now();
    float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    if (t < first + done * period) return;
    done++;
    app::Settings& s = app::Get();
    s.display_mode = app::DisplayMode((uint32_t(s.display_mode) + 1) % 3);
    if (s.display_mode == app::DisplayMode::Borderless) s.width = s.height = 0;
    app::ApplyDisplay();
    LOGR("test: display mode %u", uint32_t(s.display_mode));
}

// Testing only: TW_VSYNCCYCLE=<first second>,<period>,<times> flips V-Sync
static void VsyncCycle() {
    static float first = -1, period = 0;
    static int times = 0, done = 0;
    static const bool on = [] {
        const char* e = std::getenv("TW_VSYNCCYCLE");
        return e && std::sscanf(e, "%f,%f,%d", &first, &period, &times) == 3;
    }();
    if (!on || done >= times) return;
    static const auto t0 = std::chrono::steady_clock::now();
    float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    if (t < first + done * period) return;
    done++;
    app::Get().vsync = !app::Get().vsync;
    LOGR("test: vsync %d", int(app::Get().vsync));
}

void TW_Pad(int port, input::Pad& pad) {
    TW_GlyphsPad(port);
    if (port == 0) {
        ModeCycle();
        VsyncCycle();
    }
    AutoPad(port, pad);
    TW_MenuPad(port, pad);
}
