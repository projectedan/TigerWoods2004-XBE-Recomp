// Tiger Woods PGA Tour 2004: F9 switches the resampler while the game runs, to compare by ear.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <thread>

#include "app/app.h"
#include "audio/audio.h"
#include "gpu/backend.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include "imgui.h"

void TW_MenuUi();                  // tw_menu.cpp
bool TW_MenuKey(uintptr_t key);

namespace {
std::chrono::steady_clock::time_point g_shown, g_movie_seen;
const char* g_label = "";
}

bool TW_Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t*) {
    constexpr uint32_t kKeyDown = 0x100;
    constexpr uintptr_t kF9 = 0x78;
    if (msg == kKeyDown && TW_MenuKey(wp)) return true;
    if (msg != kKeyDown || wp != kF9 || (lp & (1 << 30))) return false;
    g_shown = std::chrono::steady_clock::now();
    bool on = !audio::g_sinc.load();
    audio::g_sinc = on;
    g_label = on ? "Resampler: sinc (new)" : "Resampler: linear (old)";
    LOGI("audio: resampler %s", on ? "sinc" : "linear");
    return true;
}

void TW_Ui(double) {
    TW_MenuUi();
    // TW_FRAMES set: frame times over each 5 s, test only
    static const bool frames = std::getenv("TW_FRAMES") != nullptr;
    if (frames) {
        using C = std::chrono::steady_clock;
        static C::time_point last = C::now(), start = last;
        static int n, over[4];
        static double maxms;
        auto now = C::now();
        double ms = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
        n++;
        maxms = std::max(maxms, ms);
        static double near_sum, near_square;
        static int near_n;
        if (ms < 25) {  // the spread of ordinary frames: a load's stall would swamp it
            near_sum += ms;
            near_square += ms * ms;
            near_n++;
        }
        over[0] += ms > 18;
        over[1] += ms > 20;
        over[2] += ms > 25;
        over[3] += ms > 34;
        if (now - start > std::chrono::seconds(5)) {
            double mean = near_n ? near_sum / near_n : 0, spread = near_n ? std::sqrt(std::max(0.0, near_square / near_n - mean * mean)) : 0;
            near_sum = near_square = 0;
            near_n = 0;
            LOGR("frames: %d in 5 s, spread %.2f ms, over 18 ms %d, 20 ms %d, 25 ms %d, 34 ms %d, longest %.1f", n, spread, over[0], over[1], over[2], over[3], maxms);
            n = 0;
            over[0] = over[1] = over[2] = over[3] = 0;
            maxms = 0;
            start = now;
        }
    }
    if (gpu::g_render_mask.load() && std::chrono::steady_clock::now() - g_movie_seen > std::chrono::milliseconds(300))
        gpu::g_render_mask = 0;
    if (std::chrono::steady_clock::now() - g_shown > std::chrono::seconds(2)) return;
    ImGui::GetForegroundDrawList()->AddText(ImVec2(24, 24), IM_COL32(255, 255, 255, 255), g_label);
}

// Movies. The loop (0xF9360) spins until 30.3 ms after each swap, which on the console leaves the frame on the next
// 33.3 ms vertical blank. Here the wait for the display comes after that spin, on top of it: 46.9 ms a frame, 21 fps.
// The spin is replaced by one that counts 33.3 ms from the last time through it, the wait for the display included.
// The renderer reads the movie's picture from guest memory while the decoder rewrites it: on the game's core the two
// take turns, as on the console, and the picture does not tear. The rest of the game has the render thread to itself.
bool TW_MoviePace(X86Context&, uint8_t*) {
    g_movie_seen = std::chrono::steady_clock::now();
    gpu::g_render_mask = app::OneCoreMask();
    using Clock = std::chrono::steady_clock;
    static Clock::time_point last;
    constexpr auto kFrame = std::chrono::microseconds(33333);
    auto next = last + kFrame, now = Clock::now();
    while ((now = Clock::now()) < next) std::this_thread::sleep_for(std::chrono::microseconds(500));
    last = now - next < kFrame ? next : now;  // a late frame does not start a burst of short ones
    return true;
}

// the loop polls for the decoder's next picture: without a yield here the poll takes the core from the decoder, and
// movie frames ran late (35 ms instead of 33.3, measured)
void TW_MovieIdle(X86Context&, uint8_t*) { std::this_thread::yield(); }
