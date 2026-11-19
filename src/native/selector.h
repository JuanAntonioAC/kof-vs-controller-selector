#ifndef KOFVS_SELECTOR_H
#define KOFVS_SELECTOR_H

#include "pad.h"

#define SEL_MAX_DEVICES 16

#define NAV_LEFT 1u
#define NAV_RIGHT 2u
#define NAV_CONFIRM 4u
#define NAV_CANCEL 8u
#define NAV_UP 16u
#define NAV_DOWN 32u

enum SelState {
    S_IDLE,
    S_DETECTING,
    S_SELECTING,
    S_CONFIRMING,
    S_LOCKED,
    S_PLAYING,
    S_MATCH_FINISHED,
    S_RESETTING,
    S_LAYOUTS
};

enum SideState {
    SIDE_AVAILABLE,
    SIDE_SELECTED,
    SIDE_READY,
    SIDE_LOCKED
};

enum GameAccess {
    ACCESS_PASSTHROUGH,
    ACCESS_BLOCK_ALL,
    ACCESS_ASSIGNED
};

typedef struct SelDevice {
    int uid;
    int number;
    unsigned nav;
    Pad pad;
    u16 vid;
    u16 pid;
    char key[72];
} SelDevice;

typedef struct SelSignals {
    int vs_active;
    /* Local human participants for this session: 1 = single panel, 2 = dual.
       Online must report 1 (remote player is not configured by this mod). */
    int local_humans;
    int match_boundary;
    int admin_reset;
    int admin_bypass;
} SelSignals;

typedef struct SelConfig {
    int lock_delay_ms;
    int always_show;
    int reselect_on_new_device;
    int default_layout;
    /* 1 = cada control elige su perfil y despues se reclaman P1 y P2.
       KOF XIII. En '98 y 2002 queda en 0. */
    int layout_first;
} SelConfig;

typedef void (*SelLogFn)(void* ctx, const char* text);

typedef struct SelPrev {
    int uid;
    unsigned nav;
    unsigned fight;
    int back;
    int start;
    int cross;
} SelPrev;

typedef struct Selector {
    SelConfig cfg;
    int state;
    int side_uid[2];
    int side_number[2];
    int side_ready[2];
    int side_layout[2];
    int side_cal_step[2];
    int side_cal[2][8];
    char side_key[2][72];
    u16 side_vid[2];
    u16 side_pid[2];
    int passthrough;
    int lock_left_ms;
    int vs_was_active;
    int local_humans; /* 1 or 2; drives single vs dual UI and ready rules */
    int session;
    int match;
    int known_count;
    int lay_uid[SEL_MAX_DEVICES];
    int lay_value[SEL_MAX_DEVICES];
    int lay_ready[SEL_MAX_DEVICES];
    int lay_n;
    SelPrev prev[SEL_MAX_DEVICES];
    int prev_count;
    char message[96];
    SelLogFn log;
    void* log_ctx;
} Selector;

void sel_init(Selector* s, const SelConfig* cfg, SelLogFn log, void* ctx);
void sel_update(Selector* s, const SelDevice* devs, int count, const SelSignals* sig, int dt_ms);

int sel_access(const Selector* s);
int sel_side_uid(const Selector* s, int side);
int sel_side_state(const Selector* s, int side);
int sel_side_layout(const Selector* s, int side);
int sel_side_cal_step(const Selector* s, int side);
int sel_uid_side(const Selector* s, int uid);
int sel_overlay_visible(const Selector* s);
int sel_local_humans(const Selector* s);
int sel_dev_layout(const Selector* s, int uid);
int sel_dev_layout_ready(const Selector* s, int uid);
const char* sel_state_name(int state);
const char* sel_side_name(int side_state);

#endif
