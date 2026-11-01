// The game's button pictures, drawn afresh in the look of the controller in use. Made on the player's PC from the
// textures the game loaded, known by their pictures: 64 x 64 sheets of four, each button drawn over in its own place.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "app/app.h"
#include "core/input.h"
#include "gpu/texture_pack.h"

namespace {

constexpr uint32_t kSize = 64, kBigger = 4;
constexpr int kStylePlayStation = 1;
constexpr float kRoot = 0.70710678f;

enum class Button { A, B, X, Y, Black };
struct Place {
    float x, y, wide, high;  // middle and half sizes, in the game's texels
};
constexpr Place kUpperLeft{15.5f, 16.0f, 11, 13.5f}, kUpperRight{47.5f, 15.5f, 11, 11}, kLowerLeft{15.5f, 47.5f, 14, 14},
                kLowerRight{47.5f, 47.5f, 10, 10};

float Cover(float distance) { return std::clamp(0.5f - distance * float(kBigger), 0.0f, 1.0f); }

float Segment(float x, float y, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay, t = std::clamp(((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
    return std::hypot(x - ax - t * dx, y - ay - t * dy);
}

// the nearest of a run of strokes: x, y pairs, each stroke from one pair to the next
struct Stroke {
    float x, y;
    bool joined;  // from the point before
};
template <size_t N>
float Strokes(float x, float y, const Stroke (&line)[N]) {
    float d = 1e9f;
    for (size_t i = 1; i < N; i++)
        if (line[i].joined) d = std::min(d, Segment(x, y, line[i - 1].x, line[i - 1].y, line[i].x, line[i].y));
    return d;
}

// a letter's strokes, in a button of radius 1
float Letter(float x, float y, Button b) {
    static constexpr Stroke kA[] = {{-0.42f, 0.5f, false}, {0, -0.5f, true}, {0.42f, 0.5f, true}, {-0.25f, 0.15f, false}, {0.25f, 0.15f, true}};
    static constexpr Stroke kB[] = {{-0.32f, 0.5f, false}, {-0.32f, -0.5f, true}, {0.12f, -0.5f, true}, {0.3f, -0.32f, true}, {0.3f, -0.16f, true},
                                    {0.12f, 0, true},      {-0.32f, 0, false},    {0.16f, 0, true},     {0.36f, 0.18f, true}, {0.36f, 0.32f, true},
                                    {0.16f, 0.5f, true},   {-0.32f, 0.5f, true}};
    static constexpr Stroke kX[] = {{-0.38f, -0.48f, false}, {0.38f, 0.48f, true}, {0.38f, -0.48f, false}, {-0.38f, 0.48f, true}};
    static constexpr Stroke kY[] = {{-0.4f, -0.5f, false}, {0, 0, true}, {0.4f, -0.5f, true}, {0, 0, false}, {0, 0.5f, true}};
    switch (b) {
        case Button::A: return Strokes(x, y, kA);
        case Button::B: return Strokes(x, y, kB);
        case Button::X: return Strokes(x, y, kX);
        case Button::Y: return Strokes(x, y, kY);
        default: return 1e9f;
    }
}

// a shoulder or trigger's name in a button of radius 1: "L" or "R" and its number, or on Xbox its "B" or "T"
float Name(float x, float y, bool right, bool second, bool playstation) {
    static constexpr Stroke kL[] = {{-0.52f, -0.42f, false}, {-0.52f, 0.42f, true}, {-0.14f, 0.42f, true}};
    static constexpr Stroke kR[] = {{-0.52f, 0.42f, false}, {-0.52f, -0.42f, true}, {-0.18f, -0.42f, true}, {-0.08f, -0.28f, true}, {-0.08f, -0.14f, true},
                                    {-0.18f, 0, true},      {-0.52f, 0, true},      {-0.2f, 0, false},      {-0.08f, 0.42f, true}};
    static constexpr Stroke k1[] = {{0.2f, -0.28f, false}, {0.36f, -0.42f, true}, {0.36f, 0.42f, true}};
    static constexpr Stroke k2[] = {{0.16f, -0.3f, false}, {0.3f, -0.42f, true}, {0.46f, -0.3f, true}, {0.16f, 0.42f, true}, {0.5f, 0.42f, true}};
    static constexpr Stroke kBumper[] = {{0.14f, 0.42f, false}, {0.14f, -0.42f, true}, {0.42f, -0.42f, true}, {0.54f, -0.3f, true}, {0.54f, -0.14f, true},
                                         {0.42f, 0, true},      {0.14f, 0, false},     {0.44f, 0, true},      {0.58f, 0.14f, true}, {0.58f, 0.3f, true},
                                         {0.44f, 0.42f, true},  {0.14f, 0.42f, true}};
    static constexpr Stroke kTrigger[] = {{0.1f, -0.42f, false}, {0.6f, -0.42f, true}, {0.35f, -0.42f, false}, {0.35f, 0.42f, true}};
    float second_mark = playstation ? (second ? Strokes(x, y, k2) : Strokes(x, y, k1)) : second ? Strokes(x, y, kTrigger) : Strokes(x, y, kBumper);
    return std::min(right ? Strokes(x, y, kR) : Strokes(x, y, kL), second_mark);
}

// a PlayStation button's mark, in a button of radius 1
float Symbol(float x, float y, Button b) {
    switch (b) {
        case Button::A: return std::min(Segment(x, y, -0.4f, -0.4f, 0.4f, 0.4f), Segment(x, y, 0.4f, -0.4f, -0.4f, 0.4f));
        case Button::B: return std::fabs(std::hypot(x, y) - 0.5f);
        case Button::X: return std::fabs(std::max(std::fabs(x), std::fabs(y)) - 0.44f);
        case Button::Y:
            return std::min({Segment(x, y, 0, -0.6f, 0.56f, 0.42f), Segment(x, y, 0.56f, 0.42f, -0.56f, 0.42f), Segment(x, y, -0.56f, 0.42f, 0, -0.6f)});
        default: return 1e9f;
    }
}

struct Colour {
    float r, g, b;
};

// over the pixel at p, by its coverage
void Over(uint8_t* p, const Colour& col, float cover) {
    const float c[3] = {col.r, col.g, col.b};
    float was = p[3] / 255.0f, a = cover + was * (1 - cover);
    for (int i = 0; i < 3; i++) p[i] = a > 0 ? uint8_t((c[i] * cover + p[i] * was * (1 - cover)) / a + 0.5f) : 0;
    p[3] = uint8_t(a * 255 + 0.5f);
}

// the button's face and what is drawn on it
void Look(Button b, bool playstation, Colour& face, Colour& ink) {
    static constexpr Colour kDark{52, 54, 62}, kWhite{240, 242, 246};
    face = kDark;
    ink = kWhite;
    if (playstation) {
        if (b == Button::A) ink = {132, 178, 240};
        if (b == Button::B) ink = {238, 92, 92};
        if (b == Button::X) ink = {226, 134, 190};
        if (b == Button::Y) ink = {80, 206, 164};
        return;
    }
    if (b == Button::A) face = {62, 168, 58};
    if (b == Button::B) face = {206, 48, 44};
    if (b == Button::X) face = {46, 104, 206};
    if (b == Button::Y) face = {226, 180, 34};
    if (b == Button::Y) ink = {40, 36, 24};
}

// the button, over what the game drew at that place
void Draw(uint8_t* out, uint32_t W, const Place& at, Button b, bool playstation) {
    constexpr float kEdge = 2.5f, kStroke = 0.115f, kRim = 0.9f;
    constexpr Colour kRimColour{150, 154, 166};
    int x0 = std::max(int((at.x - at.wide - kEdge) * kBigger), 0), x1 = std::min(int((at.x + at.wide + kEdge) * kBigger), int(W));
    int y0 = std::max(int((at.y - at.high - kEdge) * kBigger), 0), y1 = std::min(int((at.y + at.high + kEdge) * kBigger), int(W));
    float tall = at.high / at.wide, r = at.wide;  // some of the game's are stretched, as they are drawn narrowed
    Colour face, ink;
    Look(b, playstation, face, ink);
    uint32_t source = app::Get().buttons[4];  // what plays the black button: input::kSources, its shoulders and triggers
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint8_t* p = out + (size_t(y) * W + size_t(x)) * 4;
            p[0] = p[1] = p[2] = p[3] = 0;
            float tx = (float(x) + 0.5f) / kBigger - at.x, ty = ((float(y) + 0.5f) / kBigger - at.y) / tall;
            float d = std::hypot(tx, ty), lit = 1.08f - std::clamp((ty + r) / (2 * r), 0.0f, 1.0f) * 0.5f;
            Colour shaded{std::min(face.r * lit, 255.0f), std::min(face.g * lit, 255.0f), std::min(face.b * lit, 255.0f)};
            Over(p, kRimColour, Cover(d - r));
            Over(p, shaded, Cover(d - (r - kRim)));
            float ux = tx / r, uy = ty / r, mark = 1e9f;
            if (b == Button::Black) {
                mark = Name(ux, uy, source & 1, source == 6 || source == 7, playstation);
            } else {
                mark = playstation ? Symbol(ux, uy, b) : Letter(ux, uy, b);
            }
            Over(p, ink, Cover((mark - kStroke) * r));
        }
}

// how much of a quarter of the sheet is drawn, and its average colour
struct Patch {
    int drawn = 0;
    float r = 0, g = 0, b = 0;
};
Patch Measure(const std::vector<uint8_t>& rgba, uint32_t x0, uint32_t y0) {
    constexpr uint32_t kCell = 32;
    constexpr int kSolid = 100;
    Patch out;
    for (uint32_t y = y0; y < y0 + kCell; y++)
        for (uint32_t x = x0; x < x0 + kCell; x++) {
            const uint8_t* p = rgba.data() + (size_t(y) * kSize + x) * 4;
            if (p[3] <= kSolid) continue;
            out.drawn++;
            out.r += p[0];
            out.g += p[1];
            out.b += p[2];
        }
    if (out.drawn) out.r /= float(out.drawn), out.g /= float(out.drawn), out.b /= float(out.drawn);
    return out;
}

bool Styled(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, int style, gpu::pack::Image& out) {
    constexpr int kButton = 250;
    constexpr float kColoured = 30, kBlackMost = 80, kGreenish = 6;
    if (w != kSize || h != kSize) return false;
    Patch ul = Measure(rgba, 0, 0), ur = Measure(rgba, 32, 0), ll = Measure(rgba, 0, 32), lr = Measure(rgba, 32, 32);
    bool green = ur.drawn > kButton && ur.g > ur.r + kColoured && ur.g > ur.b + kColoured;
    bool red = ur.drawn > kButton && ur.r > ur.g + kColoured && ur.r > ur.b + kColoured;
    if (!green && !red) return false;
    if (style < 0) return true;
    bool playstation = style == kStylePlayStation;
    uint32_t W = w * kBigger, H = h * kBigger;
    std::vector<uint8_t> big(size_t(W) * H * 4);
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++)
            std::copy_n(rgba.data() + (size_t(y / kBigger) * w + x / kBigger) * 4, 4, big.data() + (size_t(y) * W + x) * 4);
    Draw(big.data(), W, kUpperRight, green ? Button::A : Button::B, playstation);
    if (ul.drawn > kButton) Draw(big.data(), W, kUpperLeft, ul.g > ul.r + kGreenish ? Button::Y : Button::X, playstation);
    if (ll.drawn > kButton && ll.g > ll.r + kColoured) Draw(big.data(), W, kLowerLeft, Button::A, playstation);  // the A on teal
    if (lr.drawn > kButton && lr.r < kBlackMost) Draw(big.data(), W, kLowerRight, Button::Black, playstation);
    out = {};
    out.width = W;
    out.height = H;
    out.format = gpu::pack::Format::RGBA8;
    out.levels.push_back(std::move(big));
    gpu::pack::MakeLevels(out);
    return true;
}

}  // namespace

uint32_t g_tw_prompts = 0;  // [input] button_prompts: 0 by the controller, 1 Xbox, 2 PlayStation

void TW_GlyphsStart() { gpu::pack::g_styled = Styled; }

void TW_GlyphsApply() {
    gpu::pack::g_style = g_tw_prompts == 2 ? kStylePlayStation : g_tw_prompts == 1 ? 0 : input::Style(0);
}

// the controller's kind, looked at now and then: it takes the input lock
void TW_GlyphsPad(int port) {
    constexpr int kEvery = 60;
    static int count = 0;
    if (!port && ++count % kEvery == 1) TW_GlyphsApply();
}
