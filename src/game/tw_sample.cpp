// Testing only: TW_SAMPLE=<first second>,<last second> samples every thread's instruction pointer and writes the
// busiest addresses to tw_sample.txt beside the exe. Nothing happens without the variable.
#include <windows.h>

#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

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
