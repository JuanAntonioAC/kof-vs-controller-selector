#include "app.h"
#include "layout.h"
#include "overlay.h"

App g_app;

static HANDLE g_log = INVALID_HANDLE;
static volatile long g_log_lock;

__declspec(dllimport) short WINAPI GetAsyncKeyState(int key);
__declspec(dllimport) HWND WINAPI GetForegroundWindow(void);
__declspec(dllimport) DWORD WINAPI GetWindowThreadProcessId(HWND hwnd, DWORD* pid);

void app_log_open(const wch* path) {
    HANDLE in;
    DWORD sz;
    in = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (in != INVALID_HANDLE) {
        sz = GetFileSize(in, 0);
        if (sz != 0xFFFFFFFFu && sz > 400000u) {
            wch prev[320];
            HANDLE out;
            char chunk[4096];
            DWORD got = 0, wr = 0;
            wstr_copy(prev, 320, path);
            wstr_cat(prev, 320, L".1");
            out = CreateFileW(prev, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
            if (out != INVALID_HANDLE) {
                while (ReadFile(in, chunk, (DWORD)sizeof(chunk), &got, 0) && got)
                    WriteFile(out, chunk, got, &wr, 0);
                CloseHandle(out);
            }
            CloseHandle(in);
            in = CreateFileW(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
            if (in != INVALID_HANDLE) CloseHandle(in);
        } else {
            CloseHandle(in);
        }
    }
    g_log = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
}

void app_log(const char* text) {
    SYSTEMTIME_ t;
    char buf[720];
    Str s;
    DWORD w;
    if (g_log == INVALID_HANDLE || !text) return;
    GetLocalTime(&t);
    str_init(&s, buf, (int)sizeof(buf));
    str_int(&s, t.year);
    str_ch(&s, '-');
    str_ch(&s, (char)('0' + t.month / 10));
    str_ch(&s, (char)('0' + t.month % 10));
    str_ch(&s, '-');
    str_ch(&s, (char)('0' + t.day / 10));
    str_ch(&s, (char)('0' + t.day % 10));
    str_ch(&s, ' ');
    str_ch(&s, (char)('0' + t.hour / 10));
    str_ch(&s, (char)('0' + t.hour % 10));
    str_ch(&s, ':');
    str_ch(&s, (char)('0' + t.minute / 10));
    str_ch(&s, (char)('0' + t.minute % 10));
    str_ch(&s, ':');
    str_ch(&s, (char)('0' + t.second / 10));
    str_ch(&s, (char)('0' + t.second % 10));
    str_ch(&s, '.');
    str_ch(&s, (char)('0' + t.ms / 100));
    str_ch(&s, (char)('0' + t.ms / 10 % 10));
    str_ch(&s, (char)('0' + t.ms % 10));
    str_ch(&s, ' ');
    str_add(&s, text);
    str_add(&s, "\r\n");
    while (_InterlockedCompareExchange(&g_log_lock, 1, 0) != 0) Sleep(0);
    WriteFile(g_log, buf, (DWORD)s.len, &w, 0);
    _InterlockedExchange(&g_log_lock, 0);
}

static void log_cb(void* ctx, const char* text) {
    (void)ctx;
    app_log(text);
}

void app_config_defaults(AppConfig* c) {
    memset(c, 0, sizeof(*c));
    c->enabled = 1;
    c->sel.lock_delay_ms = 900;
    c->sel.always_show = 0;
    c->sel.reselect_on_new_device = 1;
    c->sel.default_layout = LAYOUT_CLASSIC;
    c->san_felipe_l1_select = -1;
    c->pol.xbox_via_dinput = 0;
    c->pol.steam_dedupe = STEAM_DEDUPE_AUTO;
    phys_default_config(&c->phys);
    c->block_keyboard = 1;
    c->hotkeys = 1;
    c->diagnostics = 1;
    c->tick_ms = 4;
    c->failsafe_ms = 5000;
}

static int hex4(const char* s, u16* out) {
    int i, v = 0;
    for (i = 0; i < 4; ++i) {
        char c = s[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return 0;
        v = v * 16 + d;
    }
    *out = (u16)v;
    return 1;
}

static void apply_di_profile(PhysConfig* c, const char* k, const char* v) {
    PhysDiProfile* p;
    u16 vid, pid;
    int idx = 0;
    if (cstr_len(k) != 16 || k[11] != '_' || !hex4(k + 7, &vid) || !hex4(k + 12, &pid)) return;
    if (c->di_profile_count >= PHYS_MAX_DI_PROFILES) return;
    p = &c->di_profiles[c->di_profile_count];
    memset(p, 0, sizeof(*p));
    p->vid = vid;
    p->pid = pid;
    while (*v && idx < 32) {
        char name[16];
        int n = 0, code;
        while (*v == ' ' || *v == '\t' || *v == ',') ++v;
        while (*v && *v != ' ' && *v != '\t' && *v != ',') {
            if (n + 1 < (int)sizeof(name)) name[n++] = *v;
            ++v;
        }
        name[n] = 0;
        if (!n) break;
        code = layout_button_code(name);
        p->map[idx++] = code < 0 ? 0 : code;
    }
    if (idx) ++c->di_profile_count;
}

/* "X,A,Y,B": the pad buttons the game reads as A, B, C, D (any order of names). */
static int parse_game_buttons(const char* v, u16* out) {
    u16 seen = 0;
    int idx = 0;
    while (*v && idx < 4) {
        char name[16];
        int n = 0, code;
        while (*v == ' ' || *v == '\t' || *v == ',') ++v;
        while (*v && *v != ' ' && *v != '\t' && *v != ',') {
            if (n + 1 < (int)sizeof(name)) name[n++] = *v;
            ++v;
        }
        name[n] = 0;
        if (!n) break;
        code = layout_button_code(name);
        if (code <= 0 || code > 0xFFFF || (seen & (u16)code)) return 0;
        seen = (u16)(seen | (u16)code);
        out[idx++] = (u16)code;
    }
    return idx == 4;
}

static void apply_key(AppConfig* c, const char* k, const char* v) {
    int n = 0;
    int has = parse_int(v, &n);
    if (cstr_ieq(k, "enabled") && has) c->enabled = n;
    else if (cstr_ieq(k, "lock_delay_ms") && has) c->sel.lock_delay_ms = n;
    else if (cstr_ieq(k, "always_show") && has) c->sel.always_show = n;
    else if (cstr_ieq(k, "reselect_on_new_device") && has) c->sel.reselect_on_new_device = n;
    else if (cstr_ieq(k, "xbox_via_dinput") && has) c->pol.xbox_via_dinput = n;
    else if (cstr_ieq(k, "steam_dedupe")) {
        if (cstr_ieq(v, "off")) c->pol.steam_dedupe = STEAM_DEDUPE_OFF;
        else if (cstr_ieq(v, "all")) c->pol.steam_dedupe = STEAM_DEDUPE_ALL;
        else c->pol.steam_dedupe = STEAM_DEDUPE_AUTO;
    } else if (cstr_ieq(k, "use_dinput") && has) c->phys.use_dinput = n;
    else if (cstr_ieq(k, "xinput_caps") && has) c->phys.xinput_caps = n;
    else if (cstr_ieq(k, "deadzone") && has) c->phys.deadzone = n;
    else if (cstr_ieq(k, "enum_interval_ms") && has) c->phys.enum_interval_ms = n;
    else if (cstr_ieq(k, "block_keyboard") && has) c->block_keyboard = n;
    else if (cstr_ieq(k, "hotkeys") && has) c->hotkeys = n;
    else if (cstr_ieq(k, "diagnostics") && has) c->diagnostics = n;
    else if (cstr_ieq(k, "tick_ms") && has) c->tick_ms = n < 1 ? 1 : n;
    else if (cstr_ieq(k, "failsafe_ms") && has) c->failsafe_ms = n;
    else if (cstr_ieq(k, "debug_force_vs") && has) c->debug_force_vs = n;
    else if (cstr_ieq(k, "debug_snapshot") && has) c->debug_snapshot = n;
    else if (cstr_ieq(k, "default_layout") && layout_parse(v) >= 0) c->sel.default_layout = layout_parse(v);
    else if (cstr_ieq(k, "san_felipe_l1_select") && has) c->san_felipe_l1_select = n;
    else if (cstr_ieq(k, "steam_pad_map")) c->steam_pad_standard = cstr_ieq(v, "standard");
    else if (cstr_ieq(k, "select_bgm")) {
        if (c->select_bgm_count < BOUNDARY_MAX_PATTERNS && v[0])
            cstr_copy(c->select_bgm[c->select_bgm_count++], 40, v);
    } else if (cstr_ieq(k, "log_buttons") && has) c->log_buttons = n;
    else if (cstr_ieq(k, "game_buttons")) c->game_buttons_set = parse_game_buttons(v, c->game_buttons);
    else if (k[0] == 'd' && cstr_find_ci(k, "di_map_") == 0) apply_di_profile(&c->phys, k, v);
    else if (k[0] == 'd' && cstr_find_ci(k, "di_button_") == 0) {
        int idx = -1;
        int code = layout_button_code(v);
        if (parse_int(k + 10, &idx) && idx >= 0 && idx < 32 && code >= 0) c->phys.di_button_map[idx] = code;
    }
}

int app_config_load(AppConfig* c, const wch* path, const char* section) {
    static char text[16384];
    HANDLE f;
    DWORD size, read = 0;
    int i = 0, active = 1;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE) return 0;
    size = GetFileSize(f, 0);
    if (size >= sizeof(text)) size = sizeof(text) - 1;
    ReadFile(f, text, size, &read, 0);
    CloseHandle(f);
    text[read] = 0;
    while (text[i]) {
        char line[160];
        int n = 0, eq = -1, j;
        while (text[i] && text[i] != '\n' && text[i] != '\r') {
            if (n + 1 < (int)sizeof(line)) line[n++] = text[i];
            ++i;
        }
        while (text[i] == '\n' || text[i] == '\r') ++i;
        while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) --n;
        line[n] = 0;
        j = 0;
        while (line[j] == ' ' || line[j] == '\t') ++j;
        if (!line[j] || line[j] == '#' || line[j] == ';') continue;
        if (line[j] == '[') {
            char name[32];
            int k = 0;
            ++j;
            while (line[j] && line[j] != ']' && k + 1 < 32) name[k++] = line[j++];
            name[k] = 0;
            active = cstr_ieq(name, "global") || (section && cstr_ieq(name, section));
            continue;
        }
        if (!active) continue;
        for (n = j; line[n]; ++n)
            if (line[n] == '=') {
                eq = n;
                break;
            }
        if (eq < 0) continue;
        {
            char key[48];
            int e = eq;
            const char* value = line + eq + 1;
            while (e > j && (line[e - 1] == ' ' || line[e - 1] == '\t')) --e;
            cstr_copy(key, e - j + 1 < 48 ? e - j + 1 : 48, line + j);
            while (*value == ' ' || *value == '\t') ++value;
            apply_key(c, key, value);
        }
    }
    return 1;
}

void app_init(App* a, XInputGetState_fn xget, HWND hwnd) {
    int i;
    sel_init(&a->sel, &a->cfg.sel, log_cb, 0);
    reg_init(&a->reg, &a->cfg.pol, log_cb, 0);
    phys_init(&a->phys, &a->reg, &a->cfg.phys, xget, hwnd, log_cb, 0);
    for (i = 0; i < a->cfg.select_bgm_count; ++i) boundary_add_select(&a->bnd, a->cfg.select_bgm[i]);
    a->access = ACCESS_PASSTHROUGH;
    a->p1_uid = -1;
    a->p2_uid = -1;
    a->prev_state = S_IDLE;
    a->ready = 1;
    if (!a->footer[0]) cstr_copy(a->footer, (int)sizeof(a->footer), "ADMIN: CTRL+SHIFT+F9 RESET SELECTION   CTRL+SHIFT+F10 NORMAL INPUT");
}

void app_bgm_event(App* a, int kind) {
    while (_InterlockedCompareExchange(&a->ev_lock, 1, 0) != 0) Sleep(0);
    if (((a->ev_head + 1) & 63) != a->ev_tail) {
        a->ev[a->ev_head] = kind;
        a->ev_head = (a->ev_head + 1) & 63;
    }
    _InterlockedExchange(&a->ev_lock, 0);
}

void app_side_pad(App* a, int side, Pad* pad) {
    int uid = (int)(side == 0 ? a->p1_uid : a->p2_uid);
    int layout = (int)(side == 0 ? a->p1_layout : a->p2_layout);
    RegEntry* e = reg_by_uid(&a->reg, uid);
    int cal[LAYOUT_POS_COUNT];
    int has = 0;
    if (layout != LAYOUT_CLASSIC) {
        if (e) has = layout_fill_cal(e->info.key, e->info.vid, e->info.pid, cal);
        else if (side == 0 && a->p1_has_cal) { has = 1; memcpy(cal, a->p1_cal, sizeof(cal)); }
        else if (side == 1 && a->p2_has_cal) { has = 1; memcpy(cal, a->p2_cal, sizeof(cal)); }
    }
    if (has) layout_cal_normalize(pad, cal);
    layout_apply(pad, layout, a->layout_flags);
}

int app_music_pads(App* a, AppMusicPad* out, int max) {
    int n = 0;
    int access;
    if (!a->ready || !a->cfg.enabled || !out || max <= 0) return -1;
    access = (int)a->access;
    if (access == ACCESS_BLOCK_ALL) return 0;
    if (access == ACCESS_ASSIGNED) {
        int side;
        for (side = 0; side < 2 && n < max; ++side) {
            int uid = (int)(side == 0 ? a->p1_uid : a->p2_uid);
            Pad pad;
            if (uid < 0 || !phys_peek(&a->phys, uid, &pad)) continue;
            app_side_pad(a, side, &pad);
            out[n].id = uid;
            out[n].side = side;
            out[n].pad = pad;
            ++n;
        }
        return n;
    }
    {
        int uids[REG_MAX];
        Pad pads[REG_MAX];
        int i, m = phys_peek_all(&a->phys, uids, pads, REG_MAX);
        for (i = 0; i < m && n < max; ++i) {
            out[n].id = uids[i];
            out[n].side = -1;
            out[n].pad = pads[i];
            ++n;
        }
    }
    return n;
}

void app_music_menu(App* a, int open) {
    a->music_menu_until = open ? GetTickCount64() + 500 : 0;
}

int app_music_menu_active(App* a) {
    u64 until = a->music_menu_until;
    return until && GetTickCount64() < until;
}

static int our_window_focused(void) {
    DWORD pid = 0;
    HWND fg = GetForegroundWindow();
    if (!fg) return 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static void read_hotkeys(App* a, SelSignals* sig) {
    int now = 0;
    int pressed;
    if (!a->cfg.hotkeys || !our_window_focused()) {
        a->hot_prev = 0;
        return;
    }
    if ((GetAsyncKeyState(0x11) & 0x8000) && (GetAsyncKeyState(0x10) & 0x8000)) {
        if (GetAsyncKeyState(0x78) & 0x8000) now |= 1;
        if (GetAsyncKeyState(0x79) & 0x8000) now |= 2;
    }
    pressed = now & ~a->hot_prev;
    a->hot_prev = now;
    if (pressed & 1) sig->admin_reset = 1;
    if (pressed & 2) sig->admin_bypass = 1;
}

static int pad_keys_differ(const Pad* a, const Pad* b) {
    return a->buttons != b->buttons || (a->lt > 0x3F) != (b->lt > 0x3F) || (a->rt > 0x3F) != (b->rt > 0x3F);
}

static void log_inputs(App* a) {
    int i;
    for (i = 0; i < REG_MAX; ++i) {
        RegEntry* e = &a->reg.e[i];
        char buf[560], label[64], text[160];
        Str s;
        int side;
        Pad game;
        if (!e->used || !pad_keys_differ(&e->pad, &e->logged)) continue;
        e->logged = e->pad;
        if (!e->pad.buttons && e->pad.lt <= 0x3F && e->pad.rt <= 0x3F) continue;
        reg_label(e, label, (int)sizeof(label));
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "INPUT CONTROL ");
        str_int(&s, e->number);
        str_ch(&s, ' ');
        str_add(&s, label);
        if (e->mirror_of) {
            str_add(&s, " [DUPLICATE OF uid ");
            str_int(&s, e->mirror_of);
            str_ch(&s, ']');
        }
        if (e->info.kind == KIND_DINPUT) {
            int b, first = 1;
            str_add(&s, " | DI buttons=");
            for (b = 0; b < 32; ++b) {
                if (!(e->raw_buttons & (1u << b))) continue;
                if (!first) str_ch(&s, ',');
                str_int(&s, b);
                first = 0;
            }
            if (first) str_ch(&s, '-');
            str_add(&s, " pov=");
            str_int(&s, e->raw_pov);
        }
        layout_pad_text(&e->pad, text, (int)sizeof(text));
        str_add(&s, " | position: ");
        str_add(&s, text);
        side = sel_uid_side(&a->sel, e->uid);
        if (side >= 0) {
            int layout = sel_side_layout(&a->sel, side);
            int cal[LAYOUT_POS_COUNT];
            game = e->pad;
            if (layout != LAYOUT_CLASSIC && layout_fill_cal(e->info.key, e->info.vid, e->info.pid, cal))
                layout_cal_normalize(&game, cal);
            layout_apply(&game, layout, a->layout_flags);
            layout_game_text(&game, text, (int)sizeof(text));
            str_add(&s, side == 0 ? " | P1 " : " | P2 ");
            str_add(&s, layout_name(layout));
            str_add(&s, " -> game: ");
            str_add(&s, text);
            if (a->cfg.game_buttons_set && layout != LAYOUT_CLASSIC)
                layout_remap_attacks(&game, a->cfg.game_buttons);
            else if (layout_is_deck_pad(e->info.vid, e->info.pid))
                layout_apply_deck(&game, layout);
            else if (layout != LAYOUT_CLASSIC && (a->p2_only_wire == 0 || (a->p2_only_wire == 1 && side == 1))) {
                static const u16 wire[4] = { XB_X, XB_Y, XB_RB, XB_A };
                layout_remap_attacks(&game, wire);
            }
            if (layout != LAYOUT_CLASSIC || layout_is_deck_pad(e->info.vid, e->info.pid)) {
                layout_pad_text(&game, text, (int)sizeof(text));
                str_add(&s, " | sent: ");
                str_add(&s, text);
            }
        }
        app_log(buf);
    }
}

void app_tick(App* a, int dt_ms) {
    SelDevice devs[SEL_MAX_DEVICES];
    SelSignals sig;
    int n = 0;
    phys_tick(&a->phys);
    if (a->cfg.log_buttons) log_inputs(a);
    memset(&sig, 0, sizeof(sig));
    sig.vs_active = a->vs_active_fn ? a->vs_active_fn() : 0;
    if (a->local_humans_fn) {
        int h = a->local_humans_fn();
        sig.local_humans = (h <= 1) ? 1 : 2;
    } else {
        sig.local_humans = 2;
    }
    read_hotkeys(a, &sig);
    if (_InterlockedExchange(&a->admin_reset_req, 0)) sig.admin_reset = 1;
    if (_InterlockedExchange(&a->admin_bypass_req, 0)) sig.admin_bypass = 1;

    {
        u64 now = GetTickCount64();
        int held = a->bnd.select_hold;
        while (_InterlockedCompareExchange(&a->ev_lock, 1, 0) != 0) Sleep(0);
        while (a->ev_tail != a->ev_head) {
            if (boundary_feed(&a->bnd, a->ev[a->ev_tail], now))
                app_log("BGM BOUNDARY CANCELLED: select track replaced (keep controls; next fight/order)");
            else if (!held && a->bnd.select_hold) {
                held = 1;
                app_log("BGM SELECT candidate: waiting to confirm real character select return");
            }
            a->ev_tail = (a->ev_tail + 1) & 63;
        }
        _InterlockedExchange(&a->ev_lock, 0);
        boundary_poll(&a->bnd, now);
    }
    if (boundary_take(&a->bnd)) {
        if (a->sel.state == S_PLAYING) {
            sig.match_boundary = 1;
            app_log("BGM BOUNDARY CONFIRMED: character select BGM held; reopen control screen");
        } else app_log("BGM BOUNDARY OUTSIDE PLAYING: ignored");
    }
    if (!sig.match_boundary && a->match_edge_fn && a->match_edge_fn() && a->sel.state == S_PLAYING)
        sig.match_boundary = 1;

    if (sel_access(&a->sel) == ACCESS_BLOCK_ALL) {
        u64 frames = overlay_frames();
        if (frames != a->failsafe_frames) {
            a->failsafe_frames = frames;
            a->blocked_ms = 0;
        } else {
            a->blocked_ms += dt_ms;
        }
        if (a->cfg.failsafe_ms > 0 && a->blocked_ms > a->cfg.failsafe_ms) {
            app_log(frames == 0 ? "FAILSAFE: the overlay never presented a frame; releasing input (normal game input)"
                                : "FAILSAFE: the overlay stopped presenting frames; releasing input (normal game input)");
            sig.admin_bypass = 1;
            a->blocked_ms = 0;
        }
    } else {
        a->blocked_ms = 0;
        a->failsafe_frames = overlay_frames();
    }

    sel_devices_from_registry(&a->reg, devs, &n);
    sel_update(&a->sel, devs, n, &sig, dt_ms);

    if (a->sel.state == S_PLAYING && a->prev_state != S_PLAYING) boundary_match_started(&a->bnd);
    a->prev_state = a->sel.state;

    a->p1_uid = sel_side_uid(&a->sel, 0);
    a->p2_uid = sel_side_uid(&a->sel, 1);
    a->p1_layout = sel_side_layout(&a->sel, 0);
    a->p2_layout = sel_side_layout(&a->sel, 1);
    {
        int side;
        for (side = 0; side < 2; ++side) {
            RegEntry* e = reg_by_uid(&a->reg, sel_side_uid(&a->sel, side));
            int* cal = side == 0 ? a->p1_cal : a->p2_cal;
            int has = 0;
            if (e) has = layout_fill_cal(e->info.key, e->info.vid, e->info.pid, cal);
            if (side == 0) a->p1_has_cal = has;
            else a->p2_has_cal = has;
        }
    }
    _ReadWriteBarrier();
    a->access = sel_access(&a->sel);

    if (sel_overlay_visible(&a->sel)) {
        UiModel m;
        uimodel_build(&m, &a->sel, &a->reg, a->subtitle, a->footer);
        cstr_copy(m.legend, (int)sizeof(m.legend), a->legend);
        while (_InterlockedCompareExchange(&a->ui_lock, 1, 0) != 0) Sleep(0);
        a->ui = m;
        _InterlockedExchange(&a->ui_lock, 0);
        a->ui_visible = 1;
    } else {
        a->ui_visible = 0;
    }
}

void app_frame(void* device, int width, int height) {
    static UiModel m;
    static UiRect rects[16000];
    int n;
    if (!g_app.ui_visible) return;
    while (_InterlockedCompareExchange(&g_app.ui_lock, 1, 0) != 0) {}
    m = g_app.ui;
    _InterlockedExchange(&g_app.ui_lock, 0);
    n = ui_build(&m, width, height, rects, 16000);
    overlay_draw(device, rects, n);
}

DWORD WINAPI app_thread(void* param) {
    App* a = (App*)param;
    u64 last = GetTickCount64();
    a->running = 1;
    while (a->running) {
        u64 now;
        Sleep((DWORD)a->cfg.tick_ms);
        now = GetTickCount64();
        app_tick(a, (int)(now - last));
        last = now;
    }
    return 0;
}
