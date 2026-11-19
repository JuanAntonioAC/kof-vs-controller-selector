/* D3D9 Present hook and overlay draw for the 32-bit KOF XIII build.
   Button layouts are drawn with Clear rects. The game's own button config is not touched. */
#include "overlay.h"
#include "dinput_min.h"

int _fltused = 0;

#define D3D_SDK_VERSION_ 32u
#define D3DDEVTYPE_HAL_ 1u
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING_ 0x20u
#define D3DSWAPEFFECT_DISCARD_ 1u
#define D3DCLEAR_TARGET_ 1u
#define D3DRS_SCISSORTESTENABLE_ 174u
#define D3DRS_COLORWRITEENABLE_ 168u
#define D3DBACKBUFFER_TYPE_MONO_ 0u
#define MEM_COMMIT_ 0x1000u
#define MEM_RESERVE_ 0x2000u

enum {
    D3D9_CreateDevice = 16,
    DEV_Present = 17,
    DEV_GetBackBuffer = 18,
    DEV_SetRenderTarget = 37,
    DEV_GetRenderTarget = 38,
    DEV_Clear = 43,
    DEV_SetViewport = 47,
    DEV_GetViewport = 48,
    DEV_SetRenderState = 57,
    DEV_GetRenderState = 58,
    SURF_Release = 2,
    SURF_GetDesc = 12
};

typedef struct D3DPRESENT_PARAMETERS_ {
    u32 BackBufferWidth;
    u32 BackBufferHeight;
    u32 BackBufferFormat;
    u32 BackBufferCount;
    u32 MultiSampleType;
    DWORD MultiSampleQuality;
    u32 SwapEffect;
    HWND hDeviceWindow;
    int Windowed;
    int EnableAutoDepthStencil;
    u32 AutoDepthStencilFormat;
    DWORD Flags;
    u32 FullScreen_RefreshRateInHz;
    u32 PresentationInterval;
} D3DPRESENT_PARAMETERS_;

typedef struct D3DSURFACE_DESC_ {
    u32 Format;
    u32 Type;
    DWORD Usage;
    u32 Pool;
    u32 MultiSampleType;
    DWORD MultiSampleQuality;
    u32 Width;
    u32 Height;
} D3DSURFACE_DESC_;

typedef struct D3DVIEWPORT9_ {
    DWORD X, Y, Width, Height;
    float MinZ, MaxZ;
} D3DVIEWPORT9_;

typedef struct D3DRECT_ {
    LONG x1, y1, x2, y2;
} D3DRECT_;

typedef void* (WINAPI *Direct3DCreate9_fn)(u32);
typedef HRESULT (WINAPI *CreateDevice_fn)(void*, u32, u32, HWND, DWORD, D3DPRESENT_PARAMETERS_*, void**);
typedef HRESULT (WINAPI *Present_fn)(void*, const void*, const void*, HWND, const void*);
typedef HRESULT (WINAPI *GetBackBuffer_fn)(void*, u32, u32, u32, void**);
typedef HRESULT (WINAPI *GetRenderTarget_fn)(void*, DWORD, void**);
typedef HRESULT (WINAPI *SetRenderTarget_fn)(void*, DWORD, void*);
typedef HRESULT (WINAPI *Viewport_fn)(void*, D3DVIEWPORT9_*);
typedef HRESULT (WINAPI *GetRenderState_fn)(void*, DWORD, DWORD*);
typedef HRESULT (WINAPI *SetRenderState_fn)(void*, DWORD, DWORD);
typedef HRESULT (WINAPI *Clear_fn)(void*, DWORD, const D3DRECT_*, DWORD, DWORD, float, DWORD);
typedef HRESULT (WINAPI *GetDesc_fn)(void*, D3DSURFACE_DESC_*);

__declspec(dllimport) void* WINAPI VirtualAlloc(void* addr, ksz size, DWORD type, DWORD protect);
__declspec(dllimport) int WINAPI FlushInstructionCache(HANDLE process, const void* addr, ksz size);
__declspec(dllimport) HWND WINAPI GetForegroundWindow(void);

typedef ksz (WINAPI *VQ_fn)(const void*, void*, ksz);
typedef struct MBI86_ {
    void* BaseAddress;
    void* AllocationBase;
    DWORD AllocationProtect;
    DWORD RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MBI86_;

static Present_fn g_present;
static void** g_live_vt;
typedef HRESULT (WINAPI *CreateDeviceHook_fn)(void*, u32, u32, HWND, DWORD, D3DPRESENT_PARAMETERS_*, void**);
typedef HRESULT (WINAPI *Reset_fn)(void*, D3DPRESENT_PARAMETERS_*);
static CreateDeviceHook_fn g_create_real;
static Reset_fn g_reset;
void app_log(const char* text);
static OverlayFrameFn g_on_frame;
static volatile u64 g_frames;
static volatile long g_in_present;
static u8* g_tramp;

int overlay_readable(const void* addr, u64 size) {
    (void)addr;
    (void)size;
    return 1;
}

void overlay_describe(const void* addr, char* out, int cap) {
    Str s;
    str_init(&s, out, cap);
    str_add(&s, "0x");
    str_hex(&s, (u32)(u64)addr, 8);
}

u64 overlay_frames(void) {
    return g_frames;
}

static int backbuffer_size(void* dev, int* w, int* h) {
    void* bb = 0;
    D3DSURFACE_DESC_ desc;
    if (VT(dev, DEV_GetBackBuffer, GetBackBuffer_fn)(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO_, &bb) < 0 || !bb) return 0;
    memset(&desc, 0, sizeof(desc));
    VT(bb, SURF_GetDesc, GetDesc_fn)(bb, &desc);
    VT(bb, SURF_Release, Release_fn)(bb);
    *w = (int)desc.Width;
    *h = (int)desc.Height;
    return desc.Width > 0 && desc.Height > 0;
}

int overlay_backbuffer_size(void* dev, int* w, int* h) {
    return backbuffer_size(dev, w, h);
}

int overlay_draw(void* dev, const UiRect* rects, int count) {
    static D3DRECT_ batch[512];
    void* bb = 0;
    void* old_rt = 0;
    D3DVIEWPORT9_ vp;
    DWORD scissor = 0, colorwrite = 0xF;
    int i;
    if (VT(dev, DEV_GetBackBuffer, GetBackBuffer_fn)(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO_, &bb) < 0 || !bb) return 0;
    VT(dev, DEV_GetViewport, Viewport_fn)(dev, &vp);
    VT(dev, DEV_GetRenderTarget, GetRenderTarget_fn)(dev, 0, &old_rt);
    VT(dev, DEV_GetRenderState, GetRenderState_fn)(dev, D3DRS_SCISSORTESTENABLE_, &scissor);
    VT(dev, DEV_GetRenderState, GetRenderState_fn)(dev, D3DRS_COLORWRITEENABLE_, &colorwrite);
    VT(dev, DEV_SetRenderTarget, SetRenderTarget_fn)(dev, 0, bb);
    VT(dev, DEV_SetRenderState, SetRenderState_fn)(dev, D3DRS_SCISSORTESTENABLE_, 0);
    VT(dev, DEV_SetRenderState, SetRenderState_fn)(dev, D3DRS_COLORWRITEENABLE_, 0xF);
    for (i = 0; i < count;) {
        u32 color = rects[i].color;
        int n = 0;
        while (i < count && rects[i].color == color && n < 512) {
            batch[n].x1 = rects[i].x1;
            batch[n].y1 = rects[i].y1;
            batch[n].x2 = rects[i].x2;
            batch[n].y2 = rects[i].y2;
            ++n;
            ++i;
        }
        VT(dev, DEV_Clear, Clear_fn)(dev, (DWORD)n, batch, D3DCLEAR_TARGET_, color, 1.0f, 0);
    }
    VT(dev, DEV_SetRenderState, SetRenderState_fn)(dev, D3DRS_COLORWRITEENABLE_, colorwrite);
    VT(dev, DEV_SetRenderState, SetRenderState_fn)(dev, D3DRS_SCISSORTESTENABLE_, scissor);
    if (old_rt) {
        VT(dev, DEV_SetRenderTarget, SetRenderTarget_fn)(dev, 0, old_rt);
        VT(old_rt, SURF_Release, Release_fn)(old_rt);
    }
    VT(dev, DEV_SetViewport, Viewport_fn)(dev, &vp);
    VT(bb, SURF_Release, Release_fn)(bb);
    return 1;
}

int overlay_snapshot_png(void* dev, const wch* path) {
    (void)dev;
    (void)path;
    return 0;
}

u32 g_snap_format, g_snap_ms;

static HRESULT WINAPI hook_present(void* dev, const void* src, const void* dst, HWND wnd, const void* dirty) {
    if (g_on_frame && _InterlockedCompareExchange(&g_in_present, 1, 0) == 0) {
        int w = 0, h = 0;
        g_frames++;
        if (backbuffer_size(dev, &w, &h)) g_on_frame(dev, w, h);
        _InterlockedExchange(&g_in_present, 0);
    }
    return g_present(dev, src, dst, wnd, dirty);
}

static int insn_len(const u8* p) {
    u8 b = p[0];
    if (b >= 0x50 && b <= 0x5F) return 1;
    if (b == 0x90 || b == 0xC3) return 1;
    if (b == 0x6A) return 2;
    if (b == 0x68 || b == 0xE8 || b == 0xE9) return 5;
    if (b == 0xEB) return 2;
    if ((b == 0x33 || b == 0x31 || b == 0x85 || b == 0x3B) && (p[1] & 0xC0) == 0xC0) return 2;
    if (b == 0x83) {
        u8 mod = p[1] >> 6, rm = p[1] & 7;
        int n = 3;
        if (mod != 3 && rm == 4) n++;
        if (mod == 1) n++;
        else if (mod == 2 || (mod == 0 && rm == 5)) n += 4;
        return n;
    }
    if (b == 0x89 || b == 0x8B || b == 0x8D) {
        u8 mod = p[1] >> 6, rm = p[1] & 7;
        int n = 2;
        if (mod != 3 && rm == 4) n++;
        if (mod == 1) n++;
        else if (mod == 2 || (mod == 0 && rm == 5)) n += 4;
        return n;
    }
    return 0;
}

static int prologue_len(const u8* code, int need) {
    int i = 0;
    while (i < need) {
        int n = insn_len(code + i);
        if (n <= 0) return 0;
        if (code[i] == 0xE8 || code[i] == 0xE9 || code[i] == 0xEB || code[i] == 0xC3) return 0;
        i += n;
    }
    return i;
}

static void write_rel(u8* at, const void* dest) {
    i32 rel = (i32)((u8*)dest - (at + 5));
    at[0] = 0xE9;
    at[1] = (u8)rel;
    at[2] = (u8)(rel >> 8);
    at[3] = (u8)(rel >> 16);
    at[4] = (u8)(rel >> 24);
}

static int patch_bytes(void* target, const u8* bytes, int len) {
    DWORD old = 0;
    if (!VirtualProtect(target, (ksz)len, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(target, bytes, (unsigned long long)len);
    VirtualProtect(target, (ksz)len, old, &old);
    FlushInstructionCache((HANDLE)(long)-1, target, (ksz)len);
    return 1;
}

static int hook_code(u8* target, void* detour, void** orig, char* err, int errcap, const char* what) {
    int len;
    u8 patch[16];
    u8* tramp;
    Str s;
    str_init(&s, err, errcap);
    if (target[0] == 0xE9) {
        str_add(&s, what);
        str_add(&s, " already has a jump; left untouched");
        return 0;
    }
    len = prologue_len(target, 5);
    if (len < 5 || len > 16) {
        str_add(&s, what);
        str_add(&s, " prologue is not a 5-byte hook");
        return 0;
    }
    tramp = (u8*)VirtualAlloc(0, 0x1000, MEM_COMMIT_ | MEM_RESERVE_, PAGE_EXECUTE_READWRITE);
    if (!tramp) {
        str_add(&s, "trampoline alloc failed");
        return 0;
    }
    memcpy(tramp, target, (unsigned long long)len);
    write_rel(tramp + len, target + len);
    *orig = tramp;
    write_rel(patch, detour);
    memset(patch + 5, 0x90, (unsigned long long)(len - 5));
    if (!patch_bytes(target, patch, len)) {
        str_add(&s, "VirtualProtect failed on ");
        str_add(&s, what);
        return 0;
    }
    str_add(&s, what);
    str_add(&s, " hooked inline");
    return 1;
}

static int hook_at(u8* target, char* err, int errcap) {
    return hook_code(target, (void*)hook_present, (void**)&g_present, err, errcap, "Present");
}

static int ptr_in(const void* p, const u8* base, u32 size) {
    const u8* a = (const u8*)p;
    return a && a >= base && a < base + size;
}

static u32 image_size(HMODULE mod) {
    u8* b = (u8*)mod;
    u32 pe = *(u32*)(b + 0x3C);
    return *(u32*)(b + pe + 80);
}

static const u8 g_present_pat[24] = {
    0x8B, 0xFF, 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x51, 0x51, 0x56, 0x8B, 0x75, 0x08,
    0x8B, 0xCE, 0xF7, 0xD9, 0x57, 0x1B, 0xC9, 0x8D, 0x46, 0x04
};

static u8* exec_lo(HMODULE mod, u8** hi) {
    u8* b = (u8*)mod;
    u32 pe = *(u32*)(b + 0x3C);
    u16 nsec = *(u16*)(b + pe + 6);
    u16 opt = *(u16*)(b + pe + 20);
    u8* sec = b + pe + 24 + opt;
    u8* lo = 0;
    u32 i;
    *hi = 0;
    for (i = 0; i < nsec; ++i) {
        u32 ch = *(u32*)(sec + i * 40 + 36);
        u32 va = *(u32*)(sec + i * 40 + 12);
        u32 vsz = *(u32*)(sec + i * 40 + 8);
        if (!(ch & 0x20000000u)) continue;
        if (!lo || b + va < lo) lo = b + va;
        if (b + va + vsz > *hi) *hi = b + va + vsz;
    }
    return lo;
}

static int collect_present_hits(u8* lo, u8* hi, u8** hits, int max) {
    int n = 0;
    u8* p;
    for (p = lo; p + 24 < hi && n < max; ++p) {
        int k, ok = 1;
        for (k = 0; k < 24; ++k)
            if (p[k] != g_present_pat[k]) {
                ok = 0;
                break;
            }
        if (ok) hits[n++] = p;
    }
    return n;
}

static void* find_live_present(void) {
    HMODULE d3d = GetModuleHandleW(L"d3d9.dll");
    VQ_fn vq = (VQ_fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "VirtualQuery");
    u8* lo;
    u8* hi;
    u8* hits[8];
    u8* image;
    u8* addr;
    u32 image_sz;
    int nh, h;
    MBI86_ mi;
    if (!d3d || !vq) return 0;
    lo = exec_lo(d3d, &hi);
    image = (u8*)d3d;
    image_sz = image_size(d3d);
    if (!lo || !hi || image_sz < 0x10000 || image_sz > 0x2000000) return 0;
    nh = collect_present_hits(lo, hi, hits, 8);
    if (!nh) return 0;
    addr = (u8*)0x10000;
    while ((u32)addr < 0x80000000u && vq(addr, &mi, sizeof(mi))) {
        u8* p;
        u8* end;
        u32 next;
        if (!mi.BaseAddress || mi.RegionSize < 4) break;
        next = (u32)mi.BaseAddress + mi.RegionSize;
        if (next <= (u32)addr) break;
        if (mi.State == MEM_COMMIT_ && mi.Type == 0x20000u && mi.RegionSize <= 0x4000000u &&
            !(mi.Protect & (PAGE_NOACCESS_ | PAGE_GUARD_))) {
            p = (u8*)mi.BaseAddress;
            end = p + mi.RegionSize;
            if (p + 72 <= end) p += 72;
            for (; p + 4 <= end; p += 4) {
                u8* fn = *(u8**)p;
                void** vt;
                void** orig;
                int match = 0;
                for (h = 0; h < nh; ++h)
                    if (fn == hits[h]) match = 1;
                if (!match) continue;
                vt = (void**)(p - 17 * 4);
                if ((u8*)vt < (u8*)mi.BaseAddress) continue;
                orig = (void**)vt[-1];
                if (!ptr_in(orig, image, image_sz - 80)) continue;
                if (orig[17] != (void*)fn) continue;
                if ((u8*)vt[0] < lo || (u8*)vt[0] >= hi) continue;
                g_live_vt = vt;
                return fn;
            }
        }
        addr = (u8*)next;
    }
    return 0;
}

static void clamp_hd(D3DPRESENT_PARAMETERS_* pp) {
    if (!pp) return;
    if (pp->BackBufferWidth > 1920 || pp->BackBufferHeight > 1080) {
        pp->BackBufferWidth = 1920;
        pp->BackBufferHeight = 1080;
        app_log("DISPLAY 1920x1080 HD");
    }
}

static HRESULT WINAPI hook_createdevice(void* d9, u32 adapter, u32 type, HWND wnd, DWORD flags, D3DPRESENT_PARAMETERS_* pp, void** out) {
    clamp_hd(pp);
    return g_create_real(d9, adapter, type, wnd, flags, pp, out);
}

static HRESULT WINAPI hook_reset(void* dev, D3DPRESENT_PARAMETERS_* pp) {
    clamp_hd(pp);
    return g_reset(dev, pp);
}

static void patch_reset(void) {
    DWORD old = 0;
    void** slot;
    if (!g_live_vt || g_reset) return;
    slot = &g_live_vt[16];
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return;
    g_reset = (Reset_fn)*slot;
    *slot = (void*)hook_reset;
    VirtualProtect(slot, sizeof(void*), old, &old);
    app_log("DISPLAY Reset limitado a HD");
}

int kof13_force_hd(char* err, int errcap) {
    HMODULE d3d = LoadLibraryW(L"d3d9.dll");
    Direct3DCreate9_fn create = d3d ? (Direct3DCreate9_fn)GetProcAddress(d3d, "Direct3DCreate9") : 0;
    void* d9;
    void** vt;
    if (g_create_real) return 1;
    if (!create) {
        Str s;
        str_init(&s, err, errcap);
        str_add(&s, "d3d9.dll/Direct3DCreate9 not found");
        return 0;
    }
    d9 = create(D3D_SDK_VERSION_);
    if (!d9) return 0;
    vt = *(void***)d9;
    if (!hook_code((u8*)vt[D3D9_CreateDevice], (void*)hook_createdevice, (void**)&g_create_real, err, errcap, "CreateDevice")) {
        VT(d9, 2, Release_fn)(d9);
        return 0;
    }
    VT(d9, 2, Release_fn)(d9);
    return 1;
}

static int create_and_hook(void* d9, HWND wnd, DWORD behavior, char* err, int errcap) {
    D3DPRESENT_PARAMETERS_ pp;
    void* dev = 0;
    HRESULT hr;
    void** vt;
    if (!wnd) return 0;
    memset(&pp, 0, sizeof(pp));
    pp.Windowed = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD_;
    pp.hDeviceWindow = wnd;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    hr = VT(d9, D3D9_CreateDevice, CreateDevice_fn)(d9, 0, D3DDEVTYPE_HAL_, wnd, behavior, &pp, &dev);
    if (hr < 0 || !dev) return 0;
    vt = *(void***)dev;
    if (!hook_at((u8*)vt[DEV_Present], err, errcap)) {
        VT(dev, 2, Release_fn)(dev);
        return 0;
    }
    VT(dev, 2, Release_fn)(dev);
    return 1;
}

int kof13_install_present(HWND hwnd, OverlayFrameFn on_frame, char* err, int errcap) {
    Str s;
    (void)hwnd;
    (void)on_frame;
    str_init(&s, err, errcap);
    str_add(&s, "overlay left off so the game process is not killed");
    return 0;
#if 0
    HMODULE d3d = LoadLibraryW(L"d3d9.dll");
    Direct3DCreate9_fn create = d3d ? (Direct3DCreate9_fn)GetProcAddress(d3d, "Direct3DCreate9") : 0;
    void* d9 = 0;
    void* present;
    HWND fg;
    Str s;
    DWORD behaviors[3];
    int i;
    str_init(&s, err, errcap);
    g_on_frame = on_frame;
    if (g_present) {
        str_add(&s, "Present already hooked");
        return 1;
    }
    if (!create) {
        str_add(&s, "d3d9.dll/Direct3DCreate9 not found");
        return 0;
    }
    present = find_live_present();
    if (present && g_live_vt) {
        void** slot = &g_live_vt[DEV_Present];
        DWORD old = 0;
        if (*slot != (void*)hook_present && VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
            g_present = (Present_fn)*slot;
            *slot = (void*)hook_present;
            VirtualProtect(slot, sizeof(void*), old, &old);
            str_init(&s, err, errcap);
            str_add(&s, "Present hooked on the device vtable");
            return 1;
        }
    }
    d9 = create(D3D_SDK_VERSION_);
    if (!d9) {
        str_add(&s, "Direct3DCreate9 failed");
        return 0;
    }
    fg = GetForegroundWindow();
    behaviors[0] = D3DCREATE_SOFTWARE_VERTEXPROCESSING_;
    behaviors[1] = 0x40u;
    behaviors[2] = 0x80u;
    for (i = 0; i < 3; ++i) {
        if (create_and_hook(d9, fg, behaviors[i], err, errcap) || create_and_hook(d9, hwnd, behaviors[i], err, errcap)) {
            VT(d9, 2, Release_fn)(d9);
            return 1;
        }
    }
    VT(d9, 2, Release_fn)(d9);
    str_init(&s, err, errcap);
    str_add(&s, "D3D9 device not ready");
    return 0;
#endif
}

void* g_kof13_real_d3d;
static void* g_create_saved;

void kof13_set_frame(OverlayFrameFn fn) {
    g_on_frame = fn;
}

static HRESULT WINAPI hk_createdevice(void* d9, u32 adapter, u32 kind, HWND focus, DWORD behavior, void* pp, void** outdev) {
    typedef HRESULT (WINAPI *Fn)(void*, u32, u32, HWND, DWORD, void*, void**);
    Fn orig = (Fn)g_create_saved;
    HRESULT hr;
    if (!orig) return (HRESULT)0x80004005;
    hr = orig(d9, adapter, kind, focus, behavior, pp, outdev);
    if (hr >= 0 && outdev && *outdev) {
        void** vt = *(void***)(*outdev);
        void** slot;
        DWORD old = 0;
        if (!vt) return hr;
        slot = &vt[DEV_Present];
        if (*slot != (void*)hook_present && VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
            if (!g_present) g_present = (Present_fn)*slot;
            *slot = (void*)hook_present;
            VirtualProtect(slot, sizeof(void*), old, &old);
        }
    }
    return hr;
}

void* WINAPI kof13_Direct3DCreate9(u32 sdk) {
    typedef void* (WINAPI *Fn)(u32);
    Fn orig = (Fn)g_kof13_real_d3d;
    void* d9;
    if (!orig) return 0;
    d9 = orig(sdk);
    if (d9) {
        void** vt = *(void***)d9;
        void** slot;
        DWORD old = 0;
        if (!vt) return d9;
        slot = &vt[16];
        if (*slot != (void*)hk_createdevice && VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
            if (!g_create_saved) g_create_saved = *slot;
            *slot = (void*)hk_createdevice;
            VirtualProtect(slot, sizeof(void*), old, &old);
        }
    }
    return d9;
}

int overlay_find_present(HWND hwnd, void** out_present, char* err, int errcap) {
    (void)hwnd;
    (void)out_present;
    (void)err;
    (void)errcap;
    return 0;
}
void* overlay_device_present(void* device) {
    (void)device;
    return 0;
}
int overlay_install(void* present_fn, OverlayFrameFn on_frame, char* err, int errcap) {
    (void)present_fn;
    (void)on_frame;
    (void)err;
    (void)errcap;
    return 0;
}
int overlay_install_vtable(OverlayFrameFn on_frame, char* err, int errcap) {
    (void)on_frame;
    (void)err;
    (void)errcap;
    return 0;
}
void overlay_uninstall(void) {}
