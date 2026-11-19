#include "layout.h"
#include "win.h"

/* Both games read DirectInput pads in PlayStation HID order: their default DirectInput
   key config (2002 0x1407CE210, '98 0x14053BBE0) uses button 0 = LP (Square),
   1 = LK (Cross), 2 = HK (Circle), 3 = HP (Triangle), 8 = SELECT, 9 = START.
   Each DirectInput button is converted to the XInput button in the same physical position. */
const int g_layout_di_default[LAYOUT_DI_DEFAULT_COUNT] = {
    XB_X, XB_A, XB_B, XB_Y, XB_LB, XB_RB, PHYS_LT, PHYS_RT, XB_BACK, XB_START, XB_LTHUMB, XB_RTHUMB
};

/* Left-to-right on the 8-button panel (PlayStation / Xbox):
     top    SQUARE/X  TRIANGLE/Y  R1/RB  L1/LB
     bottom CROSS/A   CIRCLE/B    R2/RT  L2/LT */
static const int g_pos_code[LAYOUT_POS_COUNT] = {
    XB_X, XB_Y, XB_RB, XB_LB, XB_A, XB_B, PHYS_RT, PHYS_LT
};

/* Steam Deck virtual pad (28DE:11FF) with Juan's per-game Steam Input configs
   (USB kofvs.log captures, v4 and v5 sessions):
     KOF '98: R1 and L1 both send LB, R2 sends SELECT+RT, L2 sends SELECT+LT.
              So R1 -> LB, L1 -> LT, R2 -> RT; the SELECT riding on them passes through.
     KOF 2002: R1 and R2 both send RT, L1 and L2 both send LT. So R1 -> RT, L1 -> LT
               (or LB), R2 -> RB if Steam ever sends it. A cal code may OR several codes. */
static const int g_pos_steam_98[LAYOUT_POS_COUNT] = {
    XB_X, XB_Y, XB_LB, PHYS_LT, XB_A, XB_B, PHYS_RT, XB_RB
};
static const int g_pos_steam_2002[LAYOUT_POS_COUNT] = {
    XB_X, XB_Y, PHYS_RT, PHYS_LT | XB_LB, XB_A, XB_B, XB_RB, 0
};

static int g_game_kof98;
static int g_game_kof2002;
static int g_steam_standard;

/* Six distinct 2x4 grids. A,B,C,D each appear once. Classic and San Felipe match the
   layouts already used by this mod. The other four are documented arcade/Neo Geo grids
   (see LAYOUTS.md); they are not ranked as "most popular worldwide". */
static const u8 g_profile[LAYOUT_COUNT][LAYOUT_POS_COUNT] = {
    { LAYOUT_ACT_A, LAYOUT_ACT_C, LAYOUT_ACT_NONE, LAYOUT_ACT_NONE, LAYOUT_ACT_B, LAYOUT_ACT_D, LAYOUT_ACT_NONE, LAYOUT_ACT_NONE },
    { LAYOUT_ACT_A, LAYOUT_ACT_B, LAYOUT_ACT_C,    LAYOUT_ACT_NONE, LAYOUT_ACT_D, LAYOUT_ACT_NONE, LAYOUT_ACT_NONE, LAYOUT_ACT_NONE },
    { LAYOUT_ACT_A, LAYOUT_ACT_B, LAYOUT_ACT_C,    LAYOUT_ACT_D,    LAYOUT_ACT_NONE, LAYOUT_ACT_NONE, LAYOUT_ACT_NONE, LAYOUT_ACT_NONE },
    { LAYOUT_ACT_B, LAYOUT_ACT_C, LAYOUT_ACT_D,    LAYOUT_ACT_NONE, LAYOUT_ACT_A, LAYOUT_ACT_NONE, LAYOUT_ACT_NONE, LAYOUT_ACT_NONE },
    { LAYOUT_ACT_A, LAYOUT_ACT_B, LAYOUT_ACT_NONE, LAYOUT_ACT_NONE, LAYOUT_ACT_C, LAYOUT_ACT_D, LAYOUT_ACT_NONE, LAYOUT_ACT_NONE },
    { LAYOUT_ACT_A, LAYOUT_ACT_B, LAYOUT_ACT_C,    LAYOUT_ACT_NONE, LAYOUT_ACT_NONE, LAYOUT_ACT_NONE, LAYOUT_ACT_D, LAYOUT_ACT_NONE }
};

static const char* const g_names_ui[LAYOUT_COUNT] = {
    "CLASICA", "SAN FELIPE", "LINEAL", "PULGAR", "AB/CD", "MEXICO"
};
static const char* const g_slugs[LAYOUT_COUNT] = {
    "classic", "san_felipe", "lineal", "pulgar", "abcd", "mexico"
};

static const struct { const char* name; int code; } g_names[] = {
    { "A", XB_A }, { "B", XB_B }, { "X", XB_X }, { "Y", XB_Y },
    { "LB", XB_LB }, { "RB", XB_RB }, { "LT", PHYS_LT }, { "RT", PHYS_RT },
    { "BACK", XB_BACK }, { "START", XB_START }, { "LS", XB_LTHUMB }, { "RS", XB_RTHUMB },
    { "UP", XB_UP }, { "DOWN", XB_DOWN }, { "LEFT", XB_LEFT }, { "RIGHT", XB_RIGHT },
    { "CROSS", XB_A }, { "CIRCLE", XB_B }, { "SQUARE", XB_X }, { "TRIANGLE", XB_Y },
    { "L1", XB_LB }, { "R1", XB_RB }, { "L2", PHYS_LT }, { "R2", PHYS_RT },
    { "SELECT", XB_BACK }, { "SHARE", XB_BACK }, { "OPTIONS", XB_START }, { "L3", XB_LTHUMB }, { "R3", XB_RTHUMB },
    { "NONE", 0 }
};

const char* layout_name(int layout) {
    if (layout < 0 || layout >= LAYOUT_COUNT) return "CLASICA";
    return g_names_ui[layout];
}

const char* layout_slug(int layout) {
    if (layout < 0 || layout >= LAYOUT_COUNT) return "classic";
    return g_slugs[layout];
}

void layout_set_game_id(const char* id) {
    g_game_kof98 = id && cstr_ieq(id, "kof98");
    g_game_kof2002 = id && cstr_ieq(id, "kof2002");
}

void layout_set_steam_standard(int on) {
    g_steam_standard = on ? 1 : 0;
}

/* Juan's per-game Steam Input configs never send RB. A Steam virtual pad that does
   is using Steam's standard map (R1 = RB, R2 = RT, L1 = LB, L2 = LT), so that pad
   (and only that pad) stops using the custom table until it disconnects. */
#define STEAM_STD_MAX 16
static char g_std_key[STEAM_STD_MAX][72];
static volatile long g_std_used[STEAM_STD_MAX];

static int steam_custom_applies(u16 vid, u16 pid) {
    return !g_steam_standard && vid == 0x28DE && pid == 0x11FF && (g_game_kof98 || g_game_kof2002);
}

static int steam_std_find(const char* key) {
    int i;
    for (i = 0; i < STEAM_STD_MAX; ++i)
        if (g_std_used[i] && cstr_eq(g_std_key[i], key)) return i;
    return -1;
}

int layout_steam_observe(const char* key, u16 vid, u16 pid, const Pad* p) {
    int i;
    if (!key || !key[0] || !p || !(p->buttons & XB_RB) || !steam_custom_applies(vid, pid)) return 0;
    if (steam_std_find(key) >= 0) return 0;
    for (i = 0; i < STEAM_STD_MAX; ++i) {
        if (g_std_used[i]) continue;
        cstr_copy(g_std_key[i], (int)sizeof(g_std_key[i]), key);
        g_std_used[i] = 1;
        return 1;
    }
    return 0;
}

void layout_steam_forget(const char* key) {
    int i;
    if (!key) return;
    while ((i = steam_std_find(key)) >= 0) g_std_used[i] = 0;
}

int layout_fill_cal(const char* key, u16 vid, u16 pid, int* cal) {
    int i;
    if (layout_memory_cal(key, vid, pid, cal)) return 1;
    if (!cal) return 0;
    if (steam_custom_applies(vid, pid) && !(key && steam_std_find(key) >= 0)) {
        const int* src = g_game_kof98 ? g_pos_steam_98 : g_pos_steam_2002;
        for (i = 0; i < LAYOUT_POS_COUNT; ++i) cal[i] = src[i];
        return 1;
    }
    return 0;
}

unsigned layout_fight_mask(const Pad* p) {
    unsigned m = 0;
    int pos;
    for (pos = 0; pos < LAYOUT_POS_COUNT; ++pos)
        if (layout_pos_held(p, pos, 0)) m |= 1u << pos;
    return m;
}

int layout_pos_code(int pos) {
    if (pos < 0 || pos >= LAYOUT_POS_COUNT) return 0;
    return g_pos_code[pos];
}

int layout_parse(const char* v) {
    if (!v || !v[0]) return -1;
    if (cstr_ieq(v, "classic") || cstr_ieq(v, "clasica")) return LAYOUT_CLASSIC;
    if (cstr_ieq(v, "san_felipe") || cstr_ieq(v, "sanfelipe")) return LAYOUT_SAN_FELIPE;
    if (cstr_ieq(v, "lineal") || cstr_ieq(v, "linear") || cstr_ieq(v, "neo_geo_lineal") || cstr_ieq(v, "neogeo")) return LAYOUT_LINEAL;
    if (cstr_ieq(v, "pulgar") || cstr_ieq(v, "thumb") || cstr_ieq(v, "neo_geo_pulgar")) return LAYOUT_PULGAR;
    if (cstr_ieq(v, "abcd") || cstr_ieq(v, "ab_cd") || cstr_ieq(v, "cuadrado") || cstr_ieq(v, "square")) return LAYOUT_ABCD;
    if (cstr_ieq(v, "mexico") || cstr_ieq(v, "mexico_alt") || cstr_ieq(v, "mexico_alternativo")) return LAYOUT_MEXICO;
    return -1;
}

int layout_button_code(const char* v) {
    int i;
    for (i = 0; i < (int)(sizeof(g_names) / sizeof(g_names[0])); ++i)
        if (cstr_ieq(v, g_names[i].name)) return g_names[i].code;
    return -1;
}

const char* layout_button_name(int code) {
    int i;
    for (i = 0; i < (int)(sizeof(g_names) / sizeof(g_names[0])); ++i)
        if (g_names[i].code == code) return g_names[i].name;
    return "NONE";
}

char layout_cell_char(int layout, int pos) {
    static const char letters[] = "-ABCD";
    int act = layout_cell_act(layout, pos);
    return letters[act];
}

int layout_cell_act(int layout, int pos) {
    if (layout < 0 || layout >= LAYOUT_COUNT || pos < 0 || pos >= LAYOUT_POS_COUNT) return LAYOUT_ACT_NONE;
    return g_profile[layout][pos];
}

int layout_profiles_ok(void) {
    int a, b, pos;
    unsigned seen, mask = (1u << LAYOUT_ACT_A) | (1u << LAYOUT_ACT_B) | (1u << LAYOUT_ACT_C) | (1u << LAYOUT_ACT_D);
    if (g_profile[LAYOUT_CLASSIC][0] != LAYOUT_ACT_A || g_profile[LAYOUT_CLASSIC][1] != LAYOUT_ACT_C) return 0;
    if (g_profile[LAYOUT_CLASSIC][4] != LAYOUT_ACT_B || g_profile[LAYOUT_CLASSIC][5] != LAYOUT_ACT_D) return 0;
    if (g_profile[LAYOUT_SAN_FELIPE][0] != LAYOUT_ACT_A || g_profile[LAYOUT_SAN_FELIPE][1] != LAYOUT_ACT_B) return 0;
    if (g_profile[LAYOUT_SAN_FELIPE][2] != LAYOUT_ACT_C || g_profile[LAYOUT_SAN_FELIPE][4] != LAYOUT_ACT_D) return 0;
    for (a = 0; a < LAYOUT_COUNT; ++a) {
        seen = 0;
        for (pos = 0; pos < LAYOUT_POS_COUNT; ++pos) {
            int act = g_profile[a][pos];
            if (!act) continue;
            if (seen & (1u << act)) return 0;
            seen |= 1u << act;
        }
        if (seen != mask) return 0;
        for (b = a + 1; b < LAYOUT_COUNT; ++b) {
            int same = 1;
            for (pos = 0; pos < LAYOUT_POS_COUNT; ++pos)
                if (g_profile[a][pos] != g_profile[b][pos]) same = 0;
            if (same) return 0;
        }
    }
    return 1;
}

static int code_held(const Pad* p, int code) {
    if ((code & PHYS_RT) && p->rt > 0x3F) return 1;
    if ((code & PHYS_LT) && p->lt > 0x3F) return 1;
    return (p->buttons & (u16)(code & 0xFFFF)) != 0;
}

int layout_pos_held(const Pad* p, int pos, const int* cal) {
    int code;
    if (!p || pos < 0 || pos >= LAYOUT_POS_COUNT) return 0;
    code = cal ? cal[pos] : g_pos_code[pos];
    return code_held(p, code);
}

int layout_pos_held_in(const Pad* p, int pos, const int* cal, int layout) {
    (void)layout;
    return layout_pos_held(p, pos, cal);
}

int layout_first_fight_code(const Pad* p) {
    int pos;
    for (pos = 0; pos < LAYOUT_POS_COUNT; ++pos)
        if (layout_pos_held(p, pos, 0)) return g_pos_code[pos];
    return 0;
}

int layout_action_from_pad(int layout, const Pad* p, const int* cal) {
    int pos;
    for (pos = 0; pos < LAYOUT_POS_COUNT; ++pos) {
        int act;
        if (!layout_pos_held_in(p, pos, cal, layout)) continue;
        act = layout_cell_act(layout, pos);
        if (act) return act;
    }
    return LAYOUT_ACT_NONE;
}

void layout_cal_normalize(Pad* p, const int* cal) {
    int held[LAYOUT_POS_COUNT];
    unsigned keep;
    int pos;
    if (!p || !cal) return;
    for (pos = 0; pos < LAYOUT_POS_COUNT; ++pos) held[pos] = layout_pos_held(p, pos, cal);
    keep = p->buttons & (u16)(0xFFFFu ^ (XB_X | XB_Y | XB_A | XB_B | XB_RB | XB_LB));
    p->buttons = (u16)keep;
    p->lt = 0;
    p->rt = 0;
    for (pos = 0; pos < LAYOUT_POS_COUNT; ++pos) {
        int code;
        if (!held[pos]) continue;
        code = g_pos_code[pos];
        if (code == PHYS_RT) p->rt = 255;
        else if (code == PHYS_LT) p->lt = 255;
        else p->buttons = (u16)(p->buttons | (u16)code);
    }
}

/* Positions on an 8-button stick, left to right:
     top    SQUARE/X  TRIANGLE/Y  R1/RB  L1/LB
     bottom CROSS/A   CIRCLE/B    R2/RT  L2/LT
   The game's default config is X = LP (A), A = LK (B), Y = HP (C), B = HK (D).
   Fallback path (no keymap patch): remap those physical positions onto A/B/C/D. */
void layout_apply(Pad* p, int layout, unsigned flags) {
    unsigned keep;
    u16 out;
    unsigned got = 0;
    int pos;
    int secret;
    if (!p || layout < 0 || layout >= LAYOUT_COUNT) return;
    /* Secrets follow L2 on every grid. L1 keeps an attack when the layout
       uses it (LINEAL) and no longer sends the factory LB select on its own. */
    secret = layout_pos_held(p, 7, 0);
    if (layout == LAYOUT_CLASSIC) {
        if (layout_pos_held(p, 3, 0)) p->buttons = (u16)(p->buttons & (u16)(0xFFFFu ^ XB_LB));
        if (secret) p->buttons = (u16)(p->buttons | XB_LB);
        if ((flags & LAYOUT_F_L1_SELECT) && secret) p->buttons |= XB_BACK;
        return;
    }
    if ((flags & LAYOUT_F_L1_SELECT) && secret) p->buttons |= XB_BACK;
    for (pos = 0; pos < LAYOUT_POS_COUNT; ++pos) {
        int act;
        if (!layout_pos_held_in(p, pos, 0, layout)) continue;
        act = g_profile[layout][pos];
        if (act) got |= 1u << (act - 1);
    }
    keep = p->buttons & (u16)(0xFFFFu ^ (XB_X | XB_Y | XB_A | XB_B | XB_RB | XB_LB));
    out = (u16)keep;
    if (got & 1) out |= XB_X;
    if (got & 2) out |= XB_A;
    if (got & 4) out |= XB_Y;
    if (got & 8) out |= XB_B;
    p->buttons = out;
    p->lt = 0;
    p->rt = 0;
    if (secret) p->buttons = (u16)(p->buttons | XB_LB);
}

int layout_is_deck_pad(u16 vid, u16 pid) {
    return vid == 0x28DEu && pid == 0x11FFu;
}

void layout_apply_deck(Pad* p, int layout) {
    /* Deck log: sending A did a strong kick, sending Y did a light kick,
       sending X did a light punch. Heavy punch is RB, not B. */
    static const u16 deck[4] = { XB_X, XB_Y, XB_RB, XB_A };
    u16 before;
    if (!p) return;
    before = p->buttons;
    layout_remap_attacks(p, deck);
    if (layout == LAYOUT_CLASSIC && (before & XB_RB) && !(before & XB_Y))
        p->buttons = (u16)(p->buttons & (u16)(0xFFFFu ^ XB_RB));
}

void layout_remap_attacks(Pad* p, const u16* map) {
    static const u16 factory[4] = { XB_X, XB_A, XB_Y, XB_B };
    u16 in, out;
    int i;
    if (!p || !map) return;
    in = p->buttons;
    out = (u16)(in & (u16)(0xFFFFu ^ (XB_X | XB_A | XB_Y | XB_B)));
    for (i = 0; i < 4; ++i)
        if (in & factory[i]) out = (u16)(out | map[i]);
    p->buttons = out;
}

static void add_name(Str* s, const char* name) {
    if (s->len) str_ch(s, ' ');
    str_add(s, name);
}

void layout_pad_text(const Pad* p, char* out, int cap) {
    static const struct { u16 bit; const char* name; } bits[] = {
        { XB_UP, "UP" }, { XB_DOWN, "DOWN" }, { XB_LEFT, "LEFT" }, { XB_RIGHT, "RIGHT" },
        { XB_X, "X/SQUARE" }, { XB_Y, "Y/TRIANGLE" }, { XB_RB, "RB/R1" }, { XB_LB, "LB/L1" },
        { XB_A, "A/CROSS" }, { XB_B, "B/CIRCLE" }, { XB_START, "START" }, { XB_BACK, "BACK/SELECT" },
        { XB_LTHUMB, "LS" }, { XB_RTHUMB, "RS" }
    };
    Str s;
    int i;
    str_init(&s, out, cap);
    for (i = 0; i < (int)(sizeof(bits) / sizeof(bits[0])); ++i)
        if (p->buttons & bits[i].bit) add_name(&s, bits[i].name);
    if (p->rt > 0x3F) add_name(&s, "RT/R2");
    if (p->lt > 0x3F) add_name(&s, "LT/L2");
    if (!s.len) str_add(&s, "-");
}

/* The games walk a per-port list of {outbits, rawbit}. Port 1/2 pointers live in
   the exe (.data); NULL means the factory table. Fight pads are remapped first by
   layout_apply onto factory X/A/Y/B (A/B/C/D). These tables keep factory attack
   raw bits and unbind leftover extras (R2 must not keep the factory HP+HK combo).
   Classic leaves the pointer NULL. */
#define LAYOUT_KEY_MAX 40
#define KEY_LK 0x00000020u
#define KEY_HK 0x00000040u
#define KEY_LP 0x00000080u
#define KEY_HP 0x00000100u
#define KEY_RB 0x00000200u
#define KEY_RT 0x00000400u
#define KEY_LB 0x00001000u
#define KEY_LT 0x00002000u
#define KEY_SELECT 0x10000000u

typedef struct LayoutKey {
    u32 outbits;
    i32 rawbit;
} LayoutKey;

static LayoutKey g_keys[LAYOUT_COUNT][LAYOUT_KEY_MAX];
static int g_built;

static void set_raw(LayoutKey* t, int n, u32 out, i32 raw) {
    int i;
    for (i = 0; i < n; ++i)
        if (t[i].outbits == out) t[i].rawbit = raw;
}

static int copy_table(LayoutKey* dst, const LayoutKey* src) {
    int i, n = 0;
    memset(dst, 0, sizeof(LayoutKey) * LAYOUT_KEY_MAX);
    if (!src) return 0;
    for (i = 0; i < LAYOUT_KEY_MAX - 1; ++i) {
        dst[i] = src[i];
        n = i + 1;
        if (!src[i].outbits) break;
    }
    return n;
}

static void build_one(int layout, const LayoutKey* src, unsigned flags) {
    int n = copy_table(g_keys[layout], src);
    int i;
    set_raw(g_keys[layout], n, KEY_LP, 14);
    set_raw(g_keys[layout], n, KEY_LK, 12);
    set_raw(g_keys[layout], n, KEY_HP, 15);
    set_raw(g_keys[layout], n, KEY_HK, 13);
    set_raw(g_keys[layout], n, KEY_RB, -1);
    set_raw(g_keys[layout], n, KEY_RT, -1);
    set_raw(g_keys[layout], n, KEY_LT, -1);
    set_raw(g_keys[layout], n, KEY_LB, -1);
    if (layout == LAYOUT_SAN_FELIPE && (flags & LAYOUT_F_L1_SELECT) && !g_profile[layout][3]) {
        for (i = 0; i < n; ++i)
            if (g_keys[layout][i].outbits == KEY_LB) {
                g_keys[layout][i].outbits = KEY_SELECT;
                g_keys[layout][i].rawbit = 8;
            }
    }
}

int layout_build_game_table(const void* default_table, unsigned flags) {
    const LayoutKey* src = (const LayoutKey*)default_table;
    int layout;
    g_built = 0;
    if (!src) return 0;
    for (layout = LAYOUT_SAN_FELIPE; layout < LAYOUT_COUNT; ++layout)
        build_one(layout, src, flags);
    g_built = 1;
    return LAYOUT_COUNT - 1;
}

const void* layout_game_table(int layout) {
    if (!g_built || layout <= LAYOUT_CLASSIC || layout >= LAYOUT_COUNT) return 0;
    return g_keys[layout];
}

int layout_table_raw_of(int layout, u32 outbits) {
    int i;
    if (!g_built || layout <= LAYOUT_CLASSIC || layout >= LAYOUT_COUNT) return -2;
    for (i = 0; i < LAYOUT_KEY_MAX; ++i) {
        if (g_keys[layout][i].outbits == outbits) return g_keys[layout][i].rawbit;
        if (!g_keys[layout][i].outbits) break;
    }
    return -2;
}

int layout_table_raw(u32 outbits) {
    return layout_table_raw_of(LAYOUT_SAN_FELIPE, outbits);
}

void layout_game_text(const Pad* p, char* out, int cap) {
    static const struct { u16 bit; const char* name; } bits[] = {
        { XB_X, "LP(A)" }, { XB_A, "LK(B)" }, { XB_Y, "HP(C)" }, { XB_B, "HK(D)" },
        { XB_START, "START" }, { XB_BACK, "SELECT" }, { XB_RB, "RB-DEFAULT(LP+LK)" }, { XB_LB, "LB-DEFAULT" }
    };
    Str s;
    int i;
    str_init(&s, out, cap);
    for (i = 0; i < (int)(sizeof(bits) / sizeof(bits[0])); ++i)
        if (p->buttons & bits[i].bit) add_name(&s, bits[i].name);
    if (p->rt > 0x3F) add_name(&s, "RT-DEFAULT(HP+HK)");
    if (p->lt > 0x3F) add_name(&s, "LT-DEFAULT");
    if (!s.len) str_add(&s, "-");
}

#define LAYOUT_MEM_MAX 32

typedef struct LayoutMem {
    char key[72];
    u16 vid;
    u16 pid;
    int layout;
    int has_cal;
    int cal[LAYOUT_POS_COUNT];
} LayoutMem;

static LayoutMem g_mem[LAYOUT_MEM_MAX];
static int g_mem_n;
static wch g_mem_path[320];

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex4(const char* s, u16* out) {
    int i, v = 0;
    for (i = 0; i < 4; ++i) {
        int d = hex_digit(s[i]);
        if (d < 0) return 0;
        v = v * 16 + d;
    }
    *out = (u16)v;
    return 1;
}

static int xinput_key(const char* key) {
    return key && key[0] == 'X' && key[1] == 'I' && key[2] == ':';
}

/* Identical pads share VID:PID (every Steam virtual pad on the Deck is 28DE:11FF),
   so XInput pads also keep their slot: each controller remembers its own layout. */
static void mem_id(char* out, int cap, const char* key, u16 vid, u16 pid, int with_slot) {
    if (vid || pid) {
        Str s;
        str_init(&s, out, cap);
        str_hex(&s, vid, 4);
        str_ch(&s, ':');
        str_hex(&s, pid, 4);
        if (with_slot && xinput_key(key)) {
            str_ch(&s, '@');
            str_add(&s, key);
        }
        return;
    }
    cstr_copy(out, cap, key ? key : "");
}

static LayoutMem* mem_find(const char* key, u16 vid, u16 pid, int create) {
    char id[72];
    int i;
    mem_id(id, (int)sizeof(id), key, vid, pid, 1);
    if (!id[0]) return 0;
    for (i = 0; i < g_mem_n; ++i) {
        if (cstr_ieq(g_mem[i].key, id)) return &g_mem[i];
        if (key && key[0] && cstr_ieq(g_mem[i].key, key)) return &g_mem[i];
    }
    if (!create && xinput_key(key) && (vid || pid)) {
        mem_id(id, (int)sizeof(id), key, vid, pid, 0);
        for (i = 0; i < g_mem_n; ++i)
            if (cstr_ieq(g_mem[i].key, id)) return &g_mem[i];
    }
    if (!create || g_mem_n >= LAYOUT_MEM_MAX) return 0;
    memset(&g_mem[g_mem_n], 0, sizeof(g_mem[0]));
    cstr_copy(g_mem[g_mem_n].key, (int)sizeof(g_mem[0].key), id);
    g_mem[g_mem_n].vid = vid;
    g_mem[g_mem_n].pid = pid;
    g_mem[g_mem_n].layout = -1;
    return &g_mem[g_mem_n++];
}

void layout_memory_reset(void) {
    memset(g_mem, 0, sizeof(g_mem));
    g_mem_n = 0;
}

void layout_memory_set_path(const wch* path) {
    wstr_copy(g_mem_path, 320, path ? path : L"");
}

void layout_memory_load(void) {
    static char text[8192];
    HANDLE f;
    DWORD size, read = 0;
    int i = 0;
    layout_memory_reset();
    if (!g_mem_path[0]) return;
    f = CreateFileW(g_mem_path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE) return;
    size = GetFileSize(f, 0);
    if (size >= sizeof(text)) size = sizeof(text) - 1;
    ReadFile(f, text, size, &read, 0);
    CloseHandle(f);
    text[read] = 0;
    while (text[i]) {
        char line[200];
        int n = 0, j, kind;
        LayoutMem* m;
        char id[72];
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
        kind = 0;
        if (line[j] == 'l' && line[j + 1] == 'a' && line[j + 2] == 's' && line[j + 3] == 't' && line[j + 4] == ' ') {
            kind = 1;
            j += 5;
        } else if (line[j] == 'c' && line[j + 1] == 'a' && line[j + 2] == 'l' && line[j + 3] == '2' && line[j + 4] == ' ') {
            /* Plain "cal" lines were recorded under the old non-standard Steam Input maps; ignore them. */
            kind = 2;
            j += 5;
        } else continue;
        {
            int k = 0;
            u16 vid = 0, pid = 0;
            while (line[j] && line[j] != ' ' && line[j] != '\t' && k + 1 < (int)sizeof(id)) id[k++] = line[j++];
            id[k] = 0;
            while (line[j] == ' ' || line[j] == '\t') ++j;
            if (cstr_len(id) == 9 && id[4] == ':') {
                parse_hex4(id, &vid);
                parse_hex4(id + 5, &pid);
            }
            m = mem_find(id, vid, pid, 1);
            if (!m) continue;
            if (kind == 1) {
                int layout = layout_parse(line + j);
                if (layout >= 0) m->layout = layout;
            } else {
                int pos = 0;
                while (line[j] && pos < LAYOUT_POS_COUNT) {
                    char name[16];
                    int nn = 0, code;
                    while (line[j] == ' ' || line[j] == '\t' || line[j] == ',') ++j;
                    while (line[j] && line[j] != ' ' && line[j] != '\t' && line[j] != ',') {
                        if (nn + 1 < (int)sizeof(name)) name[nn++] = line[j];
                        ++j;
                    }
                    name[nn] = 0;
                    if (!nn) break;
                    code = layout_button_code(name);
                    m->cal[pos++] = code < 0 ? 0 : code;
                }
                m->has_cal = pos == LAYOUT_POS_COUNT;
            }
        }
    }
}

void layout_memory_save(void) {
    char text[8192];
    Str s;
    HANDLE f;
    DWORD w = 0;
    int i, pos;
    if (!g_mem_path[0]) return;
    str_init(&s, text, (int)sizeof(text));
    str_add(&s, "# KOF VS last layout and optional 8-button calibration per device\r\n");
    for (i = 0; i < g_mem_n; ++i) {
        if (g_mem[i].layout >= 0) {
            str_add(&s, "last ");
            str_add(&s, g_mem[i].key);
            str_ch(&s, ' ');
            str_add(&s, layout_slug(g_mem[i].layout));
            str_add(&s, "\r\n");
        }
        if (g_mem[i].has_cal) {
            str_add(&s, "cal2 ");
            str_add(&s, g_mem[i].key);
            for (pos = 0; pos < LAYOUT_POS_COUNT; ++pos) {
                str_ch(&s, ' ');
                str_add(&s, layout_button_name(g_mem[i].cal[pos]));
            }
            str_add(&s, "\r\n");
        }
    }
    f = CreateFileW(g_mem_path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE) return;
    WriteFile(f, text, (DWORD)s.len, &w, 0);
    CloseHandle(f);
}

int layout_memory_last(const char* key, u16 vid, u16 pid) {
    LayoutMem* m = mem_find(key, vid, pid, 0);
    return m ? m->layout : -1;
}

void layout_memory_put(const char* key, u16 vid, u16 pid, int layout) {
    LayoutMem* m = mem_find(key, vid, pid, 1);
    if (!m || layout < 0 || layout >= LAYOUT_COUNT) return;
    m->layout = layout;
    layout_memory_save();
}

int layout_memory_cal(const char* key, u16 vid, u16 pid, int* cal) {
    LayoutMem* m = mem_find(key, vid, pid, 0);
    int pos;
    if (!m || !m->has_cal || !cal) return 0;
    for (pos = 0; pos < LAYOUT_POS_COUNT; ++pos) cal[pos] = m->cal[pos];
    return 1;
}

void layout_memory_put_cal(const char* key, u16 vid, u16 pid, const int* cal) {
    LayoutMem* m = mem_find(key, vid, pid, 1);
    int pos;
    if (!m || !cal) return;
    for (pos = 0; pos < LAYOUT_POS_COUNT; ++pos) m->cal[pos] = cal[pos];
    m->has_cal = 1;
    layout_memory_save();
}
