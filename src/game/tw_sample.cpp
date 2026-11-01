// Testing only: TW_SAMPLE=<first second>,<last second> samples every thread's instruction pointer and writes the
// busiest addresses to tw_sample.txt beside the exe. Nothing happens without the variable.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "kernel/kernel.h"

void TW_Sample() {
    const char* e = std::getenv("TW_SAMPLE");
    float first = 0, last = 0;
    if (!e || std::sscanf(e, "%f,%f", &first, &last) != 2) return;
    struct Args { float first, last; };
    static Args args{first, last};
    CreateThread(nullptr, 0, [](void*) -> DWORD {
        DWORD me = GetCurrentThreadId();
        Sleep(DWORD(args.first * 1000));
        std::map<DWORD, std::map<uint64_t, int>> hits;
        std::map<DWORD, int> total;
        for (ULONGLONG end = GetTickCount64() + ULONGLONG((args.last - args.first) * 1000); GetTickCount64() < end;) {
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            THREADENTRY32 te{sizeof te};
            for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
                if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == me) continue;
                HANDLE t = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
                if (!t) continue;
                if (SuspendThread(t) != DWORD(-1)) {
                    CONTEXT c{};
                    c.ContextFlags = CONTEXT_CONTROL;
                    if (GetThreadContext(t, &c)) {
                        hits[te.th32ThreadID][c.Rip]++;
                        total[te.th32ThreadID]++;
                    }
                    ResumeThread(t);
                }
                CloseHandle(t);
            }
            CloseHandle(snap);
            Sleep(1);
        }
        FILE* f = std::fopen("tw_sample.txt", "w");
        fprintf(f, "base %p\n", GetModuleHandleW(nullptr));
        for (auto& [id, m] : hits) {
            std::vector<std::pair<int, uint64_t>> v;
            for (auto& [rip, c] : m) v.push_back({c, rip});
            std::sort(v.rbegin(), v.rend());
            fprintf(f, "thread %lu samples %d\n", id, total[id]);
            for (size_t i = 0; i < v.size() && i < 6; i++) fprintf(f, "  %d %llx\n", v[i].first, (unsigned long long)v[i].second);
        }
        std::fclose(f);
        return 0;
    }, nullptr, 0, nullptr);
}

// Testing only: TW_TEXT=<second>,<second>,... lists the guest's strings about the console to tw_text.txt at those times
void TW_TextScan() {
    static const char* e = std::getenv("TW_TEXT");
    if (!e) return;
    CreateThread(nullptr, 0, [](void*) -> DWORD {
        FILE* f = std::fopen("tw_text.txt", "w");
        std::map<std::string, uint32_t> seen;
        ULONGLONG t0 = GetTickCount64();
        for (const char* p = e; *p;) {
            float at = float(std::atof(p));
            while (GetTickCount64() - t0 < ULONGLONG(at * 1000)) Sleep(100);
            const uint8_t* b = kernel::g_base + 0x80000000u;
            constexpr uint32_t kSize = 64u << 20;
            auto look = [&](std::string t, uint32_t where, char kind) {
                std::string low = t;
                for (char& c : low) c = char(std::tolower(uint8_t(c)));
                for (const char* w : {"xbox", "memory unit", "hard disk", "controller", "dashboard", "blocks", "storage"})
                    if (low.find(w) != std::string::npos && seen.emplace(t, where).second) {
                        for (char& c : t) if (c == '\n') c = '|';
                        fprintf(f, "%c %.0f %08X %s\n", kind, at, where, t.c_str());
                        fflush(f);
                        break;
                    }
            };
            std::string run;
            for (uint32_t i = 0; i < kSize; i++) {
                uint8_t c = b[i];
                if ((c >= 32 && c < 127) || c == 10) { run += char(c); continue; }
                if (run.size() >= 8) look(run, i - uint32_t(run.size()), 'A');
                run.clear();
            }
            run.clear();
            for (uint32_t i = 0; i + 1 < kSize; i += 2) {
                uint8_t c = b[i];
                if (((c >= 32 && c < 127) || c == 10) && !b[i + 1]) { run += char(c); continue; }
                if (run.size() >= 8) look(run, i - uint32_t(run.size()) * 2, 'U');
                run.clear();
            }
            while (*p && *p != ',') p++;
            if (*p) p++;
        }
        std::fclose(f);
        return 0;
    }, nullptr, 0, nullptr);
}
