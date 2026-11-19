#include "app.h"
#include "iat.h"
#include "layout.h"
#include "overlay.h"
#include "route.h"

__declspec(dllimport) HWND WINAPI CreateWindowExW(DWORD ex, const wch* cls, const wch* title, DWORD style, int x, int y, int w, int h, HWND parent, void* menu, HMODULE inst, void* param);
__declspec(dllimport) i64 WINAPI DefWindowProcW(HWND hwnd, u32 msg, u64 wp, i64 lp);
__declspec(dllimport) u16 WINAPI RegisterClassExW(const WNDCLASSEXW_* wc);
__declspec(dllimport) int WINAPI PeekMessageW(MSG_* msg, HWND hwnd, u32 min, u32 max, u32 remove);
__declspec(dllimport) int WINAPI TranslateMessage(const MSG_* msg);
__declspec(dllimport) i64 WINAPI DispatchMessageW(const MSG_* msg);

typedef struct GameProfile {
    const char* id;
    const char* title;
    u32 timestamp;
    u32 size_of_image;
    u32 mode_rva;
    u32 sig1_rva;
    u8 sig1[10];
    u32 portmap_rva;
    u32 sig2_rva;
    u8 sig2[7];
    const char* bgm_folder;
    const char* default_select_bgm;
    u32 keymap_default_rva;
    u32 keymap_ptrs_rva;
    int san_felipe_l1_select;
} GameProfile;

static const GameProfile g_profiles[] = {
    { "kof2002", "KOF 2002 UNLIMITED MATCH", 0x6243D069u, 0x41AE000u,
      0x8C0840u, 0x9DD08u, { 0xC7, 0x05, 0x2E, 0x2B, 0x82, 0x00, 0x02, 0x00, 0x00, 0x00 },
      0x8CAF08u, 0x60600u, { 0x8B, 0xB4, 0x2F, 0x08, 0xAF, 0x8C, 0x00 },
      "data/BGM/", "UM48000Hz/BGM03.", 0x6C3910u, 0x8CAF30u, 0 },
    { "kof98", "KOF '98 ULTIMATE MATCH FINAL EDITION", 0x62033053u, 0x1B31000u,
      0x5490C4u, 0x2B15ECu, { 0xC7, 0x05, 0xCE, 0x7A, 0x29, 0x00, 0x02, 0x00, 0x00, 0x00 },
      0x552808u, 0x5AF70u, { 0x8B, 0xB4, 0x2F, 0x08, 0x28, 0x55, 0x00 },
      "game/audio/bgm/", "bgm/PS_N.", 0x406910u, 0x552830u, 0 },
};

static HMODULE g_self;
static u8* g_base;
static const GameProfile* g_game;
static wch g_dir[300];

static XInputGetState_fn g_xget;
static XInputSetState_fn g_xset;
static DWORD WINAPI hk_xget(DWORD idx, XINPUT_STATE_* st);
static DWORD WINAPI hk_xset(DWORD idx, XINPUT_VIBRATION_* vib);

static int bind_real_xinput(void) {
    wch path[300];
    HMODULE x;
    u32 n;
    if (g_xget) return 1;
    n = GetSystemDirectoryW(path, 260);
    if (n == 0 || n >= 260) return 0;
    wstr_cat(path, 300, L"\\xinput1_3.dll");
    x = LoadLibraryW(path);
    if (!x) {
        n = GetSystemDirectoryW(path, 260);
        if (n == 0 || n >= 260) return 0;
        wstr_cat(path, 300, L"\\xinput9_1_0.dll");
        x = LoadLibraryW(path);
    }
    if (!x) return 0;
    g_xget = (XInputGetState_fn)GetProcAddress(x, (const char*)(u64)2);
    if (!g_xget) g_xget = (XInputGetState_fn)GetProcAddress(x, "XInputGetState");
    g_xset = (XInputSetState_fn)GetProcAddress(x, (const char*)(u64)3);
    if (!g_xset) g_xset = (XInputSetState_fn)GetProcAddress(x, "XInputSetState");
    return g_xget != 0;
}

__declspec(dllexport) DWORD WINAPI KofVs_XInputGetState(DWORD idx, XINPUT_STATE_* st) {
    return hk_xget(idx, st);
}

__declspec(dllexport) DWORD WINAPI KofVs_XInputSetState(DWORD idx, XINPUT_VIBRATION_* vib) {
    return hk_xset(idx, vib);
}

/* Read by Kof98Bgm.dll / Kof2002Bgm.dll. -1 means "no selector": the music
   mod then falls back to reading every XInput slot itself. */
__declspec(dllexport) int WINAPI KofVs_MusicPads(AppMusicPad* out, int max) {
    return app_music_pads(&g_app, out, max);
}

/* Called repeatedly while a music menu is on screen; expires on its own so a
   crashed menu can never leave the game without input. */
__declspec(dllexport) void WINAPI KofVs_MusicMenu(int open) {
    app_music_menu(&g_app, open);
}
typedef HANDLE (WINAPI *CreateFileW_fn)(const wch*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
static CreateFileW_fn g_createfile;
static DID_Simple_fn g_poll_a, g_poll_w;
static DID_GetDeviceState_fn g_getstate_a, g_getstate_w;
static DID_GetDeviceData_fn g_getdata_a, g_getdata_w;

static volatile long g_stat_virtual;
static volatile long g_stat_poll_blocked;
static volatile long g_stat_kb_zeroed;
static volatile long g_stat_joy_zeroed;

static Pad g_last[4];
static DWORD g_packet[4];
static u64 g_key_saved[8];
static int g_key_saved_ok;
static int g_key_active;
static int g_key_last_p1 = -1;
static int g_key_last_p2 = -1;

static void logs(const char* a, const char* b) {
    char buf[640];
    Str s;
    str_init(&s, buf, (int)sizeof(buf));
    str_add(&s, a);
    if (b) str_add(&s, b);
    app_log(buf);
}

static void logx(const char* a, u64 v) {
    char buf[200];
    Str s;
    str_init(&s, buf, (int)sizeof(buf));
    str_add(&s, a);
    str_add(&s, "0x");
    str_hex(&s, v, v > 0xFFFFFFFFull ? 16 : 8);
    app_log(buf);
}

static volatile long g_last_mode = -1000;
static int g_local_humans = 2;

/* Mode values from main-menu button handlers (same IDs in '98 and 2002):
   0 single/arcade single, 1 team/arcade, 2 single 2P VS, 3 team/arcade 2P VS,
   6 practice, 7 endless, 9 team/arcade CPU VS, 10 single CPU VS.
   Evidence: analysis/k02_mode_refs.txt and analysis/k98_mainmenu_press.txt.
   Online (btnxboxlive) does not write this mode dword in the known handlers;
   until a live log names its value, Online stays pending and is treated as inactive here. */
static const char* mode_label(int mode) {
    switch (mode) {
    case 0: return "single/arcade-1P";
    case 1: return "team/arcade-1P";
    case 2: return "single-2P-VS";
    case 3: return "team/arcade-2P-VS";
    case 6: return "practice";
    case 7: return "endless";
    case 9: return "cpu-VS-team";
    case 10: return "cpu-VS-single";
    case -1: return "menu";
    case 0xFF: return "neogeo/menu";
    default: return "unknown";
    }
}

static int classify_play_mode(int mode, int* humans_out) {
    int humans = 1;
    int active = 0;
    switch (mode) {
    case 2:
    case 3:
        humans = 2;
        active = 1;
        break;
    case 0:
    case 1:
    case 6:
    case 7:
    case 9:
    case 10:
        humans = 1;
        active = 1;
        break;
    default:
        humans = 1;
        active = 0;
        break;
    }
    if (humans_out) *humans_out = humans;
    return active;
}

static int game_vs_active(void) {
    int mode;
    int humans = 1;
    int active;
    if (!g_game) return 0;
    mode = *(volatile int*)(g_base + g_game->mode_rva);
    active = classify_play_mode(mode, &humans);
    if (mode != g_last_mode) {
        char buf[160];
        Str s;
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "GAME MODE ");
        str_int(&s, (int)g_last_mode);
        str_add(&s, " -> ");
        str_int(&s, mode);
        str_add(&s, " (");
        str_add(&s, mode_label(mode));
        str_add(&s, ") local_humans=");
        str_int(&s, active ? humans : 0);
        if (!active && mode != -1 && mode != 0xFF && mode != (int)0xFFFFFFFFu)
            str_add(&s, " SCREEN SKIP reason=mode not in character-select set");
        else if (active)
            str_add(&s, " -> intercept character select");
        app_log(buf);
        g_last_mode = mode;
    }
    if (g_app.cfg.debug_force_vs) {
        g_local_humans = 2;
        return 1;
    }
    g_local_humans = humans;
    return active;
}

static int game_local_humans(void) {
    if (g_app.cfg.debug_force_vs) return 2;
    return g_local_humans <= 1 ? 1 : 2;
}

static void dll_frame(void* dev, int w, int h) {
    static int drawn;
    app_frame(dev, w, h);
    if (g_app.cfg.debug_snapshot && g_app.ui_visible && ++drawn == 90) {
        wch path[320];
        wstr_copy(path, 320, g_dir);
        wstr_cat(path, 320, L"kofvs_overlay.png");
        char buf[160];
        Str s;
        int ok = overlay_snapshot_png(dev, path);
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, ok ? "DEBUG SNAPSHOT kofvs_overlay.png written from the game backbuffer " : "DEBUG SNAPSHOT failed: backbuffer ");
        str_int(&s, w);
        str_ch(&s, 'x');
        str_int(&s, h);
        str_add(&s, " format=");
        str_int(&s, g_snap_format);
        str_add(&s, " multisample=");
        str_int(&s, g_snap_ms);
        app_log(buf);
    }
}

static const int* game_portmap(void) {
    return g_game ? (const int*)(g_base + g_game->portmap_rva) : 0;
}

static int pad_differs(const Pad* a, const Pad* b) {
    return a->buttons != b->buttons || a->lt != b->lt || a->rt != b->rt || a->lx != b->lx || a->ly != b->ly || a->rx != b->rx || a->ry != b->ry;
}

static void fill_state(int idx, const Pad* pad, XINPUT_STATE_* st) {
    if (pad_differs(&g_last[idx], pad)) {
        g_last[idx] = *pad;
        g_packet[idx]++;
    }
    st->dwPacketNumber = g_packet[idx];
    st->wButtons = pad->buttons;
    st->bLeftTrigger = pad->lt;
    st->bRightTrigger = pad->rt;
    st->sThumbLX = pad->lx;
    st->sThumbLY = pad->ly;
    st->sThumbRX = pad->rx;
    st->sThumbRY = pad->ry;
}

static PadRide g_slot_ride[4];

static DWORD passthrough_xget(DWORD idx, XINPUT_STATE_* st) {
    DWORD r = g_xget(idx, st);
    Pad pad;
    if (r != ERROR_SUCCESS_ || idx >= 4 || !st) return r;
    if (app_music_menu_active(&g_app)) {
        memset(&pad, 0, sizeof(pad));
        fill_state((int)idx, &pad, st);
        return r;
    }
    pad.buttons = st->wButtons;
    pad.lt = st->bLeftTrigger;
    pad.rt = st->bRightTrigger;
    pad.lx = pad.ly = pad.rx = pad.ry = 0;
    pad_strip_riding(&g_slot_ride[idx], &pad);
    st->wButtons = pad.buttons;
    return r;
}

static DWORD WINAPI hk_xget(DWORD idx, XINPUT_STATE_* st) {
    int access = (int)g_app.access;
    int p1, p2, kind;
    Pad pad;
    if (access == ACCESS_PASSTHROUGH || idx >= 4 || !st) return passthrough_xget(idx, st);
    p1 = (int)g_app.p1_uid;
    p2 = (int)g_app.p2_uid;
    kind = route_decide(access, game_portmap(), (int)idx, p1 >= 0, p2 >= 0);
    _InterlockedIncrement(&g_stat_virtual);
    memset(&pad, 0, sizeof(pad));
    if (kind == ROUTE_P1 || kind == ROUTE_P2) {
        if (!phys_read_live(&g_app.phys, kind == ROUTE_P1 ? p1 : p2, &pad)) memset(&pad, 0, sizeof(pad));
        app_side_pad(&g_app, kind == ROUTE_P1 ? 0 : 1, &pad);
    } else if (kind == ROUTE_DISCONNECTED) {
        XINPUT_STATE_ real;
        if (g_xget(idx, &real) != ERROR_SUCCESS_) return ERROR_DEVICE_NOT_CONNECTED_;
    }
    if (app_music_menu_active(&g_app)) memset(&pad, 0, sizeof(pad));
    fill_state((int)idx, &pad, st);
    return ERROR_SUCCESS_;
}

static DWORD WINAPI hk_xset(DWORD idx, XINPUT_VIBRATION_* vib) {
    int access = (int)g_app.access;
    int kind, phys;
    if (access == ACCESS_PASSTHROUGH || idx >= 4) return g_xset(idx, vib);
    kind = route_decide(access, game_portmap(), (int)idx, g_app.p1_uid >= 0, g_app.p2_uid >= 0);
    if (kind != ROUTE_P1 && kind != ROUTE_P2) return ERROR_SUCCESS_;
    phys = phys_xinput_index(&g_app.phys, (int)(kind == ROUTE_P1 ? g_app.p1_uid : g_app.p2_uid));
    if (phys < 0) return ERROR_SUCCESS_;
    return g_xset((DWORD)phys, vib);
}

static int di_ours(void* dev) {
    return phys_owns_device(&g_app.phys, dev);
}

static int di_passthrough(void) {
    return (int)g_app.access == ACCESS_PASSTHROUGH && !app_music_menu_active(&g_app);
}

/* Returning INPUTLOST makes the game keep the last joystick snapshot (classic
   buttons) and OR it with our remapped XInput, so San Felipe never sticks.
   Empty success clears that cache. */
static HRESULT WINAPI hk_poll_a(void* dev) {
    if (!di_passthrough() && !di_ours(dev)) {
        _InterlockedIncrement(&g_stat_poll_blocked);
        return 0;
    }
    return g_poll_a(dev);
}

static HRESULT WINAPI hk_poll_w(void* dev) {
    if (!di_passthrough() && !di_ours(dev)) {
        _InterlockedIncrement(&g_stat_poll_blocked);
        return 0;
    }
    return g_poll_w(dev);
}

static HRESULT silence_state(DID_GetDeviceState_fn orig, void* dev, DWORD size, void* data) {
    if (di_ours(dev) || di_passthrough()) return orig(dev, size, data);
    if (size == 256 && !g_app.cfg.block_keyboard) return orig(dev, size, data);
    if (data && size) memset(data, 0, size);
    if (size == 256) _InterlockedIncrement(&g_stat_kb_zeroed);
    else _InterlockedIncrement(&g_stat_joy_zeroed);
    return 0;
}

static HRESULT WINAPI hk_getstate_a(void* dev, DWORD size, void* data) {
    return silence_state(g_getstate_a, dev, size, data);
}

static HRESULT WINAPI hk_getstate_w(void* dev, DWORD size, void* data) {
    return silence_state(g_getstate_w, dev, size, data);
}

static HRESULT silence_data(DID_GetDeviceData_fn orig, void* dev, DWORD cb, void* recs, DWORD* count, DWORD flags) {
    if (di_ours(dev) || di_passthrough()) return orig(dev, cb, recs, count, flags);
    if (count) *count = 0;
    _InterlockedIncrement(&g_stat_joy_zeroed);
    return 0;
}

static HRESULT WINAPI hk_getdata_a(void* dev, DWORD cb, void* recs, DWORD* count, DWORD flags) {
    return silence_data(g_getdata_a, dev, cb, recs, count, flags);
}

static HRESULT WINAPI hk_getdata_w(void* dev, DWORD cb, void* recs, DWORD* count, DWORD flags) {
    return silence_data(g_getdata_w, dev, cb, recs, count, flags);
}

static HANDLE WINAPI hk_createfile(const wch* name, DWORD access, DWORD share, void* sec, DWORD disp, DWORD flags, HANDLE templ) {
    int kind = boundary_classify(&g_app.bnd, name);
    if (kind != BGM_NONE) {
        app_bgm_event(&g_app, kind);
        if (g_app.cfg.diagnostics) {
            char buf[300];
            char path[260];
            Str s;
            wstr_to_ascii(name, path, (int)sizeof(path));
            str_init(&s, buf, (int)sizeof(buf));
            str_add(&s, kind == BGM_SELECT ? "BGM SELECT-TRACK " : "BGM OTHER-TRACK ");
            str_add(&s, path);
            str_add(&s, " state=");
            str_add(&s, sel_state_name(g_app.sel.state));
            app_log(buf);
        }
    }
    return g_createfile(name, access, share, sec, disp, flags, templ);
}

static const GameProfile* detect_game(void) {
    u32 ts = 0, size = 0;
    int i, j;
    char buf[160];
    Str s;
    if (!pe_identity((HMODULE)g_base, &ts, &size)) return 0;
    str_init(&s, buf, (int)sizeof(buf));
    str_add(&s, "EXE timestamp=0x");
    str_hex(&s, ts, 8);
    str_add(&s, " SizeOfImage=0x");
    str_hex(&s, size, 8);
    app_log(buf);
    for (i = 0; i < (int)(sizeof(g_profiles) / sizeof(g_profiles[0])); ++i) {
        const GameProfile* g = &g_profiles[i];
        int ok = 1;
        if (g->timestamp != ts || g->size_of_image != size) continue;
        for (j = 0; j < 10; ++j)
            if (g_base[g->sig1_rva + j] != g->sig1[j]) ok = 0;
        for (j = 0; j < 7; ++j)
            if (g_base[g->sig2_rva + j] != g->sig2[j]) ok = 0;
        logs(ok ? "GAME MATCH " : "GAME SIGNATURE MISMATCH ", g->id);
        if (ok) return g;
    }
    return 0;
}

static i64 WINAPI wndproc(HWND hwnd, u32 msg, u64 wp, i64 lp) {
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND make_window(void) {
    WNDCLASSEXW_ wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = g_self;
    wc.lpszClassName = L"KofVsSelectorHidden";
    RegisterClassExW(&wc);
    return CreateWindowExW(0, L"KofVsSelectorHidden", L"KofVsSelector", 0x00C00000u, 0, 0, 64, 64, 0, 0, g_self, 0);
}

static void hook_dinput_iface(const GUID* iid, const char* tag,
                              DID_Simple_fn poll_hk, DID_Simple_fn* poll_orig,
                              DID_GetDeviceState_fn state_hk, DID_GetDeviceState_fn* state_orig,
                              DID_GetDeviceData_fn data_hk, DID_GetDeviceData_fn* data_orig) {
    HMODULE dm = phys_load_system_dinput8();
    DirectInput8Create_fn create = dm ? (DirectInput8Create_fn)GetProcAddress(dm, "DirectInput8Create") : 0;
    void* di = 0;
    void* kb = 0;
    void** vt;
    char buf[160];
    Str s;
    if (!create || create(GetModuleHandleW(0), DI_VERSION, iid, &di, 0) < 0 || !di) {
        logs("DINPUT HOOK FAILED: DirectInput8Create ", tag);
        return;
    }
    if (VT(di, DI8_CreateDevice, DI8_CreateDevice_fn)(di, &GUID_SysKeyboard_, &kb, 0) < 0 || !kb) {
        logs("DINPUT HOOK FAILED: CreateDevice(SysKeyboard) ", tag);
        VT(di, DID_Release, Release_fn)(di);
        return;
    }
    vt = *(void***)kb;
    str_init(&s, buf, (int)sizeof(buf));
    str_add(&s, "DINPUT");
    str_add(&s, tag);
    str_add(&s, " device vtable 0x");
    str_hex(&s, (u64)vt, 16);
    app_log(buf);
    if (vtable_patch(vt, DID_Poll, (void*)poll_hk, (void**)poll_orig) &&
        vtable_patch(vt, DID_GetDeviceState, (void*)state_hk, (void**)state_orig) &&
        vtable_patch(vt, DID_GetDeviceData, (void*)data_hk, (void**)data_orig))
        logs("HOOK IDirectInputDevice8 Poll/GetDeviceState/GetDeviceData ", tag);
    else
        logs("DINPUT HOOK FAILED: vtable patch ", tag);
    VT(kb, DID_Release, Release_fn)(kb);
    VT(di, DID_Release, Release_fn)(di);
}

static void log_stats(void) {
    char buf[200];
    Str s;
    long v = _InterlockedExchange(&g_stat_virtual, 0);
    long p = _InterlockedExchange(&g_stat_poll_blocked, 0);
    long k = _InterlockedExchange(&g_stat_kb_zeroed, 0);
    long j = _InterlockedExchange(&g_stat_joy_zeroed, 0);
    if (!v && !p && !k && !j) return;
    str_init(&s, buf, (int)sizeof(buf));
    str_add(&s, "STATS 10s: virtual XInput reads=");
    str_int(&s, v);
    str_add(&s, " blocked DI polls=");
    str_int(&s, p);
    str_add(&s, " keyboard frames zeroed=");
    str_int(&s, k);
    str_add(&s, " DI joystick states zeroed=");
    str_int(&s, j);
    str_add(&s, " overlay frames=");
    str_int(&s, (long long)overlay_frames());
    app_log(buf);
}

static void log_route(void) {
    const int* pm = game_portmap();
    char buf[200];
    Str s;
    int i;
    str_init(&s, buf, (int)sizeof(buf));
    str_add(&s, "GAME PORT MAP (XInput slot -> player port):");
    for (i = 0; i < 4; ++i) {
        str_add(&s, " ");
        str_int(&s, i);
        str_add(&s, "->");
        str_int(&s, route_port_of(pm, i));
    }
    app_log(buf);
    if (route_slot_for_port(pm, 1) < 0 || route_slot_for_port(pm, 2) < 0)
        app_log("WARNING: no XInput slot feeds port 1 or port 2 in the game's controller settings");
}

static void keymap_restore(void) {
    u64* ptrs;
    DWORD old = 0;
    int i;
    if (!g_key_saved_ok || !g_game || !g_game->keymap_ptrs_rva) return;
    ptrs = (u64*)(g_base + g_game->keymap_ptrs_rva);
    if (!VirtualProtect(ptrs, 64, PAGE_READWRITE, &old)) return;
    for (i = 0; i < 8; ++i) ptrs[i] = g_key_saved[i];
    VirtualProtect(ptrs, 64, old, &old);
    g_key_active = 0;
    g_key_last_p1 = g_key_last_p2 = -1;
    app_log("KEYMAP restored (game default tables)");
}

static void keymap_apply(int p1, int p2) {
    u64* ptrs;
    DWORD old = 0;
    int i;
    char buf[160];
    Str s;
    if (!g_game || !g_game->keymap_ptrs_rva || !g_game->keymap_default_rva) return;
    if (p1 == g_key_last_p1 && p2 == g_key_last_p2 && g_key_active) return;
    ptrs = (u64*)(g_base + g_game->keymap_ptrs_rva);
    if (!VirtualProtect(ptrs, 64, PAGE_READWRITE, &old)) {
        app_log("KEYMAP PATCH FAILED: VirtualProtect");
        return;
    }
    if (!g_key_saved_ok) {
        for (i = 0; i < 8; ++i) g_key_saved[i] = ptrs[i];
        g_key_saved_ok = 1;
        layout_build_game_table(g_base + g_game->keymap_default_rva, g_app.layout_flags);
    }
    ptrs[0] = (u64)layout_game_table(p1);
    ptrs[1] = (u64)layout_game_table(p2);
    VirtualProtect(ptrs, 64, old, &old);
    g_key_active = 1;
    g_key_last_p1 = p1;
    g_key_last_p2 = p2;
    str_init(&s, buf, (int)sizeof(buf));
    str_add(&s, "KEYMAP port1=");
    str_add(&s, layout_name(p1));
    str_add(&s, " port2=");
    str_add(&s, layout_name(p2));
    str_add(&s, " (game table per player)");
    app_log(buf);
}

static DWORD WINAPI worker(void* param) {
    wch path[320];
    HWND hwnd;
    void* present = 0;
    char err[512];
    int ok;
    u64 last, last_stats;
    int last_access = -1;
    int di_hooks_done = 0;
    (void)param;

    g_game = detect_game();
    if (!g_game) {
        app_log("UNSUPPORTED EXECUTABLE: mod stays inactive (no hooks installed)");
        return 0;
    }
    app_config_defaults(&g_app.cfg);
    wstr_copy(path, 320, g_dir);
    wstr_cat(path, 320, L"kofvs.ini");
    if (!app_config_load(&g_app.cfg, path, g_game->id)) app_log("CONFIG kofvs.ini not found: defaults");
    wstr_copy(path, 320, g_dir);
    wstr_cat(path, 320, L"kofvs-layouts.ini");
    layout_memory_set_path(path);
    layout_memory_load();
    layout_set_game_id(g_game->id);
    layout_set_steam_standard(g_app.cfg.steam_pad_standard);
    app_log("CONFIG kofvs-layouts.ini (last layout / calibration per device)");
    if (g_app.cfg.steam_pad_standard)
        app_log("PAD MAP v6 standard Gamepad: R1=RB L1=LB R2=RT L2=LT");
    else if (cstr_ieq(g_game->id, "kof98"))
        app_log("PAD MAP v6 kof98 Steam Virtual: R1=LB L1=LT(+SELECT EX) R2=RT L2=RB");
    else if (cstr_ieq(g_game->id, "kof2002"))
        app_log("PAD MAP v6 kof2002 Steam Virtual: R1=RT L1=LT|LB R2=RB");
    if (!g_app.cfg.enabled) {
        app_log("DISABLED by kofvs.ini (enabled=0): no hooks installed");
        return 0;
    }
    boundary_init(&g_app.bnd, g_game->bgm_folder);
    if (g_app.cfg.select_bgm_count == 0 && g_game->default_select_bgm) {
        cstr_copy(g_app.cfg.select_bgm[0], 40, g_game->default_select_bgm);
        g_app.cfg.select_bgm_count = 1;
    }
    if (g_app.cfg.select_bgm_count == 0)
        app_log("NOTE: no select_bgm configured: between-match reselection only by CTRL+SHIFT+F9 (see README)");
    cstr_copy(g_app.subtitle, (int)sizeof(g_app.subtitle), g_game->title);
    if (g_app.cfg.san_felipe_l1_select < 0 ? g_game->san_felipe_l1_select : g_app.cfg.san_felipe_l1_select)
        g_app.layout_flags |= LAYOUT_F_L1_SELECT;
    cstr_copy(g_app.legend, (int)sizeof(g_app.legend),
        "A=LP B=LK C=HP D=HK  START/SELECT KEEP THEIR ROLES");
    logs("LAYOUTS default=", layout_name(g_app.cfg.sel.default_layout));
    app_log("LAYOUT CLASICA: [A][C][-][-] / [B][D][-][-] factory diamond");
    app_log("LAYOUT SAN FELIPE: [A][B][C][-] / [D][-][-][-]");
    app_log("LAYOUT LINEAL: [A][B][C][D] / [-][-][-][-] Neo Geo AES row");
    app_log("LAYOUT PULGAR: [B][C][D][-] / [A][-][-][-] thumb-A");
    app_log("LAYOUT AB/CD: [A][B][-][-] / [C][D][-][-] weaks top, heavies bottom");
    app_log("LAYOUT MEXICO: [A][B][C][-] / [-][-][D][-] D on R2");
    logs("L1", (g_app.layout_flags & LAYOUT_F_L1_SELECT) ? " +SELECT in every layout (EX versions; START keeps its own role)" : " no SELECT");

    hwnd = make_window();
    if (!bind_real_xinput()) {
        app_log("HOOK FAILED: system XInputGetState not found");
        return 0;
    }
    {
        void* orig_get = (void*)g_xget;
        void* orig_set = (void*)g_xset;
        int hooked = 0;
        hooked |= iat_patch((HMODULE)g_base, "XINPUT1_4.dll", 0, 2, (void*)hk_xget, &orig_get);
        hooked |= iat_patch((HMODULE)g_base, "XINPUT1_3.dll", 0, 2, (void*)hk_xget, &orig_get);
        hooked |= iat_patch((HMODULE)g_base, "XINPUT9_1_0.dll", 0, 2, (void*)hk_xget, &orig_get);
        iat_patch((HMODULE)g_base, "XINPUT1_4.dll", 0, 3, (void*)hk_xset, &orig_set);
        iat_patch((HMODULE)g_base, "XINPUT1_3.dll", 0, 3, (void*)hk_xset, &orig_set);
        iat_patch((HMODULE)g_base, "XINPUT9_1_0.dll", 0, 3, (void*)hk_xset, &orig_set);
        logs("HOOK XInputGetState ", hooked ? "game IAT + system xinput1_3" : "system xinput1_3 (IAT skipped; xinput1_4 proxy feeds the remap)");
    }
    if (iat_patch((HMODULE)g_base, 0, "CreateFileW", 0, (void*)hk_createfile, (void**)&g_createfile)) app_log("HOOK CreateFileW (BGM signal)");
    /* Proton + KoM-as-dinput8: do not LoadLibrary dinput8 early (breaks stage3d).
       Hook now only if already mapped; otherwise retry in the tick loop. */
    if (GetModuleHandleW(L"dinput8.dll")) {
        hook_dinput_iface(&IID_IDirectInput8A_, "A", hk_poll_a, &g_poll_a, hk_getstate_a, &g_getstate_a, hk_getdata_a, &g_getdata_a);
        hook_dinput_iface(&IID_IDirectInput8W_, "W", hk_poll_w, &g_poll_w, hk_getstate_w, &g_getstate_w, hk_getdata_w, &g_getdata_w);
        di_hooks_done = 1;
    } else {
        app_log("DINPUT HOOK DEFER (waiting for game/KoM dinput8)");
        di_hooks_done = 0;
    }

    app_init(&g_app, g_xget, hwnd);
    g_app.vs_active_fn = game_vs_active;
    g_app.local_humans_fn = game_local_humans;
    log_route();
    app_log("MODE GATE: character-select modes 0,1,2,3,6,7,9,10; 2/3=2 local humans; others=1; Online value pending live log");

    if (g_app.cfg.debug_force_vs) app_log("DEBUG: debug_force_vs=1, selector forced active outside VS");
    app_log("OVERLAY creating temporary D3D9 device");
    ok = overlay_find_present(hwnd, &present, err, (int)sizeof(err));
    logs("OVERLAY ", err);
    if (ok && overlay_install(present, dll_frame, err, (int)sizeof(err))) {
        logx("HOOK IDirect3DDevice9::Present (inline, shared by every device) at ", (u64)present);
        logs("OVERLAY ", err);
    } else if (overlay_install_vtable(dll_frame, err, (int)sizeof(err))) {
        app_log("HOOK IDirect3DDevice9::Present (vtable slot in module image, DXVK-style)");
        logs("OVERLAY ", err);
    } else {
        logs("OVERLAY FAILED: ", err);
        app_log("Without overlay the selector cannot be shown: input stays normal (failsafe)");
    }
    app_log("READY");

    last = last_stats = GetTickCount64();
    for (;;) {
        MSG_ msg;
        u64 now;
        while (PeekMessageW(&msg, 0, 0, 0, 1)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep((DWORD)g_app.cfg.tick_ms);
        now = GetTickCount64();
        if (!di_hooks_done && GetModuleHandleW(L"dinput8.dll")) {
            hook_dinput_iface(&IID_IDirectInput8A_, "A", hk_poll_a, &g_poll_a, hk_getstate_a, &g_getstate_a, hk_getdata_a, &g_getdata_a);
            hook_dinput_iface(&IID_IDirectInput8W_, "W", hk_poll_w, &g_poll_w, hk_getstate_w, &g_getstate_w, hk_getdata_w, &g_getdata_w);
            di_hooks_done = 1;
            app_log("DINPUT HOOK LATE (after dinput8/KoM mapped)");
        }
        app_tick(&g_app, (int)(now - last));
        last = now;
        if ((int)g_app.access != last_access) {
            last_access = (int)g_app.access;
            logs("ACCESS ", last_access == ACCESS_PASSTHROUGH ? "PASSTHROUGH (normal game input)" :
                            last_access == ACCESS_BLOCK_ALL ? "BLOCK_ALL (selection screen)" : "ASSIGNED (only P1/P2 devices reach the game)");
            if (last_access == ACCESS_ASSIGNED) {
                char buf[120];
                Str s;
                log_route();
                str_init(&s, buf, (int)sizeof(buf));
                str_add(&s, "APPLY TO GAME P1=");
                str_add(&s, layout_name((int)g_app.p1_layout));
                str_add(&s, " P2=");
                str_add(&s, layout_name((int)g_app.p2_layout));
                app_log(buf);
            }
        }
        if ((int)g_app.access == ACCESS_ASSIGNED)
            keymap_apply((int)g_app.p1_layout, (int)g_app.p2_layout);
        else if (g_key_active)
            keymap_restore();
        if (g_app.cfg.diagnostics && now - last_stats >= 10000) {
            log_stats();
            last_stats = now;
        }
    }
}

int WINAPI DllMain(HMODULE instance, DWORD reason, void* reserved) {
    (void)reserved;
    if (reason == 1) {
        int n;
        wch log_path[320];
        g_self = instance;
        g_base = (u8*)GetModuleHandleW(0);
        DisableThreadLibraryCalls(instance);
        n = (int)GetModuleFileNameW(instance, g_dir, 300);
        while (n > 0 && g_dir[n - 1] != '\\' && g_dir[n - 1] != '/') --n;
        g_dir[n] = 0;
        wstr_copy(log_path, 320, g_dir);
        wstr_cat(log_path, 320, L"kofvs.log");
        app_log_open(log_path);
        app_log("==== KOF VS Controller Selector loaded");
        if (!bind_real_xinput()) app_log("WARNING: system XInput not bound yet");
        CreateThread(0, 0, worker, 0, 0, 0);
    }
    return 1;
}
