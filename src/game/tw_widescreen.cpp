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
bool Place(const gpu::State&, float x0, float x1, float, float, float& a, float& b) {
    if (!app::Widescreen() || (x0 < -0.97f && x1 > 0.97f)) return false;
    a = (4.0f / 3.0f) / app::FrameAspect();
    b = 0;
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
bool g_tw_haze = true, g_tw_bloom = true, g_tw_focus = true, g_tw_ao = true;
float g_tw_haze_amount = 0.5f, g_tw_bloom_amount = 0.5f;

void TW_ApplyScene() {
    gpu::g_haze_most = g_tw_haze_amount * 0.76f;
    gpu::g_bloom_glow = g_tw_bloom_amount * 1.6f;
    gpu::g_focus_blur = 0.05f * 0.016f;
    gpu::g_ao_strength = 0.35f * 3;
    gpu::g_scene_pass = (g_tw_haze ? 1 : 0) | (g_tw_bloom ? 2 : 0) | (g_tw_focus ? 4 : 0) | (g_tw_ao ? 16 : 0);
}

void TW_Sample();  // tw_sample.cpp, test only

void TW_Window(void*) {
    TW_Sample();
    gpu::g_viewport_hook = Viewport;
    gpu::g_scissor_hook = Scissor;
    const char* pass = std::getenv("TW_SCENE_PASS");  // testing: start in that view
    TW_ApplyScene();
    if (pass) gpu::g_scene_pass = std::atoi(pass);
    gpu::g_projection_hook = Projection;
    gpu::g_ortho_place = Place;
    if (app::Widescreen()) {
        gpu::g_wide_targets = true;
        gpu::g_program_widen = (4.0f / 3.0f) / app::FrameAspect();
    }
    LOGI("tw: frame shape %.3f", app::FrameAspect());
}
