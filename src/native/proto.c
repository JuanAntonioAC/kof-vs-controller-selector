#include "app.h"
#include "overlay.h"
#include "route.h"
#include "ui.h"
#include "layout.h"

__declspec(dllimport) HWND WINAPI CreateWindowExW(DWORD ex, const wch* cls, const wch* title, DWORD style, int x, int y, int w, int h, HWND parent, void* menu, HMODULE inst, void* param);
__declspec(dllimport) i64 WINAPI DefWindowProcW(HWND hwnd, u32 msg, u64 wp, i64 lp);
__declspec(dllimport) u16 WINAPI RegisterClassExW(const WNDCLASSEXW_* wc);
__declspec(dllimport) int WINAPI PeekMessageW(MSG_* msg, HWND hwnd, u32 min, u32 max, u32 remove);
__declspec(dllimport) int WINAPI TranslateMessage(const MSG_* msg);
__declspec(dllimport) i64 WINAPI DispatchMessageW(const MSG_* msg);
__declspec(dllimport) int WINAPI ShowWindow(HWND hwnd, int cmd);
__declspec(dllimport) int WINAPI AdjustWindowRect(RECT_* r, DWORD style, int menu);
__declspec(dllimport) void WINAPI PostQuitMessage(int code);
__declspec(dllimport) void* WINAPI LoadCursorW(HMODULE inst, const wch* name);
__declspec(dllimport) short WINAPI GetAsyncKeyState(int key);
__declspec(dllimport) HWND WINAPI GetForegroundWindow(void);
__declspec(dllimport) wch* WINAPI GetCommandLineW(void);

typedef void* (WINAPI *Direct3DCreate9_fn)(u32);
typedef HRESULT (WINAPI *CreateDevice_fn)(void*, u32, u32, HWND, DWORD, void*, void**);
typedef HRESULT (WINAPI *Present_fn)(void*, const void*, const void*, HWND, const void*);
typedef HRESULT (WINAPI *Clear_fn)(void*, DWORD, const void*, DWORD, DWORD, float, DWORD);
typedef HRESULT (WINAPI *Scene_fn)(void*);

#define WIDTH 960
#define HEIGHT 540

static HWND g_hwnd;
static volatile long g_vs;
static volatile long g_quit;
static int g_sim_count = 2;
static XInputGetState_fn g_xget;
static int g_snapshot_req;
static int g_auto;

typedef struct SimKeys {
    int left, right, confirm, cancel;
    const char* name;
} SimKeys;

static const SimKeys g_sim[3] = {
    { 'A', 'D', 'J', 'K', "KEYBOARD A/D J/K" },
    { 0x25, 0x27, 0x0D, 0x08, "KEYBOARD ARROWS ENTER/BACKSPACE" },
    { 0x64, 0x66, 0x65, 0x60, "KEYBOARD NUMPAD 4/6 5/0" },
};

static volatile long g_auto_nav[3];

static i64 WINAPI wndproc(HWND hwnd, u32 msg, u64 wp, i64 lp) {
    if (msg == 0x0010 || msg == 0x0002) {
        g_quit = 1;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int focused(void) {
    return GetForegroundWindow() == g_hwnd;
}

static int key(int vk) {
    return focused() && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static int sim_enum(void* ctx, DevInfo* out, int max) {
    int i, n = 0;
    (void)ctx;
    for (i = 0; i < g_sim_count && n < max; ++i) {
        DevInfo* d = &out[n++];
        memset(d, 0, sizeof(*d));
        d->kind = KIND_SIM;
        d->xi_index = i;
        d->key[0] = 'K';
        d->key[1] = 'B';
        d->key[2] = ':';
        d->key[3] = (char)('1' + i);
        d->key[4] = 0;
        cstr_copy(d->name, (int)sizeof(d->name), g_sim[i].name);
    }
    return n;
}

static int sim_read(void* ctx, const DevInfo* d, Pad* out) {
    const SimKeys* k = &g_sim[d->xi_index];
    unsigned a = (unsigned)g_auto_nav[d->xi_index];
    (void)ctx;
    memset(out, 0, sizeof(*out));
    if (key(k->left) || (a & NAV_LEFT)) out->buttons |= XB_LEFT;
    if (key(k->right) || (a & NAV_RIGHT)) out->buttons |= XB_RIGHT;
    if (key(k->confirm) || (a & NAV_CONFIRM)) out->buttons |= XB_A;
    if (key(k->cancel) || (a & NAV_CANCEL)) out->buttons |= XB_B;
    return 1;
}

static int vs_active(void) {
    return (int)g_vs;
}

static int virtual_read(int idx, Pad* pad) {
    int access = (int)g_app.access;
    int kind = route_decide(access, 0, idx, g_app.p1_uid >= 0, g_app.p2_uid >= 0);
    XINPUT_STATE_ st;
    memset(pad, 0, sizeof(*pad));
    if (kind == ROUTE_REAL) {
        if (!g_xget || g_xget((DWORD)idx, &st) != ERROR_SUCCESS_) return 0;
        pad->buttons = st.wButtons;
        pad->lx = st.sThumbLX;
        pad->ly = st.sThumbLY;
        return 1;
    }
    if (kind == ROUTE_P1 || kind == ROUTE_P2) {
        phys_read_live(&g_app.phys, (int)(kind == ROUTE_P1 ? g_app.p1_uid : g_app.p2_uid), pad);
        return 1;
    }
    if (kind == ROUTE_NEUTRAL) return 1;
    return 0;
}

static void game_view(void* dev, int w, int h) {
    static UiRect rects[6000];
    int n = 0, i;
    char buf[120];
    Str s;
    const char* acc = g_app.access == ACCESS_PASSTHROUGH ? "PASSTHROUGH (NORMAL INPUT)" :
                      g_app.access == ACCESS_BLOCK_ALL ? "BLOCK ALL" : "ASSIGNED: ONLY P1/P2 DEVICES";
    (void)w;
    (void)h;
    n += ui_text_rects("PROTOTYPE - NOT THE GAME", 30, 20, 3, 0xFFF0F0F0u, rects + n, 6000 - n);
    n += ui_text_rects("WHAT THE GAME WOULD READ FROM XINPUT SLOTS 1-4", 30, 60, 2, 0xFFB0B0C0u, rects + n, 6000 - n);
    str_init(&s, buf, (int)sizeof(buf));
    str_add(&s, "ACCESS: ");
    str_add(&s, acc);
    n += ui_text_rects(buf, 30, 90, 2, 0xFFF0C000u, rects + n, 6000 - n);
    str_init(&s, buf, (int)sizeof(buf));
    str_add(&s, "SELECTOR STATE: ");
    str_add(&s, sel_state_name(g_app.sel.state));
    n += ui_text_rects(buf, 30, 115, 2, 0xFFF0C000u, rects + n, 6000 - n);
    for (i = 0; i < 4; ++i) {
        Pad pad;
        int connected = virtual_read(i, &pad);
        int y = 160 + i * 70;
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "SLOT ");
        str_int(&s, i + 1);
        str_add(&s, " -> PORT ");
        str_int(&s, i + 1);
        str_add(&s, connected ? "  CONNECTED" : "  NOT CONNECTED");
        n += ui_text_rects(buf, 30, y, 2, connected ? 0xFFF0F0F0u : 0xFF707084u, rects + n, 6000 - n);
        if (connected) {
            str_init(&s, buf, (int)sizeof(buf));
            str_add(&s, "BUTTONS 0x");
            str_hex(&s, pad.buttons, 4);
            str_add(&s, "  LX ");
            str_int(&s, pad.lx);
            n += ui_text_rects(buf, 30, y + 22, 2, 0xFFB0B0C0u, rects + n, 6000 - n);
            if (n < 6000 && (pad.buttons || pad.lx > 20000 || pad.lx < -20000)) {
                rects[n].x1 = 700;
                rects[n].y1 = y;
                rects[n].x2 = 900;
                rects[n].y2 = y + 40;
                rects[n].color = i == 0 ? 0xFFFF4848u : i == 1 ? 0xFF48A8FFu : 0xFF20D050u;
                ++n;
            }
        }
    }
    n += ui_text_rects("F1 ENTER/LEAVE VS   F2 END MATCH   F12 SNAPSHOT   CTRL+SHIFT+F9 RESET   CTRL+SHIFT+F10 NORMAL", 30, 460, 1, 0xFF9090A0u, rects + n, 6000 - n);
    n += ui_text_rects("SIM PADS: A/D J/K   ARROWS ENTER/BACKSPACE   NUMPAD 4/6 5/0", 30, 480, 1, 0xFF9090A0u, rects + n, 6000 - n);
    overlay_draw(dev, rects, n);
}

static void proto_frame(void* dev, int w, int h) {
    if (!g_app.ui_visible) game_view(dev, w, h);
    app_frame(dev, w, h);
    if (g_snapshot_req) {
        wch path[200];
        char ascii[200];
        Str s;
        str_init(&s, ascii, (int)sizeof(ascii));
        str_add(&s, "D:\\KOF-VS-Controller-Selector\\build\\proto_snapshot_");
        str_int(&s, g_snapshot_req);
        str_add(&s, ".png");
        ascii_to_wstr(ascii, path, 200);
        app_log(overlay_snapshot_png(dev, path) ? "SNAPSHOT written from the real D3D9 backbuffer" : "SNAPSHOT failed");
        g_snapshot_req = 0;
    }
}

static int has_arg(const char* arg) {
    char line[400];
    wstr_to_ascii(GetCommandLineW(), line, (int)sizeof(line));
    return cstr_find_ci(line, arg) >= 0;
}

static void auto_step(u64 t) {
    static int step;
    struct { u64 at; int dev; unsigned nav; int snap; } plan[] = {
        { 600, -1, 0, 0 }, { 900, -1, 0, 1 },
        { 1000, 1, NAV_LEFT, 0 }, { 1100, 1, 0, 0 },
        { 1200, 0, NAV_RIGHT, 0 }, { 1300, 0, 0, 0 },
        { 1400, 2, NAV_LEFT, 0 }, { 1500, 2, 0, 0 },
        { 1600, 1, NAV_CONFIRM, 0 }, { 1700, 1, 0, 0 },
        { 1900, -1, 0, 2 },
        { 2000, 0, NAV_CONFIRM, 0 }, { 2100, 0, 0, 0 },
        { 2300, -1, 0, 3 },
        { 3100, 1, NAV_CONFIRM, 0 },
        { 3300, -1, 0, 5 },
        { 3400, 1, 0, 0 },
        { 3600, 2, NAV_RIGHT | NAV_CONFIRM, 0 },
        { 3800, -1, 0, 4 },
        { 4000, 2, 0, 0 },
    };
    while (step < (int)(sizeof(plan) / sizeof(plan[0])) && t >= plan[step].at) {
        if (plan[step].dev >= 0) g_auto_nav[plan[step].dev] = (long)plan[step].nav;
        if (plan[step].snap) g_snapshot_req = plan[step].snap;
        ++step;
    }
    if (step >= (int)(sizeof(plan) / sizeof(plan[0])) && t > 4500) g_quit = 1;
}

int Main(void) {
    WNDCLASSEXW_ wc;
    RECT_ r;
    HMODULE self = GetModuleHandleW(0);
    HMODULE xi, d3d;
    Direct3DCreate9_fn create;
    void* d9;
    void* dev = 0;
    u32 pp[16];
    u64 start;
    int f1_prev = 0, f2_prev = 0, f12_prev = 0;
    HRESULT hr;

    CreateDirectoryW(L"D:\\KOF-VS-Controller-Selector\\build", 0);
    app_log_open(L"D:\\KOF-VS-Controller-Selector\\build\\proto.log");
    app_log("==== PROTOTYPE start");
    g_auto = has_arg("--auto");
    if (has_arg("--sim0")) g_sim_count = 0;
    if (has_arg("--sim1")) g_sim_count = 1;
    if (has_arg("--sim3")) g_sim_count = 3;
    if (g_auto) g_sim_count = 3;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = self;
    wc.hCursor = LoadCursorW(0, (const wch*)(u64)32512);
    wc.lpszClassName = L"KofVsProto";
    RegisterClassExW(&wc);
    r.left = 0;
    r.top = 0;
    r.right = WIDTH;
    r.bottom = HEIGHT;
    AdjustWindowRect(&r, 0x00CF0000u, 0);
    g_hwnd = CreateWindowExW(0, L"KofVsProto", L"KOF VS Controller Selector - PROTOTIPO (no es el juego)", 0x00CF0000u | 0x10000000u,
                             80, 80, r.right - r.left, r.bottom - r.top, 0, 0, self, 0);
    ShowWindow(g_hwnd, 5);

    xi = LoadLibraryW(L"xinput1_4.dll");
    if (!xi) xi = LoadLibraryW(L"xinput9_1_0.dll");
    g_xget = xi ? (XInputGetState_fn)GetProcAddress(xi, "XInputGetState") : 0;
    app_log(g_xget ? "XInputGetState loaded" : "XInput not available");

    d3d = LoadLibraryW(L"d3d9.dll");
    create = d3d ? (Direct3DCreate9_fn)GetProcAddress(d3d, "Direct3DCreate9") : 0;
    d9 = create ? create(32) : 0;
    if (!d9) {
        app_log("Direct3DCreate9 failed");
        ExitProcess(2);
    }
    memset(pp, 0, sizeof(pp));
    pp[0] = WIDTH;
    pp[1] = HEIGHT;
    pp[2] = 22;
    pp[3] = 1;
    pp[6] = 1;
    *(HWND*)&pp[8] = g_hwnd;
    pp[10] = 1;
    hr = VT(d9, 16, CreateDevice_fn)(d9, 0, 1, g_hwnd, 0x20u, pp, &dev);
    if (hr < 0 || !dev) {
        app_log("CreateDevice failed");
        ExitProcess(3);
    }

    app_config_defaults(&g_app.cfg);
    app_config_load(&g_app.cfg, L"D:\\KOF-VS-Controller-Selector\\config\\kofvs.ini", "proto");
    layout_memory_set_path(L"D:\\KOF-VS-Controller-Selector\\dist\\kofvs-layouts.ini");
    layout_memory_load();
    g_app.cfg.failsafe_ms = 0;
    cstr_copy(g_app.subtitle, (int)sizeof(g_app.subtitle), "PROTOTYPE - REAL DEVICES + KEYBOARD SIMULATED PADS");
    boundary_init(&g_app.bnd, "proto/bgm/");
    boundary_add_select(&g_app.bnd, "select");
    app_init(&g_app, g_xget, g_hwnd);
    g_app.phys.extra_enum = sim_enum;
    g_app.phys.extra_read = sim_read;
    g_app.vs_active_fn = vs_active;

    {
        char err[512];
        void* present = 0;
        int ok = overlay_find_present(g_hwnd, &present, err, (int)sizeof(err));
        app_log(err);
        if (present != overlay_device_present(dev)) app_log("NOTE: temporary device Present differs from the window device Present");
        if (!ok || !overlay_install(present, proto_frame, err, (int)sizeof(err))) {
            app_log("overlay_install failed:");
            app_log(err);
        } else {
            app_log("Present hooked inline from a temporary device (same path as in the game)");
        }
    }
    CreateThread(0, 0, app_thread, &g_app, 0, 0);
    if (g_auto) g_vs = 1;

    start = GetTickCount64();
    while (!g_quit) {
        MSG_ msg;
        int f1, f2, f12;
        while (PeekMessageW(&msg, 0, 0, 0, 1)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        f1 = key(0x70);
        f2 = key(0x71);
        f12 = key(0x7B);
        if (f1 && !f1_prev) {
            g_vs = !g_vs;
            app_log(g_vs ? "PROTO: VS session started (F1)" : "PROTO: VS session left (F1)");
        }
        if (f2 && !f2_prev) {
            app_log("PROTO: simulated match end (F2): fight track then select track");
            app_bgm_event(&g_app, BGM_OTHER);
            app_bgm_event(&g_app, BGM_SELECT);
        }
        if (f12 && !f12_prev) g_snapshot_req = 9;
        f1_prev = f1;
        f2_prev = f2;
        f12_prev = f12;
        if (g_auto) auto_step(GetTickCount64() - start);
        VT(dev, 43, Clear_fn)(dev, 0, 0, 1, 0xFF303040u, 1.0f, 0);
        VT(dev, 41, Scene_fn)(dev);
        VT(dev, 42, Scene_fn)(dev);
        VT(dev, 17, Present_fn)(dev, 0, 0, 0, 0);
    }
    g_app.running = 0;
    Sleep(50);
    overlay_uninstall();
    phys_shutdown(&g_app.phys);
    app_log("==== PROTOTYPE end");
    ExitProcess(0);
    return 0;
}
