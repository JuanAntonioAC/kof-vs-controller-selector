#include "overlay.h"
#include "dinput_min.h"
#include "png.h"
#include "hook.h"

int _fltused = 0;

#define D3D_SDK_VERSION_ 32u
#define D3DDEVTYPE_HAL_ 1u
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING_ 0x20u
#define D3DSWAPEFFECT_DISCARD_ 1u
#define D3DCLEAR_TARGET_ 1u
#define D3DRS_SCISSORTESTENABLE_ 174u
#define D3DRS_COLORWRITEENABLE_ 168u
#define D3DBACKBUFFER_TYPE_MONO_ 0u
#define D3DFMT_X8R8G8B8_ 22u
#define D3DFMT_A8R8G8B8_ 21u
#define D3DPOOL_SYSTEMMEM_ 2u
#define D3DLOCK_READONLY_ 0x10u

enum {
    D3D9_CreateDevice = 16,
    DEV_Release = 2,
    DEV_Reset = 16,
    DEV_Present = 17,
    DEV_GetBackBuffer = 18,
    DEV_GetRenderTargetData = 32,
    DEV_CreateOffscreenPlainSurface = 36,
    DEV_SetRenderTarget = 37,
    DEV_GetRenderTarget = 38,
    DEV_Clear = 43,
    DEV_SetViewport = 47,
    DEV_GetViewport = 48,
    DEV_SetRenderState = 57,
    DEV_GetRenderState = 58,
    SURF_Release = 2,
    SURF_GetDesc = 12,
    SURF_LockRect = 13,
    SURF_UnlockRect = 14
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

typedef struct D3DLOCKED_RECT_ {
    int Pitch;
    void* pBits;
} D3DLOCKED_RECT_;

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
typedef HRESULT (WINAPI *GetRenderTargetData_fn)(void*, void*, void*);
typedef HRESULT (WINAPI *CreateOffscreen_fn)(void*, u32, u32, u32, u32, void**, void*);
typedef HRESULT (WINAPI *LockRect_fn)(void*, D3DLOCKED_RECT_*, const void*, DWORD);
typedef HRESULT (WINAPI *UnlockRect_fn)(void*);

static InlineHook g_hook;
static int g_installed;
static void** g_found_slot;
static void** g_patched_slot;
static Present_fn g_present;
static OverlayFrameFn g_on_frame;
static volatile u64 g_frames;
static volatile long g_in_present;

int overlay_readable(const void* addr, u64 size) {
    MEMORY_BASIC_INFORMATION_ mi;
    const u8* p = (const u8*)addr;
    if (!addr) return 0;
    if (VirtualQuery(addr, &mi, sizeof(mi)) == 0) return 0;
    if (mi.State != MEM_COMMIT_ || (mi.Protect & (PAGE_NOACCESS_ | PAGE_GUARD_)) || mi.Protect == 0) return 0;
    return p + size <= (const u8*)mi.BaseAddress + mi.RegionSize;
}

void overlay_describe(const void* addr, char* out, int cap) {
    MEMORY_BASIC_INFORMATION_ mi;
    Str s;
    str_init(&s, out, cap);
    str_add(&s, "0x");
    str_hex(&s, (u32)((u64)addr >> 32), 8);
    str_hex(&s, (u32)(u64)addr, 8);
    if (!addr || VirtualQuery(addr, &mi, sizeof(mi)) == 0) {
        str_add(&s, " (unmapped)");
        return;
    }
    str_add(&s, " state=0x");
    str_hex(&s, mi.State, 4);
    str_add(&s, " protect=0x");
    str_hex(&s, mi.Protect, 3);
    if (mi.Type == MEM_IMAGE_ && mi.AllocationBase) {
        wch path[260];
        char a[260];
        DWORD n = GetModuleFileNameW((HMODULE)mi.AllocationBase, path, 260);
        if (n > 0 && n < 260) {
            const wch* base = path;
            DWORD i;
            for (i = 0; i < n; ++i)
                if (path[i] == '\\' || path[i] == '/') base = path + i + 1;
            wstr_to_ascii(base, a, (int)sizeof(a));
            str_add(&s, " module=");
            str_add(&s, a);
            str_add(&s, "+0x");
            str_hex(&s, (u32)((const u8*)addr - (const u8*)mi.AllocationBase), 6);
        } else {
            str_add(&s, " module=?");
        }
    } else {
        str_add(&s, mi.Type == MEM_IMAGE_ ? " image" : " private/heap");
    }
}

static HRESULT WINAPI hook_present(void* dev, const void* src, const void* dst, HWND wnd, const void* dirty) {
    if (g_on_frame && _InterlockedCompareExchange(&g_in_present, 1, 0) == 0) {
        int w = 0, h = 0;
        g_frames++;
        if (overlay_backbuffer_size(dev, &w, &h)) g_on_frame(dev, w, h);
        _InterlockedExchange(&g_in_present, 0);
    }
    return g_present(dev, src, dst, wnd, dirty);
}

void* overlay_device_present(void* device) {
    void** vt = device ? *(void***)device : 0;
    if (!overlay_readable(vt, (DEV_Present + 1) * sizeof(void*))) return 0;
    return vt[DEV_Present];
}

int overlay_find_present(HWND hwnd, void** out_present, char* err, int errcap) {
    HMODULE d3d = LoadLibraryW(L"d3d9.dll");
    Direct3DCreate9_fn create = d3d ? (Direct3DCreate9_fn)GetProcAddress(d3d, "Direct3DCreate9") : 0;
    void* d9;
    void* dev = 0;
    D3DPRESENT_PARAMETERS_ pp;
    HRESULT hr;
    Str s;
    str_init(&s, err, errcap);
    *out_present = 0;
    if (!create) {
        str_add(&s, "d3d9.dll/Direct3DCreate9 not found");
        return 0;
    }
    d9 = create(D3D_SDK_VERSION_);
    if (!d9) {
        str_add(&s, "Direct3DCreate9 failed");
        return 0;
    }
    memset(&pp, 0, sizeof(pp));
    pp.Windowed = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD_;
    pp.hDeviceWindow = hwnd;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    hr = VT(d9, D3D9_CreateDevice, CreateDevice_fn)(d9, 0, D3DDEVTYPE_HAL_, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING_, &pp, &dev);
    if (hr < 0 || !dev) {
        str_add(&s, "temporary CreateDevice failed hr=0x");
        str_hex(&s, (u32)hr, 8);
        VT(d9, DEV_Release, Release_fn)(d9);
        return 0;
    }
    {
        char a[200];
        MEMORY_BASIC_INFORMATION_ mi;
        void** vt = *(void***)dev;
        g_found_slot = 0;
        if (VirtualQuery(vt, &mi, sizeof(mi)) && mi.Type == MEM_IMAGE_ && overlay_readable(vt, (DEV_Present + 1) * sizeof(void*)))
            g_found_slot = &vt[DEV_Present];
        overlay_describe(vt, a, (int)sizeof(a));
        str_add(&s, "temporary device vtable ");
        str_add(&s, a);
        *out_present = overlay_device_present(dev);
        overlay_describe(*out_present, a, (int)sizeof(a));
        str_add(&s, " | Present ");
        str_add(&s, a);
        if (g_found_slot) str_add(&s, " | vtable is in a module image (DXVK-style fallback available)");
    }
    VT(dev, DEV_Release, Release_fn)(dev);
    VT(d9, DEV_Release, Release_fn)(d9);
    return *out_present != 0;
}

int overlay_install(void* present_fn, OverlayFrameFn on_frame, char* err, int errcap) {
    void* orig = 0;
    if (!present_fn || g_installed) return 0;
    if (!overlay_readable(present_fn, 32)) {
        Str s;
        str_init(&s, err, errcap);
        str_add(&s, "Present code not readable");
        return 0;
    }
    g_on_frame = on_frame;
    if (!hook_install(&g_hook, present_fn, (void*)hook_present, &orig, err, errcap)) return 0;
    g_present = (Present_fn)orig;
    g_installed = 1;
    {
        Str s;
        str_init(&s, err, errcap);
        if (g_hook.chained) {
            char a[200];
            overlay_describe((void*)(g_hook.trampoline - 16), a, (int)sizeof(a));
            str_add(&s, "chained in front of an existing hook; relay ");
            str_add(&s, a);
            overlay_describe((void*)*(u64*)(g_hook.trampoline + 6), a, (int)sizeof(a));
            str_add(&s, " | previous hook target ");
            str_add(&s, a);
        } else {
            str_add(&s, "prologue relocated, bytes stolen=");
            str_int(&s, g_hook.stolen);
        }
    }
    return 1;
}

int overlay_install_vtable(OverlayFrameFn on_frame, char* err, int errcap) {
    DWORD old = 0;
    Str s;
    str_init(&s, err, errcap);
    if (g_installed) return 0;
    if (!g_found_slot) {
        str_add(&s, "device vtable is not in a module image (per-device copy): vtable patch not possible");
        return 0;
    }
    if (!VirtualProtect(g_found_slot, sizeof(void*), PAGE_READWRITE, &old)) {
        str_add(&s, "VirtualProtect on the vtable slot failed");
        return 0;
    }
    g_on_frame = on_frame;
    g_present = (Present_fn)*g_found_slot;
    *g_found_slot = (void*)hook_present;
    VirtualProtect(g_found_slot, sizeof(void*), old, &old);
    g_patched_slot = g_found_slot;
    g_installed = 1;
    str_add(&s, "vtable slot patched (shared module vtable)");
    return 1;
}

void overlay_uninstall(void) {
    DWORD old = 0;
    if (!g_installed) return;
    g_on_frame = 0;
    if (g_patched_slot) {
        if (VirtualProtect(g_patched_slot, sizeof(void*), PAGE_READWRITE, &old)) {
            *g_patched_slot = (void*)g_present;
            VirtualProtect(g_patched_slot, sizeof(void*), old, &old);
        }
        g_patched_slot = 0;
    } else {
        hook_remove(&g_hook);
    }
    g_installed = 0;
}

u64 overlay_frames(void) {
    return g_frames;
}

int overlay_backbuffer_size(void* dev, int* w, int* h) {
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

u32 g_snap_format, g_snap_ms;

int overlay_snapshot_png(void* dev, const wch* path) {
    void* bb = 0;
    void* sys = 0;
    D3DSURFACE_DESC_ desc;
    D3DLOCKED_RECT_ lr;
    int ok = 0;
    if (VT(dev, DEV_GetBackBuffer, GetBackBuffer_fn)(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO_, &bb) < 0 || !bb) return 0;
    memset(&desc, 0, sizeof(desc));
    VT(bb, SURF_GetDesc, GetDesc_fn)(bb, &desc);
    g_snap_format = desc.Format;
    g_snap_ms = desc.MultiSampleType;
    if ((desc.Format == D3DFMT_X8R8G8B8_ || desc.Format == D3DFMT_A8R8G8B8_) &&
        VT(dev, DEV_CreateOffscreenPlainSurface, CreateOffscreen_fn)(dev, desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM_, &sys, 0) >= 0 && sys) {
        if (VT(dev, DEV_GetRenderTargetData, GetRenderTargetData_fn)(dev, bb, sys) >= 0 &&
            VT(sys, SURF_LockRect, LockRect_fn)(sys, &lr, 0, D3DLOCK_READONLY_) >= 0) {
            ok = png_write_bgrx(path, (int)desc.Width, (int)desc.Height, (const u8*)lr.pBits, lr.Pitch);
            VT(sys, SURF_UnlockRect, UnlockRect_fn)(sys);
        }
        VT(sys, SURF_Release, Release_fn)(sys);
    }
    VT(bb, SURF_Release, Release_fn)(bb);
    return ok;
}
