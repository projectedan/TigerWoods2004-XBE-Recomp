// The port's options, in the look of the game's own screens: Back and Start together (or F1, F10, or both stick clicks)
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
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "app/app.h"
#include "core/input.h"
#include "core/keys.h"
#include "core/log.h"
#include "gpu/texture_pack.h"
#include "imgui.h"

namespace keys {
void Swallow();  // keyboard.cpp: keys down now are not read again until released
}

void TW_ApplyScene();
void TW_GlyphsApply();
void TW_GlyphsKey();
extern uint32_t g_tw_prompts;
extern bool g_tw_bloom, g_tw_focus, g_tw_ao, g_tw_full_detail;
extern float g_tw_bloom_amount;

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

enum class Kind { Mode, Scale, Smoothing, Filtering, VSync, Prompts, Tick, Amount };
struct Row {
    Kind kind;
    const char* label;
    bool* flag = nullptr;
    float* amount = nullptr;
};
constexpr int kPageCount = 3;
const std::vector<Row> kPages[kPageCount] = {
    {{Kind::Mode, "WINDOW"}, {Kind::Scale, "RESOLUTION SCALING"}, {Kind::Smoothing, "ANTI-ALIASING"}, {Kind::Filtering, "TEXTURE FILTERING"}, {Kind::VSync, "V-SYNC"},
     {Kind::Prompts, "BUTTON PROMPTS"}, {Kind::Tick, "FULL DETAIL TREES", &g_tw_full_detail}},
    {{Kind::Tick, "BLOOM", &g_tw_bloom},
     {Kind::Amount, "INTENSITY", nullptr, &g_tw_bloom_amount},
     {Kind::Tick, "DEPTH OF FIELD", &g_tw_focus},
     {Kind::Tick, "AMBIENT OCCLUSION", &g_tw_ao}},
    {},  // the controls, to read (DrawControls)
};
const char* const kPageNames[kPageCount] = {"DISPLAY", "EFFECTS", "CONTROLS"};
constexpr int kControlsPage = 2;
const char* const kModeNames[3] = {"FULLSCREEN", "BORDERLESS", "WINDOWED"};
// the frame's size: the console's, the screen's, multiples of the screen's. Taken with A, as each step remakes the targets
const char* const kScaleNames[4] = {"CLASSIC", "DESKTOP", "2X", "3X"};
constexpr uint32_t kScalePercent[4] = {100, 100, 200, 300};
const char* const kSmoothingNames[3] = {"OFF", "SMAA", "MSAA 2X"};
const char* const kFilterNames[5] = {"OFF", "2X", "4X", "8X", "16X"};
constexpr uint32_t kFilterValues[5] = {0, 2, 4, 8, 16};
const char* const kPromptNames[4] = {"AUTO", "XBOX", "PLAYSTATION", "KEYBOARD"};

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

int FilteringNow() {
    uint32_t a = app::Get().aniso;
    return a >= 16 ? 4 : a >= 8 ? 3 : a >= 4 ? 2 : a >= 2 ? 1 : 0;
}

int SmoothingNow() {
    const app::Settings& s = app::Get();
    return s.msaa >= 2 ? 2 : s.smaa ? 1 : 0;
}

void Close() {
    if (!g_open.exchange(false)) return;
    g_swallow = true;
    keys::Swallow();  // Esc closing the menu is not also the game's pause
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
        case Kind::Filtering:
            if (confirm) return;
            s.aniso = kFilterValues[(FilteringNow() + step + 5) % 5];
            break;
        case Kind::Prompts:
            if (confirm) return;
            g_tw_prompts = (g_tw_prompts + 4 + step) % 4;
            TW_GlyphsApply();
            break;
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
        g.page = (g.page + ((pressed & kNext) ? 1 : kPageCount - 1)) % kPageCount;
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

// a key's name as the diagram labels it
std::string KeyWords(const std::string& key) {
    if (key == "Mouse1") return "LEFT CLICK";
    if (key == "Mouse2") return "RIGHT CLICK";
    if (key == "Mouse3") return "MIDDLE CLICK";
    if (key == "LShift" || key == "RShift") return "SHIFT";
    if (key == "Grave") return "~ KEY";
    if (key.empty()) return "NOT SET";
    std::string out = key;
    for (char& c : out) c = char(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// a pad's outline, right half, top middle to bottom middle: x, y from the pad's middle in the units of DrawPad. Runs of
// three points are Bezier curves from the point before. Proportions after TheHoodieGuy02's "8th Gen Console Vector
// Gamepad Collection" (OpenGameArt, CC0)
constexpr ImVec2 kOutlineStart{0, -66};
constexpr ImVec2 kOutline[][3] = {{{14, -68}, {28, -71}, {40, -72}},   {{50, -76}, {60, -76}, {68, -74}},  {{84, -72}, {96, -62}, {100, -50}},
                                  {{105, -35}, {110, -15}, {113, 5}},  {{116, 25}, {118, 45}, {117, 58}},  {{116, 75}, {108, 86}, {96, 87}},
                                  {{86, 88}, {80, 78}, {72, 68}},      {{66, 60}, {60, 48}, {54, 40}},     {{36, 38}, {18, 38}, {0, 38}}};

void PadOutline(std::vector<ImVec2>& out, ImVec2 mid, float k) {
    constexpr int kSteps = 12;
    std::vector<ImVec2> half;
    ImVec2 from = kOutlineStart;
    half.push_back(from);
    for (const auto& c : kOutline) {
        for (int i = 1; i <= kSteps; i++) {
            float t = float(i) / kSteps, m = 1 - t;
            float w0 = m * m * m, w1 = 3 * m * m * t, w2 = 3 * m * t * t, w3 = t * t * t;
            half.push_back(ImVec2(w0 * from.x + w1 * c[0].x + w2 * c[1].x + w3 * c[2].x, w0 * from.y + w1 * c[0].y + w2 * c[1].y + w3 * c[2].y));
        }
        from = c[2];
    }
    out.clear();
    for (const ImVec2& v : half) out.push_back(ImVec2(mid.x + v.x * k, mid.y + v.y * k));
    for (size_t i = half.size(); i-- > 0;) out.push_back(ImVec2(mid.x - half[i].x * k, mid.y + half[i].y * k));
}

// a shape that is not convex, a row of pixels at a time
void FillOutline(ImDrawList* dl, const std::vector<ImVec2>& line, float step, ImU32 col) {
    float low = FLT_MAX, high = -FLT_MAX;
    for (const ImVec2& v : line) low = std::min(low, v.y), high = std::max(high, v.y);
    std::vector<float> at;
    for (float y = low; y < high; y += step) {
        at.clear();
        for (size_t i = 0; i < line.size(); i++) {
            const ImVec2& a = line[i];
            const ImVec2& b = line[(i + 1) % line.size()];
            if ((a.y <= y) != (b.y <= y)) at.push_back(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x));
        }
        std::sort(at.begin(), at.end());
        for (size_t i = 0; i + 1 < at.size(); i += 2) dl->AddRectFilled(ImVec2(at[i], y), ImVec2(at[i + 1], y + step), col);
    }
}

// the pad, its face buttons in the look in use; the words are the game's loading screen's. Each line's place is chosen
// so that none crosses another: over the pad by order along it, beside it by order down it
void DrawPad(ImDrawList* dl, float u, float cx, float top, bool ps) {
    constexpr ImU32 kBody = IM_COL32(44, 46, 54, 255), kEdge = IM_COL32(150, 154, 168, 255), kPart = IM_COL32(24, 25, 31, 255), kBack = IM_COL32(32, 33, 40, 255);
    constexpr float kScale = 0.8f, kMiddle = 118;
    ImVec2 mid(cx, top + kMiddle * u);
    auto at = [&](float x, float y) { return ImVec2(mid.x + x * kScale * u, mid.y + y * kScale * u); };
    // the triggers and the shoulder buttons behind the body
    for (float side : {-1.0f, 1.0f}) {
        ImVec2 t0 = at(side * 46, -92), t1 = at(side * 76, -74), b0 = at(side * 32, -84), b1 = at(side * 88, -64);
        dl->AddRectFilled(ImVec2(std::min(t0.x, t1.x), t0.y), ImVec2(std::max(t0.x, t1.x), t1.y), kBack, 5 * u);
        dl->AddRect(ImVec2(std::min(t0.x, t1.x), t0.y), ImVec2(std::max(t0.x, t1.x), t1.y), kEdge, 5 * u, 0, 1.1f * u);
        dl->AddRectFilled(ImVec2(std::min(b0.x, b1.x), b0.y), ImVec2(std::max(b0.x, b1.x), b1.y), kBack, 6 * u);
        dl->AddRect(ImVec2(std::min(b0.x, b1.x), b0.y), ImVec2(std::max(b0.x, b1.x), b1.y), kEdge, 6 * u, 0, 1.1f * u);
    }
    std::vector<ImVec2> line;
    PadOutline(line, mid, kScale * u);
    FillOutline(dl, line, 0.5f, kBody);
    dl->AddPolyline(line.data(), int(line.size()), kEdge, ImDrawFlags_Closed, 1.3f * u);
    Text(dl, 8 * u, cx - 48 * u, mid.y - 64 * u, kWhite, ps ? "L1" : "LB", 0.5f);
    Text(dl, 8 * u, cx + 48 * u, mid.y - 64 * u, kWhite, ps ? "R1" : "RB", 0.5f);
    auto stick = [&](ImVec2 c) {
        dl->AddCircleFilled(c, 13 * u, kPart, 32);
        dl->AddCircle(c, 13 * u, kEdge, 32, 1.1f * u);
        dl->AddCircleFilled(c, 9 * u, kBack, 28);
        dl->AddCircle(c, 9 * u, kEdge, 28, 1.0f * u);
    };
    ImVec2 left_stick = at(-58, -39), right_stick = at(29, 6), pad = at(-30, 5), view = at(-16, -40), menu = at(17, -40);
    stick(left_stick);
    stick(right_stick);
    float arm = 13 * u, wide = 4.6f * u;
    dl->AddRectFilled(ImVec2(pad.x - arm, pad.y - wide), ImVec2(pad.x + arm, pad.y + wide), kPart, 1.2f * u);
    dl->AddRectFilled(ImVec2(pad.x - wide, pad.y - arm), ImVec2(pad.x + wide, pad.y + arm), kPart, 1.2f * u);
    for (const ImVec2& c : {view, menu}) {
        dl->AddCircleFilled(c, 4 * u, kPart, 16);
        dl->AddCircle(c, 4 * u, kEdge, 16, 1.0f * u);
    }
    struct Face {
        float x, y;
        ImU32 box, mark;
        const char* letter;
    };
    constexpr ImU32 kPsFace = IM_COL32(24, 25, 31, 255);
    const Face faces[4] = {{60, -23, ps ? kPsFace : IM_COL32(62, 168, 58, 255), IM_COL32(132, 178, 240, 255), "A"},
                           {76, -39, ps ? kPsFace : IM_COL32(206, 48, 44, 255), IM_COL32(238, 92, 92, 255), "B"},
                           {43, -39, ps ? kPsFace : IM_COL32(46, 104, 206, 255), IM_COL32(226, 134, 190, 255), "X"},
                           {60, -55, ps ? kPsFace : IM_COL32(226, 180, 34, 255), IM_COL32(80, 206, 164, 255), "Y"}};
    for (int i = 0; i < 4; i++) {
        const Face& f = faces[i];
        ImVec2 c = at(f.x, f.y);
        float r = 6.6f * u, m = 3 * u;
        dl->AddCircleFilled(c, r, f.box, 20);
        if (!ps) {
            Text(dl, 8 * u, c.x, c.y - 4.2f * u, kWhite, f.letter, 0.5f);
        } else if (i == 0) {
            dl->AddLine(ImVec2(c.x - m, c.y - m), ImVec2(c.x + m, c.y + m), f.mark, 1.2f * u);
            dl->AddLine(ImVec2(c.x + m, c.y - m), ImVec2(c.x - m, c.y + m), f.mark, 1.2f * u);
        } else if (i == 1) {
            dl->AddCircle(c, m, f.mark, 14, 1.2f * u);
        } else if (i == 2) {
            dl->AddRect(ImVec2(c.x - m, c.y - m), ImVec2(c.x + m, c.y + m), f.mark, 0, 0, 1.2f * u);
        } else {
            dl->AddTriangle(ImVec2(c.x, c.y - m - u), ImVec2(c.x + m, c.y + m - u), ImVec2(c.x - m, c.y + m - u), f.mark, 1.2f * u);
        }
    }
    auto words = [&](float x, float y, float align, const char* first, const char* second, ImVec2 to) {
        constexpr ImU32 kLine = IM_COL32(214, 218, 228, 210);
        float lx = cx + x * u, ly = top + y * u;
        Text(dl, 10 * u, lx, ly, kWhite, first, align);
        if (second) Text(dl, 10 * u, lx, ly + 10 * u, kWhite, second, align);
        ImVec2 from = align == 0.5f ? ImVec2(lx, ly + (second ? 22 : 12) * u) : ImVec2(lx + (align == 1 ? 4 : -4) * u, ly + (second ? 10 : 6) * u);
        dl->AddLine(from, to, kLine, 1.1f * u);
        dl->AddCircleFilled(to, 1.8f * u, kWhite, 10);
    };
    words(-96, 2, 0.5f, "SWITCH CLUBS", "(TRIGGERS)", at(-60, -84));
    words(-26, 2, 0.5f, "REVIEW HOLE", "FLY-BY", view);
    words(42, 2, 0.5f, "PAUSE", nullptr, menu);
    words(-109, 34, 1, "POWER BOOST", nullptr, at(-68, -74));
    words(-109, 98, 1, "SWING CLUB /", "SPIN DIRECTION", left_stick);
    words(-109, 170, 1, "MOVE AIMING", "MARKER", pad);
    words(109, 30, 0, "SPIN CONTROL", nullptr, at(68, -74));
    words(109, 50, 0, "RESET TARGETING", "MARK", at(60, -55));
    words(109, 80, 0, "CHANGE SHOT TYPE /", "REPLAY SHOT", at(43, -39));
    words(109, 110, 0, "ZOOM TO TARGET /", "MULLIGAN", at(76, -39));
    words(109, 140, 0, "CHANGE CAMERA", "VIEW", at(60, -23));
    words(109, 170, 0, "SWING CLUB /", "SPIN DIRECTION", right_stick);
}

// where a key sits on the drawn keyboard, in key widths
struct KeySpot {
    float x, y, w;
};
bool FindKey(const std::string& name, KeySpot& out) {
    static const struct {
        const char* name;
        KeySpot at;
    } kNamed[] = {{"Esc", {0, 0, 1}},       {"F1", {2, 0, 1}},       {"Grave", {0, 1, 1}},  {"Backspace", {13, 1, 2}}, {"Tab", {0, 2, 1.5f}},
                  {"Enter", {12.75f, 3, 2.25f}}, {"LShift", {0, 4, 2.25f}}, {"RShift", {12.25f, 4, 0.75f}}, {"LCtrl", {0, 5, 1.5f}}, {"Space", {3.75f, 5, 6.25f}},
                  {"Up", {13, 4, 1}},       {"Left", {12, 5, 1}},    {"Down", {13, 5, 1}},  {"Right", {14, 5, 1}}};
    for (const auto& k : kNamed)
        if (name == k.name) return out = k.at, true;
    static const struct {
        const char* keys;
        float x, y;
    } kRows[] = {{"1234567890", 1, 1}, {"QWERTYUIOP", 1.5f, 2}, {"ASDFGHJKL", 1.75f, 3}, {"ZXCVBNM", 2.25f, 4}};
    if (name.size() == 1)
        for (const auto& r : kRows)
            if (const char* c = std::strchr(r.keys, name[0])) return out = {r.x + float(c - r.keys), r.y, 1}, true;
    return false;
}

void DrawKeyboard(ImDrawList* dl, float u, float cx, float top) {
    constexpr ImU32 kBody = IM_COL32(26, 28, 36, 255), kEdge = IM_COL32(120, 124, 140, 255), kPart = IM_COL32(58, 60, 72, 255);
    constexpr float kKey = 10, kGap = 0.9f;
    float x0 = cx - 75 * u, y0 = top + 46 * u, wide = 15 * kKey * u, high = 6 * kKey * u;
    dl->AddRectFilled(ImVec2(x0 - 4 * u, y0 - 4 * u), ImVec2(x0 + wide + 4 * u, y0 + high + 4 * u), kBody, 4 * u);
    dl->AddRect(ImVec2(x0 - 4 * u, y0 - 4 * u), ImVec2(x0 + wide + 4 * u, y0 + high + 4 * u), kEdge, 4 * u, 0, 1.1f * u);
    auto cap = [&](const KeySpot& k, ImU32 col) {
        dl->AddRectFilled(ImVec2(x0 + k.x * kKey * u + kGap * u, y0 + k.y * kKey * u + kGap * u),
                          ImVec2(x0 + (k.x + k.w) * kKey * u - kGap * u, y0 + (k.y + 1) * kKey * u - kGap * u), col, 1.5f * u);
    };
    static const KeySpot kPlain[] = {{0, 0, 1},     {2, 0, 1},   {3, 0, 1},   {4, 0, 1},   {5, 0, 1},   {7, 0, 1},   {8, 0, 1},   {9, 0, 1},    {10, 0, 1},    {12, 0, 1},   {13, 0, 1},
                                     {14, 0, 1},    {0, 1, 13},  {13, 1, 2},  {0, 2, 1.5f}, {1.5f, 2, 13}, {14.5f, 2, 0.5f}, {0, 3, 1.75f}, {1.75f, 3, 11}, {12.75f, 3, 2.25f},
                                     {0, 4, 2.25f}, {2.25f, 4, 10}, {13, 4, 1}, {0, 5, 1.5f}, {1.5f, 5, 2.25f}, {3.75f, 5, 6.25f}, {10, 5, 2}, {12, 5, 1},  {13, 5, 1},   {14, 5, 1}};
    for (const KeySpot& k : kPlain) {
        if (k.w < 3) {
            cap(k, kPart);
        } else if (k.w == 6.25f) {
            cap(k, kPart);
        } else {
            for (float i = 0; i < k.w; i += 1) cap({k.x + i, k.y, 1}, kPart);
        }
    }
    auto point = [&](const KeySpot& k) { return ImVec2(x0 + (k.x + k.w / 2) * kKey * u, y0 + (k.y + 0.5f) * kKey * u); };
    // words at x, y (align 0 from it, 1 to it, 0.5 about it), their line to a point: each place is chosen so that no two
    // lines cross and none crosses the mouse, taking the bindings' keys where the defaults put them
    auto words = [&](float x, float y, float align, const std::string& key, const char* first, const char* second, ImVec2 to) {
        constexpr ImU32 kLine = IM_COL32(214, 218, 228, 210);
        float lx = cx + x * u, ly = top + y * u;
        Text(dl, 11 * u, lx, ly, kOrangeLit, key.c_str(), align);
        Text(dl, 10 * u, lx, ly + 10.5f * u, kWhite, first, align);
        if (second) Text(dl, 10 * u, lx, ly + 20.5f * u, kWhite, second, align);
        if (to.x == 0) return;
        ImVec2 from = align == 0.5f ? ImVec2(lx, ly + (second ? 32 : 22) * u) : ImVec2(lx + (align == 1 ? 4 : -4) * u, ly + 10 * u);
        dl->AddLine(from, to, kLine, 1.1f * u);
        dl->AddCircleFilled(to, 1.8f * u, kWhite, 10);
    };
    auto bound = [&](keys::Action a) {
        KeySpot k;
        std::string key = keys::FirstKey(a);
        if (!FindKey(key, k)) return ImVec2(0, 0);
        cap(k, kOrange);
        return point(k);
    };
    // over the keyboard, by the keys' order along it
    KeySpot f1{2, 0, 1};
    cap(f1, kOrange);
    words(-80, 0, 0.5f, KeyWords(keys::FirstKey(keys::kX)), "CHANGE SHOT TYPE /", "REPLAY SHOT", bound(keys::kX));
    words(10, 0, 0.5f, "F1", "QUICK MENU", nullptr, point(f1));
    words(90, 0, 0.5f, KeyWords(keys::FirstKey(keys::kBlack)), "SPIN CONTROL", nullptr, bound(keys::kBlack));
    // beside it, by the keys' order down it
    words(-109, 36, 1, KeyWords(keys::FirstKey(keys::kStart)), "PAUSE", nullptr, bound(keys::kStart));
    words(-109, 61, 1, KeyWords(keys::FirstKey(keys::kBack)), "REVIEW HOLE FLY-BY", nullptr, bound(keys::kBack));
    words(-109, 86, 1, KeyWords(keys::FirstKey(keys::kWhite)), "POWER BOOST", nullptr, bound(keys::kWhite));
    words(-109, 111, 1, KeyWords(keys::FirstKey(keys::kLeftTrigger)), "SWITCH CLUBS", nullptr, bound(keys::kLeftTrigger));
    words(109, 36, 0, KeyWords(keys::FirstKey(keys::kB)), "ZOOM TO TARGET /", "MULLIGAN", bound(keys::kB));
    words(109, 78.5f, 0, KeyWords(keys::FirstKey(keys::kA)), "CHANGE CAMERA VIEW", nullptr, bound(keys::kA));
    KeySpot arrows{14, 5, 1};
    cap({13, 4, 1}, kOrange), cap({12, 5, 1}, kOrange), cap({13, 5, 1}, kOrange), cap(arrows, kOrange);
    words(109, 111, 0, "ARROWS", "MOVE AIMING MARKER", nullptr, point(arrows));

    // under it the mouse, its buttons lit, and the swing's two strokes on either side
    float mx = cx, my = top + 172 * u, mw = 15 * u, mh = 26 * u, split = my - 8 * u, round = 15 * u;
    dl->AddRectFilled(ImVec2(mx - mw, my - mh), ImVec2(mx + mw, my + mh), kBody, round);
    dl->AddRectFilled(ImVec2(mx - mw, my - mh), ImVec2(mx, split), kOrange, round, ImDrawFlags_RoundCornersTopLeft);
    dl->AddRectFilled(ImVec2(mx, my - mh), ImVec2(mx + mw, split), kOrange, round, ImDrawFlags_RoundCornersTopRight);
    dl->AddRect(ImVec2(mx - mw, my - mh), ImVec2(mx + mw, my + mh), kEdge, round, 0, 1.1f * u);
    dl->AddLine(ImVec2(mx, my - mh), ImVec2(mx, split), kBody, 1.4f * u);
    dl->AddLine(ImVec2(mx - mw, split), ImVec2(mx + mw, split), kBody, 1.4f * u);
    words(-56, 142, 1, "LEFT CLICK", "SWING CLUB (HOLD)", nullptr, ImVec2(mx - mw / 2, my - mh / 2 - 4 * u));
    words(56, 142, 0, KeyWords(keys::FirstKey(keys::kY)), "RESET TARGETING MARK", nullptr, ImVec2(mx + mw / 2, my - mh / 2 - 4 * u));
    auto stroke = [&](float x, float dir) {  // an arrow up (-1) or down (1) beside the mouse
        float px = cx + x * u, y = my + 6 * u, half = 12 * u, head = 6 * u;
        dl->AddLine(ImVec2(px, y - dir * half), ImVec2(px, y + dir * (half - head)), kOrangeLit, 2.4f * u);
        dl->AddTriangleFilled(ImVec2(px, y + dir * (half + 3 * u)), ImVec2(px - head, y + dir * (half - head)), ImVec2(px + head, y + dir * (half - head)), kOrangeLit);
    };
    stroke(-36, 1);
    stroke(36, -1);
    words(-56, 170, 1, "DRAG DOWN", "BACKSWING", nullptr, ImVec2(0, 0));
    words(56, 170, 0, "DRAG UP", "DOWNSWING", nullptr, ImVec2(0, 0));
}

void DrawControls(ImDrawList* dl, float u, float cx, float top) {
    int look = gpu::pack::g_style.load();
    if (look == 3) DrawKeyboard(dl, u, cx, top);
    else DrawPad(dl, u, cx, top, look == 1);
}

void Draw() {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 size = ImGui::GetIO().DisplaySize;
    float u = size.y / 480.0f, cx = size.x / 2;  // u: a console line
    app::Settings& s = app::Get();

    // a box in the middle of the screen, the height of the longest page so that it holds still between them
    constexpr size_t kMostRows = 7;
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
    Text(dl, 13 * u, cx - side * 0.62f, mid - 7.5f * u, kDim, kPageNames[(g.page + kPageCount - 1) % kPageCount], 0.5f);
    Text(dl, 13 * u, cx + side * 0.62f, mid - 7.5f * u, kDim, kPageNames[(g.page + 1) % kPageCount], 0.5f);

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
            case Kind::Filtering: Text(dl, 16 * u, value, ty, val, kFilterNames[FilteringNow()]); break;
            case Kind::Prompts: Text(dl, 16 * u, value, ty, val, kPromptNames[g_tw_prompts % 4]); break;
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

    if (g.page == kControlsPage) DrawControls(dl, u, cx, first);

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
    int look = gpu::pack::g_style.load();
    if (look == 3) {  // the keys as words
        Text(dl, 12 * u, left + 12 * u, fy - 6 * u, kWhite, "ESC  BACK");
        Text(dl, 12 * u, cx - 40 * u, fy - 6 * u, kWhite, "ENTER  SELECT");
        Text(dl, 12 * u, right - 12 * u, fy - 6 * u, kWhite, "Q / E  PAGE", 1);
    } else {
        bool ps = look == 1;
        constexpr ImU32 kDark = IM_COL32(44, 46, 54, 255);
        Glyph(dl, u, left + 18 * u, fy, ps ? kDark : IM_COL32(190, 34, 30, 255), ps ? "O" : "B", "Back");
        Glyph(dl, u, cx - 40 * u, fy, ps ? kDark : IM_COL32(40, 150, 50, 255), ps ? "X" : "A", "Select");
        Text(dl, 12 * u, right - 12 * u, fy - 6 * u, kWhite, ps ? "L1 / R1  PAGE" : "LB / RB  PAGE", 1);
    }
}

}  // namespace

// after Open and Close, what the runtime asks
const unsigned char* TW_TitleFont() { return kTitleFont; }

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
        case VK_UP: case 'W': bit = kUp; break;
        case VK_DOWN: case 'S': bit = kDown; break;
        case VK_LEFT: case 'A': bit = kLeft; break;
        case VK_RIGHT: case 'D': bit = kRight; break;
        case VK_RETURN: case VK_SPACE: bit = kA; break;
        case VK_ESCAPE: case VK_BACK: bit = kB; break;
        case 'Q': bit = kPrev; break;
        case 'E': bit = kNext; break;
        default: return false;
    }
    g_keys |= bit;
    TW_GlyphsKey();
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
