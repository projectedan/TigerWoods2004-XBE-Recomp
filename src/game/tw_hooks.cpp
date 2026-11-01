// Tiger Woods PGA Tour 2004: instruction hooks ([[hook]] entries in config/hooks.toml).
#include <windows.h>

#include <cstdint>

#include "core/log.h"
#include "cpu/x86_context.h"

// XapiInitProcess: the Xbox drive authentication (0x110A52, SCSI pass-through 0x4D014) fails on PC, which on the
// console ends in XapiBootToDash (0x110C8C). Startup continues at 0x110C91 instead.
bool TW_DiscCheckFailed(X86Context& ctx, uint8_t* base) {
    (void)ctx;
    (void)base;
    LOGI("tw: no Xbox drive, continuing XAPI startup (exe at %p)", GetModuleHandleW(nullptr));
    return true;
}
