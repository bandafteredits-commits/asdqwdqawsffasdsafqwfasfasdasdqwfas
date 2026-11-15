// cplatency.cpp - CheesyProxxy "Better reg" DLL for Minecraft Bedrock (input latency / frame pacing).
// Process-level timing and scheduling tweaks ONLY. It does not touch the FPS counter, reach, attack
// speed, hit detection, rod behaviour or any packet, and it hooks/patches nothing, so it can sit next to
// other client DLLs (Oderso etc.) without conflicting.
//   - 0.5 ms system timer resolution (finer sleeps/waits -> steadier frame pacing and input wakeups)
//   - opts the process out of Windows power / timer-resolution throttling
//   - ABOVE_NORMAL process priority (never REALTIME)
// Switched on/off from CheesyProxxy -> Advanced tools -> "Better reg" via C:\Users\Public\CheesyProxxy\latency.cfg
// (8 bytes: magic 0x4354414C, then 1 = on / 0 = off). Polled 4x/second; OFF restores everything.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG (NTAPI* NtSetTimerResolution_t)(ULONG, BOOLEAN, PULONG);
static NtSetTimerResolution_t pNtSet = nullptr;
static volatile LONG g_stop = 0;
static HANDLE g_thread = nullptr;
static const wchar_t* kCfg = L"C:\\Users\\Public\\CheesyProxxy\\latency.cfg";

static bool WantOn() {
    HANDLE f = CreateFileW(kCfg, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD buf[2] = {0, 0}, got = 0;
    BOOL ok = ReadFile(f, buf, sizeof(buf), &got, nullptr);
    CloseHandle(f);
    return ok && got >= 8 && buf[0] == 0x4354414C && buf[1] == 1;
}

static void Throttling(bool on) {
    PROCESS_POWER_THROTTLING_STATE s = {};
    s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    if (on) { // on = we take control and DISABLE throttling
        s.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        s.StateMask = 0;
    } // off = ControlMask 0 hands control back to the system
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof(s));
}

static void Apply(bool on, DWORD oldPrio) {
    ULONG cur;
    if (pNtSet) pNtSet(5000, on ? TRUE : FALSE, &cur); // 5000 x 100 ns = 0.5 ms
    Throttling(on);
    if (on) { if (oldPrio == NORMAL_PRIORITY_CLASS) SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS); }
    else if (oldPrio) SetPriorityClass(GetCurrentProcess(), oldPrio);
}

static DWORD WINAPI Worker(LPVOID) {
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    if (nt) pNtSet = (NtSetTimerResolution_t)GetProcAddress(nt, "NtSetTimerResolution");
    DWORD oldPrio = GetPriorityClass(GetCurrentProcess());
    bool active = false;
    int tick = 0;
    while (!g_stop) {
        bool want = WantOn();
        if (want && !active) { Apply(true, oldPrio); active = true; tick = 0; }
        else if (!want && active) { Apply(false, oldPrio); active = false; }
        else if (active && ++tick >= 20) { Apply(true, oldPrio); tick = 0; } // re-assert every ~5 s if something resets it
        Sleep(250);
    }
    if (active) Apply(false, oldPrio);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        g_thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_stop, 1);
    }
    return TRUE;
}
