#include "win.h"
#include "selector.h"
#include "registry.h"
#include "route.h"
#include "boundary.h"
#include "uimodel.h"
#include "png.h"
#include "hook.h"
#include "layout.h"
#include "physical.h"

static HANDLE g_out;
static int g_pass;
static int g_fail;
static int g_test_fail;
static int g_verbose;

static void out(const char* text) {
    DWORD w;
    WriteFile(g_out, text, (DWORD)cstr_len(text), &w, 0);
}

static void outln(const char* text) {
    out(text);
    out("\r\n");
}

static void check(int cond, const char* what, int line) {
    char buf[200];
    Str s;
    if (cond) {
        ++g_pass;
        return;
    }
    ++g_fail;
    ++g_test_fail;
    str_init(&s, buf, (int)sizeof(buf));
    str_add(&s, "    FAIL line ");
    str_int(&s, line);
    str_add(&s, ": ");
    str_add(&s, what);
    outln(buf);
}

#define CHECK(c) check((c), #c, __LINE__)

static void log_event(void* ctx, const char* text) {
    (void)ctx;
    if (!g_verbose) return;
    out("      | ");
    outln(text);
}

typedef struct Sim {
    Selector s;
    Registry r;
    DevInfo found[REG_MAX];
    int found_count;
    SelSignals sig;
    Boundary b;
} Sim;

static void sim_sync(Sim* m) {
    reg_sync(&m->r, m->found, m->found_count, 0, 0);
}

static int sim_add(Sim* m, const char* key, int kind, int xi, u16 vid, u16 pid, const char* name, int xinput_path) {
    DevInfo* d = &m->found[m->found_count++];
    RegEntry* e;
    memset(d, 0, sizeof(*d));
    cstr_copy(d->key, (int)sizeof(d->key), key);
    d->kind = kind;
    d->xi_index = xi;
    d->vid = vid;
    d->pid = pid;
    d->xinput_path = xinput_path;
    cstr_copy(d->name, (int)sizeof(d->name), name);
    sim_sync(m);
    e = reg_by_key(&m->r, key);
    return e ? e->uid : -1;
}

static void sim_remove(Sim* m, const char* key) {
    int i, j = 0;
    for (i = 0; i < m->found_count; ++i)
        if (!cstr_eq(m->found[i].key, key)) m->found[j++] = m->found[i];
    m->found_count = j;
    sim_sync(m);
}

static void tick_dt(Sim* m, int dt) {
    SelDevice devs[SEL_MAX_DEVICES];
    int n = 0;
    sel_devices_from_registry(&m->r, devs, &n);
    sel_update(&m->s, devs, n, &m->sig, dt);
    m->sig.match_boundary = 0;
    m->sig.admin_reset = 0;
    m->sig.admin_bypass = 0;
}

static void tick(Sim* m) {
    tick_dt(m, 16);
}

static void set_nav(Sim* m, int uid, unsigned nav) {
    RegEntry* e = reg_by_uid(&m->r, uid);
    if (e) e->nav = nav;
}

static void press(Sim* m, int uid, unsigned nav) {
    set_nav(m, uid, nav);
    tick(m);
    set_nav(m, uid, 0);
    tick(m);
}

static void finish_lock(Sim* m) {
    int i;
    for (i = 0; i < 100 && m->s.state == S_LOCKED; ++i) tick(m);
}

static void sim_init(Sim* m) {
    SelConfig cfg;
    RegPolicy pol;
    memset(m, 0, sizeof(*m));
    memset(&cfg, 0, sizeof(cfg));
    cfg.lock_delay_ms = 600;
    cfg.always_show = 0;
    cfg.reselect_on_new_device = 1;
    cfg.default_layout = LAYOUT_CLASSIC;
    pol.xbox_via_dinput = 0;
    pol.steam_dedupe = STEAM_DEDUPE_AUTO;
    layout_memory_reset();
    sel_init(&m->s, &cfg, log_event, 0);
    reg_init(&m->r, &pol, log_event, 0);
    boundary_init(&m->b, "data/BGM/");
    boundary_add_select(&m->b, "UM48000Hz/BGM03.");
    /* Instant confirm in simulation; the live DLL uses BOUNDARY_DEFAULT_CONFIRM_MS. */
    m->b.confirm_ms = 0;
}

static int add_pad(Sim* m, int n) {
    char key[16];
    char name[24];
    Str s;
    str_init(&s, key, (int)sizeof(key));
    str_add(&s, "SIM:");
    str_int(&s, n);
    str_init(&s, name, (int)sizeof(name));
    str_add(&s, "SIM PAD ");
    str_int(&s, n);
    return sim_add(m, key, KIND_DINPUT, -1, 0x1234, (u16)n, name, 0);
}

static void snapshot(Sim* m, const char* file, const char* subtitle) {
    static UiRect rects[20000];
    UiModel model;
    wch path[260];
    char ascii[260];
    Str s;
    int n;
    uimodel_build(&model, &m->s, &m->r, subtitle, "ADMIN: CTRL+SHIFT+F9 RESET   CTRL+SHIFT+F10 NORMAL INPUT");
    cstr_copy(model.legend, (int)sizeof(model.legend), "SAN FELIPE: SQUARE=A  TRIANGLE=B  R1=C  CROSS=D  L1=SELECT (EX)");
    n = ui_build(&model, 960, 540, rects, 20000);
    str_init(&s, ascii, (int)sizeof(ascii));
    str_add(&s, "D:\\KOF-VS-Controller-Selector\\build\\ui\\");
    str_add(&s, file);
    ascii_to_wstr(ascii, path, 260);
    png_render_rects(path, 960, 540, rects, n);
}

static void begin(const char* name) {
    g_test_fail = 0;
    out("  ");
    outln(name);
}

static void end(void) {
    outln(g_test_fail ? "    -> FALLIDA" : "    -> OK (simulada)");
}

static void route_check(Sim* m, int expect_p1_uid, int expect_p2_uid) {
    int v;
    int p1 = sel_side_uid(&m->s, 0);
    int p2 = sel_side_uid(&m->s, 1);
    int access = sel_access(&m->s);
    CHECK(p1 == expect_p1_uid);
    CHECK(p2 == expect_p2_uid);
    for (v = 0; v < ROUTE_SLOTS; ++v) {
        int kind = route_decide(access, 0, v, p1 >= 0, p2 >= 0);
        if (v == 0) CHECK(kind == ROUTE_P1);
        else if (v == 1) CHECK(kind == ROUTE_P2);
        else CHECK(kind == ROUTE_DISCONNECTED);
    }
}

static void test_a(void) {
    Sim m;
    int c1, c2, c3;
    begin("Prueba A: 3 controles; C3 toma P1, C1 toma P2, C2 no interfiere");
    sim_init(&m);
    c1 = add_pad(&m, 1);
    c2 = add_pad(&m, 2);
    c3 = add_pad(&m, 3);
    m.sig.vs_active = 1;
    tick(&m);
    CHECK(m.s.state == S_SELECTING);
    CHECK(sel_access(&m.s) == ACCESS_BLOCK_ALL);
    snapshot(&m, "A1_inicio.png", "KOF 2002 UM - VS (SIMULACION)");
    press(&m, c3, NAV_LEFT);
    CHECK(sel_side_uid(&m.s, 0) == c3);
    CHECK(sel_side_state(&m.s, 0) == SIDE_SELECTED);
    press(&m, c2, NAV_LEFT);
    CHECK(sel_side_uid(&m.s, 0) == c3);
    press(&m, c1, NAV_RIGHT);
    CHECK(sel_side_uid(&m.s, 1) == c1);
    CHECK(m.s.state == S_CONFIRMING);
    snapshot(&m, "A2_ambos_seleccionados.png", "KOF 2002 UM - VS (SIMULACION)");
    press(&m, c2, NAV_LEFT);
    press(&m, c2, NAV_RIGHT);
    press(&m, c2, NAV_CONFIRM);
    press(&m, c2, NAV_CANCEL);
    CHECK(sel_side_uid(&m.s, 0) == c3 && sel_side_uid(&m.s, 1) == c1);
    CHECK(!m.s.side_ready[0] && !m.s.side_ready[1]);
    press(&m, c3, NAV_CONFIRM);
    CHECK(sel_side_state(&m.s, 0) == SIDE_READY);
    snapshot(&m, "A3_p1_ready.png", "KOF 2002 UM - VS (SIMULACION)");
    press(&m, c2, NAV_CANCEL);
    CHECK(sel_side_state(&m.s, 0) == SIDE_READY);
    press(&m, c1, NAV_CONFIRM);
    CHECK(m.s.state == S_LOCKED);
    snapshot(&m, "A4_locked.png", "KOF 2002 UM - VS (SIMULACION)");
    finish_lock(&m);
    CHECK(m.s.state == S_PLAYING);
    CHECK(sel_access(&m.s) == ACCESS_ASSIGNED);
    route_check(&m, c3, c1);
    press(&m, c2, NAV_LEFT | NAV_CONFIRM | NAV_CANCEL);
    CHECK(m.s.state == S_PLAYING);
    route_check(&m, c3, c1);
    end();
}

static void test_b(void) {
    Sim m;
    int c1, c2, c3, c4;
    begin("Prueba B: 4 controles; C4 toma P1, C2 toma P2, C1 y C3 bloqueados");
    sim_init(&m);
    c1 = add_pad(&m, 1);
    c2 = add_pad(&m, 2);
    c3 = add_pad(&m, 3);
    c4 = add_pad(&m, 4);
    m.sig.vs_active = 1;
    tick(&m);
    press(&m, c4, NAV_LEFT);
    press(&m, c2, NAV_RIGHT);
    press(&m, c1, NAV_LEFT);
    press(&m, c3, NAV_RIGHT);
    press(&m, c1, NAV_CONFIRM);
    press(&m, c3, NAV_CANCEL);
    CHECK(sel_side_uid(&m.s, 0) == c4);
    CHECK(sel_side_uid(&m.s, 1) == c2);
    press(&m, c4, NAV_CONFIRM);
    press(&m, c2, NAV_CONFIRM);
    finish_lock(&m);
    CHECK(m.s.state == S_PLAYING);
    route_check(&m, c4, c2);
    CHECK(sel_uid_side(&m.s, c1) < 0 && sel_uid_side(&m.s, c3) < 0);
    snapshot(&m, "B_playing_model.png", "KOF 2002 UM - VS (SIMULACION)");
    end();
}

static void play_match_bgm(Sim* m) {
    boundary_feed(&m->b, boundary_classify(&m->b, L"D:/Game/data/BGM/UM48000Hz/BGM25.ogg"), 0);
}

static void select_bgm(Sim* m) {
    boundary_feed(&m->b, boundary_classify(&m->b, L"D:/Game/data/BGM/UM48000Hz/BGM03.ogg"), 0);
    boundary_poll(&m->b, 0);
    if (boundary_take(&m->b)) m->sig.match_boundary = 1;
}

static void lock_pair(Sim* m, int p1, int p2) {
    press(m, p1, NAV_LEFT);
    press(m, p2, NAV_RIGHT);
    press(m, p1, NAV_CONFIRM);
    press(m, p2, NAV_CONFIRM);
    finish_lock(m);
    boundary_match_started(&m->b);
}

static void test_c(void) {
    Sim m;
    int c1, c2, c3;
    begin("Prueba C: liberacion y nueva seleccion tras el enfrentamiento, con otros jugadores");
    sim_init(&m);
    c1 = add_pad(&m, 1);
    c2 = add_pad(&m, 2);
    c3 = add_pad(&m, 3);
    m.sig.vs_active = 1;
    tick(&m);
    select_bgm(&m);
    tick(&m);
    lock_pair(&m, c3, c1);
    CHECK(m.s.state == S_PLAYING);
    select_bgm(&m);
    tick(&m);
    CHECK(m.s.state == S_PLAYING);
    play_match_bgm(&m);
    tick(&m);
    select_bgm(&m);
    CHECK(m.sig.match_boundary == 1);
    tick(&m);
    CHECK(m.s.state == S_SELECTING);
    CHECK(sel_side_uid(&m.s, 0) < 0 && sel_side_uid(&m.s, 1) < 0);
    CHECK(sel_access(&m.s) == ACCESS_BLOCK_ALL);
    snapshot(&m, "C_reseleccion.png", "KOF 2002 UM - VS - ENFRENTAMIENTO 2 (SIMULACION)");
    lock_pair(&m, c2, c3);
    CHECK(m.s.state == S_PLAYING);
    route_check(&m, c2, c3);
    CHECK(m.s.match == 2);
    m.sig.vs_active = 0;
    tick(&m);
    CHECK(m.s.state == S_IDLE);
    CHECK(sel_access(&m.s) == ACCESS_PASSTHROUGH);
    end();
}

static void test_d(void) {
    Sim m;
    int c1, c2, c3, i;
    begin("Prueba D: ningun control puede robar un lado ocupado; sin repeticion al mantener");
    sim_init(&m);
    c1 = add_pad(&m, 1);
    c2 = add_pad(&m, 2);
    c3 = add_pad(&m, 3);
    set_nav(&m, c3, NAV_CONFIRM);
    m.sig.vs_active = 1;
    tick(&m);
    tick(&m);
    CHECK(m.s.side_uid[0] < 0 && m.s.side_uid[1] < 0);
    set_nav(&m, c3, 0);
    tick(&m);
    press(&m, c1, NAV_LEFT);
    press(&m, c2, NAV_LEFT);
    CHECK(sel_side_uid(&m.s, 0) == c1);
    press(&m, c1, NAV_CONFIRM);
    press(&m, c2, NAV_LEFT);
    CHECK(sel_side_uid(&m.s, 0) == c1 && m.s.side_ready[0]);
    press(&m, c2, NAV_RIGHT);
    CHECK(sel_side_uid(&m.s, 1) == c2);
    press(&m, c2, NAV_LEFT);
    CHECK(sel_side_uid(&m.s, 1) == c2 && sel_side_uid(&m.s, 0) == c1);
    press(&m, c3, NAV_LEFT);
    press(&m, c3, NAV_RIGHT);
    CHECK(sel_side_uid(&m.s, 0) == c1 && sel_side_uid(&m.s, 1) == c2);
    press(&m, c1, NAV_RIGHT);
    CHECK(sel_side_uid(&m.s, 0) == c1 && sel_side_uid(&m.s, 1) == c2);
    press(&m, c2, NAV_CANCEL);
    CHECK(sel_side_uid(&m.s, 1) < 0);
    set_nav(&m, c3, NAV_RIGHT);
    for (i = 0; i < 30; ++i) tick(&m);
    CHECK(sel_side_uid(&m.s, 1) == c3);
    set_nav(&m, c3, NAV_RIGHT | NAV_CANCEL);
    tick(&m);
    CHECK(sel_side_uid(&m.s, 1) < 0);
    for (i = 0; i < 30; ++i) tick(&m);
    CHECK(sel_side_uid(&m.s, 1) < 0);
    set_nav(&m, c3, 0);
    tick(&m);
    press(&m, c3, NAV_RIGHT);
    CHECK(sel_side_uid(&m.s, 1) == c3);
    set_nav(&m, c3, NAV_CONFIRM);
    for (i = 0; i < 30; ++i) tick(&m);
    CHECK(m.s.side_ready[1] == 1);
    set_nav(&m, c3, 0);
    tick(&m);
    CHECK(m.s.state == S_LOCKED);
    end();
}

static void test_e(void) {
    Sim m;
    int c1, c2, c3, c1b, i;
    begin("Prueba E: P2 se desconecta; recuperacion controlada sin asignar otro control");
    sim_init(&m);
    c1 = add_pad(&m, 1);
    c2 = add_pad(&m, 2);
    c3 = add_pad(&m, 3);
    m.sig.vs_active = 1;
    tick(&m);
    lock_pair(&m, c3, c1);
    CHECK(m.s.state == S_PLAYING);
    sim_remove(&m, "SIM:1");
    tick(&m);
    CHECK(m.s.state == S_SELECTING);
    CHECK(sel_access(&m.s) == ACCESS_BLOCK_ALL);
    CHECK(sel_side_uid(&m.s, 0) == c3 && m.s.side_ready[0] == 1);
    CHECK(sel_side_uid(&m.s, 1) < 0);
    snapshot(&m, "E_p2_desconectado.png", "KOF 2002 UM - VS (SIMULACION)");
    for (i = 0; i < 200; ++i) tick(&m);
    CHECK(sel_side_uid(&m.s, 1) < 0);
    c1b = add_pad(&m, 1);
    CHECK(c1b != c1);
    for (i = 0; i < 50; ++i) tick(&m);
    CHECK(sel_side_uid(&m.s, 1) < 0);
    press(&m, c2, NAV_RIGHT);
    press(&m, c2, NAV_CONFIRM);
    finish_lock(&m);
    CHECK(m.s.state == S_PLAYING);
    route_check(&m, c3, c2);
    end();
}

static void test_f(void) {
    Sim m;
    int c1, c2, c3, i;
    begin("Prueba F: la asignacion se mantiene entre rounds y cambios de personaje del equipo");
    sim_init(&m);
    c1 = add_pad(&m, 1);
    c2 = add_pad(&m, 2);
    c3 = add_pad(&m, 3);
    m.sig.vs_active = 1;
    tick(&m);
    lock_pair(&m, c2, c3);
    for (i = 0; i < 9; ++i) {
        play_match_bgm(&m);
        press(&m, c2, NAV_LEFT | NAV_CONFIRM);
        press(&m, c3, NAV_RIGHT | NAV_CANCEL);
        press(&m, c1, NAV_LEFT | NAV_RIGHT | NAV_CONFIRM | NAV_CANCEL);
    }
    for (i = 0; i < 60 * 60 * 5; ++i) tick(&m);
    CHECK(m.s.state == S_PLAYING);
    route_check(&m, c2, c3);
    CHECK(m.s.match == 1);
    end();
}

static void test_g(void) {
    Sim m;
    const char* reason;
    RegEntry* e;
    int uids[8];
    int count, i, j, distinct = 1;
    begin("Prueba G: 5+ dispositivos detectados y diferenciados (incluye duplicados XInput/Steam)");
    sim_init(&m);
    uids[0] = sim_add(&m, "XI:0", KIND_XINPUT, 0, 0x0F0D, 0x01FB, "HORI", 0);
    uids[1] = sim_add(&m, "XI:1", KIND_XINPUT, 1, 0, 0, "", 0);
    uids[2] = sim_add(&m, "XI:2", KIND_XINPUT, 2, 0x28DE, 0x11FF, "", 0);
    uids[3] = sim_add(&m, "XI:3", KIND_XINPUT, 3, 0x045E, 0x028E, "", 0);
    uids[4] = sim_add(&m, "DI:{A}", KIND_DINPUT, -1, 0x0CA3, 0x0024, "ARCADE STICK", 0);
    uids[5] = sim_add(&m, "DI:{B}", KIND_DINPUT, -1, 0x0079, 0x0006, "USB GAMEPAD", 0);
    count = reg_count(&m.r);
    CHECK(count == 6);
    for (i = 0; i < 6; ++i)
        for (j = i + 1; j < 6; ++j)
            if (uids[i] == uids[j] || uids[i] < 0) distinct = 0;
    CHECK(distinct);
    for (i = 0; i < 6; ++i) {
        e = reg_by_uid(&m.r, uids[i]);
        CHECK(e && e->number == i + 1);
    }
    {
        DevInfo dup;
        memset(&dup, 0, sizeof(dup));
        cstr_copy(dup.key, 72, "DI:{C}");
        dup.kind = KIND_DINPUT;
        dup.xinput_path = 1;
        CHECK(!reg_filter(&m.r.policy, &dup, m.found, m.found_count, &reason));
        cstr_copy(dup.key, 72, "DI:{D}");
        dup.xinput_path = 0;
        dup.vid = 0x054C;
        dup.pid = 0x09CC;
        CHECK(!reg_filter(&m.r.policy, &dup, m.found, m.found_count, &reason));
        dup.vid = 0x0CA3;
        CHECK(reg_filter(&m.r.policy, &dup, m.found, m.found_count, &reason));
        dup.vid = 0x0F0D;
        dup.pid = 0x01FB;
        CHECK(!reg_filter(&m.r.policy, &dup, m.found, m.found_count, &reason));
    }
    e = reg_by_uid(&m.r, uids[2]);
    CHECK(e && e->steam_virtual);
    sim_remove(&m, "XI:1");
    CHECK(reg_count(&m.r) == 5);
    i = sim_add(&m, "DI:{E}", KIND_DINPUT, -1, 0x0E8F, 0x3013, "FIGHTSTICK", 0);
    e = reg_by_uid(&m.r, i);
    CHECK(e && e->number == 2);
    j = sim_add(&m, "XI:1", KIND_XINPUT, 1, 0, 0, "", 0);
    e = reg_by_uid(&m.r, j);
    CHECK(e && e->number == 7 && j != uids[1]);
    m.sig.vs_active = 1;
    tick(&m);
    CHECK(m.s.state == S_SELECTING);
    press(&m, uids[5], NAV_LEFT);
    press(&m, j, NAV_RIGHT);
    press(&m, uids[0], NAV_LEFT);
    press(&m, uids[5], NAV_CONFIRM);
    press(&m, j, NAV_CONFIRM);
    finish_lock(&m);
    route_check(&m, uids[5], j);
    snapshot(&m, "G_siete_controles.png", "KOF '98 UM FE - VS (SIMULACION)");
    end();
}

static void test_extra(void) {
    Sim m;
    int c1, c2, c3;
    int portmap[4] = { 2, 1, -1, 0 };
    begin("Extra: siempre pantalla con 2 controles; admin bypass/reset; mapa de puertos");
    sim_init(&m);
    c1 = add_pad(&m, 1);
    c2 = add_pad(&m, 2);
    m.sig.vs_active = 1;
    m.sig.local_humans = 2;
    tick(&m);
    CHECK(m.s.state == S_SELECTING);
    CHECK(sel_access(&m.s) == ACCESS_BLOCK_ALL);
    CHECK(!m.s.passthrough);
    c3 = add_pad(&m, 3);
    tick(&m);
    CHECK(m.s.state == S_SELECTING);
    m.sig.admin_bypass = 1;
    tick(&m);
    CHECK(sel_access(&m.s) == ACCESS_PASSTHROUGH);
    m.sig.admin_reset = 1;
    tick(&m);
    CHECK(m.s.state == S_SELECTING);
    lock_pair(&m, c1, c3);
    CHECK(route_decide(sel_access(&m.s), portmap, 0, 1, 1) == ROUTE_P2);
    CHECK(route_decide(sel_access(&m.s), portmap, 1, 1, 1) == ROUTE_P1);
    CHECK(route_decide(sel_access(&m.s), portmap, 2, 1, 1) == ROUTE_DISCONNECTED);
    CHECK(route_decide(sel_access(&m.s), portmap, 3, 1, 1) == ROUTE_DISCONNECTED);
    CHECK(route_slot_for_port(portmap, 1) == 1);
    CHECK(route_decide(ACCESS_BLOCK_ALL, portmap, 0, 1, 1) == ROUTE_NEUTRAL);
    (void)c2;
    end();
}

static void test_single_local(void) {
    Sim m;
    int c1, c2, c3;
    begin("1P local: un rectangulo; Arcade/Practice/Online; P2 sin asignar");
    sim_init(&m);
    c1 = add_pad(&m, 1);
    c2 = add_pad(&m, 2);
    c3 = add_pad(&m, 3);
    m.sig.vs_active = 1;
    m.sig.local_humans = 1;
    tick(&m);
    CHECK(m.s.state == S_SELECTING);
    CHECK(sel_local_humans(&m.s) == 1);
    snapshot(&m, "1P_inicio.png", "KOF 98 UM FE - ARCADE 1P (SIMULACION)");
    press(&m, c2, NAV_CONFIRM);
    CHECK(sel_side_uid(&m.s, 0) == c2);
    CHECK(sel_side_uid(&m.s, 1) < 0);
    CHECK(m.s.state == S_CONFIRMING);
    press(&m, c1, NAV_LEFT);
    CHECK(sel_side_uid(&m.s, 0) == c2);
    CHECK(sel_side_uid(&m.s, 1) < 0);
    press(&m, c2, NAV_DOWN);
    CHECK(sel_side_layout(&m.s, 0) != LAYOUT_CLASSIC);
    press(&m, c2, NAV_CONFIRM);
    finish_lock(&m);
    CHECK(m.s.state == S_PLAYING);
    CHECK(sel_side_uid(&m.s, 0) == c2);
    CHECK(sel_side_uid(&m.s, 1) < 0);
    CHECK(sel_access(&m.s) == ACCESS_ASSIGNED);
    CHECK(route_decide(sel_access(&m.s), 0, 0, 1, 0) == ROUTE_P1);
    CHECK(route_decide(sel_access(&m.s), 0, 1, 1, 0) == ROUTE_NEUTRAL);
    snapshot(&m, "1P_playing.png", "KOF 98 UM FE - ARCADE 1P (SIMULACION)");
    (void)c3;
    end();
}

static void test_two_local_sides(void) {
    Sim m;
    int c1, c2;
    begin("2P local: dos rectangulos; lados independientes; Classic + San Felipe");
    sim_init(&m);
    c1 = add_pad(&m, 1);
    c2 = add_pad(&m, 2);
    m.sig.vs_active = 1;
    m.sig.local_humans = 2;
    tick(&m);
    CHECK(sel_local_humans(&m.s) == 2);
    press(&m, c2, NAV_LEFT);
    press(&m, c1, NAV_RIGHT);
    CHECK(sel_side_uid(&m.s, 0) == c2);
    CHECK(sel_side_uid(&m.s, 1) == c1);
    press(&m, c1, NAV_DOWN);
    CHECK(sel_side_layout(&m.s, 1) == LAYOUT_SAN_FELIPE);
    CHECK(sel_side_layout(&m.s, 0) == LAYOUT_CLASSIC);
    press(&m, c2, NAV_CONFIRM);
    press(&m, c1, NAV_CONFIRM);
    finish_lock(&m);
    CHECK(m.s.state == S_PLAYING);
    CHECK(sel_side_layout(&m.s, 0) == LAYOUT_CLASSIC);
    CHECK(sel_side_layout(&m.s, 1) == LAYOUT_SAN_FELIPE);
    snapshot(&m, "2P_layouts.png", "KOF 98 UM FE - VS 2P (SIMULACION)");
    end();
}

static void test_boundary(void) {
    Boundary b;
    begin("Extra: clasificacion de BGM y limite entre enfrentamientos");
    boundary_init(&b, "data/BGM/");
    boundary_add_select(&b, "UM48000Hz/BGM03.");
    b.confirm_ms = 0;
    CHECK(boundary_classify(&b, L"D:\\G\\data\\BGM\\UM48000Hz\\BGM03.ogg") == BGM_SELECT);
    CHECK(boundary_classify(&b, L"D:\\G\\data\\BGM\\UM48000Hz\\BGM30.ogg") == BGM_OTHER);
    CHECK(boundary_classify(&b, L"D:\\G\\data\\voice\\x.ogg") == BGM_NONE);
    boundary_match_started(&b);
    CHECK(boundary_feed(&b, BGM_SELECT, 0) == 0);
    CHECK(boundary_feed(&b, BGM_OTHER, 0) == 0);
    CHECK(boundary_feed(&b, BGM_OTHER, 0) == 0);
    CHECK(boundary_feed(&b, BGM_SELECT, 0) == 0);
    CHECK(boundary_take(&b) == 1);
    CHECK(boundary_feed(&b, BGM_SELECT, 0) == 0);

    boundary_init(&b, "game/audio/bgm/");
    boundary_add_select(&b, "bgm/PS_N.");
    b.confirm_ms = 0;
    CHECK(boundary_classify(&b, L"data/game/audio/bgm/ps_n.ogg") == BGM_SELECT);
    CHECK(boundary_classify(&b, L"data/game/audio/bgm/R_PS_N.ogg") == BGM_OTHER);
    CHECK(boundary_classify(&b, L"data/game/audio/bgm/loop.csv") == BGM_OTHER);
    boundary_match_started(&b);
    CHECK(boundary_feed(&b, boundary_classify(&b, L"data/game/audio/bgm/mae_n.ogg"), 0) == 0);
    CHECK(boundary_feed(&b, boundary_classify(&b, L"data/game/audio/bgm/0737.ogg"), 0) == 0);
    CHECK(boundary_feed(&b, boundary_classify(&b, L"data/game/audio/bgm/win_n.ogg"), 0) == 0);
    CHECK(boundary_feed(&b, boundary_classify(&b, L"data/game/audio/bgm/ps_n.ogg"), 0) == 0);
    CHECK(boundary_take(&b) == 1);

    /* Arcade win→next fight: PS_N flashes then stage BGM; must keep controls. */
    boundary_init(&b, "game/audio/bgm/");
    boundary_add_select(&b, "bgm/PS_N.");
    b.confirm_ms = 500;
    boundary_match_started(&b);
    CHECK(boundary_feed(&b, BGM_OTHER, 1000) == 0); /* win_n */
    CHECK(boundary_feed(&b, BGM_SELECT, 7000) == 0); /* ps_n flash */
    CHECK(b.select_hold == 1);
    CHECK(boundary_feed(&b, BGM_OTHER, 7233) == 1); /* 0730 within 500ms */
    CHECK(b.select_hold == 0);
    boundary_poll(&b, 8000);
    CHECK(boundary_take(&b) == 0);

    /* Real return to character select: PS_N holds past confirm_ms. */
    boundary_match_started(&b);
    CHECK(boundary_feed(&b, BGM_OTHER, 10000) == 0);
    CHECK(boundary_feed(&b, BGM_SELECT, 11000) == 0);
    boundary_poll(&b, 11400);
    CHECK(boundary_take(&b) == 0);
    boundary_poll(&b, 11500);
    CHECK(boundary_take(&b) == 1);
    end();
}

static void test_hook_decoder(void) {
    static const u8 d3d9_present[] = { 0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x08, 0x49, 0x89, 0x6B, 0x10, 0x49, 0x89, 0x73, 0x18, 0x57,
                                       0x48, 0x83, 0xEC, 0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    static const u8 rsp_prologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00,
                                       0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    static const u8 hooked[32] = { 0xE9, 0x10, 0x20, 0x30, 0x40 };
    static const u8 rip_rel[32] = { 0x48, 0x8B, 0x05, 0x11, 0x22, 0x33, 0x44, 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x57, 0x57, 0x57 };
    static const u8 unknown[32] = { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0x00, 0x00, 0x00, 0x00 };
    char err[160];
    begin("Extra: decodificador de prologo para el hook de Present");
    CHECK(hook_prologue_len(d3d9_present, HOOK_PATCH_LEN, err, (int)sizeof(err)) == 15);
    CHECK(hook_prologue_len(rsp_prologue, HOOK_PATCH_LEN, err, (int)sizeof(err)) == 18);
    CHECK(hook_prologue_len(hooked, HOOK_PATCH_LEN, err, (int)sizeof(err)) == 0);
    CHECK(hook_prologue_len(rip_rel, HOOK_PATCH_LEN, err, (int)sizeof(err)) == 0);
    CHECK(hook_prologue_len(unknown, HOOK_PATCH_LEN, err, (int)sizeof(err)) == 0);
    {
        static const u8 steam_like[16] = { 0xE9, 0xA7, 0x76, 0xC1, 0x78, 0x5B, 0x08 };
        static u8 abs_jmp[16] = { 0xFF, 0x25, 0, 0, 0, 0, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 };
        u64 dest = 0;
        CHECK(hook_jump_dest(steam_like, &dest) == 5);
        CHECK(dest == (u64)(steam_like + 5) + 0x78C176A7ull);
        CHECK(hook_jump_dest(abs_jmp, &dest) == 6);
        CHECK(dest == 0x1122334455667788ull);
        CHECK(hook_jump_dest(d3d9_present, &dest) == 0);
    }
    end();
}

enum { POS_SQUARE, POS_TRIANGLE, POS_R1, POS_L1, POS_CROSS, POS_CIRCLE, POS_R2, POS_L2, POS_START, POS_SELECT, POS_COUNT };

static Pad xbox_press(int pos) {
    static const int bits[POS_COUNT] = { XB_X, XB_Y, XB_RB, XB_LB, XB_A, XB_B, PHYS_RT, PHYS_LT, XB_START, XB_BACK };
    Pad p;
    memset(&p, 0, sizeof(p));
    if (bits[pos] == PHYS_RT) p.rt = 255;
    else if (bits[pos] == PHYS_LT) p.lt = 255;
    else p.buttons = (u16)bits[pos];
    return p;
}

static Pad ps_di_press(const PhysConfig* c, int pos) {
    static const int index[POS_COUNT] = { 0, 3, 5, 4, 1, 2, 7, 6, 9, 8 };
    DIJOYSTATE_ js;
    Pad p;
    memset(&js, 0, sizeof(js));
    js.rgdwPOV[0] = 0xFFFFFFFFu;
    js.rgbButtons[index[pos]] = 0x80;
    di_to_pad(c, c->di_button_map, &js, 1, &p);
    return p;
}

static Pad game_of(Pad p, int layout, unsigned flags) {
    layout_apply(&p, layout, flags);
    return p;
}

static int same_keys(Pad a, Pad b) {
    return a.buttons == b.buttons && (a.lt > 0x3F) == (b.lt > 0x3F) && (a.rt > 0x3F) == (b.rt > 0x3F);
}

static void test_layout_map(void) {
    static const u16 classic[POS_COUNT] = { XB_X, XB_Y, XB_RB, 0, XB_A, XB_B, 0, (u16)(XB_LB | XB_BACK), XB_START, XB_BACK };
    static const u16 san_felipe[POS_COUNT] = { XB_X, XB_A, XB_Y, 0, XB_B, 0, 0, XB_LB, XB_START, XB_BACK };
    PhysConfig pc;
    int pos;
    begin("Extra: CLASICA y SAN FELIPE por posicion fisica (HORI PS por DirectInput = HORI Xbox por XInput)");
    phys_default_config(&pc);
    for (pos = 0; pos < POS_COUNT; ++pos) {
        Pad x = xbox_press(pos), d = ps_di_press(&pc, pos);
        CHECK(same_keys(x, d));
        CHECK(same_keys(game_of(x, LAYOUT_CLASSIC, LAYOUT_F_L1_SELECT), game_of(d, LAYOUT_CLASSIC, LAYOUT_F_L1_SELECT)));
        CHECK(same_keys(game_of(x, LAYOUT_SAN_FELIPE, LAYOUT_F_L1_SELECT), game_of(d, LAYOUT_SAN_FELIPE, LAYOUT_F_L1_SELECT)));
        CHECK(game_of(x, LAYOUT_CLASSIC, LAYOUT_F_L1_SELECT).buttons == classic[pos]);
        if (pos != POS_L1) CHECK(game_of(x, LAYOUT_SAN_FELIPE, 0).buttons == san_felipe[pos]);
    }
    CHECK(game_of(xbox_press(POS_R2), LAYOUT_CLASSIC, 0).rt == 255);
    CHECK(game_of(xbox_press(POS_L2), LAYOUT_CLASSIC, 0).lt == 255);
    CHECK(game_of(xbox_press(POS_R2), LAYOUT_SAN_FELIPE, 0).rt == 0);
    CHECK(game_of(xbox_press(POS_L2), LAYOUT_SAN_FELIPE, 0).lt == 0);
    CHECK(game_of(xbox_press(POS_L1), LAYOUT_SAN_FELIPE, LAYOUT_F_L1_SELECT).buttons == 0);
    CHECK(game_of(xbox_press(POS_L2), LAYOUT_SAN_FELIPE, LAYOUT_F_L1_SELECT).buttons == (XB_LB | XB_BACK));
    CHECK(game_of(xbox_press(POS_L1), LAYOUT_SAN_FELIPE, 0).buttons == 0);
    CHECK(game_of(xbox_press(POS_START), LAYOUT_SAN_FELIPE, LAYOUT_F_L1_SELECT).buttons == XB_START);
    CHECK(!(game_of(xbox_press(POS_L1), LAYOUT_SAN_FELIPE, LAYOUT_F_L1_SELECT).buttons & XB_START));
    {
        Pad p = xbox_press(POS_SQUARE);
        p.buttons |= XB_RB | XB_DOWN | XB_LEFT;
        CHECK(game_of(p, LAYOUT_SAN_FELIPE, LAYOUT_F_L1_SELECT).buttons == (XB_X | XB_Y | XB_DOWN | XB_LEFT));
    }
    {
        PhysConfig c2;
        phys_default_config(&c2);
        c2.di_profile_count = 1;
        c2.di_profiles[0].vid = 0x0F0D;
        c2.di_profiles[0].pid = 0x01FA;
        c2.di_profiles[0].map[0] = XB_A;
        CHECK(phys_di_map(&c2, 0x0F0D, 0x01FA) == c2.di_profiles[0].map);
        CHECK(phys_di_map(&c2, 0x0F0D, 0x01B4) == c2.di_button_map);
    }
    CHECK(layout_button_code("SQUARE") == XB_X && layout_button_code("options") == XB_START && layout_button_code("R2") == PHYS_RT);
    CHECK(layout_parse("san_felipe") == LAYOUT_SAN_FELIPE && layout_parse("classic") == LAYOUT_CLASSIC && layout_parse("x") < 0);
    CHECK(layout_parse("lineal") == LAYOUT_LINEAL && layout_parse("pulgar") == LAYOUT_PULGAR);
    CHECK(layout_parse("abcd") == LAYOUT_ABCD && layout_parse("mexico") == LAYOUT_MEXICO);
    CHECK(LAYOUT_COUNT == 6 && layout_profiles_ok());
    {
        static const char* grids[LAYOUT_COUNT] = {
            "AC--BD--", "ABC-D---", "ABCD----", "BCD-A---", "AB--CD--", "ABC---D-"
        };
        static const u16 factory[5] = { 0, XB_X, XB_A, XB_Y, XB_B };
        int layout, pos;
        for (layout = 0; layout < LAYOUT_COUNT; ++layout) {
            unsigned seen = 0;
            for (pos = 0; pos < 8; ++pos) {
                char cell = layout_cell_char(layout, pos);
                int act = layout_cell_act(layout, pos);
                Pad hit = xbox_press(pos), di, game;
                CHECK(cell == grids[layout][pos]);
                CHECK(layout_action_from_pad(layout, &hit, 0) == act);
                game = game_of(hit, layout, 0);
                di = ps_di_press(&pc, pos);
                CHECK(same_keys(game_of(hit, layout, 0), game_of(di, layout, 0)));
                if (layout == LAYOUT_CLASSIC) {
                    if (pos == POS_R2) CHECK(game.rt == 255);
                    else if (pos == POS_L2) {
                        CHECK(game.lt == 255);
                        CHECK(game.buttons == XB_LB);
                    } else if (pos == POS_L1) CHECK(game.buttons == 0);
                    else CHECK(game.buttons == xbox_press(pos).buttons);
                } else {
                    CHECK(game.buttons == (u16)(factory[act] | (pos == POS_L2 ? XB_LB : 0)));
                    CHECK(game.lt == 0 && game.rt == 0);
                }
                if (act) {
                    CHECK(!(seen & (1u << act)));
                    seen |= 1u << act;
                }
            }
            CHECK(seen == ((1u << LAYOUT_ACT_A) | (1u << LAYOUT_ACT_B) | (1u << LAYOUT_ACT_C) | (1u << LAYOUT_ACT_D)));
            CHECK(game_of(xbox_press(POS_START), layout, LAYOUT_F_L1_SELECT).buttons == XB_START);
            CHECK(game_of(xbox_press(POS_SELECT), layout, 0).buttons == XB_BACK);
            CHECK(!(game_of(xbox_press(POS_START), layout, LAYOUT_F_L1_SELECT).buttons & (XB_X | XB_A | XB_Y | XB_B)));
        }
        CHECK(game_of(xbox_press(POS_L1), LAYOUT_SAN_FELIPE, LAYOUT_F_L1_SELECT).buttons == 0);
        CHECK(game_of(xbox_press(POS_L2), LAYOUT_SAN_FELIPE, LAYOUT_F_L1_SELECT).buttons == (XB_LB | XB_BACK));
        CHECK(game_of(xbox_press(POS_R1), LAYOUT_PULGAR, 0).buttons == XB_B);
        CHECK(game_of(xbox_press(POS_TRIANGLE), LAYOUT_PULGAR, 0).buttons == XB_Y);
        CHECK(game_of(xbox_press(POS_L1), LAYOUT_PULGAR, 0).buttons == 0);
        CHECK(game_of(xbox_press(POS_R1), LAYOUT_LINEAL, 0).buttons == XB_Y);
        CHECK(game_of(xbox_press(POS_L1), LAYOUT_LINEAL, 0).buttons == XB_B);
        CHECK(game_of(xbox_press(POS_CROSS), LAYOUT_ABCD, 0).buttons == XB_Y);
        CHECK(game_of(xbox_press(POS_CIRCLE), LAYOUT_ABCD, 0).buttons == XB_B);
        CHECK(game_of(xbox_press(POS_R2), LAYOUT_MEXICO, 0).buttons == XB_B);
        CHECK(game_of(xbox_press(POS_R1), LAYOUT_SAN_FELIPE, 0).buttons == XB_Y);
        CHECK(game_of(xbox_press(POS_TRIANGLE), LAYOUT_CLASSIC, 0).buttons == XB_Y);
    }
    {
        int cal98[8] = { XB_X, XB_Y, XB_LB, XB_RB, XB_A, XB_B, PHYS_RT, PHYS_LT };
        Pad p = xbox_press(POS_L1);
        layout_cal_normalize(&p, cal98);
        CHECK(game_of(p, LAYOUT_LINEAL, 0).buttons == XB_Y);
        p = xbox_press(POS_L1);
        layout_cal_normalize(&p, cal98);
        CHECK(game_of(p, LAYOUT_PULGAR, 0).buttons == XB_B);
        p = xbox_press(POS_L1);
        layout_cal_normalize(&p, cal98);
        CHECK(game_of(p, LAYOUT_MEXICO, 0).buttons == XB_Y);
        CHECK(layout_fill_cal("XI:0", 0x28DE, 0x11FF, cal98) == 0);
        CHECK(layout_fill_cal("XI:0", 0x2DC8, 0x6001, cal98) == 0);
    }
    {
        /* Steam Gamepad template, uncalibrated: '98 runs with L1=SELECT, 2002 without. */
        static const int lay[6] = { LAYOUT_CLASSIC, LAYOUT_SAN_FELIPE, LAYOUT_LINEAL, LAYOUT_PULGAR, LAYOUT_ABCD, LAYOUT_MEXICO };
        static const u16 want_r1[6] = { XB_RB, XB_Y, XB_Y, XB_B, 0, XB_Y };
        static const u16 want_l1_2002[6] = { 0, 0, XB_B, 0, 0, 0 };
        static const u16 want_r2[6] = { 0, 0, 0, 0, 0, XB_B };
        int k;
        for (k = 0; k < 6; ++k) {
            Pad r1 = game_of(xbox_press(POS_R1), lay[k], 0);
            Pad l1 = game_of(xbox_press(POS_L1), lay[k], 0);
            Pad l1ex = game_of(xbox_press(POS_L1), lay[k], LAYOUT_F_L1_SELECT);
            Pad r2 = game_of(xbox_press(POS_R2), lay[k], 0);
            Pad l2 = game_of(xbox_press(POS_L2), lay[k], LAYOUT_F_L1_SELECT);
            CHECK(r1.buttons == want_r1[k]);
            CHECK(l1.buttons == want_l1_2002[k]);
            CHECK(l1ex.buttons == want_l1_2002[k]);
            CHECK(r2.buttons == want_r2[k]);
            CHECK(l2.buttons == (u16)(XB_LB | XB_BACK));
            CHECK(!(l2.buttons & (XB_X | XB_A | XB_Y | XB_B)));
            CHECK(!(game_of(xbox_press(POS_R1), lay[k], LAYOUT_F_L1_SELECT).buttons & XB_BACK));
        }
        {
            Pad q = xbox_press(POS_L2);
            q.buttons |= XB_BACK;
            CHECK(!(pad_nav(&q) & NAV_CANCEL));
            q = xbox_press(POS_SELECT);
            CHECK(pad_nav(&q) & NAV_CANCEL);
        }
    }
    {
        /* Juan's Steam Input codes from the USB logs. Top row slot 3 (R1) carries C,
           or D in PULGAR, in both games. */
        static const int lay[5] = { LAYOUT_SAN_FELIPE, LAYOUT_LINEAL, LAYOUT_PULGAR, LAYOUT_MEXICO, LAYOUT_ABCD };
        static const u16 want_r1[5] = { XB_Y, XB_Y, XB_B, XB_Y, 0 };
        int cal[8], k;
        Pad q;
        layout_set_game_id("kof98");
        CHECK(layout_fill_cal("XI:0", 0x28DE, 0x11FF, cal) == 1);
        for (k = 0; k < 5; ++k) {
            q = xbox_press(POS_L1);
            layout_cal_normalize(&q, cal);
            CHECK(game_of(q, lay[k], LAYOUT_F_L1_SELECT).buttons == want_r1[k]);
        }
        q = xbox_press(POS_L2);
        q.buttons |= XB_BACK;
        layout_cal_normalize(&q, cal);
        CHECK(game_of(q, LAYOUT_LINEAL, LAYOUT_F_L1_SELECT).buttons == (XB_B | XB_BACK));
        q = xbox_press(POS_R2);
        q.buttons |= XB_BACK;
        layout_cal_normalize(&q, cal);
        CHECK(game_of(q, LAYOUT_MEXICO, LAYOUT_F_L1_SELECT).buttons == (XB_B | XB_BACK));
        q = xbox_press(POS_L1);
        CHECK(game_of(q, LAYOUT_CLASSIC, LAYOUT_F_L1_SELECT).buttons == 0);
        q = xbox_press(POS_L2);
        CHECK(game_of(q, LAYOUT_CLASSIC, LAYOUT_F_L1_SELECT).buttons == (XB_LB | XB_BACK));
        layout_set_game_id("kof2002");
        CHECK(layout_fill_cal("XI:0", 0x28DE, 0x11FF, cal) == 1);
        for (k = 0; k < 5; ++k) {
            q = xbox_press(POS_R2);
            layout_cal_normalize(&q, cal);
            CHECK(game_of(q, lay[k], 0).buttons == want_r1[k]);
        }
        q = xbox_press(POS_L2);
        layout_cal_normalize(&q, cal);
        CHECK(game_of(q, LAYOUT_LINEAL, 0).buttons == XB_B);
        q = xbox_press(POS_L1);
        layout_cal_normalize(&q, cal);
        CHECK(game_of(q, LAYOUT_LINEAL, 0).buttons == XB_B);
        CHECK(layout_fill_cal("XI:0", 0x2DC8, 0x6001, cal) == 0);
        layout_set_game_id(0);
        CHECK(layout_fill_cal("XI:0", 0x28DE, 0x11FF, cal) == 0);
        layout_set_steam_standard(1);
        layout_set_game_id("kof2002");
        CHECK(layout_fill_cal("XI:0", 0x28DE, 0x11FF, cal) == 0);
        CHECK(game_of(xbox_press(POS_R1), LAYOUT_LINEAL, 0).buttons == XB_Y);
        CHECK(game_of(xbox_press(POS_L1), LAYOUT_LINEAL, 0).buttons == XB_B);
        CHECK(game_of(xbox_press(POS_R2), LAYOUT_MEXICO, 0).buttons == XB_B);
        layout_set_steam_standard(0);
        layout_set_game_id(0);
    }
    {
        /* A second Steam pad on Steam's standard map: once it sends RB it leaves the
           custom table (C on R1 works in SAN FELIPE); the other Steam pad keeps it. */
        int cal[8];
        Pad q;
        layout_set_game_id("kof98");
        CHECK(layout_fill_cal("XI:1", 0x28DE, 0x11FF, cal) == 1);
        q = xbox_press(POS_R1);
        CHECK(layout_steam_observe("XI:1", 0x28DE, 0x11FF, &q) == 1);
        CHECK(layout_steam_observe("XI:1", 0x28DE, 0x11FF, &q) == 0);
        CHECK(layout_fill_cal("XI:1", 0x28DE, 0x11FF, cal) == 0);
        CHECK(game_of(q, LAYOUT_SAN_FELIPE, LAYOUT_F_L1_SELECT).buttons == XB_Y);
        CHECK(layout_fill_cal("XI:0", 0x28DE, 0x11FF, cal) == 1);
        CHECK(layout_steam_observe("XI:2", 0x2DC8, 0x6001, &q) == 0);
        q = xbox_press(POS_L1);
        CHECK(layout_steam_observe("XI:0", 0x28DE, 0x11FF, &q) == 0);
        CHECK(layout_fill_cal("XI:0", 0x28DE, 0x11FF, cal) == 1);
        layout_steam_forget("XI:1");
        CHECK(layout_fill_cal("XI:1", 0x28DE, 0x11FF, cal) == 1);
        layout_set_game_id("kof13");
        q = xbox_press(POS_R1);
        CHECK(layout_steam_observe("XI:1", 0x28DE, 0x11FF, &q) == 0);
        layout_set_game_id(0);
    }
    {
        Sim m;
        int c1, c2;
        sim_init(&m);
        c1 = add_pad(&m, 1);
        c2 = add_pad(&m, 2);
        add_pad(&m, 3);
        m.sig.vs_active = 1;
        tick(&m);
        tick(&m);
        press(&m, c1, NAV_LEFT);
        press(&m, c2, NAV_RIGHT);
        press(&m, c1, NAV_LEFT);
        CHECK(sel_side_cal_step(&m.s, 0) == 0);
        CHECK(sel_side_uid(&m.s, 0) == c1);
        CHECK(sel_side_layout(&m.s, 0) == m.s.cfg.default_layout);
    }
    {
        int cal[8] = { XB_B, XB_Y, XB_RB, XB_LB, XB_A, XB_X, PHYS_RT, PHYS_LT };
        Pad p = xbox_press(POS_CIRCLE);
        layout_cal_normalize(&p, cal);
        CHECK(p.buttons == XB_X);
    }
    {
        static const struct { u32 outbits; i32 rawbit; } def[] = {
            { 1, 0 }, { 2, 1 }, { 4, 2 }, { 8, 3 },
            { 0x20, 12 }, { 0x40, 13 }, { 0x80, 14 }, { 0x100, 15 },
            { 0x200, 9 }, { 0x400, 11 }, { 0x1000, 8 }, { 0x2000, 10 },
            { 0x08000000, 4 }, { 0x10000000, 5 }, { 0, 0 }
        };
        CHECK(layout_build_game_table(def, LAYOUT_F_L1_SELECT) > 0);
        CHECK(layout_table_raw(0x80) == 14);
        CHECK(layout_table_raw(0x20) == 12);
        CHECK(layout_table_raw(0x100) == 15);
        CHECK(layout_table_raw(0x40) == 13);
        CHECK(layout_table_raw(0x200) == -1);
        CHECK(layout_table_raw(0x400) == -1);
        CHECK(layout_table_raw(0x2000) == -1);
        CHECK(layout_game_table(LAYOUT_CLASSIC) == 0);
        CHECK(layout_game_table(LAYOUT_SAN_FELIPE) != 0);
        CHECK(layout_game_table(LAYOUT_LINEAL) != 0 && layout_game_table(LAYOUT_PULGAR) != 0);
        CHECK(layout_game_table(LAYOUT_ABCD) != 0 && layout_game_table(LAYOUT_MEXICO) != 0);
        CHECK(layout_table_raw_of(LAYOUT_LINEAL, 0x40) == 13);
        CHECK(layout_table_raw_of(LAYOUT_PULGAR, 0x80) == 14);
        CHECK(layout_table_raw_of(LAYOUT_ABCD, 0x100) == 15);
        CHECK(layout_table_raw_of(LAYOUT_MEXICO, 0x40) == 13);
        CHECK(layout_table_raw_of(LAYOUT_SAN_FELIPE, 0x08000000) == 4);
        CHECK(layout_build_game_table(def, 0) > 0);
        CHECK(layout_table_raw(0x1000) == -1);
        CHECK(layout_table_raw(0x40) == 13);
    }
    {
        wch path[80];
        ascii_to_wstr("D:\\KOF-VS-Controller-Selector\\build\\kofvs-layouts-test.ini", path, 80);
        layout_memory_reset();
        layout_memory_set_path(path);
        layout_memory_put("0F0D:01B5", 0x0F0D, 0x01B5, LAYOUT_SAN_FELIPE);
        CHECK(layout_memory_last("0F0D:01B5", 0x0F0D, 0x01B5) == LAYOUT_SAN_FELIPE);
        layout_memory_reset();
        layout_memory_load();
        CHECK(layout_memory_last(0, 0x0F0D, 0x01B5) == LAYOUT_SAN_FELIPE);
        layout_memory_reset();
        layout_memory_put("XI:9", 0x28DE, 0x11FF, LAYOUT_LINEAL);
        CHECK(layout_memory_last("XI:3", 0x28DE, 0x11FF) == -1);
        layout_memory_reset();
        layout_memory_put("28DE:11FF", 0x28DE, 0x11FF, LAYOUT_MEXICO);
        layout_memory_put("XI:0", 0x28DE, 0x11FF, LAYOUT_ABCD);
        layout_memory_put("XI:1", 0x28DE, 0x11FF, LAYOUT_PULGAR);
        layout_memory_put("XI:0", 0x28DE, 0x11FF, LAYOUT_SAN_FELIPE);
        CHECK(layout_memory_last("XI:0", 0x28DE, 0x11FF) == LAYOUT_SAN_FELIPE);
        CHECK(layout_memory_last("XI:1", 0x28DE, 0x11FF) == LAYOUT_PULGAR);
        CHECK(layout_memory_last("XI:2", 0x28DE, 0x11FF) == LAYOUT_MEXICO);
        layout_memory_reset();
        layout_memory_load();
        CHECK(layout_memory_last("XI:0", 0x28DE, 0x11FF) == LAYOUT_SAN_FELIPE);
        CHECK(layout_memory_last("XI:1", 0x28DE, 0x11FF) == LAYOUT_PULGAR);
        CHECK(layout_memory_last("XI:2", 0x28DE, 0x11FF) == LAYOUT_MEXICO);
        layout_memory_reset();
        layout_memory_set_path(L"");
    }
    end();
}

static void mirror_step(Sim* m, int d_uid, int v_uid, u16 d_buttons, u16 v_buttons, u64* now, int v_delay) {
    RegEntry* d = reg_by_uid(&m->r, d_uid);
    RegEntry* v = reg_by_uid(&m->r, v_uid);
    if (d) d->pad.buttons = d_buttons;
    reg_mirror_update(&m->r, *now);
    *now += (u64)v_delay;
    if (v) v->pad.buttons = v_buttons;
    reg_mirror_update(&m->r, *now);
    *now += 200;
    reg_mirror_update(&m->r, *now);
}

static void test_mirror(void) {
    Sim m;
    SelDevice devs[SEL_MAX_DEVICES];
    int n = 0, steam, steam2, hori, other;
    u64 now = 1000;
    begin("Extra: duplicados por entradas reales (HORI modo PS envuelto por Steam Input)");
    sim_init(&m);
    steam = sim_add(&m, "XI:0", KIND_XINPUT, 0, 0x28DE, 0x11FF, "", 0);
    steam2 = sim_add(&m, "XI:1", KIND_XINPUT, 1, 0x28DE, 0x11FF, "", 0);
    hori = sim_add(&m, "DI:HORI", KIND_DINPUT, -1, 0x0F0D, 0x01FA, "HORI FIGHTING STICK", 0);
    other = sim_add(&m, "DI:OTHER", KIND_DINPUT, -1, 0x0F0D, 0x01B4, "HORI Fighting Stick mini", 0);
    mirror_step(&m, hori, steam, XB_LEFT, XB_LEFT, &now, 8);
    mirror_step(&m, hori, steam, 0, 0, &now, 5);
    CHECK(reg_by_uid(&m.r, hori)->mirror_of == 0);
    mirror_step(&m, hori, steam, XB_X, XB_X, &now, 12);
    CHECK(reg_by_uid(&m.r, hori)->mirror_of == steam);
    CHECK(reg_by_uid(&m.r, hori)->number == 0);
    mirror_step(&m, other, steam2, XB_A, 0, &now, 5);
    mirror_step(&m, other, steam2, 0, XB_A, &now, 300);
    mirror_step(&m, other, steam2, XB_B, XB_B, &now, 300);
    mirror_step(&m, other, steam2, 0, 0, &now, 300);
    mirror_step(&m, other, steam2, XB_Y, XB_Y, &now, 300);
    CHECK(reg_by_uid(&m.r, other)->mirror_of == 0);
    {
        int xbox = sim_add(&m, "XI:XBOX", KIND_XINPUT, 2, 0x0F0D, 0x01B5, "HORI Mini Xbox", 1);
        mirror_step(&m, other, xbox, XB_DOWN, XB_DOWN, &now, 8);
        mirror_step(&m, other, xbox, 0, 0, &now, 8);
        mirror_step(&m, other, xbox, XB_UP, XB_UP, &now, 8);
        CHECK(reg_by_uid(&m.r, other)->mirror_of == 0);
    }
    sel_devices_from_registry(&m.r, devs, &n);
    CHECK(n == 4);
    sim_remove(&m, "XI:0");
    CHECK(reg_by_uid(&m.r, hori)->mirror_of == 0 && reg_by_uid(&m.r, hori)->number > 0);
    sel_devices_from_registry(&m.r, devs, &n);
    CHECK(n == 4);
    end();
}

static void test_mirror_releases_side(void) {
    Sim m;
    int steam, hori, c3;
    u64 now = 1000;
    begin("Extra: si el duplicado tenia un lado, se libera y no ocupa los dos lados");
    sim_init(&m);
    steam = sim_add(&m, "XI:0", KIND_XINPUT, 0, 0x28DE, 0x11FF, "", 0);
    hori = sim_add(&m, "DI:HORI", KIND_DINPUT, -1, 0x0F0D, 0x01FA, "HORI FIGHTING STICK", 0);
    c3 = add_pad(&m, 3);
    m.sig.vs_active = 1;
    tick(&m);
    press(&m, hori, NAV_LEFT);
    press(&m, steam, NAV_RIGHT);
    CHECK(sel_side_uid(&m.s, 0) == hori && sel_side_uid(&m.s, 1) == steam);
    mirror_step(&m, hori, steam, XB_X, XB_X, &now, 4);
    mirror_step(&m, hori, steam, 0, 0, &now, 4);
    mirror_step(&m, hori, steam, XB_A, XB_A, &now, 4);
    CHECK(reg_by_uid(&m.r, hori)->mirror_of == steam);
    tick(&m);
    CHECK(sel_side_uid(&m.s, 0) < 0 && sel_side_uid(&m.s, 1) == steam);
    press(&m, c3, NAV_LEFT);
    CHECK(sel_side_uid(&m.s, 0) == c3);
    end();
}

static void test_layout_select(void) {
    Sim m;
    Pad up;
    int c1, c2, c3;
    begin("Extra: eleccion de botones por jugador en la pantalla de seleccion");
    sim_init(&m);
    c1 = add_pad(&m, 1);
    c2 = add_pad(&m, 2);
    c3 = add_pad(&m, 3);
    m.sig.vs_active = 1;
    tick(&m);
    memset(&up, 0, sizeof(up));
    up.buttons = XB_UP;
    CHECK(pad_nav(&up) == NAV_UP);
    up.buttons = 0;
    up.ly = -30000;
    CHECK(pad_nav(&up) == NAV_DOWN);
    press(&m, c1, NAV_DOWN);
    CHECK(sel_side_uid(&m.s, 0) < 0 && sel_side_uid(&m.s, 1) < 0);
    press(&m, c1, NAV_LEFT);
    CHECK(sel_side_layout(&m.s, 0) == LAYOUT_CLASSIC);
    press(&m, c1, NAV_DOWN);
    CHECK(sel_side_layout(&m.s, 0) == LAYOUT_SAN_FELIPE);
    press(&m, c2, NAV_RIGHT);
    CHECK(m.s.state == S_CONFIRMING);
    CHECK(sel_side_layout(&m.s, 0) == LAYOUT_SAN_FELIPE && sel_side_layout(&m.s, 1) == LAYOUT_CLASSIC);
    press(&m, c1, NAV_DOWN);
    CHECK(sel_side_layout(&m.s, 0) == LAYOUT_LINEAL && sel_side_layout(&m.s, 1) == LAYOUT_CLASSIC);
    press(&m, c1, NAV_UP);
    CHECK(sel_side_layout(&m.s, 0) == LAYOUT_SAN_FELIPE);
    press(&m, c3, NAV_UP);
    CHECK(sel_side_layout(&m.s, 1) == LAYOUT_CLASSIC);
    snapshot(&m, "L1_p1_san_felipe.png", "KOF '98 UM FE - VS (SIMULACION)");
    press(&m, c1, NAV_CONFIRM);
    press(&m, c1, NAV_UP);
    CHECK(sel_side_layout(&m.s, 0) == LAYOUT_SAN_FELIPE);
    press(&m, c2, NAV_UP);
    CHECK(sel_side_layout(&m.s, 1) == LAYOUT_MEXICO);
    press(&m, c2, NAV_DOWN);
    CHECK(sel_side_layout(&m.s, 1) == LAYOUT_CLASSIC);
    press(&m, c2, NAV_CONFIRM);
    finish_lock(&m);
    CHECK(m.s.state == S_PLAYING);
    CHECK(sel_side_layout(&m.s, 0) == LAYOUT_SAN_FELIPE && sel_side_layout(&m.s, 1) == LAYOUT_CLASSIC);
    select_bgm(&m);
    tick(&m);
    play_match_bgm(&m);
    tick(&m);
    select_bgm(&m);
    tick(&m);
    CHECK(m.s.state == S_SELECTING);
    CHECK(sel_side_layout(&m.s, 0) == LAYOUT_CLASSIC);
    press(&m, c1, NAV_RIGHT);
    CHECK(sel_side_layout(&m.s, 1) == LAYOUT_SAN_FELIPE);
    press(&m, c2, NAV_LEFT);
    CHECK(m.s.state == S_CONFIRMING);
    press(&m, c1, NAV_DOWN);
    CHECK(sel_side_layout(&m.s, 1) == LAYOUT_LINEAL && sel_side_layout(&m.s, 0) == LAYOUT_CLASSIC);
    press(&m, c1, NAV_UP);
    CHECK(sel_side_uid(&m.s, 1) == c1 && sel_side_layout(&m.s, 1) == LAYOUT_SAN_FELIPE && sel_side_layout(&m.s, 0) == LAYOUT_CLASSIC);
    press(&m, c1, NAV_CANCEL);
    CHECK(sel_side_layout(&m.s, 1) == LAYOUT_CLASSIC);
    press(&m, c2, NAV_CANCEL);
    press(&m, c1, NAV_LEFT);
    press(&m, c2, NAV_RIGHT);
    CHECK(sel_side_layout(&m.s, 0) == LAYOUT_SAN_FELIPE && sel_side_layout(&m.s, 1) == LAYOUT_CLASSIC);
    press(&m, c2, NAV_DOWN);
    CHECK(sel_side_layout(&m.s, 0) == LAYOUT_SAN_FELIPE && sel_side_layout(&m.s, 1) == LAYOUT_SAN_FELIPE);
    snapshot(&m, "L2_button_layout.png", "KOF '98 UM FE - BUTTON LAYOUT (SIMULACION)");
    end();
}

static void test_kof13_layouts(void) {
    Sim m;
    int c[7], i;
    RegEntry* e;
    begin("KOF XIII: 7 controles eligen perfil y despues P1/P2");
    sim_init(&m);
    m.s.cfg.layout_first = 1;
    for (i = 0; i < 7; ++i) c[i] = add_pad(&m, i + 1);
    m.sig.vs_active = 1;
    tick(&m);
    CHECK(m.s.state == S_LAYOUTS);
    CHECK(sel_access(&m.s) == ACCESS_BLOCK_ALL);
    CHECK(sel_dev_layout(&m.s, c[6]) == LAYOUT_CLASSIC);
    e = reg_by_uid(&m.r, c[0]);
    e->pad.ly = -25000;
    e->nav = pad_nav(&e->pad);
    tick(&m);
    e->pad.ly = 0;
    e->nav = 0;
    tick(&m);
    CHECK(sel_dev_layout(&m.s, c[0]) == LAYOUT_SAN_FELIPE);
    e->pad.buttons = XB_A;
    e->nav = NAV_CONFIRM;
    tick(&m);
    e->pad.buttons = 0;
    e->nav = 0;
    tick(&m);
    CHECK(sel_dev_layout_ready(&m.s, c[0]) == 1);
    e = reg_by_uid(&m.r, c[1]);
    e->pad.buttons = XB_START;
    e->nav = NAV_CONFIRM;
    tick(&m);
    CHECK(m.s.state == S_SELECTING);
    e->pad.buttons = 0;
    e->nav = 0;
    tick(&m);
    press(&m, c[0], NAV_LEFT);
    press(&m, c[1], NAV_RIGHT);
    CHECK(sel_side_uid(&m.s, 0) == c[0]);
    CHECK(sel_side_uid(&m.s, 1) == c[1]);
    CHECK(sel_side_layout(&m.s, 0) == LAYOUT_SAN_FELIPE);
    CHECK(sel_side_layout(&m.s, 1) == LAYOUT_CLASSIC);
    press(&m, c[0], NAV_DOWN);
    CHECK(sel_side_layout(&m.s, 0) == LAYOUT_SAN_FELIPE);
    press(&m, c[0], NAV_CONFIRM);
    press(&m, c[1], NAV_CONFIRM);
    CHECK(m.s.state == S_LOCKED);
    {
        static const u16 game[4] = { XB_X, XB_Y, XB_A, XB_B };
        Pad p = xbox_press(POS_TRIANGLE);
        layout_apply(&p, LAYOUT_SAN_FELIPE, 0);
        CHECK(p.buttons == XB_A);
        layout_remap_attacks(&p, game);
        CHECK(p.buttons == XB_Y);
        p = xbox_press(POS_R1);
        p.buttons |= XB_START | XB_DOWN;
        layout_apply(&p, LAYOUT_SAN_FELIPE, 0);
        layout_remap_attacks(&p, game);
        CHECK(p.buttons == (XB_A | XB_START | XB_DOWN));
        p = xbox_press(POS_CROSS);
        layout_apply(&p, LAYOUT_SAN_FELIPE, 0);
        layout_remap_attacks(&p, game);
        CHECK(p.buttons == XB_B);
    }
    {
        /* Measured on the Deck: XIII reads X=LP, Y=LK, B=HP, A=HK. */
        static const u16 xiii[4] = { XB_X, XB_Y, XB_B, XB_A };
        Pad p = xbox_press(POS_TRIANGLE);
        layout_apply(&p, LAYOUT_SAN_FELIPE, 0);
        layout_remap_attacks(&p, xiii);
        CHECK(p.buttons == XB_Y);
        p = xbox_press(POS_R1);
        layout_apply(&p, LAYOUT_SAN_FELIPE, 0);
        layout_remap_attacks(&p, xiii);
        CHECK(p.buttons == XB_B);
        p = xbox_press(POS_SQUARE);
        layout_apply(&p, LAYOUT_SAN_FELIPE, 0);
        layout_remap_attacks(&p, xiii);
        CHECK(p.buttons == XB_X);
        p = xbox_press(POS_CROSS);
        layout_apply(&p, LAYOUT_SAN_FELIPE, 0);
        layout_remap_attacks(&p, xiii);
        CHECK(p.buttons == XB_A);
    }
    end();
}

static void test_start_select_physical(void) {
    PadRide r;
    Pad p;
    begin("START/SELECT solo desde sus botones fisicos (sin SELECT montado en R2/L2/L1)");
    memset(&r, 0, sizeof(r));
    memset(&p, 0, sizeof(p));
    p.buttons = XB_BACK;
    pad_strip_riding(&r, &p);
    CHECK(p.buttons == XB_BACK);
    memset(&p, 0, sizeof(p));
    pad_strip_riding(&r, &p);
    p.buttons = XB_START;
    pad_strip_riding(&r, &p);
    CHECK(p.buttons == XB_START);
    memset(&p, 0, sizeof(p));
    pad_strip_riding(&r, &p);

    p.buttons = XB_BACK;
    p.rt = 255;
    pad_strip_riding(&r, &p);
    CHECK(p.buttons == 0);
    CHECK(p.rt == 255);
    p.rt = 0;
    pad_strip_riding(&r, &p);
    CHECK(p.buttons == 0);
    p.buttons = 0;
    pad_strip_riding(&r, &p);
    p.buttons = XB_BACK;
    pad_strip_riding(&r, &p);
    CHECK(p.buttons == XB_BACK);
    memset(&p, 0, sizeof(p));
    pad_strip_riding(&r, &p);

    p.buttons = XB_START | XB_LB;
    pad_strip_riding(&r, &p);
    CHECK(p.buttons == XB_LB);
    p.buttons = XB_START;
    pad_strip_riding(&r, &p);
    CHECK(p.buttons == 0);
    memset(&p, 0, sizeof(p));
    pad_strip_riding(&r, &p);

    p.lt = 255;
    pad_strip_riding(&r, &p);
    p.buttons = XB_BACK;
    pad_strip_riding(&r, &p);
    CHECK(p.buttons == XB_BACK);
    memset(&p, 0, sizeof(p));
    pad_strip_riding(&r, &p);

    p.buttons = XB_START | XB_BACK | XB_B;
    p.lt = 255;
    pad_strip_riding(&r, &p);
    CHECK(p.buttons == XB_B);
    memset(&p, 0, sizeof(p));
    pad_strip_riding(&r, &p);

    memset(&p, 0, sizeof(p));
    p.buttons = XB_LB | XB_A;
    layout_apply(&p, LAYOUT_SAN_FELIPE, 0);
    CHECK((p.buttons & (XB_BACK | XB_START)) == 0);
    end();
}

int Main(void) {
    wch env[8];
    g_out = GetStdHandle((DWORD)-11);
    g_verbose = GetEnvironmentVariableW(L"KOFVS_VERBOSE", env, 8) > 0;
    CreateDirectoryW(L"D:\\KOF-VS-Controller-Selector\\build", 0);
    CreateDirectoryW(L"D:\\KOF-VS-Controller-Selector\\build\\ui", 0);
    outln("KOF VS Controller Selector - pruebas automatizadas con controles simulados");
    test_a();
    test_b();
    test_c();
    test_d();
    test_e();
    test_f();
    test_g();
    test_extra();
    test_single_local();
    test_two_local_sides();
    test_boundary();
    test_hook_decoder();
    test_layout_map();
    test_layout_select();
    test_kof13_layouts();
    test_mirror();
    test_mirror_releases_side();
    test_start_select_physical();
    {
        char buf[96];
        Str s;
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "Comprobaciones: ");
        str_int(&s, g_pass);
        str_add(&s, " correctas, ");
        str_int(&s, g_fail);
        str_add(&s, " fallidas");
        outln(buf);
    }
    outln("Nota: la prueba H (exclusividad real en el juego) no se puede superar con simulaciones.");
    ExitProcess(g_fail ? 1 : 0);
    return 0;
}
