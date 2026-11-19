#include "modload.h"

static wch g_dir[300];
static volatile long g_once;

static void note(const char* text) {
    wch path[320];
    HANDLE f;
    DWORD w;
    if (!g_dir[0] || !text) return;
    wstr_copy(path, 320, g_dir);
    wstr_cat(path, 320, L"kofvs-loader.log");
    f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE) return;
    WriteFile(f, text, (DWORD)cstr_len(text), &w, 0);
    WriteFile(f, "\r\n", 2, &w, 0);
    CloseHandle(f);
}

void modload_init(HMODULE self, const char* from) {
    int n;
    if (!self) return;
    n = (int)GetModuleFileNameW(self, g_dir, 300);
    while (n > 0 && g_dir[n - 1] != '\\' && g_dir[n - 1] != '/') --n;
    g_dir[n] = 0;
    note(from);
}

int modload_selector(void) {
    wch path[320];
    HMODULE m;
    int ok;
    if (_InterlockedCompareExchange(&g_once, 1, 0) != 0) return 1;
    if (!g_dir[0]) {
        note("loader: no dir");
        return 0;
    }
    wstr_copy(path, 320, g_dir);
    wstr_cat(path, 320, L"KofVsSelector.dll");
    m = LoadLibraryW(path);
    if (m) {
        note("loader: KofVsSelector.dll loaded");
        ok = 1;
    } else {
        note("loader: KofVsSelector.dll FAILED");
        ok = 0;
    }
    wstr_copy(path, 320, g_dir);
    wstr_cat(path, 320, L"Kof98Bgm.dll");
    m = LoadLibraryW(path);
    if (m) note("loader: Kof98Bgm.dll loaded");
    else note("loader: Kof98Bgm.dll not present");
    wstr_copy(path, 320, g_dir);
    wstr_cat(path, 320, L"Kof2002Bgm.dll");
    m = LoadLibraryW(path);
    if (m) note("loader: Kof2002Bgm.dll loaded");
    else note("loader: Kof2002Bgm.dll not present");
    /* King of Moves 3D:
       On Steam Deck/Proton, dinput8.dll MUST be the KoM module (~8 MB).
       Never LoadLibrary kom_module.dll if dinput8 is already loaded as KoM —
       a second init breaks patches ("unexpected bytes") and stage3d never arms. */
    {
        HMODULE di_mod;
        int skip_kom = 0;
        di_mod = GetModuleHandleW(L"dinput8.dll");
        if (di_mod) {
            u8* base = (u8*)di_mod;
            u32 pe;
            u32 size_of_image = 0;
            if (base[0] == 'M' && base[1] == 'Z') {
                pe = *(u32*)(base + 0x3C);
                /* PE32+ OptionalHeader.SizeOfImage at +0x50 from PE sig for PE32+ */
                if (*(u16*)(base + pe) == 0x4550) {
                    u16 magic = *(u16*)(base + pe + 0x18);
                    if (magic == 0x20B) size_of_image = *(u32*)(base + pe + 0x18 + 0x38);
                    else size_of_image = *(u32*)(base + pe + 0x18 + 0x38);
                }
            }
            if (size_of_image > 0x100000u) {
                skip_kom = 1;
                note("loader: dinput8.dll is King of Moves in-memory (skip kom_module)");
            }
        }
        if (!skip_kom) {
            wch di[320];
            HANDLE hf;
            DWORD hi = 0, lo = 0;
            wstr_copy(di, 320, g_dir);
            wstr_cat(di, 320, L"dinput8.dll");
            hf = CreateFileW(di, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
            if (hf != INVALID_HANDLE) {
                lo = GetFileSize(hf, &hi);
                CloseHandle(hf);
                if (lo > 1000000u) skip_kom = 1;
            }
        }
        if (skip_kom) {
            note("loader: skip kom_module (KoM already active as dinput8)");
        } else {
            wstr_copy(path, 320, g_dir);
            wstr_cat(path, 320, L"kom_module.dll");
            m = LoadLibraryW(path);
            if (m) note("loader: kom_module.dll loaded");
            else note("loader: kom_module.dll not present");
        }
    }
    wstr_copy(path, 320, g_dir);
    wstr_cat(path, 320, L"Kof98Color.dll");
    m = LoadLibraryW(path);
    if (m) note("loader: Kof98Color.dll loaded");
    else note("loader: Kof98Color.dll not present");
    /* Optional. Absent file changes nothing. Training acts only in Practice. */
    wstr_copy(path, 320, g_dir);
    wstr_cat(path, 320, L"Kof98Training.dll");
    m = LoadLibraryW(path);
    if (m) note("loader: Kof98Training.dll loaded");
    else note("loader: Kof98Training.dll not present");
    return ok;
}
