/* 32-bit selector for KOF XIII.
   Layouts are applied on the XInput state handed to the game.
   The game's controller configuration is never written. */
#include "app.h"
#include "dinput_min.h"
#include "layout.h"
#include "overlay.h"
#include "route.h"

void* WINAPI kof13_Direct3DCreate9(u32 sdk);
void kof13_set_frame(OverlayFrameFn fn);
extern void* g_kof13_real_d3d;

#define KOF13_TS 0x686CE4A0u
#define KOF13_SIZE 0xA5E000u
#define SVC_TS 0x66E8D201u
#define SVC_SIZE 0x00FBC000u

typedef struct Msg32_ {
    HWND hwnd;
    u32 message;
    u32 wParam;
    i32 lParam;
    DWORD time;
    POINT_ pt;
    DWORD priv;
} Msg32_;

__declspec(dllimport) HWND WINAPI CreateWindowExW(DWORD ex, const wch* cls, const wch* title, DWORD style, int x, int y, int w, int h, HWND parent, void* menu, HMODULE inst, void* param);
__declspec(dllimport) long WINAPI DefWindowProcW(HWND hwnd, u32 msg, u32 wp, i32 lp);
__declspec(dllimport) u16 WINAPI RegisterClassExW(const WNDCLASSEXW_* wc);
__declspec(dllimport) int WINAPI PeekMessageW(Msg32_* msg, HWND hwnd, u32 min, u32 max, u32 remove);
__declspec(dllimport) int WINAPI TranslateMessage(const Msg32_* msg);
__declspec(dllimport) long WINAPI DispatchMessageW(const Msg32_* msg);
#ifndef MEM_RESERVE_
#define MEM_RESERVE_ 0x2000u
#endif

static HMODULE g_self;
static u8* g_base;
static int g_svc;
static int svc_vs;
static u64 svc_grid_at;
static u64 svc_fight_at;
static int svc_phase;
static u64 svc_away_at;
static int svc_last_screen;
static int svc_saw_fight;
static u64 svc_attack_ms;
static u64 svc_last_attack_at;
static u64 svc_last_combo_at;
static int svc_dirs;
static unsigned short svc_prev_dir;
static int svc_steam_menu;
static int g_edge;
static u32 svc_select_mode[8];
static int svc_select_n;
static u32 svc_mode_now;
static u64 svc_mode_since;
static u64 svc_off_grid_since;
static int svc_mode_left;
static int svc_mode_logs;
__declspec(dllimport) void* WINAPI VirtualAlloc(void* addr, ksz size, DWORD type, DWORD protect);
__declspec(dllimport) int WINAPI FlushInstructionCache(HANDLE process, const void* addr, ksz size);
static wch g_dir[300];
static XInputGetState_fn g_xget;
static XInputSetState_fn g_xset;
static DID_Simple_fn g_poll_a, g_poll_w;
static DID_GetDeviceState_fn g_getstate_a, g_getstate_w;
static DID_GetDeviceData_fn g_getdata_a, g_getdata_w;
static Pad g_last[4];
static DWORD g_packet[4];
static volatile int g_hold;
static volatile int g_entry;
static int g_session;
static int g_vs_latched;
static u64 g_boot;
typedef HANDLE (WINAPI *CreateFileW_fn)(const wch*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI *CreateFileA_fn)(const char*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
typedef int (WINAPI *PlaySoundA_fn)(const char*, HMODULE, DWORD);
typedef HANDLE (WINAPI *FindFirstFileExW_fn)(const wch*, int, void*, int, void*, DWORD);
static CreateFileW_fn g_createfile;
static CreateFileA_fn g_createfile_a;
static PlaySoundA_fn g_playsound_a;
static FindFirstFileExW_fn g_findfirst;

static int w_has(const wch* s, const wch* n) {
    int i, k;
    if (!s || !n) return 0;
    for (i = 0; s[i]; ++i) {
        for (k = 0; n[k]; ++k) {
            wch a = s[i + k];
            wch b = n[k];
            if (!a) return 0;
            if (a >= 'A' && a <= 'Z') a = (wch)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (wch)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (!n[k]) return 1;
    }
    return 0;
}

static void svc_on_file(const wch* name);
static void svc_feed_name(const char* name, const char* src);

static int w_select_screen(const wch* name) {
    return w_has(name, L"charactergrid") || w_has(name, L"character_select") ||
           w_has(name, L"music_p_select") || w_has(name, L"characterportrait");
}

static void svc_reset_cycle(void) {
    svc_phase = 0;
    svc_away_at = 0;
    svc_fight_at = 0;
    svc_last_screen = -1;
    svc_saw_fight = 0;
    svc_attack_ms = 0;
    svc_last_attack_at = 0;
    svc_last_combo_at = 0;
    svc_dirs = 0;
    svc_prev_dir = 0;
    svc_steam_menu = 0;
    svc_select_n = 0;
    svc_mode_now = 0;
    svc_mode_since = 0;
    svc_off_grid_since = 0;
    svc_mode_left = 0;
    svc_mode_logs = 0;
}

static void svc_note_fight(const char* why) {
    if (!g_svc || !svc_vs || (int)g_app.sel.state != S_PLAYING) return;
    if (svc_phase) return;
    svc_phase = 1;
    svc_saw_fight = 1;
    svc_away_at = 0;
    svc_fight_at = GetTickCount64();
    app_log(why);
}

static void svc_note_select(const char* why) {
    if (!g_svc || !svc_vs || (int)g_app.sel.state != S_PLAYING) return;
    if (!svc_phase) return;
    svc_phase = 0;
    svc_saw_fight = 0;
    svc_attack_ms = 0;
    svc_last_combo_at = 0;
    svc_away_at = 0;
    svc_mode_left = 0;
    g_edge = 1;
    app_log(why);
}

static int svc_atk_n(unsigned short b) {
    return ((b & XB_X) != 0) + ((b & XB_Y) != 0) + ((b & XB_A) != 0) +
           ((b & XB_B) != 0) + ((b & XB_RB) != 0) + ((b & XB_LB) != 0);
}

static void svc_feed_pad(unsigned short buttons) {
    u64 now = GetTickCount64();
    int n = svc_atk_n(buttons);
    if (!g_svc || !svc_vs || (int)g_app.sel.state != S_PLAYING) return;
    /* Any attack means the round is still going. A 3-button chord only
       marks that a real fight started; one punch must not be ignored. */
    if (n >= 1)
        svc_last_attack_at = now;
    if (n >= 3) {
        if (svc_last_combo_at && now - svc_last_combo_at < 250)
            svc_attack_ms += now - svc_last_combo_at;
        else
            svc_attack_ms += 40;
        svc_last_combo_at = now;
    }
}

static int a_has(const char* s, const char* n) {
    return s && n && cstr_find_ci(s, n) >= 0;
}

static void svc_feed_name(const char* name, const char* src) {
    char line[96];
    Str s;
    if (!name || !g_svc) return;
    if (a_has(name, "overlaymenu") || a_has(name, "commandlist") || a_has(name, "commandlogos")) return;
    if (a_has(name, "charactergrid") || a_has(name, "character_select") ||
        a_has(name, "music_p_select") || a_has(name, "characterportrait")) {
        svc_note_select(src);
        return;
    }
    if (a_has(name, "music_s_ko") || a_has(name, "music_syouri") || a_has(name, "music_b_after") ||
        a_has(name, "music_conti") || a_has(name, "music_st_") || a_has(name, "music_eki") ||
        a_has(name, "music_koujyou") || a_has(name, "music_kyoukai") || a_has(name, "music_shinrin") ||
        a_has(name, "music_shinden") || a_has(name, "music_kodai") || a_has(name, "music_rannyuu") ||
        a_has(name, "music_tengoku") || a_has(name, "music_jigoku") || a_has(name, "music_boss") ||
        a_has(name, "music_innen")) {
        str_init(&s, line, (int)sizeof(line));
        str_add(&s, "SVC fight audio (");
        str_add(&s, src);
        str_add(&s, ")");
        svc_note_fight(line);
    }
}

static void WINAPI svc_on_screen(u32 id) {
    char line[72];
    Str s;
    if (!g_svc || !svc_vs) return;
    if (id == svc_last_screen) return;
    svc_last_screen = (int)id;
    str_init(&s, line, (int)sizeof(line));
    str_add(&s, "SVC screen ");
    str_int(&s, (int)id);
    app_log(line);
    /* 15/16 = pause, 36/37 = command list. Those are NOT PLAYER SELECT.
       The id stays on the last menu after they close it, so it must not
       block the arcade select flag. */
    svc_steam_menu = (id == 15 || id == 16 || id == 36 || id == 37);
}

static HANDLE WINAPI hk_createfile(const wch* name, DWORD access, DWORD share, void* sec, DWORD disp, DWORD flags, HANDLE templ) {
    HANDLE h = g_createfile ? g_createfile(name, access, share, sec, disp, flags, templ) : (HANDLE)(i32)-1;
    if (name && w_has(name, L"controller_select") && !g_svc && g_boot && GetTickCount64() - g_boot >= 3000) {
        g_hold = 1;
        g_session = 1;
        g_entry = 1;
    }
    if (g_svc && name) {
        char ascii[160];
        svc_on_file(name);
        wstr_to_ascii(name, ascii, (int)sizeof(ascii));
        svc_feed_name(ascii, "CreateFileW");
    }
    return h;
}

static HANDLE WINAPI hk_createfile_a(const char* name, DWORD access, DWORD share, void* sec, DWORD disp, DWORD flags, HANDLE templ) {
    HANDLE h = g_createfile_a ? g_createfile_a(name, access, share, sec, disp, flags, templ) : (HANDLE)(i32)-1;
    if (g_svc) svc_feed_name(name, "CreateFileA");
    return h;
}

static int WINAPI hk_playsound_a(const char* name, HMODULE m, DWORD flags) {
    if (g_svc) svc_feed_name(name, "PlaySound");
    return g_playsound_a ? g_playsound_a(name, m, flags) : 0;
}

static HANDLE WINAPI hk_findfirst(const wch* name, int info, void* data, int op, void* extra, DWORD flags) {
    if (g_svc && name) {
        char ascii[160];
        wstr_to_ascii(name, ascii, (int)sizeof(ascii));
        svc_feed_name(ascii, "FindFirst");
    }
    return g_findfirst ? g_findfirst(name, info, data, op, extra, flags) : (HANDLE)(i32)-1;
}

static int svc_local_menu;
static u64 svc_local_at;
static int svc_file_logs;
static int svc_menu_index;
static unsigned short svc_menu_prev;

static void svc_log_file(const wch* name) {
    char buf[96];
    Str s;
    int n = 0, i, start;
    if (svc_file_logs >= 80) return;
    if (!w_has(name, L".csv") && !w_has(name, L".ogg")) return;
    while (name[n]) ++n;
    start = n > 48 ? n - 48 : 0;
    str_init(&s, buf, (int)sizeof(buf));
    str_add(&s, "SVC file ");
    for (i = start; i < n && s.len + 1 < 90; ++i) {
        wch c = name[i];
        str_ch(&s, (c >= 32 && c < 127) ? (char)c : '?');
    }
    svc_file_logs++;
    app_log(buf);
}

static void svc_on_file(const wch* name) {
    u64 now = GetTickCount64();
    int grid = w_has(name, L"charactergrid") || w_has(name, L"character_select");
    if (now > g_boot + 5000) svc_log_file(name);
    if (w_has(name, L"modes.txt")) {
        if (svc_vs) {
            svc_vs = 0;
            g_edge = 0;
            svc_reset_cycle();
            app_log("SVC local menu again: selector waits for 2 Players");
        }
        svc_local_menu = 1;
        svc_local_at = now;
        svc_menu_index = 0;
        svc_menu_prev = 0xFFFF;
        app_log("SVC local menu open, row 0 = single");
        return;
    }
    /* After a loss the game returns to a menu and does not reopen modes.txt
       (the 18:47 file log stopped at 16 lines, before the return). A menu
       file while the match is still the active session means they left it. */
    if (w_has(name, L"standardmenu.csv") && svc_vs && (int)g_app.sel.state == S_PLAYING) {
        svc_vs = 0;
        g_edge = 0;
        svc_local_menu = 0;
        svc_reset_cycle();
        app_log("SVC left the match: selector waits for 2 Players");
    }
    /* 2 Players loads the character select after modes.txt. The csv is also
       opened once at boot, so that early open must not count. */
    if (!grid || !svc_local_menu || now < svc_local_at + 400 || now < g_boot + 8000) return;
    if (svc_grid_at && now - svc_grid_at < 1500) {
        svc_grid_at = now;
        return;
    }
    svc_grid_at = now;
    if (!svc_vs) {
        svc_vs = 1;
        app_log("SVC 2 Players: choose the two controllers");
    } else if ((int)g_app.sel.state == S_PLAYING) {
        g_edge = 1;
        app_log("SVC 2 Players again: choose the two controllers");
    }
}

static long WINAPI wndproc(HWND hwnd, u32 msg, u32 wp, i32 lp) {
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND make_window(void) {
    WNDCLASSEXW_ wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = (WNDPROC_)wndproc;
    wc.hInstance = g_self;
    wc.lpszClassName = L"Kof13SelectorHidden";
    RegisterClassExW(&wc);
    return CreateWindowExW(0, L"Kof13SelectorHidden", L"Kof13Selector", 0x00C00000u, 0, 0, 64, 64, 0, 0, g_self, 0);
}

static int exe_matches(void) {
    u8* nt;
    u32 ts, size;
    int lfanew;
    if (!g_base || *(u16*)g_base != 0x5A4D) return 0;
    lfanew = *(int*)(g_base + 0x3C);
    nt = g_base + lfanew;
    if (*(u32*)nt != 0x4550) return 0;
    ts = *(u32*)(nt + 8);
    size = *(u32*)(nt + 24 + 56);
    {
        char buf[80];
        Str s;
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "EXE timestamp=0x");
        str_hex(&s, ts, 8);
        str_add(&s, " SizeOfImage=0x");
        str_hex(&s, size, 8);
        app_log(buf);
    }
    g_svc = ts == SVC_TS && size == SVC_SIZE;
    return (ts == KOF13_TS && size == KOF13_SIZE) || g_svc;
}

static void parent_of(const wch* dir, wch* out, int cap) {
    int n = wstr_len(dir);
    wstr_copy(out, cap, dir);
    if (n > 0 && (out[n - 1] == '\\' || out[n - 1] == '/')) out[--n] = 0;
    while (n > 0 && out[n - 1] != '\\' && out[n - 1] != '/') --n;
    out[n] = 0;
}

/* Steam Deck: the game folder holds our xinput1_3.dll loader and Proton runs with
   xinput1_3=n,b, so system32\xinput1_3.dll may resolve to that loader instead of
   Wine's XInput (no pad was ever seen). xinput1_4 has another name: use it then. */
static int proxy_in_game_dir(void) {
    HMODULE m = GetModuleHandleW(L"xinput1_3.dll");
    wch path[300];
    char a[300], b[300];
    int n;
    if (!m) return 0;
    n = (int)GetModuleFileNameW(m, path, 300);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/') --n;
    path[n] = 0;
    wstr_to_ascii(path, a, 300);
    wstr_to_ascii(g_dir, b, 300);
    return cstr_ieq(a, b);
}

static int bind_real_xinput(void) {
    static const wch* const names[3] = { L"\\xinput1_4.dll", L"\\xinput9_1_0.dll", L"\\xinput1_3.dll" };
    wch path[300];
    char line[340];
    Str s;
    HMODULE x = 0;
    u32 n;
    int i, first;
    if (g_xget) return 1;
    n = GetSystemDirectoryW(path, 260);
    if (n == 0 || n >= 260) return 0;
    first = proxy_in_game_dir() ? 0 : 2;
    for (i = first; i < 3 && !x; ++i) {
        path[n] = 0;
        wstr_cat(path, 300, names[i]);
        x = LoadLibraryW(path);
    }
    if (!x) return 0;
    str_init(&s, line, (int)sizeof(line));
    str_add(&s, first == 0 ? "XINPUT REAL (loader in game folder): " : "XINPUT REAL: ");
    wstr_to_ascii(path, line + s.len, (int)sizeof(line) - s.len);
    app_log(line);
    g_xget = (XInputGetState_fn)GetProcAddress(x, (const char*)(u32)2);
    if (!g_xget) g_xget = (XInputGetState_fn)GetProcAddress(x, "XInputGetState");
    g_xset = (XInputSetState_fn)GetProcAddress(x, (const char*)(u32)3);
    if (!g_xset) g_xset = (XInputSetState_fn)GetProcAddress(x, "XInputSetState");
    return g_xget != 0;
}

static int iat_name(HMODULE module, const char* dll, const char* func, void* detour, void** original) {
    u8* base = (u8*)module;
    u8* nt;
    u8* desc;
    u16 magic;
    u32 import_rva;
    int lfanew;
    if (!base || *(u16*)base != 0x5A4D || !func) return 0;
    lfanew = *(int*)(base + 0x3C);
    nt = base + lfanew;
    magic = *(u16*)(nt + 24);
    import_rva = magic == 0x20b ? *(u32*)(nt + 24 + 120) : *(u32*)(nt + 24 + 104);
    if (!import_rva) return 0;
    for (desc = base + import_rva; *(u32*)(desc + 12); desc += 20) {
        u32 orig_rva = *(u32*)desc;
        u32 thunk_rva = *(u32*)(desc + 16);
        const char* dll_name = (const char*)(base + *(u32*)(desc + 12));
        u32* orig;
        void** thunk;
        if (dll && !cstr_ieq(dll_name, dll)) continue;
        if (!orig_rva) orig_rva = thunk_rva;
        orig = (u32*)(base + orig_rva);
        thunk = (void**)(base + thunk_rva);
        for (; *orig; ++orig, ++thunk) {
            const char* name;
            DWORD old = 0;
            if (*orig & 0x80000000u) continue;
            name = (const char*)(base + *orig + 2);
            if (!cstr_eq(name, func)) continue;
            if (!VirtualProtect(thunk, sizeof(void*), PAGE_READWRITE, &old)) return 0;
            if (original && !*original) *original = *thunk;
            *thunk = detour;
            VirtualProtect(thunk, sizeof(void*), old, &old);
            return 1;
        }
    }
    return 0;
}

static int iat_ordinal(HMODULE module, const char* dll, int ordinal, void* detour, void** original) {
    u8* base = (u8*)module;
    u8* nt;
    u8* desc;
    u16 magic;
    u32 import_rva;
    int lfanew;
    if (!base || *(u16*)base != 0x5A4D) return 0;
    lfanew = *(int*)(base + 0x3C);
    nt = base + lfanew;
    magic = *(u16*)(nt + 24);
    import_rva = magic == 0x20b ? *(u32*)(nt + 24 + 120) : *(u32*)(nt + 24 + 104);
    if (!import_rva) return 0;
    for (desc = base + import_rva; *(u32*)(desc + 12); desc += 20) {
        u32 orig_rva = *(u32*)desc;
        u32 thunk_rva = *(u32*)(desc + 16);
        const char* dll_name = (const char*)(base + *(u32*)(desc + 12));
        u32* orig;
        void** thunk;
        if (!cstr_ieq(dll_name, dll)) continue;
        if (!orig_rva) orig_rva = thunk_rva;
        orig = (u32*)(base + orig_rva);
        thunk = (void**)(base + thunk_rva);
        for (; *orig; ++orig, ++thunk) {
            DWORD old = 0;
            if (!(*orig & 0x80000000u) || (int)(*orig & 0xFFFFu) != ordinal) continue;
            if (!VirtualProtect(thunk, sizeof(void*), PAGE_READWRITE, &old)) return 0;
            if (original && !*original) *original = *thunk;
            *thunk = detour;
            VirtualProtect(thunk, sizeof(void*), old, &old);
            return 1;
        }
    }
    return 0;
}

static int vtable_patch(void** vtable, int index, void* detour, void** original) {
    DWORD old = 0;
    void** slot = &vtable[index];
    if (*slot == detour) return 1;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return 0;
    if (original && !*original) *original = *slot;
    *slot = detour;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return 1;
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

static void apply_player(int side, int uid, int layout, Pad* pad) {
    RegEntry* e = reg_by_uid(&g_app.reg, uid);
    int cal[LAYOUT_POS_COUNT];
    int has = 0;
    int wire_p2 = !g_svc && side == 1;
    if (layout != LAYOUT_CLASSIC && e)
        has = layout_fill_cal(e->info.key, e->info.vid, e->info.pid, cal);
    if (has) layout_cal_normalize(pad, cal);
    layout_apply(pad, layout, g_app.layout_flags);
    if (layout != LAYOUT_CLASSIC && g_app.cfg.game_buttons_set)
        layout_remap_attacks(pad, g_app.cfg.game_buttons);
    else if (e && layout_is_deck_pad(e->info.vid, e->info.pid)) {
        static int logged_deck;
        /* CLASICA included. On the Deck, raw X/Y/A/B already feels like
           SAN FELIPE, so every layout has to be translated, not only the others. */
        layout_apply_deck(pad, layout);
        if (!logged_deck) {
            logged_deck = 1;
            app_log("SVC DECK buttons A=X B=Y C=RB D=A (Steam virtual pad)");
        }
    }
    /* SVC on PC uses the same factory diamond as KOF 98/2002 (X=LP A=LK Y=HP B=HK).
       KOF XIII keeps the measured P2 wire. */
    else if (layout != LAYOUT_CLASSIC && wire_p2) {
        static const u16 wire[4] = { XB_X, XB_Y, XB_RB, XB_A };
        static int logged_p2;
        layout_remap_attacks(pad, wire);
        if (wire_p2 && !logged_p2) {
            logged_p2 = 1;
            app_log("KOF13 P1 factory, P2 WIRE A=X B=Y C=RB D=A");
        }
    }
}

static PadRide g_slot_ride[4];

static void svc_arm_select(void) {
    svc_local_menu = 0;
    if (!svc_vs) {
        svc_vs = 1;
        app_log("SVC 2 Players confirmed: choose the two controllers");
    } else if ((int)g_app.sel.state == S_PLAYING) {
        g_edge = 1;
        app_log("SVC 2 Players again: choose the two controllers");
    }
}

/* modes.txt is single / two / practice. The 18:24 log confirms 2 Players
   with DOWN then START, and the game opens no new file on that confirm. */
static void svc_menu_feed(unsigned short buttons) {
    unsigned short pressed;
    if (!svc_local_menu || svc_vs) {
        svc_menu_prev = buttons;
        return;
    }
    pressed = (unsigned short)(buttons & (unsigned short)~svc_menu_prev);
    svc_menu_prev = buttons;
    if (pressed & XB_DOWN) {
        if (svc_menu_index < 2) svc_menu_index++;
        app_log(svc_menu_index == 1 ? "SVC local row = two" : "SVC local row moved");
    }
    if (pressed & XB_UP) {
        if (svc_menu_index > 0) svc_menu_index--;
        app_log(svc_menu_index == 1 ? "SVC local row = two" : "SVC local row moved");
    }
    if (pressed & (XB_START | XB_A)) {
        if (svc_menu_index == 1) svc_arm_select();
        else {
            app_log(svc_menu_index == 0 ? "SVC local confirm = single" : "SVC local confirm = practice");
            svc_local_menu = 0;
        }
    }
}

static DWORD passthrough_xget(DWORD idx, XINPUT_STATE_* st) {
    DWORD r = g_xget(idx, st);
    Pad pad;
    if (r != ERROR_SUCCESS_ || idx >= 4 || !st) return r;
    memset(&pad, 0, sizeof(pad));
    pad.buttons = st->wButtons;
    pad.lt = st->bLeftTrigger;
    pad.rt = st->bRightTrigger;
    pad_strip_riding(&g_slot_ride[idx], &pad);
    st->wButtons = pad.buttons;
    /* The menu pad is whichever slot actually sends a button. Slot 0 is often
       an empty Steam pad, and then DOWN/CROSS never reached the menu. */
    if (g_svc && idx < 4) {
        static int menu_pad = -1;
        if (!svc_local_menu) menu_pad = -1;
        else if (menu_pad < 0 && pad.buttons) menu_pad = (int)idx;
        if (menu_pad < 0 || (int)idx == menu_pad) svc_menu_feed(pad.buttons);
    }
    return r;
}

static DWORD WINAPI hk_xget(DWORD idx, XINPUT_STATE_* st) {
    int access = (int)g_app.access;
    int kind;
    Pad pad;
    if (g_hold && (int)g_app.sel.state != S_PLAYING && access == ACCESS_PASSTHROUGH) access = ACCESS_BLOCK_ALL;
    if (access == ACCESS_PASSTHROUGH || idx >= 4 || !st) return passthrough_xget(idx, st);
    kind = route_decide(access, 0, (int)idx, g_app.p1_uid >= 0, g_app.p2_uid >= 0);
    memset(&pad, 0, sizeof(pad));
    if (kind == ROUTE_P1 || kind == ROUTE_P2) {
        int side = kind == ROUTE_P2 ? 1 : 0;
        int uid = (int)(side ? g_app.p2_uid : g_app.p1_uid);
        int layout = (int)(side ? g_app.p2_layout : g_app.p1_layout);
        if (!phys_read_live(&g_app.phys, uid, &pad)) memset(&pad, 0, sizeof(pad));
        if (g_svc) svc_feed_pad(pad.buttons);
        apply_player(side, uid, layout, &pad);
    } else if (kind == ROUTE_DISCONNECTED) {
        XINPUT_STATE_ real;
        if (g_xget(idx, &real) != ERROR_SUCCESS_) return ERROR_DEVICE_NOT_CONNECTED_;
    }
    fill_state((int)idx, &pad, st);
    return ERROR_SUCCESS_;
}

/* The xinput proxy loads this DLL and calls these names. Without the export it
   reads the real pad and the layouts never reach the game. */
__declspec(dllexport) DWORD WINAPI KofVs_XInputGetState(DWORD idx, XINPUT_STATE_* st) {
    return hk_xget(idx, st);
}

static DWORD WINAPI hk_xset(DWORD idx, XINPUT_VIBRATION_* vib) {
    int access = (int)g_app.access;
    int kind, phys;
    if (access == ACCESS_PASSTHROUGH || idx >= 4 || !g_xset) return g_xset ? g_xset(idx, vib) : ERROR_SUCCESS_;
    kind = route_decide(access, 0, (int)idx, g_app.p1_uid >= 0, g_app.p2_uid >= 0);
    if (kind != ROUTE_P1 && kind != ROUTE_P2) return ERROR_SUCCESS_;
    phys = phys_xinput_index(&g_app.phys, (int)(kind == ROUTE_P1 ? g_app.p1_uid : g_app.p2_uid));
    if (phys < 0) return ERROR_SUCCESS_;
    return g_xset((DWORD)phys, vib);
}

__declspec(dllexport) DWORD WINAPI KofVs_XInputSetState(DWORD idx, XINPUT_VIBRATION_* vib) {
    return hk_xset(idx, vib);
}

static int di_ours(void* dev) {
    return phys_owns_device(&g_app.phys, dev);
}

static int di_passthrough(void) {
    if (g_hold && (int)g_app.sel.state != S_PLAYING) return 0;
    return (int)g_app.access == ACCESS_PASSTHROUGH;
}

static HRESULT WINAPI hk_poll_a(void* dev) {
    if (!di_passthrough() && !di_ours(dev)) return 0;
    return g_poll_a(dev);
}
static HRESULT WINAPI hk_poll_w(void* dev) {
    if (!di_passthrough() && !di_ours(dev)) return 0;
    return g_poll_w(dev);
}

static HRESULT silence_state(DID_GetDeviceState_fn orig, void* dev, DWORD size, void* data) {
    if (di_ours(dev) || di_passthrough()) return orig(dev, size, data);
    if (size == 256 && !g_app.cfg.block_keyboard) return orig(dev, size, data);
    if (data && size) memset(data, 0, size);
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
    return 0;
}
static HRESULT WINAPI hk_getdata_a(void* dev, DWORD cb, void* recs, DWORD* count, DWORD flags) {
    return silence_data(g_getdata_a, dev, cb, recs, count, flags);
}
static HRESULT WINAPI hk_getdata_w(void* dev, DWORD cb, void* recs, DWORD* count, DWORD flags) {
    return silence_data(g_getdata_w, dev, cb, recs, count, flags);
}

static void hook_dinput(const GUID* iid, const char* tag,
                         DID_Simple_fn poll_hk, DID_Simple_fn* poll_orig,
                         DID_GetDeviceState_fn state_hk, DID_GetDeviceState_fn* state_orig,
                         DID_GetDeviceData_fn data_hk, DID_GetDeviceData_fn* data_orig) {
    HMODULE dm = phys_load_system_dinput8();
    DirectInput8Create_fn create = dm ? (DirectInput8Create_fn)GetProcAddress(dm, "DirectInput8Create") : 0;
    void* di = 0;
    void* kb = 0;
    void** vt;
    if (!create || create(GetModuleHandleW(0), DI_VERSION, iid, &di, 0) < 0 || !di) {
        app_log(tag[0] == 'A' ? "DINPUT HOOK FAILED A" : "DINPUT HOOK FAILED W");
        return;
    }
    if (VT(di, DI8_CreateDevice, DI8_CreateDevice_fn)(di, &GUID_SysKeyboard_, &kb, 0) < 0 || !kb) {
        VT(di, DID_Release, Release_fn)(di);
        app_log("DINPUT HOOK FAILED: keyboard");
        return;
    }
    vt = *(void***)kb;
    if (vtable_patch(vt, DID_Poll, (void*)poll_hk, (void**)poll_orig) &&
        vtable_patch(vt, DID_GetDeviceState, (void*)state_hk, (void**)state_orig) &&
        vtable_patch(vt, DID_GetDeviceData, (void*)data_hk, (void**)data_orig))
        app_log(tag[0] == 'A' ? "HOOK DirectInput A" : "HOOK DirectInput W");
    else
        app_log("DINPUT HOOK FAILED: vtable");
    VT(kb, DID_Release, Release_fn)(kb);
    VT(di, DID_Release, Release_fn)(di);
}

#define RVA_PROFILE 0x99A218u
#define RVA_PROFILE_GUARD 0x99A598u
#define RVA_SCENE_LIST 0x9B72C4u
#define RVA_SCENE_GUARD 0x9B72E8u
#define RVA_BATTLE 0x4B0168u
#define RVA_SELECT 0x4B55F4u
#define RVA_ORDER 0x4B560Cu
#define PLAYER_STRIDE 0xECu

typedef ksz (WINAPI *VQ86_fn)(const void*, void*, ksz);
typedef struct MBI86_ {
    void* BaseAddress;
    void* AllocationBase;
    DWORD AllocationProtect;
    DWORD RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MBI86_;

static int g_on_main_menu;
static int g_battle_prev;
static int g_fought;
static u64 g_away;
static u64 g_polled;
static int g_ctrl_prev;
static int g_select_prev;
static int g_order_seen;
static u64 g_hold_at;
static char g_scene_names[640];

static int mem_ok(const void* p, u32 n) {
    static VQ86_fn vq;
    MBI86_ mi;
    const u8* a = (const u8*)p;
    if (!p || !n) return 0;
    if (!vq) vq = (VQ86_fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "VirtualQuery");
    if (!vq || !vq(p, &mi, sizeof(mi))) return 0;
    if (mi.State != MEM_COMMIT_ || (mi.Protect & (PAGE_NOACCESS_ | PAGE_GUARD_))) return 0;
    if (a < (const u8*)mi.BaseAddress || a + n > (const u8*)mi.BaseAddress + mi.RegionSize) return 0;
    return 1;
}

static int same_name(const u8* scene, u32 rva, u32 len) {
    u32 p, n, i;
    const u8* s;
    if (!mem_ok(scene + 0x40, 8)) return 0;
    p = *(const u32*)(scene + 0x40);
    n = *(const u32*)(scene + 0x44);
    if (n != len || !p) return 0;
    if (p == (u32)(g_base + rva)) return 1;
    if (!mem_ok((const void*)p, len)) return 0;
    s = (const u8*)p;
    for (i = 0; i < len; ++i)
        if (s[i] != g_base[rva + i]) return 0;
    return 1;
}

static int scene_up(const u8* scene, u32 rva, u32 len) {
    return same_name(scene, rva, len) && mem_ok(scene + 0x4C, 1) && scene[0x4C] != 0;
}

static int scene_named(const u8* scene, const char* needle) {
    u32 p, n, i, m, k;
    const u8* s;
    if (!needle || !mem_ok(scene + 0x40, 8) || !mem_ok(scene + 0x4C, 1) || scene[0x4C] == 0) return 0;
    p = *(const u32*)(scene + 0x40);
    n = *(const u32*)(scene + 0x44);
    m = 0;
    while (needle[m]) ++m;
    if (!m || n < m || n > 64 || !p || !mem_ok((const void*)p, n)) return 0;
    s = (const u8*)p;
    for (i = 0; i + m <= n; ++i) {
        for (k = 0; k < m; ++k) {
            char a = (char)s[i + k];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (a != needle[k]) break;
        }
        if (k == m) return 1;
    }
    return 0;
}

static void name_append(Str* out, const u8* scene) {
    u32 p, n, i;
    if (!mem_ok(scene + 0x40, 8) || !mem_ok(scene + 0x4C, 1)) return;
    p = *(const u32*)(scene + 0x40);
    n = *(const u32*)(scene + 0x44);
    if (!p || !n || n > 40 || !mem_ok((const void*)p, n)) return;
    if (out->len) str_ch(out, ' ');
    for (i = 0; i < n; ++i) {
        char c = (char)((const u8*)p)[i];
        str_ch(out, c >= 0x20 && c < 0x7F ? c : '?');
    }
    if (!scene[0x4C]) str_ch(out, '-');
}

/* "-" after a name = scene present but inactive. Logged once per change so the
   real scene names can be checked after playing (battle detection). */
static void read_scenes(int* battle, int* select, int* order, int* nodes, int* ctrl) {
    u32 guard, sent, node;
    int i;
    char names[640];
    Str list;
    *battle = *select = *order = *nodes = *ctrl = 0;
    g_on_main_menu = 0;
    if (!mem_ok(g_base + RVA_SCENE_GUARD, 4) || !mem_ok(g_base + RVA_SCENE_LIST, 4)) return;
    guard = *(u32*)(g_base + RVA_SCENE_GUARD);
    if (guard == 0 || guard == 0xFFFFFFFFu) return;
    sent = *(u32*)(g_base + RVA_SCENE_LIST);
    if (!sent || !mem_ok((void*)sent, 4)) return;
    str_init(&list, names, (int)sizeof(names));
    node = *(u32*)sent;
    for (i = 0; i < 48 && node && node != sent; ++i) {
        u32 scene;
        if (!mem_ok((void*)node, 12)) break;
        scene = *(u32*)(node + 8);
        ++*nodes;
        if (scene && mem_ok((void*)scene, 0x50)) {
            const u8* s = (const u8*)scene;
            name_append(&list, s);
            if (scene_up(s, RVA_BATTLE, 12)) *battle = 1;
            else if (scene_up(s, RVA_SELECT, 22)) *select = 1;
            else if (scene_up(s, RVA_ORDER, 18)) *order = 1;
            else if (scene_named(s, "controller")) *ctrl = 1;
            if (scene_named(s, "main_menu")) g_on_main_menu = 1;
            else if (scene_named(s, "battle") && !scene_named(s, "replay") && !scene_named(s, "trial")) *battle = 1;
        }
        node = *(u32*)node;
    }
    if (!cstr_eq(names, g_scene_names)) {
        char line[700];
        Str s;
        cstr_copy(g_scene_names, (int)sizeof(g_scene_names), names);
        str_init(&s, line, (int)sizeof(line));
        str_add(&s, "SCENES ");
        str_add(&s, names[0] ? names : "(none)");
        app_log(line);
    }
}

static int two_local(int* t0, int* t1, int* tr0, int* tr1) {
    u8* p;
    u32 guard;
    *t0 = *t1 = *tr0 = *tr1 = 0;
    if (!mem_ok(g_base + RVA_PROFILE_GUARD, 4)) return 0;
    guard = *(u32*)(g_base + RVA_PROFILE_GUARD);
    if (guard == 0 || guard == 0xFFFFFFFFu) return 0;
    p = g_base + RVA_PROFILE;
    if (!mem_ok(p + PLAYER_STRIDE + 0xA9, 4)) return 0;
    *t0 = *(int*)(p + 8);
    *t1 = *(int*)(p + PLAYER_STRIDE + 8);
    *tr0 = p[0xA9];
    *tr1 = p[PLAYER_STRIDE + 0xA9];
    return *t0 == 1 && *t1 == 1 && !*tr0 && !*tr1;
}

static int choosing(void) {
    int st = g_app.sel.state;
    return st == S_LAYOUTS || st == S_SELECTING || st == S_CONFIRMING || st == S_LOCKED || st == S_DETECTING;
}

static void poll_mode(void) {
    u64 now = GetTickCount64();
    int battle, select, order, nodes, ctrl, entry, t0, t1, tr0, tr1, humans, flow, playing;
    char buf[180];
    Str s;
    static int lb = -1, ls = -1, lo = -1, lh = -1, lsess = -1, lnodes = -1;
    if (now == g_polled) return;
    g_polled = now;
    read_scenes(&battle, &select, &order, &nodes, &ctrl);
    humans = two_local(&t0, &t1, &tr0, &tr1);
    flow = battle || select || order;
    playing = (int)g_app.sel.state == S_PLAYING;
    entry = g_entry || (ctrl && !g_ctrl_prev);
    g_entry = 0;
    g_ctrl_prev = ctrl;
    if (entry) {
        if (g_session && playing && g_vs_latched == 2) {
            g_edge = 1;
            app_log("VS ENTRY again: controller screen, choose the two controllers again");
        } else if (!g_session) {
            app_log("VS ENTRY: controller screen, other pads blocked");
        }
        g_session = 1;
        g_hold = 1;
        g_hold_at = now;
        g_vs_latched = 1;
        g_order_seen = 0;
    }
    if (battle || select || order) g_vs_latched = 2;
    if (order) g_order_seen = 1;
    if (battle && humans) g_fought = 1;
    else if (battle && !humans) g_fought = 0;
    /* Leaving the fight for the main menu must not open the selector.
       The next controller screen (a new VS) starts a fresh session. */
    if (!g_svc && g_on_main_menu && !ctrl && g_session && !choosing()) {
        g_session = 0;
        g_vs_latched = 0;
        g_fought = 0;
        g_edge = 0;
        g_order_seen = 0;
        g_hold = 0;
        app_log("VS EXIT main menu: selector hidden");
    }
    g_battle_prev = battle;
    if (select && !g_select_prev && g_order_seen && g_session && playing) {
        g_edge = 1;
        g_order_seen = 0;
        app_log("MATCH END: character select is back after the order screen");
    }
    g_select_prev = select;
    if (!battle && (select || order) && humans) g_session = 1;
    if (g_hold && (playing || (!choosing() && now - g_hold_at > 1500))) g_hold = 0;
    /* While the two controllers play, the session never times out: the fight
       itself is not always visible in the scene list. */
    if (g_session && (flow || choosing() || playing || g_vs_latched == 1)) g_away = 0;
    else if (g_session) {
        if (!g_away) g_away = now;
        if (now - g_away > 4000) {
            g_session = 0;
            g_vs_latched = 0;
            g_hold = 0;
            g_fought = 0;
            g_edge = 0;
        }
    }
    if (battle != lb || select != ls || order != lo || humans != lh || g_session != lsess || nodes != lnodes) {
        lb = battle;
        ls = select;
        lo = order;
        lh = humans;
        lsess = g_session;
        lnodes = nodes;
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "MODE battle=");
        str_int(&s, battle);
        str_add(&s, " select=");
        str_int(&s, select);
        str_add(&s, " order=");
        str_int(&s, order);
        str_add(&s, " nodes=");
        str_int(&s, nodes);
        str_add(&s, " humans=");
        str_int(&s, humans);
        str_add(&s, " p=");
        str_int(&s, t0);
        str_ch(&s, '/');
        str_int(&s, t1);
        str_add(&s, " train=");
        str_int(&s, tr0);
        str_ch(&s, '/');
        str_int(&s, tr1);
        str_add(&s, " session=");
        str_int(&s, g_session);
        app_log(buf);
    }
}

#define SVC_UI_VT_RVA 0x1598D8u

static u8* svc_char_ui;
static int svc_char_vis;

static u8* svc_find_char_ui(void) {
    static VQ86_fn vq;
    MBI86_ mi;
    u8* p = (u8*)0x10000;
    u32 vt = (u32)(ksz)(g_base + SVC_UI_VT_RVA);
    int scanned = 0;
    if (!vq) vq = (VQ86_fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "VirtualQuery");
    if (!vq || !vt) return 0;
    while (scanned < 48 && (ksz)p < 0x7FFF0000u) {
        if (!vq(p, &mi, sizeof(mi))) break;
        if (mi.State == MEM_COMMIT_ && mi.RegionSize >= 8 && mi.RegionSize <= 0x400000u &&
            !(mi.Protect & (PAGE_NOACCESS_ | PAGE_GUARD_)) && (mi.Protect & 4u)) {
            u8* s = (u8*)mi.BaseAddress;
            u8* e = s + mi.RegionSize - 4;
            for (; s < e; s += 4) {
                if (*(u32*)s == vt) return s;
            }
            ++scanned;
        }
        p = (u8*)mi.BaseAddress + mi.RegionSize;
    }
    return 0;
}

static void svc_poll_charui(void) {
    static u64 next_find;
    int vis;
    u64 now = GetTickCount64();
    if (!g_svc || !svc_vs || (int)g_app.sel.state != S_PLAYING) {
        svc_char_vis = 0;
        return;
    }
    if (!svc_char_ui) {
        if (now < next_find) return;
        next_find = now + 2000;
        svc_char_ui = svc_find_char_ui();
        if (svc_char_ui) {
            char line[80];
            Str s;
            str_init(&s, line, (int)sizeof(line));
            str_add(&s, "SVC character UI object found vis=");
            str_int(&s, svc_char_ui[0x4C]);
            app_log(line);
        }
    }
    if (!svc_char_ui || !mem_ok(svc_char_ui, 0x50)) return;
    vis = svc_char_ui[0x4C];
    if (vis != svc_char_vis) {
        char line[72];
        Str s;
        str_init(&s, line, (int)sizeof(line));
        str_add(&s, "SVC character UI vis ");
        str_int(&s, svc_char_vis);
        str_add(&s, " -> ");
        str_int(&s, vis);
        app_log(line);
        if (svc_saw_fight && vis && !svc_char_vis)
            svc_note_select("SVC fight ended: character select is back (UI)");
        svc_char_vis = vis;
    }
}

static void svc_poll_flags(void) {
    static int last_emu = -1, last_extra = -1;
    int emu, extra;
    if (!mem_ok(g_base + 0x2BA3C3u, 1) || !mem_ok(g_base + 0x2FEE44u, 1)) return;
    emu = g_base[0x2BA3C3u] != 0;
    extra = g_base[0x2FEE44u] != 0;
    if (emu == last_emu && extra == last_extra) return;
    {
        char line[80];
        Str s;
        str_init(&s, line, (int)sizeof(line));
        str_add(&s, "SVC state arcade=");
        str_int(&s, emu);
        str_add(&s, " select=");
        str_int(&s, extra);
        app_log(line);
    }
    if (last_emu != -1 && emu && !last_emu) svc_note_fight("SVC fight started (arcade running)");
    if (last_extra != -1 && extra && !last_extra) svc_note_select("SVC fight ended: character select flag");
    last_emu = emu;
    last_extra = extra;
}

/* Same idea as KOF: the arcade BIOS says when a player is in the match.
   0x10FDB6 / 0x10FDB7 are BIOS_PLAYER_MOD (0 idle, 1 playing). The exe reads
   them through the 1KB page table at 0x13634D4, with bytes swapped in the word. */
static int svc_neo_u8(u32 addr, int* out) {
    u8** table;
    u8* page;
    u32 idx, off;
    addr &= 0xFFFFFFu;
    idx = addr >> 10;
    table = (u8**)(g_base + 0xF634D4u);
    if (!mem_ok((u8*)(table + idx), 4)) return 0;
    page = table[idx];
    if (!page) return 0;
    off = (addr ^ 1u) & 0x3FFu;
    if (!mem_ok(page + off, 1)) return 0;
    *out = page[off];
    return 1;
}

static int svc_neo_u16(u32 addr, int* out) {
    u8** table;
    u8* page;
    u32 idx, off;
    addr &= 0xFFFFFEu;
    idx = addr >> 10;
    table = (u8**)(g_base + 0xF634D4u);
    if (!mem_ok((u8*)(table + idx), 4)) return 0;
    page = table[idx];
    if (!page) return 0;
    off = addr & 0x3FFu;
    if (!mem_ok(page + off, 2)) return 0;
    *out = *(unsigned short*)(page + off);
    return 1;
}

static int svc_neo_u32(u32 addr, u32* out) {
    int hi, lo;
    if (!svc_neo_u16(addr, &hi) || !svc_neo_u16(addr + 2, &lo)) return 0;
    *out = ((u32)(hi & 0xFFFF) << 16) | (u32)(lo & 0xFFFF);
    return 1;
}

static int svc_mode_known(u32 mode) {
    int i;
    for (i = 0; i < svc_select_n; ++i)
        if (svc_select_mode[i] == mode) return 1;
    return 0;
}

static void svc_remember_select(u32 mode) {
    char line[72];
    Str s;
    if (!mode || svc_mode_left || svc_select_n >= 8 || svc_mode_known(mode)) return;
    svc_select_mode[svc_select_n++] = mode;
    str_init(&s, line, (int)sizeof(line));
    str_add(&s, "SVC fighter-select flag 0x");
    str_hex(&s, mode, 8);
    app_log(line);
}

/* KOF 98/2002 reopens when the select track comes back after another track.
   SVC keeps that same split in the arcade: 0x10A600 is the screen routine.
   The value on the fighter grid is the select flag. When it leaves and
   later returns, CHOOSE YOUR SIDE comes back. BIOS_PLAYER_MOD stays 1
   for the whole versus session, so it cannot tell the grid from the fight. */
static void svc_poll_players(void) {
    u32 mode = 0;
    u64 now = GetTickCount64();
    int p1 = -1, p2 = -1;
    static int lp1 = -1, lp2 = -1;
    char line[96];
    Str s;
    if (svc_neo_u8(0x10FDB6u, &p1) && svc_neo_u8(0x10FDB7u, &p2) && (p1 != lp1 || p2 != lp2)) {
        str_init(&s, line, (int)sizeof(line));
        str_add(&s, "SVC match P1=");
        str_int(&s, p1);
        str_add(&s, " P2=");
        str_int(&s, p2);
        app_log(line);
        lp1 = p1;
        lp2 = p2;
    }
    if (!svc_neo_u32(0x10A600u, &mode)) return;
    if (mode != svc_mode_now) {
        /* Only the opening grid (0xD8D8) is PLAYER SELECT. 0x09BE also
           flashes between rounds, and treating it as select opened the
           overlay in the fight, where the next button closed it. */
        if (svc_mode_logs < 48) {
            str_init(&s, line, (int)sizeof(line));
            str_add(&s, "SVC screen flag 0x");
            str_hex(&s, svc_mode_now, 8);
            str_add(&s, " -> 0x");
            str_hex(&s, mode, 8);
            app_log(line);
            svc_mode_logs++;
        }
        if (svc_select_n > 0 && mode && !svc_mode_known(mode)) {
            if (!svc_off_grid_since) svc_off_grid_since = now;
        } else if (svc_mode_known(mode)) svc_off_grid_since = 0;
        svc_mode_now = mode;
        svc_mode_since = now;
        if (!svc_mode_left && svc_off_grid_since && now - svc_off_grid_since >= 1500) {
            svc_mode_left = 1;
            svc_note_fight("SVC fight flag (left the character grid)");
        }
        return;
    }
    if (!mode) return;
    if (svc_select_n == 0) {
        if (now - svc_mode_since >= 400) svc_remember_select(mode);
        return;
    }
    if (svc_mode_known(mode)) {
        svc_off_grid_since = 0;
        /* The grid stays up. A flicker of the same code during a round
           does not. Wait until it has been the grid for a full second. */
        if (svc_mode_left && now - svc_mode_since >= 1000)
            svc_note_select("SVC fight ended: fighter select is back");
        return;
    }
    /* On the Deck the fight routine changes constantly, so no single code
       stays still for 2s. Leaving the grid is enough. */
    if (!svc_off_grid_since) svc_off_grid_since = svc_mode_since ? svc_mode_since : now;
    if (!svc_mode_left && now - svc_off_grid_since >= 1500) {
        svc_mode_left = 1;
        svc_note_fight("SVC fight flag (left the character grid)");
    }
}

#define SVC_SWITCH_RVA 0x72140u

static int svc_hook_switch(void) {
    u8* t = g_base + SVC_SWITCH_RVA;
    u8* gate;
    u8 patch[6];
    i32 rel;
    DWORD old = 0;
    int i;
    if (!mem_ok(t, 8)) return 0;
    if (t[0] != 0x55 || t[1] != 0x8B || t[2] != 0xEC || t[3] != 0x8B || t[4] != 0x45 || t[5] != 0x08)
        return 0;
    gate = (u8*)VirtualAlloc(0, 64, MEM_COMMIT_ | MEM_RESERVE_, PAGE_EXECUTE_READWRITE);
    if (!gate) return 0;
    i = 0;
    gate[i++] = 0x60; /* pushad */
    gate[i++] = 0xFF; gate[i++] = 0x74; gate[i++] = 0x24; gate[i++] = 0x24; /* push [esp+36] */
    gate[i++] = 0xE8;
    rel = (i32)((u8*)svc_on_screen - (gate + i + 4));
    gate[i++] = (u8)rel;
    gate[i++] = (u8)(rel >> 8);
    gate[i++] = (u8)(rel >> 16);
    gate[i++] = (u8)(rel >> 24);
    gate[i++] = 0x61; /* popad; stdcall already popped the id */
    gate[i++] = 0x55;
    gate[i++] = 0x8B; gate[i++] = 0xEC;
    gate[i++] = 0x8B; gate[i++] = 0x45; gate[i++] = 0x08;
    gate[i++] = 0xE9;
    rel = (i32)((t + 6) - (gate + i + 4));
    gate[i++] = (u8)rel;
    gate[i++] = (u8)(rel >> 8);
    gate[i++] = (u8)(rel >> 16);
    gate[i++] = (u8)(rel >> 24);
    FlushInstructionCache((HANDLE)(i32)-1, gate, 64);
    patch[0] = 0xE9;
    rel = (i32)(gate - (t + 5));
    patch[1] = (u8)rel;
    patch[2] = (u8)(rel >> 8);
    patch[3] = (u8)(rel >> 16);
    patch[4] = (u8)(rel >> 24);
    patch[5] = 0x90;
    if (!VirtualProtect(t, 6, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(t, patch, 6);
    VirtualProtect(t, 6, old, &old);
    FlushInstructionCache((HANDLE)(i32)-1, t, 6);
    return 1;
}

static int vs_on(void) {
    if (g_svc) return svc_vs;
    poll_mode();
    return g_session;
}

static int match_edge(void) {
    if (g_svc) {
        if (!g_edge) return 0;
        g_edge = 0;
        return 1;
    }
    poll_mode();
    if (!g_edge) return 0;
    g_edge = 0;
    return 1;
}

static DWORD WINAPI worker(void* param) {
    wch parent[300], path[320];
    HWND hwnd;
    char err[256];
    u64 last;
    (void)param;
    if (!exe_matches()) {
        app_log("UNSUPPORTED EXECUTABLE: mod stays inactive (no hooks installed)");
        return 0;
    }
    app_log(g_svc ? "GAME MATCH svc" : "GAME MATCH kof13");
    /* 19:25 log: the same wire made KOF XIII P2 right and P1 wrong.
       P1 keeps the factory diamond. SVC keeps the wire on both. */
    /* 0 = wire both, 1 = wire P2 only, 2 = factory diamond (SVC PC, same as 98). */
    g_app.p2_only_wire = g_svc ? 2 : 1;
    app_config_defaults(&g_app.cfg);
    parent_of(g_dir, parent, 300);
    wstr_copy(path, 320, parent);
    wstr_cat(path, 320, L"kofvs.ini");
    if (!app_config_load(&g_app.cfg, path, g_svc ? "svc" : "kof13")) {
        wstr_copy(path, 320, g_dir);
        wstr_cat(path, 320, L"kofvs.ini");
        if (!app_config_load(&g_app.cfg, path, "kof13")) app_log("CONFIG kofvs.ini not found: defaults");
    }
    g_app.cfg.sel.layout_first = 0;
    wstr_copy(path, 320, parent);
    wstr_cat(path, 320, L"kofvs-layouts.ini");
    layout_memory_set_path(path);
    layout_memory_load();
    layout_set_game_id(g_svc ? "svc" : "kof13");
    layout_set_steam_standard(g_app.cfg.steam_pad_standard);
    if (!g_app.cfg.enabled) {
        app_log("DISABLED by kofvs.ini (enabled=0): no hooks installed");
        return 0;
    }
    boundary_init(&g_app.bnd, "");
    cstr_copy(g_app.subtitle, (int)sizeof(g_app.subtitle), g_svc ? "SVC CHAOS - LOCAL" : "KOF XIII - LOCAL");
    if (g_app.cfg.san_felipe_l1_select > 0) g_app.layout_flags |= LAYOUT_F_L1_SELECT;
    cstr_copy(g_app.legend, (int)sizeof(g_app.legend), "A=LP B=LK C=HP D=HK  CONFIG DEL JUEGO SIN CAMBIOS");
    app_log("LAYOUTS same six grids as 98/2002; applied on the input, game button config is not patched");
    app_log("SECRETS: L2 sends the select button in every layout; L1 no longer does");
    if (!g_svc)
        app_log("FLOW: CHOOSE YOUR SIDE (LEFT/RIGHT), UP/DOWN layout, A ready. Only local VS, again at each match end");
    app_log("XInput slots: 0=P1 1=P2. Extra DirectInput sticks are listed. No keymap write.");
    if (g_svc) {
        app_log("SVC: first CHOOSE YOUR SIDE on 2 Players; again when the fight ends and character select returns");
        app_log("SVC keymap in the exe is the factory diamond (X=LP A=LK Y=HP B=HK); layouts are applied on the input");
        app_log(svc_hook_switch() ? "HOOK SVC screen switch (character select after the fight)"
                                 : "HOOK FAILED: SVC screen switch");
    }
    if (g_app.cfg.game_buttons_set) {
        char line[120];
        Str s;
        int i;
        str_init(&s, line, (int)sizeof(line));
        str_add(&s, "GAME BUTTONS (kofvs.ini game_buttons):");
        for (i = 0; i < 4; ++i) {
            str_ch(&s, ' ');
            str_ch(&s, (char)('A' + i));
            str_ch(&s, '=');
            str_add(&s, layout_button_name(g_app.cfg.game_buttons[i]));
        }
        app_log(line);
    } else {
        app_log("GAME BUTTONS factory: A=X B=A C=Y D=B (set game_buttons in kofvs.ini if the game config differs)");
    }
    if (!bind_real_xinput()) {
        app_log("HOOK FAILED: system XInputGetState not found");
        return 0;
    }
    hwnd = make_window();
    {
        int hooked = 0;
        hooked |= iat_ordinal((HMODULE)g_base, "XINPUT1_3.dll", 2, (void*)hk_xget, 0);
        hooked |= iat_ordinal((HMODULE)g_base, "XINPUT1_4.dll", 2, (void*)hk_xget, 0);
        hooked |= iat_ordinal((HMODULE)g_base, "XINPUT9_1_0.dll", 2, (void*)hk_xget, 0);
        iat_ordinal((HMODULE)g_base, "XINPUT1_3.dll", 3, (void*)hk_xset, 0);
        iat_ordinal((HMODULE)g_base, "XINPUT1_4.dll", 3, (void*)hk_xset, 0);
        iat_ordinal((HMODULE)g_base, "XINPUT9_1_0.dll", 3, (void*)hk_xset, 0);
        app_log(hooked ? "HOOK XInputGetState" : "HOOK FAILED: XInputGetState ordinal 2");
    }
    hook_dinput(&IID_IDirectInput8A_, "A", hk_poll_a, &g_poll_a, hk_getstate_a, &g_getstate_a, hk_getdata_a, &g_getdata_a);
    hook_dinput(&IID_IDirectInput8W_, "W", hk_poll_w, &g_poll_w, hk_getstate_w, &g_getstate_w, hk_getdata_w, &g_getdata_w);
    app_init(&g_app, g_xget, hwnd);
    g_app.vs_active_fn = vs_on;
    /* XIII/SVC keep their existing session gate for now; local_humans defaults to 2
       until that DLL is adapted in a later pass (Phase 1 priority: KOF 98, then 2002). */
    g_app.match_edge_fn = match_edge;
    {
        XINPUT_STATE_ st;
        char line[96];
        Str s;
        int i;
        str_init(&s, line, (int)sizeof(line));
        str_add(&s, "XINPUT SLOTS");
        for (i = 0; i < 4; ++i) {
            str_add(&s, i ? ", " : ": ");
            str_int(&s, i);
            str_add(&s, g_xget((DWORD)i, &st) == ERROR_SUCCESS_ ? "=pad" : "=none");
        }
        app_log(line);
    }
    app_log("READY");
    last = GetTickCount64();
    for (;;) {
        Msg32_ msg;
        u64 now;
        static int overlay_on;
        static int overlay_wait_logged;
        static u64 overlay_next;
        while (PeekMessageW(&msg, 0, 0, 0, 1)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        now = GetTickCount64();
        if (g_svc) {
            if (svc_vs && (int)g_app.sel.state == S_PLAYING) {
                svc_poll_charui();
                svc_poll_flags();
                svc_poll_players();
            } else {
                svc_char_vis = 0;
                svc_away_at = 0;
                if ((int)g_app.sel.state != S_PLAYING) {
                    svc_saw_fight = 0;
                    svc_attack_ms = 0;
                    svc_last_attack_at = 0;
                    svc_last_combo_at = 0;
                    svc_dirs = 0;
                }
            }
        }
        if (!overlay_on && !overlay_wait_logged) {
            overlay_on = 1;
            overlay_wait_logged = 1;
            app_log("OVERLAY draws on the game Present. CHOOSE YOUR SIDE, layout with UP/DOWN.");
        }
        if (now - g_boot < 20000) {
            void* cur = 0;
            if (iat_name((HMODULE)g_base, "d3d9.dll", "Direct3DCreate9", (void*)kof13_Direct3DCreate9, &cur)
                && cur && cur != (void*)kof13_Direct3DCreate9)
                g_kof13_real_d3d = cur;
        }
        app_tick(&g_app, (int)(now - last));
        last = now;
        Sleep((DWORD)g_app.cfg.tick_ms);
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
        g_boot = GetTickCount64();
        if (exe_matches()) {
            app_log(g_svc ? "==== SVC Chaos controller selector loaded" : "==== KOF XIII controller selector loaded");
            kof13_set_frame(app_frame);
            if (iat_name((HMODULE)g_base, "d3d9.dll", "Direct3DCreate9", (void*)kof13_Direct3DCreate9, &g_kof13_real_d3d))
                app_log("HOOK Direct3DCreate9");
            else
                app_log("HOOK FAILED: Direct3DCreate9");
            if (iat_name((HMODULE)g_base, 0, "CreateFileW", (void*)hk_createfile, (void**)&g_createfile))
                app_log("HOOK CreateFileW");
            else
                app_log("HOOK FAILED: CreateFileW");
            if (iat_name((HMODULE)g_base, 0, "CreateFileA", (void*)hk_createfile_a, (void**)&g_createfile_a))
                app_log("HOOK CreateFileA");
            if (iat_name((HMODULE)g_base, "WINMM.dll", "PlaySoundA", (void*)hk_playsound_a, (void**)&g_playsound_a))
                app_log("HOOK PlaySoundA");
            if (iat_name((HMODULE)g_base, 0, "FindFirstFileExW", (void*)hk_findfirst, (void**)&g_findfirst))
                app_log("HOOK FindFirstFileExW");
        }
        CreateThread(0, 0, worker, 0, 0, 0);
    }
    return 1;
}
