// Tiger Woods PGA Tour 2004: the picture without the TV-safe border, and widescreen.
// The game draws its 3D into a 576 x 448 viewport inside 640 x 480 (black all round on the console, there too in the
// capture) and scales its 2D by 0.9 and 0.933 to match. The viewport opens to the whole frame. On a wide display the
// projection's horizontal term and the vertex programs' positions narrow by 4:3 / the frame's shape, so the same
// vertical view shows more at the sides, and the 2D is held at 4:3 in the middle.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/state.h"
#include "gpu/vertex.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"

bool g_tw_full_detail = true;  // [effects] full_detail

namespace {

void Viewport(gpu::Viewport& v) {
    if (v.x == 32 && v.y == 16 && v.w == 576 && v.h == 448) {
        v.x = v.y = 0;
        v.w = 640;
        v.h = 480;
    }
}

// the same border as a scissor rectangle, which the golfer and other late draws keep: they would stop short of the edge
bool Scissor(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    LOG_FIRST(6, "tw: scissor %d %d %d %d", x1, y1, x2, y2);
    return x1 == 32 && y1 == 16 && x2 == 608 && y2 == 464;
}

void Projection(float* m) {
    bool persp = m[11] > 0.99f && m[11] < 1.01f && std::fabs(m[15]) < 0.001f;
    if (!persp || m[0] == 0.0f || m[5] == 0.0f || !app::Widescreen()) return;
    float aspect = std::fabs(m[5] / m[0]);
    if (aspect > 1.25f && aspect < 1.4f) m[0] *= aspect / app::FrameAspect();
}

// 2D through an orthographic projection: held at 4:3, what covers the screen (fades) stays full
bool Place(const gpu::State&, float x0, float x1, float y0, float y1, float& a, float& b) {
    // the pause menu's banner is the submenus' over the course: known by the dimming over the frame, itself full width
    static uint64_t logo = 0;
    if (std::fabs(x1 - x0 - 2.149f) < 0.01f && std::fabs(y1 - y0 - 2.0f) < 0.01f) logo = gpu::g_frame_no;
    if (!app::Widescreen() || (x0 < -0.97f && x1 > 0.97f)) return false;
    a = (4.0f / 3.0f) / app::FrameAspect();
    b = 0;
    // on the course the HUD goes to the corners: its top and bottom strips, each already a left or a right piece, their
    // ends (the plates behind the bars, at 1) on the screen's edges as on the console; the bars run past them.
    // What crosses the middle (messages) and what lies between the strips (the marker, which follows the course) stays
    bool strip = y1 < -0.5f || y0 > 0.6f, crossing = x0 < -0.1f && x1 > 0.1f;
    // the flag's marker follows the course and can rise into a strip: its box stays, and what is drawn inside it
    static float box[4];
    static uint64_t box_frame = 0;
    uint64_t now = gpu::g_frame_no;
    float wide = x1 - x0, high = y1 - y0, across = (x0 + x1) / 2, up = (y0 + y1) / 2;
    bool marker = wide > 0.15f && wide < 0.5f && high > 0.12f && high < 0.5f && std::fabs(across) < 0.7f;
    if (marker) {
        box[0] = x0, box[1] = x1, box[2] = y0, box[3] = y1;
        box_frame = now;
    }
    marker = marker || (box_frame == now && across > box[0] && across < box[1] && up > box[2] && up < box[3]);
    bool header = now - logo <= 2 && y0 > 0.6f;  // (the strip along the foot keeps to the corners)
    // the panels over the strip's right end, the hint and the caddy's tip, go with it whole
    bool panel = x0 > 0.2f && y0 > -0.625f && y1 < -0.3f;
    if (gpu::g_in_scene && (strip || panel) && !crossing && !marker && !header) b = (x0 + x1 < 0 ? -1.0f : 1.0f) * (1 - a);
    // the main menu's content goes to the left edge: known by its swoosh, whose size holds as it slides in. Every piece
    // moves with it, wherever it has slid to; the backgrounds, wider, stay, as do the golfer and the Select prompt
    static uint64_t swoosh = 0;
    uint64_t frame = gpu::g_frame_no;
    if (std::fabs(x1 - x0 - 1.205f) < 0.01f && std::fabs(y1 - y0 - 2.071f) < 0.01f) swoosh = frame;
    bool prompt = x0 > 0.6f && y1 < -0.8f;
    if (!gpu::g_in_scene && frame - swoosh <= 2 && x1 - x0 < 1.3f && !prompt) b = a - 1;
    // the submenus' header across the whole frame: known by its EA logo. What spans the frame is already full width
    // (above); the banner's other pieces join it, the bubbles among them whatever width they have scrolled to, and the
    // logo and the title go to their sides. The tab row stays at 4:3: the plates at its ends fade the labels there
    bool badge = std::fabs(x1 - x0 - 0.228f) < 0.006f && std::fabs(y1 - y0 - 0.246f) < 0.006f && x0 < -0.9f;
    if (badge) logo = frame;
    if (frame - logo <= 2 && y0 > 0.65f) {
        float middle = (x0 + x1) / 2;
        bool bubbles = y1 - y0 > 0.125f && y1 - y0 < 0.135f, title = middle > 0.2f && x0 >= 0.3f;
        if (badge || (title && !bubbles)) {
            b = (middle < 0 ? -1.0f : 1.0f) * (1 - a);
        } else if (bubbles || x1 - x0 >= 0.15f) {
            a = 1;
            b = 0;
        }
    }
    // the tab row's outer plates: their inner ends stay where they fade the labels, their outer ends reach the screen's
    if (frame - logo <= 2 && y0 > 0.6f && y0 <= 0.65f && (x0 < -1.1f || x1 > 1.1f) &&
        x1 - x0 < 1) {
        bool left = x0 < 0;
        float inner = left ? x1 : x0, outer = left ? x0 : x1, at = a * inner, edge = left ? -1.01f : 1.01f;
        a = (edge - at) / (outer - inner);
        b = at - a * inner;
    }
    return true;
}

}  // namespace

// the camera's side frustum planes (0x81630) come from this tangent, kept at 4:3 by the game: objects past it are not
// drawn. Only the culling moves, the picture comes from the projection above. A wider frame needs it wider.
void TW_FrustumWidth(X86Context& ctx, uint8_t* base) {
    (void)base;
    if (!app::Widescreen()) return;
    uint32_t at = ctx.esp + 0x48;
    uint32_t bits = ::kernel::R32(at);
    float tangent;
    std::memcpy(&tangent, &bits, 4);
    tangent *= app::FrameAspect() / (4.0f / 3.0f);
    std::memcpy(&bits, &tangent, 4);
    ::kernel::W32(at, bits);
}

// the camera flight's trail (0x73F60) lays the last frame over this one, read from the view's rectangle as fractions
// of the frame, here just scaled into the TV-safe border (0x73490): left and top, then right and bottom, on the stack.
// The quad fills the viewport, opened above to the whole frame, so reading the border's rectangle enlarged the last
// frame by 7% a frame, and the trail became a tunnel.
void TW_TrailSource(X86Context& ctx, uint8_t* base) {
    (void)base;
    uint32_t bits = ::kernel::R32(ctx.esp + 0x58);
    float left;
    std::memcpy(&left, &bits, 4);
    LOG_FIRST(4, "tw: trail source left %.4f", left);
    if (std::fabs(left - 0.05f) > 0.001f) return;
    const float one = 1.0f;
    std::memcpy(&bits, &one, 4);
    ::kernel::W32(ctx.esp + 0x58, 0);
    ::kernel::W32(ctx.esp + 0x5C, 0);
    ::kernel::W32(ctx.esp + 0x68, bits);
    ::kernel::W32(ctx.esp + 0x6C, bits);
}

// [effects]: the pass over the 3D (gpu::g_scene_pass), on and off and how strong, from the menu (tw_menu.cpp). Each
// amount runs 0 to 1, a half being the look first settled on; depth of field and ambient occlusion are on or off, at
// the strength judged right
void TW_GlyphsStart();  // tw_glyphs.cpp

bool g_tw_haze = true, g_tw_bloom = true, g_tw_focus = true, g_tw_ao = true;
float g_tw_haze_amount = 0.7f, g_tw_bloom_amount = 0.5f;

// each object's level of detail (0x75DC0): by its distance it gets a level (+0x1D), the next one it dissolves into
// (+0x1E) and how far through that it is (+0x20), so trees flanking a course cross over in sight. All kept at the first
// level, the full model, they never change.
void TW_ObjectDetail(X86Context& ctx, uint8_t* base) {
    if (!g_tw_full_detail) return;
    uint32_t object = ctx.ecx + ctx.ebp;
    base[object + 0x1D] = 0;
    base[object + 0x1E] = 0;
    ::kernel::W32(object + 0x20, 0);
}

void TW_ApplyScene() {
    gpu::g_haze_most = g_tw_haze_amount * 0.76f;  // at 0.7, the strength chosen with full detail trees
    gpu::g_bloom_glow = g_tw_bloom_amount * 1.6f;
    gpu::g_focus_blur = 0.05f * 0.016f;
    gpu::g_ao_strength = 0.35f * 3;
    gpu::g_scene_pass = (g_tw_haze ? 1 : 0) | (g_tw_bloom ? 2 : 0) | (g_tw_focus ? 4 : 0) | (g_tw_ao ? 16 : 0);
}

void TW_Sample();  // tw_sample.cpp, test only
void TW_TextScan();

void TW_Window(void*) {
    TW_Sample();
    TW_TextScan();
    gpu::g_viewport_hook = Viewport;
    gpu::g_scissor_hook = Scissor;
    if (const char* e = std::getenv("TW_FULL_DETAIL")) g_tw_full_detail = std::atoi(e) != 0;  // testing
    const char* pass = std::getenv("TW_SCENE_PASS");  // testing: start in that view
    TW_ApplyScene();
    if (pass) gpu::g_scene_pass = std::atoi(pass);
    gpu::g_projection_hook = Projection;
    gpu::g_ortho_place = Place;
    TW_GlyphsStart();
    if (app::Widescreen()) {
        gpu::g_wide_targets = true;
        gpu::g_program_widen = (4.0f / 3.0f) / app::FrameAspect();
    }
    LOGI("tw: frame shape %.3f", app::FrameAspect());
}
