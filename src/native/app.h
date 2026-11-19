#ifndef KOFVS_APP_H
#define KOFVS_APP_H

#include "selector.h"
#include "registry.h"
#include "physical.h"
#include "boundary.h"
#include "uimodel.h"

typedef struct AppConfig {
    int enabled;
    SelConfig sel;
    RegPolicy pol;
    PhysConfig phys;
    int block_keyboard;
    int hotkeys;
    int diagnostics;
    int tick_ms;
    int failsafe_ms;
    int debug_force_vs;
    int debug_snapshot;
    int san_felipe_l1_select;
    int steam_pad_standard;
    int log_buttons;
    u16 game_buttons[4];
    int game_buttons_set;
    char select_bgm[BOUNDARY_MAX_PATTERNS][40];
    int select_bgm_count;
} AppConfig;

typedef struct App {
    AppConfig cfg;
    Registry reg;
    Phys phys;
    Selector sel;
    Boundary bnd;
    char subtitle[64];
    char footer[96];
    char legend[96];
    volatile long ui_lock;
    UiModel ui;
    volatile long ui_visible;
    volatile long access;
    volatile long p1_uid;
    volatile long p2_uid;
    volatile long p1_layout;
    volatile long p2_layout;
    int p1_cal[8];
    int p2_cal[8];
    volatile long p1_has_cal;
    volatile long p2_has_cal;
    unsigned layout_flags;
    int p2_only_wire;
    int (*vs_active_fn)(void);
    /* Optional. Returns 1 or 2 local humans for the active session. Default 2. */
    int (*local_humans_fn)(void);
    int (*match_edge_fn)(void);
    volatile long admin_reset_req;
    volatile long admin_bypass_req;
    volatile long ev_lock;
    int ev[64];
    int ev_head;
    int ev_tail;
    int prev_state;
    int blocked_ms;
    u64 failsafe_frames;
    int hot_prev;
    int running;
    volatile long ready;
    volatile u64 music_menu_until;
} App;

/* What the music menus may read: the same devices the game is allowed to see.
   Both mods live in one process, so the music DLL asks through an export. */
typedef struct AppMusicPad {
    int id;
    int side;
    Pad pad;
} AppMusicPad;

extern App g_app;

void app_log(const char* text);
void app_log_open(const wch* path);
void app_config_defaults(AppConfig* c);
int app_config_load(AppConfig* c, const wch* path, const char* section);
void app_init(App* a, XInputGetState_fn xget, HWND hwnd);
void app_tick(App* a, int dt_ms);
void app_frame(void* device, int width, int height);
void app_bgm_event(App* a, int kind);
void app_side_pad(App* a, int side, Pad* pad);
int app_music_pads(App* a, AppMusicPad* out, int max);
void app_music_menu(App* a, int open);
int app_music_menu_active(App* a);
DWORD WINAPI app_thread(void* param);

#endif
