// Tiger Woods PGA Tour 2004: F9 switches the resampler while the game runs, to compare by ear.
#include <chrono>

#include "audio/audio.h"
#include "core/log.h"
#include "imgui.h"

namespace {
std::chrono::steady_clock::time_point g_shown;
}

bool TW_Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t*) {
    constexpr uint32_t kKeyDown = 0x100;
    constexpr uintptr_t kF9 = 0x78;
    if (msg != kKeyDown || wp != kF9 || (lp & (1 << 30))) return false;
    bool on = !audio::g_sinc.load();
    audio::g_sinc = on;
    g_shown = std::chrono::steady_clock::now();
    LOGI("audio: resampler %s", on ? "sinc" : "linear");
    return true;
}

void TW_Ui(double) {
    if (std::chrono::steady_clock::now() - g_shown > std::chrono::seconds(2)) return;
    ImGui::GetForegroundDrawList()->AddText(ImVec2(24, 24), IM_COL32(255, 255, 255, 255),
                                            audio::g_sinc.load() ? "Resampler: sinc (new)" : "Resampler: linear (old)");
}
