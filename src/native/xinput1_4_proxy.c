#include "win.h"
#include "modload.h"

/* Proton often ignores a game-folder dinput8.dll and keeps its builtin.
   This game always imports XINPUT1_4.dll, so a native xinput1_4 proxy is a more
   reliable way to load KofVsSelector.dll. Forwards to Wine/Windows xinput1_3
   (a different DLL name, so the override does not recurse). */

/* KOF XIII (32-bit) imports XINPUT1_3.dll instead: the same file is built as
   xinput1_3.dll with XPROXY_NAME/XPROXY_SYS set so it never loads itself. */
#ifndef XPROXY_NAME
#define XPROXY_NAME "xinput1_4.dll"
#define XPROXY_SYS L"xinput1_3.dll"
#endif

static DWORD (WINAPI *g_getstate)(DWORD, void*);
static DWORD (WINAPI *g_setstate)(DWORD, void*);
static DWORD (WINAPI *g_getcaps)(DWORD, DWORD, void*);
static void (WINAPI *g_enable)(int);
static DWORD (WINAPI *g_vs_get)(DWORD, void*);
static DWORD (WINAPI *g_vs_set)(DWORD, void*);
static HMODULE g_sys;

static void* proc(HMODULE m, const char* name, int ordinal) {
    void* p = GetProcAddress(m, name);
    if (!p) p = GetProcAddress(m, (const char*)(ksz)(unsigned)ordinal);
    return p;
}

static int load_sys(void) {
    if (g_getstate) return 1;
    g_sys = LoadLibraryW(XPROXY_SYS);
    if (!g_sys) g_sys = LoadLibraryW(L"xinput9_1_0.dll");
    if (!g_sys) return 0;
    g_getstate = (DWORD (WINAPI *)(DWORD, void*))proc(g_sys, "XInputGetState", 2);
    g_setstate = (DWORD (WINAPI *)(DWORD, void*))proc(g_sys, "XInputSetState", 3);
    g_getcaps = (DWORD (WINAPI *)(DWORD, DWORD, void*))proc(g_sys, "XInputGetCapabilities", 4);
    g_enable = (void (WINAPI *)(int))proc(g_sys, "XInputEnable", 5);
    return g_getstate != 0;
}

static void bind_selector(void) {
    HMODULE m;
    modload_selector();
    if (g_vs_get) return;
    m = GetModuleHandleW(L"KofVsSelector.dll");
    if (!m) return;
    g_vs_get = (DWORD (WINAPI *)(DWORD, void*))GetProcAddress(m, "KofVs_XInputGetState");
    g_vs_set = (DWORD (WINAPI *)(DWORD, void*))GetProcAddress(m, "KofVs_XInputSetState");
}

DWORD WINAPI XInputGetState(DWORD idx, void* state) {
    bind_selector();
    if (g_vs_get) return g_vs_get(idx, state);
    if (!load_sys() || !g_getstate) return 1167u;
    return g_getstate(idx, state);
}

DWORD WINAPI XInputSetState(DWORD idx, void* vib) {
    bind_selector();
    if (g_vs_set) return g_vs_set(idx, vib);
    if (!load_sys() || !g_setstate) return 1167u;
    return g_setstate(idx, vib);
}

DWORD WINAPI XInputGetCapabilities(DWORD idx, DWORD flags, void* caps) {
    if (!load_sys() || !g_getcaps) return 1167u;
    return g_getcaps(idx, flags, caps);
}

void WINAPI XInputEnable(int enable) {
    if (!load_sys() || !g_enable) return;
    g_enable(enable);
}

int WINAPI DllMain(HMODULE instance, DWORD reason, void* reserved) {
    (void)reserved;
    if (reason == 1) {
        DisableThreadLibraryCalls(instance);
        modload_init(instance, "loader: " XPROXY_NAME);
        modload_selector();
    }
    return 1;
}
