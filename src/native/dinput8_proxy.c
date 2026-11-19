#include "win.h"
#include "dinput_min.h"
#include "modload.h"

HRESULT WINAPI DirectInput8Create(HMODULE inst, DWORD ver, const GUID* iid, void** out, void* unk);

static HMODULE g_sys;
static DirectInput8Create_fn g_create;
static HRESULT (WINAPI *g_canunload)(void);
static HRESULT (WINAPI *g_getclass)(const GUID*, const GUID*, void**);
static HRESULT (WINAPI *g_register)(void);
static HRESULT (WINAPI *g_unregister)(void);
static void* (WINAPI *g_getdf)(void);

static HMODULE load_sys(void) {
    wch path[300];
    u32 n;
    if (g_sys) return g_sys;
    n = GetSystemDirectoryW(path, 260);
    if (n == 0 || n >= 260) return 0;
    wstr_cat(path, 300, L"\\dinput8.dll");
    g_sys = LoadLibraryW(path);
    if (!g_sys) return 0;
    g_create = (DirectInput8Create_fn)GetProcAddress(g_sys, "DirectInput8Create");
    g_canunload = (HRESULT (WINAPI *)(void))GetProcAddress(g_sys, "DllCanUnloadNow");
    g_getclass = (HRESULT (WINAPI *)(const GUID*, const GUID*, void**))GetProcAddress(g_sys, "DllGetClassObject");
    g_register = (HRESULT (WINAPI *)(void))GetProcAddress(g_sys, "DllRegisterServer");
    g_unregister = (HRESULT (WINAPI *)(void))GetProcAddress(g_sys, "DllUnregisterServer");
    g_getdf = (void* (WINAPI *)(void))GetProcAddress(g_sys, "GetdfDIJoystick");
    if (g_create == DirectInput8Create) {
        g_sys = 0;
        g_create = 0;
        return 0;
    }
    return g_sys;
}

HRESULT WINAPI DirectInput8Create(HMODULE inst, DWORD ver, const GUID* iid, void** out, void* unk) {
    modload_selector();
    if (!load_sys() || !g_create) return (HRESULT)0x80004005L;
    return g_create(inst, ver, iid, out, unk);
}

HRESULT WINAPI DllCanUnloadNow(void) {
    if (!load_sys() || !g_canunload) return 1;
    return g_canunload();
}

HRESULT WINAPI DllGetClassObject(const GUID* clsid, const GUID* iid, void** out) {
    if (!load_sys() || !g_getclass) return (HRESULT)0x80040111L;
    return g_getclass(clsid, iid, out);
}

HRESULT WINAPI DllRegisterServer(void) {
    if (!load_sys() || !g_register) return (HRESULT)0x80004005L;
    return g_register();
}

HRESULT WINAPI DllUnregisterServer(void) {
    if (!load_sys() || !g_unregister) return (HRESULT)0x80004005L;
    return g_unregister();
}

void* WINAPI GetdfDIJoystick(void) {
    if (!load_sys() || !g_getdf) return 0;
    return g_getdf();
}

int WINAPI DllMain(HMODULE instance, DWORD reason, void* reserved) {
    (void)reserved;
    if (reason == 1) {
        DisableThreadLibraryCalls(instance);
        modload_init(instance, "loader: dinput8.dll");
        modload_selector();
    }
    return 1;
}
