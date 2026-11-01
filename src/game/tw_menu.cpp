// The port's options, in the look of the game's own screens: Back and Start together (or F10, or both stick clicks)
// open and close it. The game plays on behind it without the controllers, so what changes shows at once. Drawn by
// hand with ImGui's draw lists, and steered like the game's menus: up and down a row, left and right its value.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"
#include "imgui.h"

void TW_ApplyScene();
extern bool g_tw_haze, g_tw_bloom, g_tw_focus, g_tw_ao, g_tw_full_detail;
extern float g_tw_haze_amount, g_tw_bloom_amount;

namespace {

// what a controller or the keyboard holds: the pad's own bits, then these
constexpr uint32_t kUp = 1, kDown = 2, kLeft = 4, kRight = 8, kStart = 0x10, kBack = 0x20, kA = 1 << 16, kB = 1 << 17, kPrev = 1 << 18,
                   kNext = 1 << 19;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
const unsigned char kTitleFont[] = {
#embed "archivoblack/ArchivoBlack-Regular.ttf"
};
#pragma clang diagnostic pop

std::atomic<bool> g_open{false}, g_swallow{false}, g_fresh{false};  // swallow: closed, the game waits for every button up
std::atomic<uint32_t> g_pad[4], g_keys{0};
ImFont* g_font = nullptr;

enum class Kind { Mode, Scale, Smoothing, VSync, Tick, Amount };
struct Row {
    Kind kind;
    const char* label;
    bool* flag = nullptr;
    float* amount = nullptr;
};
const std::vector<Row> kPages[2] = {
    {{Kind::Mode, "WINDOW"}, {Kind::Scale, "RESOLUTION SCALING"}, {Kind::Smoothing, "ANTI-ALIASING"}, {Kind::VSync, "V-SYNC"},
     {Kind::Tick, "FULL DETAIL TREES", &g_tw_full_detail}},
    {{Kind::Tick, "DISTANCE HAZE", &g_tw_haze},
     {Kind::Amount, "INTENSITY", nullptr, &g_tw_haze_amount},
     {Kind::Tick, "BLOOM", &g_tw_bloom},
     {Kind::Amount, "INTENSITY", nullptr, &g_tw_bloom_amount},
     {Kind::Tick, "DEPTH OF FIELD", &g_tw_focus},
     {Kind::Tick, "AMBIENT OCCLUSION", &g_tw_ao}},
};
const char* const kPageNames[2] = {"DISPLAY", "EFFECTS"};
const char* const kModeNames[3] = {"FULLSCREEN", "BORDERLESS", "WINDOWED"};
// the frame's size: the console's, the screen's, multiples of the screen's. Taken with A, as each step remakes the targets
const char* const kScaleNames[4] = {"CLASSIC", "DESKTOP", "2X", "3X"};
constexpr uint32_t kScalePercent[4] = {100, 100, 200, 300};
const char* const kSmoothingNames[3] = {"OFF", "SMAA", "MSAA 2X"};

// the game's look
constexpr ImU32 kOrange = IM_COL32(236, 124, 32, 255), kOrangeLit = IM_COL32(255, 170, 70, 255), kWhite = IM_COL32(255, 255, 255, 255),
                kGrey = IM_COL32(176, 180, 196, 255), kDim = IM_COL32(104, 106, 122, 255), kBlack = IM_COL32(0, 0, 0, 255),
                kShade = IM_COL32(0, 0, 0, 170);
constexpr float kSlant = 0.2f, kNarrow = 0.82f;

// the render thread's alone
struct State {
    int page = 0, row = 0;
    bool dirty = false;
    int pending = 0;  // the scale on show, past the one in use while pending_on
    bool pending_on = false, leaving = false;  // leaving: Exit Game waits for A a second time
    uint32_t last = 0, repeat = 0;
    std::chrono::steady_clock::time_point held;
} g;

int ScaleNow() {
    if (app::g_render_classic) return 0;
    uint32_t p = app::Get().render_percent;
    return p >= 300 ? 3 : p >= 200 ? 2 : 1;
}

int SmoothingNow() {
    const app::Settings& s = app::Get();
    return s.msaa >= 2 ? 2 : s.smaa ? 1 : 0;
}

void Close() {
    if (!g_open.exchange(false)) return;
    g_swallow = true;
    g.pending_on = g.leaving = false;
    if (g.dirty) {
        app::Save();
        g.dirty = false;
    }
    LOGI("menu: closed");
}

void Open() {
    if (g_open.exchange(true)) return;
    g_fresh = true;
    if (app::Get().msaa > 2) {  // a higher count from the settings file: the most this menu offers
        app::Get().msaa = 2;
        g.dirty = true;
    }
    LOGI("menu: open");
}

void Toggle() {
    if (g_open) Close();
    else Open();
}

// the rows, then Exit Game
void Move(int step) {
    int n = int(kPages[g.page].size()) + 1;
    g.row = (g.row + step + n) % n;
}

// a row's value one step on (-1, 1); confirm: A, not left or right
void Change(const Row& r, int step, bool confirm) {
    app::Settings& s = app::Get();
    switch (r.kind) {
        case Kind::Mode:
            s.display_mode = app::DisplayMode((uint32_t(s.display_mode) + 3 + step) % 3);
            if (s.display_mode == app::DisplayMode::Borderless) s.width = s.height = 0;  // the desktop's size
            app::ApplyDisplay();
            break;
        case Kind::Scale: {
            if (confirm) {
                if (!g.pending_on) return;
                app::g_render_classic = g.pending == 0;
                s.render_percent = kScalePercent[g.pending];
                g.pending_on = false;
                break;
            }
            int now = g.pending_on ? g.pending : ScaleNow();
            g.pending = (now + step + 4) % 4;
            g.pending_on = g.pending != ScaleNow();
            return;
        }
        case Kind::Smoothing: {
            if (confirm) return;
            int k = (SmoothingNow() + step + 3) % 3;
            s.smaa = k == 1;
            s.msaa = k == 2 ? 2 : 0;
            break;
        }
        case Kind::VSync: s.vsync = !s.vsync; break;
        case Kind::Tick:
            *r.flag = !*r.flag;
            TW_ApplyScene();
            break;
        case Kind::Amount:
            if (confirm) return;
            *r.amount = std::clamp(std::round(*r.amount * 20 + step) / 20, 0.0f, 1.0f);
            TW_ApplyScene();
            break;
    }
    g.dirty = true;
}

void Input() {
    uint32_t now = g_keys.exchange(0), keys = now;
    for (auto& p : g_pad) now |= p.load();
    if (g_fresh.exchange(false)) g.last = now;
    uint32_t pressed = now & ~g.last;
    g.last = now & ~keys;  // a key's press comes once, the system repeats it
    // held up, down, left or right repeats
    auto t = std::chrono::steady_clock::now();
    uint32_t arrows = now & (kUp | kDown | kLeft | kRight);
    if (arrows != g.repeat) {
        g.repeat = arrows;
        g.held = t + std::chrono::milliseconds(350);
    } else if (arrows && t >= g.held) {
        g.held = t + std::chrono::milliseconds(70);
        pressed |= arrows;
    }
    if (pressed & (kB | kStart)) {
        if (g.leaving) g.leaving = false;
        else if (!(now & kBack)) Close();
        return;
    }
    if (pressed & (kPrev | kNext)) {
        g.page = g.page ^ 1;
        g.row = 0;
        g.pending_on = g.leaving = false;
    }
    const auto& rows = kPages[g.page];
    if (pressed & kUp) Move(-1);
    if (pressed & kDown) Move(1);
    if (pressed & (kUp | kDown)) g.pending_on = g.leaving = false;
    if (g.row == int(rows.size())) {
        if (pressed & kA) {
            if (g.leaving) {
                Close();
                PostMessageW(static_cast<HWND>(app::Window()), WM_CLOSE, 0, 0);
                return;
            }
            g.leaving = true;
        }
        return;
    }
    const Row& r = rows[size_t(g.row)];
    if (pressed & kLeft) Change(r, -1, false);
    if (pressed & kRight) Change(r, 1, false);
    if (pressed & kA) Change(r, 1, true);
}

// text slanted and narrowed like the game's titles; align: 0 from x, 0.5 about it, 1 to it
void Text(ImDrawList* dl, float px, float x, float y, ImU32 col, const char* s, float align = 0) {
    float w = g_font->CalcTextSizeA(px, FLT_MAX, 0, s).x * kNarrow;
    x -= w * align;
    for (int shadow = 1; shadow >= 0; shadow--) {
        float off = shadow ? std::max(1.0f, px * 0.06f) : 0;
        float ox = x + off, oy = y + off;
        int first = dl->VtxBuffer.Size;
        dl->AddText(g_font, px, ImVec2(ox, oy), shadow ? kShade : col, s);
        for (int i = first; i < dl->VtxBuffer.Size; i++) {
            ImDrawVert& v = dl->VtxBuffer[i];
            v.pos.x = ox + (v.pos.x - ox) * kNarrow + (oy + px - v.pos.y) * kSlant;
        }
    }
}

void Glyph(ImDrawList* dl, float u, float x, float y, ImU32 col, const char* letter, const char* word) {
    float r = 8 * u;
    dl->AddCircleFilled(ImVec2(x, y), r + 1.2f * u, IM_COL32(230, 230, 236, 255), 24);
    dl->AddCircleFilled(ImVec2(x, y), r, col, 24);
    // the letter by what it covers: its advance and the font's line would leave it high and, slanted, to one side
    int first = dl->VtxBuffer.Size;
    Text(dl, 10 * u, x, y, kWhite, letter, 0.5f);
    ImVec2 lo(FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX);
    for (int i = first; i < dl->VtxBuffer.Size; i++) {
        const ImVec2& p = dl->VtxBuffer[i].pos;
        lo = ImVec2(std::min(lo.x, p.x), std::min(lo.y, p.y));
        hi = ImVec2(std::max(hi.x, p.x), std::max(hi.y, p.y));
    }
    float shade = 0.3f * u;  // half its shadow's reach, which the extent takes in
    for (int i = first; i < dl->VtxBuffer.Size; i++) {
        ImDrawVert& v = dl->VtxBuffer[i];
        v.pos.x += x - (lo.x + hi.x) / 2 + shade;
        v.pos.y += y - (lo.y + hi.y) / 2 + shade;
    }
    Text(dl, 12 * u, x + r + 5 * u, y - 6 * u, kWhite, word);
}

void Arrow(ImDrawList* dl, float u, float x, float y, float dir) {
    dl->AddTriangleFilled(ImVec2(x + dir * 15 * u, y), ImVec2(x - dir * 8 * u, y - 10 * u), ImVec2(x - dir * 2 * u, y), kOrange);
    dl->AddTriangleFilled(ImVec2(x + dir * 15 * u, y), ImVec2(x - dir * 8 * u, y + 10 * u), ImVec2(x - dir * 2 * u, y), kOrangeLit);
}

void Tick(ImDrawList* dl, float u, float x, float y, bool on, bool selected) {
    float s = 15 * u;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + s, y + s), IM_COL32(16, 16, 26, 230));
    dl->AddRect(ImVec2(x, y), ImVec2(x + s, y + s), selected ? kOrangeLit : kGrey, 0, 0, 1.4f * u);
    if (on) {
        ImVec2 p[3] = {ImVec2(x + 3 * u, y + 8 * u), ImVec2(x + 6.5f * u, y + 12 * u), ImVec2(x + 13 * u, y + 3 * u)};
        dl->AddPolyline(p, 3, kOrangeLit, 0, 2.6f * u);
    }
}

void Bar(ImDrawList* dl, float u, float x, float y, float w, float amount, bool on, bool selected) {
    float h = 7 * u, knob = x + w * amount;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(12, 12, 22, 235));
    dl->AddRectFilled(ImVec2(x, y), ImVec2(knob, y + h), on ? kOrange : kDim);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(120, 122, 140, 255));
    dl->AddRectFilled(ImVec2(knob - 2.5f * u, y - 3 * u), ImVec2(knob + 2.5f * u, y + h + 3 * u), selected ? kWhite : kGrey);
}

void Draw() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 size = ImGui::GetIO().DisplaySize;
    float u = size.y / 480.0f, cx = size.x / 2;  // u: a console line
    app::Settings& s = app::Get();

    // a box in the middle of the screen, the height of the longest page so that it holds still between them
    constexpr size_t kMostRows = 6;
    float pw = 225 * u, band = 42 * u, tabs = 24 * u, high = 34 * u, gap = 6 * u, exit = 38 * u, foot = 26 * u;
    float body = high * kMostRows + gap * 2 + exit, total = band + tabs + body + foot;
    float left = cx - pw, right = cx + pw, head = (size.y - total) / 2, tab = head + band, top = tab + tabs, bottom = top + body;
    for (int i = 3; i > 0; i--) {
        float grow = float(i) * 4 * u;
        dl->AddRectFilled(ImVec2(left - grow, head - grow), ImVec2(right + grow, bottom + foot + grow), IM_COL32(0, 0, 0, 44), 4 * u * float(i));
    }

    // the header: a band with the golf ball's dimples, its title on the right, and the page row under it
    dl->AddRectFilledMultiColor(ImVec2(left, head), ImVec2(right, tab), IM_COL32(112, 114, 130, 255), IM_COL32(58, 42, 104, 255),
                                IM_COL32(34, 28, 66, 255), IM_COL32(58, 62, 84, 255));
    dl->AddRectFilled(ImVec2(left, head), ImVec2(right, head + 3 * u), IM_COL32(186, 186, 196, 255));
    dl->PushClipRect(ImVec2(left, head + 3 * u), ImVec2(right, tab), true);
    for (int y = 0; y < 5; y++)
        for (float x = left + (y & 1) * 5 * u; x < right + 5 * u; x += 10 * u)
            dl->AddCircleFilled(ImVec2(x, head + (6 + y * 9) * u), 2.6f * u, IM_COL32(190, 200, 225, 34), 8);
    dl->PopClipRect();
    Text(dl, 26 * u, right - 12 * u, head + 8 * u, kWhite, "OPTIONS", 1);
    dl->AddRectFilledMultiColor(ImVec2(left, tab), ImVec2(right, top), kBlack, kBlack, IM_COL32(24, 26, 48, 255), IM_COL32(24, 26, 48, 255));
    float mid = tab + tabs / 2, side = pw - 34 * u;
    Arrow(dl, u, cx - side, mid, -1);
    Arrow(dl, u, cx + side, mid, 1);
    Text(dl, 17 * u, cx, mid - 10 * u, kWhite, kPageNames[g.page], 0.5f);
    Text(dl, 13 * u, cx - side * 0.62f, mid - 7.5f * u, kDim, kPageNames[g.page ^ 1], 0.5f);
    Text(dl, 13 * u, cx + side * 0.62f, mid - 7.5f * u, kDim, kPageNames[g.page ^ 1], 0.5f);

    // the panel: the game's slate over its green and purple
    const auto& rows = kPages[g.page];
    dl->AddRectFilledMultiColor(ImVec2(left, top), ImVec2(right, bottom), IM_COL32(54, 66, 68, 244), IM_COL32(50, 56, 70, 244),
                                IM_COL32(52, 34, 84, 248), IM_COL32(34, 40, 76, 248));

    float label = cx - 10 * u, value = cx + 10 * u, first = top + gap;
    for (size_t i = 0; i < rows.size(); i++) {
        const Row& r = rows[i];
        bool sel = int(i) == g.row;
        float y = first + high * i;
        if (sel) {
            dl->AddRectFilled(ImVec2(left, y + 2 * u), ImVec2(right, y + high - 2 * u), IM_COL32(236, 124, 32, 48));
            dl->AddRectFilled(ImVec2(left, y + 2 * u), ImVec2(left + 4 * u, y + high - 2 * u), kOrange);
        }
        float ty = y + 6 * u;
        Text(dl, r.kind == Kind::Amount ? 13 * u : 17 * u, label, r.kind == Kind::Amount ? ty + 3 * u : ty, sel ? kWhite : kGrey, r.label, 1);
        ImU32 val = sel ? kOrangeLit : kWhite;
        char pct[16];
        switch (r.kind) {
            case Kind::Mode: Text(dl, 16 * u, value, ty, val, kModeNames[uint32_t(s.display_mode) % 3]); break;
            case Kind::Scale: {
                bool wait = g.pending_on;
                Text(dl, 16 * u, value, ty, val, kScaleNames[wait ? g.pending : ScaleNow()]);
                if (wait) Text(dl, 10 * u, right - 8 * u, ty + 4 * u, kGrey, "A TO APPLY", 1);
                break;
            }
            case Kind::Smoothing: Text(dl, 16 * u, value, ty, val, kSmoothingNames[SmoothingNow()]); break;
            case Kind::VSync: Tick(dl, u, value, ty + 1 * u, s.vsync, sel); break;
            case Kind::Tick: Tick(dl, u, value, ty + 1 * u, *r.flag, sel); break;
            case Kind::Amount: {
                bool live = *rows[i - 1].flag;
                // a line down from its parent's box, so that the slider reads as belonging to it
                float drop = value + 7.5f * u, along = ty + 9.5f * u, reach = value + 20 * u;
                ImU32 link = live ? kOrange : kDim;
                dl->AddLine(ImVec2(drop, y - 10 * u), ImVec2(drop, along), link, 1.6f * u);
                dl->AddLine(ImVec2(drop - 0.8f * u, along), ImVec2(reach, along), link, 1.6f * u);
                Bar(dl, u, reach, ty + 6 * u, 112 * u, *r.amount, live, sel);
                std::snprintf(pct, sizeof pct, "%d%%", int(*r.amount * 100 + 0.5f));
                Text(dl, 13 * u, value + 180 * u, ty + 3 * u, val, pct, 1);
                break;
            }
        }
    }

    // Exit Game, in the same place on every page
    bool leave = g.row == int(rows.size());
    float ey = bottom - exit;
    dl->AddRectFilled(ImVec2(left + 12 * u, ey), ImVec2(right - 12 * u, ey + 1.5f * u), IM_COL32(120, 122, 140, 120));
    if (leave) {
        dl->AddRectFilled(ImVec2(left, ey + 4 * u), ImVec2(right, bottom - 3 * u), IM_COL32(236, 124, 32, 48));
        dl->AddRectFilled(ImVec2(left, ey + 4 * u), ImVec2(left + 4 * u, bottom - 3 * u), kOrange);
    }
    Text(dl, 17 * u, cx, ey + 9 * u, leave ? (g.leaving ? kOrangeLit : kWhite) : kGrey, g.leaving ? "PRESS A TO CONFIRM" : "EXIT GAME", 0.5f);

    // the footer: the game's black strip with its buttons
    float fy = bottom + foot / 2;
    dl->AddRectFilled(ImVec2(left, bottom), ImVec2(right, bottom + foot), IM_COL32(4, 4, 8, 255));
    Glyph(dl, u, left + 18 * u, fy, IM_COL32(190, 34, 30, 255), "B", "Back");
    Glyph(dl, u, cx - 40 * u, fy, IM_COL32(40, 150, 50, 255), "A", "Select");
    Text(dl, 12 * u, right - 12 * u, fy - 6 * u, kWhite, "LB / RB  PAGE", 1);
}

}  // namespace

// after Open and Close, what the runtime asks
void TW_MenuFonts() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig fc{};
    fc.FontDataOwnedByAtlas = false;
    fc.OversampleH = 2;
    fc.OversampleV = 2;
    g_font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kTitleFont), int(sizeof(kTitleFont)), 48.0f, &fc);
}

void TW_MenuToggle() { Toggle(); }
bool TW_MenuOpen() { return g_open.load(); }

// the renderer's ImGui frame (TW_Ui)
void TW_MenuUi() {
    // the game blurred behind it, eased in and out over about a sixth of a second
    float blur = app::g_backdrop_blur.load(), want = g_open ? 1.0f : 0.0f;
    if (blur != want) {
        blur += (want - blur) * std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);
        app::g_backdrop_blur = std::fabs(want - blur) < 0.01f ? want : blur;
    }
    if (!g_open) return;
    Input();
    if (g_open && g_font) Draw();
}

// window messages: the keyboard steers it
bool TW_MenuKey(uintptr_t key) {
    if (!g_open) return false;
    uint32_t bit = 0;
    switch (key) {
        case VK_UP: bit = kUp; break;
        case VK_DOWN: bit = kDown; break;
        case VK_LEFT: bit = kLeft; break;
        case VK_RIGHT: bit = kRight; break;
        case VK_RETURN: case VK_SPACE: bit = kA; break;
        case VK_ESCAPE: bit = kB; break;
        case VK_PRIOR: bit = kPrev; break;
        case VK_NEXT: bit = kNext; break;
        default: return false;
    }
    g_keys |= bit;
    return true;
}

// each pad as the game reads it: already polled, so nothing here waits on a controller
void TW_MenuPad(int port, input::Pad& pad) {
    if (port < 0 || port > 3) return;
    constexpr int kStick = 16000;
    uint32_t b = pad.buttons & 0x3F;
    if (pad.ly > kStick) b |= kUp;
    if (pad.ly < -kStick) b |= kDown;
    if (pad.lx < -kStick) b |= kLeft;
    if (pad.lx > kStick) b |= kRight;
    if (pad.analog[0] > 30) b |= kA;
    if (pad.analog[1] > 30) b |= kB;
    if (pad.analog[5] > 30) b |= kPrev;
    if (pad.analog[4] > 30) b |= kNext;
    g_pad[port] = b;
    static std::atomic<bool> combo[4];
    bool both = (b & (kStart | kBack)) == (kStart | kBack);
    if (both && !combo[port].exchange(true)) Toggle();
    if (!both) combo[port] = false;
    if (!g_open && g_swallow) {
        bool any = false;
        for (auto& p : g_pad) any = any || p.load();
        if (!any) g_swallow = false;
    }
    if (g_open || g_swallow) {
        bool connected = pad.connected;
        pad = input::Pad{};
        pad.connected = connected;
    }
}
