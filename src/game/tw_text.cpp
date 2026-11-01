// The console's name out of the game's words. The sentences live compressed in FEnd.xbx and GlbData.xbx, so they are
// changed where the game unpacks them: after files open, a thread of its own goes through guest memory and rewrites
// them in place, each no longer than it was (the game's tables point at the same starts). Nothing else waits on it.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "kernel/kernel.h"

namespace {

// applied in order, within a sentence
const std::pair<const char*, const char*> kWords[] = {
    {"Please do not remove memory units or disconnect any controllers.", "Please do not close the game or disconnect controllers."},
    {"your Xbox console", "your PC"},
    {"Your Xbox", "Your PC"},
    {"select Xbox storage location", "select a storage location"},
    {"No Memory Unit found", "No storage found"},
    {"on the Xbox ", "on "},
    {"from Xbox ", "from "},
    {"on Xbox ", "on "},
    {"The Xbox ", "The "},
    {"Xbox ", ""},
    {"on the Hard Disk", "in storage"},
    {"on the hard disk", "in storage"},
    {"from the Hard Disk", "from storage"},
    {"to the Hard Disk", "to storage"},
    {"to the hard disk", "to storage"},
};

constexpr uint32_t kPhysical = 0x80000000u, kSize = 64u << 20;
constexpr uint32_t kDiskName = 0x16EBA4;  // "Hard Disk", the game's name for the save location

std::atomic<ULONGLONG> g_opened{0};

bool Letter(uint8_t c) { return (c >= 32 && c != 127) || c == 10; }  // the trademark sign among them

// the sentence around one hit, in place
void Rewrite(uint8_t* b, uint32_t hit) {
    uint32_t start = hit, end = hit;
    while (start > 0 && hit - start < 400 && Letter(b[start - 1])) start--;
    while (end < kSize && end - hit < 400 && Letter(b[end])) end++;
    if (end >= kSize || b[end] != 0) return;  // a sentence ends in a zero; anything else is not text
    std::string t(reinterpret_cast<char*>(b + start), end - start);
    if (t.find(' ') == std::string::npos || t.find(char(92)) != std::string::npos) return;  // no paths
    size_t was = t.size();
    for (const auto& [from, to] : kWords)
        for (size_t at = t.find(from); at != std::string::npos; at = t.find(from, at + std::strlen(to))) t.replace(at, std::strlen(from), to);
    if (t.size() >= was) return;
    std::memcpy(b + start, t.c_str(), t.size() + 1);
}

void Scan() {
    uint8_t* b = kernel::g_base + kPhysical;
    for (const char* needle : {"Xbox", "memory units", "Memory Unit", "Hard Disk", "hard disk"}) {
        size_t n = std::strlen(needle);
        for (uint32_t i = 0; i + n < kSize;) {
            auto* p = static_cast<uint8_t*>(std::memchr(b + i, needle[0], kSize - n - i));
            if (!p) break;
            i = uint32_t(p - b);
            if (!std::memcmp(p, needle, n)) Rewrite(b, i);
            i++;
        }
    }
}

}  // namespace

void TW_FileOpened(const char*) { g_opened = GetTickCount64(); }

// once, the game in memory
void TW_TextStart() {
    static const char kName[] = "Storage";
    uint8_t* disk = kernel::g_base + kDiskName;
    if (!std::memcmp(disk, "Hard Disk", 10)) std::memcpy(disk, kName, sizeof(kName));
    CreateThread(nullptr, 0, [](void*) -> DWORD {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        ULONGLONG done = 0;
        for (int after = 0;; Sleep(500)) {
            ULONGLONG opened = g_opened.load();
            if (opened != done) {
                done = opened;
                after = 6;  // the file is read and unpacked some time after it opens
            }
            if (after > 0 && (after-- & 1) == 0) Scan();
        }
        return 0;
    }, nullptr, 0, nullptr);
}
