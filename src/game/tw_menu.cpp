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
extern bool g_tw_haze, g_tw_bloom, g_tw_focus, g_tw_ao, g_tw_hud_corners, g_tw_full_detail;
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

enum class Kind { Mode, Resolution, VSync, Tick, Amount };
struct Row {
    Kind kind;
    const char* label;
    bool* flag = nullptr;
    float* amount = nullptr;
};
const std::vector<Row> kPages[2] = {
    {{Kind::Mode, "WINDOW"}, {Kind::Resolution, "RESOLUTION"}, {Kind::VSync, "V-SYNC"}, {Kind::Tick, "HUD AT CORNERS", &g_tw_hud_corners},
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

using Size = std::pair<uint32_t, uint32_t>;

// the game's look
constexpr ImU32 kOrange = IM_COL32(236, 124, 32, 255), kOrangeLit = IM_COL32(255, 170, 70, 255), kWhite = IM_COL32(255, 255, 255, 255),
                kGrey = IM_COL32(176, 180, 196, 255), kDim = IM_COL32(104, 106, 122, 255), kBlack = IM_COL32(0, 0, 0, 255),
                kShade = IM_COL32(0, 0, 0, 170);
constexpr float kSlant = 0.2f, kNarrow = 0.82f;

// the render thread's alone
struct State {
    int page = 0, row = 0;
    bool dirty = false;
    std::vector<Size> sizes;
    Size pending{};  // the size on show; taken with A, as each step would resize the window
    bool pending_on = false;
    uint32_t last = 0, repeat = 0;
    std::chrono::steady_clock::time_point held;
} g;

std::vector<Size> ScreenSizes() {
    std::vector<Size> list;
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromWindow(static_cast<HWND>(app::Window()), MONITOR_DEFAULTTOPRIMARY), &mi)) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        for (DWORD i = 0; EnumDisplaySettingsW(mi.szDevice, i, &dm); i++)
            if (dm.dmPelsWidth >= 640 && dm.dmPelsHeight >= 480) list.emplace_back(dm.dmPelsWidth, dm.dmPelsHeight);
    }
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
    list.insert(list.begin(), {0u, 0u});  // the desktop's
    return list;
}

std::string SizeText(Size s) {
    char t[48];
    app::Mode d = app::Desktop();
    if (s.first) std::snprintf(t, sizeof t, "%u X %u", s.first, s.second);
    else std::snprintf(t, sizeof t, "DESKTOP (%u X %u)", d.width, d.height);
    return t;
}

bool Sized() { return app::Get().display_mode != app::DisplayMode::Borderless; }
bool Selectable(const Row& r) { return r.kind != Kind::Resolution || Sized(); }

void Close() {
    if (!g_open.exchange(false)) return;
    g_swallow = true;
    g.pending_on = false;
    if (g.dirty) {
        app::Save();
        g.dirty = false;
    }
    LOGI("menu: closed");
}

void Open() {
    if (g_open.exchange(true)) return;
    g_fresh = true;
    LOGI("menu: open");
}

void Toggle() {
    if (g_open) Close();
    else Open();
}

void Move(int step) {
    const auto& rows = kPages[g.page];
    int n = int(rows.size());
    for (int i = 0; i < n; i++) {
        g.row = (g.row + step + n) % n;
        if (Selectable(rows[size_t(g.row)])) return;
    }
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
        case Kind::Resolution: {
            Size now = g.pending_on ? g.pending : Size{s.width, s.height};
            if (confirm) {
                if (!g.pending_on) return;
                s.width = now.first;
                s.height = now.second;
                g.pending_on = false;
                app::ApplyDisplay();
                break;
            }
            if (g.sizes.empty()) g.sizes = ScreenSizes();
            auto it = std::find(g.sizes.begin(), g.sizes.end(), now);
            int i = it == g.sizes.end() ? 0 : int(it - g.sizes.begin()), n = int(g.sizes.size());
            g.pending = g.sizes[size_t((i + step + n) % n)];
            g.pending_on = g.pending != Size{s.width, s.height};
            return;
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
        if (!(now & kBack)) Close();
        return;
    }
    if (pressed & (kPrev | kNext)) {
        g.page = g.page ^ 1;
        g.row = 0;
        g.pending_on = false;
    }
    const auto& rows = kPages[g.page];
    if (pressed & kUp) Move(-1);
    if (pressed & kDown) Move(1);
    if (pressed & (kUp | kDown)) g.pending_on = false;
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
    Text(dl, 10 * u, x, y - 6.5f * u, kWhite, letter, 0.5f);
    Text(dl, 12 * u, x + r + 5 * u, y - 7.5f * u, kWhite, word);
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
    float u = size.y / 480.0f, W = size.x, cx = W / 2, side = std::min(W / 2, 300 * u);  // u: a console line
    app::Settings& s = app::Get();

    // the header: a band with the golf ball's dimples, its title on the right, and the page row under it
    float band = 52 * u, row = 26 * u;
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, band), IM_COL32(112, 114, 130, 255), IM_COL32(58, 42, 104, 255),
                                IM_COL32(34, 28, 66, 255), IM_COL32(58, 62, 84, 255));
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, 3 * u), IM_COL32(186, 186, 196, 255));
    for (int y = 0; y < 5; y++)
        for (float x = (y & 1) * 5 * u; x < W; x += 10 * u)
            dl->AddCircleFilled(ImVec2(x, (9 + y * 9) * u), 2.6f * u, IM_COL32(190, 200, 225, 34), 8);
    Text(dl, 12 * u, cx - side, 15 * u, kGrey, "TIGER WOODS PGA TOUR 2004");
    Text(dl, 34 * u, cx + side, 8 * u, kWhite, "OPTIONS", 1);
    dl->AddRectFilledMultiColor(ImVec2(0, band), ImVec2(W, band + row), kBlack, kBlack, IM_COL32(24, 26, 48, 255), IM_COL32(24, 26, 48, 255));
    float mid = band + row / 2;
    Arrow(dl, u, cx - side, mid, -1);
    Arrow(dl, u, cx + side, mid, 1);
    Text(dl, 19 * u, cx, mid - 11 * u, kWhite, kPageNames[g.page], 0.5f);
    Text(dl, 14 * u, cx - side * 0.62f, mid - 8 * u, kDim, kPageNames[g.page ^ 1], 0.5f);
    Text(dl, 14 * u, cx + side * 0.62f, mid - 8 * u, kDim, kPageNames[g.page ^ 1], 0.5f);

    // the panel: the game's slate over its green and purple, the game itself showing through
    const auto& rows = kPages[g.page];
    float high = 36 * u, pw = 225 * u, top = 112 * u, bottom = top + high * rows.size() + 16 * u;
    dl->AddRectFilledMultiColor(ImVec2(cx - pw, top), ImVec2(cx + pw, bottom), IM_COL32(54, 66, 68, 214), IM_COL32(50, 56, 70, 214),
                                IM_COL32(52, 34, 84, 222), IM_COL32(34, 40, 76, 222));
    dl->AddRectFilled(ImVec2(cx - pw, top - 4 * u), ImVec2(cx + pw, top), IM_COL32(12, 12, 20, 255));
    dl->AddRectFilled(ImVec2(cx - pw, bottom), ImVec2(cx + pw, bottom + 4 * u), IM_COL32(12, 12, 20, 255));

    float label = cx - 10 * u, value = cx + 10 * u;
    for (size_t i = 0; i < rows.size(); i++) {
        const Row& r = rows[i];
        bool sel = int(i) == g.row, on = Selectable(r);
        float y = top + 8 * u + high * i;
        if (sel) {
            dl->AddRectFilled(ImVec2(cx - pw, y + 2 * u), ImVec2(cx + pw, y + high - 2 * u), IM_COL32(236, 124, 32, 48));
            dl->AddRectFilled(ImVec2(cx - pw, y + 2 * u), ImVec2(cx - pw + 4 * u, y + high - 2 * u), kOrange);
        }
        float ty = y + 8 * u;
        Text(dl, r.kind == Kind::Amount ? 13 * u : 17 * u, label, r.kind == Kind::Amount ? ty + 3 * u : ty, sel ? kWhite : on ? kGrey : kDim, r.label, 1);
        ImU32 val = sel ? kOrangeLit : on ? kWhite : kDim;
        char pct[16];
        switch (r.kind) {
            case Kind::Mode: Text(dl, 16 * u, value, ty, val, kModeNames[uint32_t(s.display_mode) % 3]); break;
            case Kind::Resolution: {
                bool wait = g.pending_on && sel;
                Size now = wait ? g.pending : Size{s.width, s.height};
                Text(dl, 15 * u, value, ty, val, SizeText(now).c_str());
                if (wait) Text(dl, 10 * u, cx + pw - 8 * u, ty + 18 * u, kGrey, "A TO APPLY", 1);
                break;
            }
            case Kind::VSync: Tick(dl, u, value, ty + 1 * u, s.vsync, sel); break;
            case Kind::Tick: Tick(dl, u, value, ty + 1 * u, *r.flag, sel); break;
            case Kind::Amount: {
                bool live = *rows[i - 1].flag;
                Bar(dl, u, value, ty + 6 * u, 130 * u, *r.amount, live, sel);
                std::snprintf(pct, sizeof pct, "%d%%", int(*r.amount * 100 + 0.5f));
                Text(dl, 13 * u, value + 180 * u, ty + 3 * u, val, pct, 1);
                break;
            }
        }
    }

    // the footer: the game's black strip with its buttons
    float foot = 30 * u, fy = size.y - foot / 2;
    dl->AddRectFilled(ImVec2(0, size.y - foot), ImVec2(W, size.y), IM_COL32(4, 4, 8, 255));
    Glyph(dl, u, cx - side + 12 * u, fy, IM_COL32(190, 34, 30, 255), "B", "Back");
    Glyph(dl, u, cx - 40 * u, fy, IM_COL32(40, 150, 50, 255), "A", "Select");
    Text(dl, 12 * u, cx + side, fy - 7.5f * u, kWhite, "LB / RB  PAGE", 1);
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
