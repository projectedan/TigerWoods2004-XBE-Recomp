// Tiger Woods PGA Tour 2004: host versions of XDK 5558 D3D calls the SDK does not replace.
#include <cstdint>

#include "core/log.h"
#include "kernel/memory.h"
#include "xdk/hle.h"
#include "xdk/layout.h"

using kernel::R32;
using kernel::W32;

// The game records each frame between these two (0xFC510, 0xFC530) into one of two 1.5 MB buffers, then runs it
// (0xFC5A7). The D3D calls here draw at once, so a frame needs no recording.
X86_FUNC(sub_0012B640) {  // D3DDevice_BeginPushBuffer(buffer)
    (void)base;
    ::kernel::Return(ctx, 0, 1);
}
X86_FUNC(sub_0012B6B0) {  // D3DDevice_EndPushBuffer()
    (void)base;
    ::kernel::Return(ctx, 0, 0);
}

// D3DDevice_Reset(params): the game calls it before its movies (0xFB23A) with the same 640x480 mode. The host window
// and targets do not change, and the original would free device surfaces the SDK does not keep in guest memory.
X86_FUNC(sub_00128580) {
    (void)base;
    LOG_FIRST(4, "tw: D3DDevice_Reset ignored");
    ::kernel::Return(ctx, 0, 1);
}

// (push buffer, fixup). Reports what it is given: the frame buffers above have nothing to run; 0xF98F3 runs
// prebuilt ones, not interpreted yet.
HLE_STDCALL(D3DDevice_RunPushBuffer, 2) {
    LOG_FIRST(8, "tw: RunPushBuffer %08X fixup %08X (data %08X size %08X)", a[0], a[1], a[0] ? R32(a[0] + 4) : 0,
              a[0] ? R32(a[0] + 8) : 0);
    return 0;
}
HLE_STDCALL(D3DDevice_KickPushBuffer, 0) { return 0; }

// (type, callback, context): run when the GPU reaches it. Nothing runs behind the host renderer, so it runs now.
HLE_STDCALL(D3DDevice_InsertCallback, 3) {
    if (a[1]) ::kernel::CallGuest(ctx, a[1], {a[2]});  // void __cdecl callback(DWORD context)
    return 0;
}

// (D3DPIXELSHADERDEF*): a shader set straight from its definition (as in The Two Towers' port)
HLE_STDCALL(D3DDevice_SetPixelShaderProgram, 1) {
    static uint32_t holder = 0;
    if (!a[0]) return ::kernel::CallGuest(ctx, SYM(D3DDevice_SetPixelShader), {0u});
    if (!holder) holder = mem::AllocSystem(16);
    W32(holder + XDK_PixelShader_Definition, a[0]);
    return ::kernel::CallGuest(ctx, SYM(D3DDevice_SetPixelShader), {holder});
}
