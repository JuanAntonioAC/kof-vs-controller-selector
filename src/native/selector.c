#include "selector.h"
#include "layout.h"

static const char* const g_state_names[] = {
    "IDLE", "DETECTING", "SELECTING", "CONFIRMING", "LOCKED", "PLAYING", "MATCH_FINISHED", "RESETTING", "LAYOUTS"
};

static const char* const g_side_names[] = { "AVAILABLE", "SELECTED", "READY", "LOCKED" };

const char* sel_state_name(int state) {
    if (state < 0 || state > S_LAYOUTS) return "?";
    return g_state_names[state];
}

const char* sel_side_name(int side_state) {
    if (side_state < 0 || side_state > SIDE_LOCKED) return "?";
    return g_side_names[side_state];
}

static void emit(Selector* s, const char* text, int show) {
    if (show) cstr_copy(s->message, (int)sizeof(s->message), text);
    if (s->log) s->log(s->log_ctx, text);
}

static void set_state(Selector* s, int state) {
    char buf[96];
    Str m;
    if (s->state == state) return;
    str_init(&m, buf, (int)sizeof(buf));
    str_add(&m, "STATE ");
    str_add(&m, sel_state_name(s->state));
    str_add(&m, " -> ");
    str_add(&m, sel_state_name(state));
    s->state = state;
    emit(s, buf, 0);
}

static void side_event(Selector* s, const char* what, int side, int number) {
    char buf[96];
    Str m;
    str_init(&m, buf, (int)sizeof(buf));
    str_add(&m, "CONTROL ");
    str_int(&m, number);
    str_ch(&m, ' ');
    str_add(&m, what);
    str_add(&m, side == 0 ? " P1" : " P2");
    emit(s, buf, 1);
}

static void clear_sides(Selector* s) {
    int i;
    for (i = 0; i < 2; ++i) {
        s->side_uid[i] = -1;
        s->side_number[i] = 0;
        s->side_ready[i] = 0;
        s->side_layout[i] = s->cfg.default_layout;
        s->side_cal_step[i] = 0;
        memset(s->side_cal[i], 0, sizeof(s->side_cal[i]));
        s->side_key[i][0] = 0;
        s->side_vid[i] = 0;
        s->side_pid[i] = 0;
    }
}

static int suggested_layout(const Selector* s, const SelDevice* d) {
    int last = layout_memory_last(d->key, d->vid, d->pid);
    if (last >= 0 && last < LAYOUT_COUNT) return last;
    return s->cfg.default_layout;
}

static void remember_device(Selector* s, int side, const SelDevice* d) {
    cstr_copy(s->side_key[side], (int)sizeof(s->side_key[side]), d->key);
    s->side_vid[side] = d->vid;
    s->side_pid[side] = d->pid;
}

static void take_side(Selector* s, int side, const SelDevice* d, int layout) {
    s->side_uid[side] = d->uid;
    s->side_number[side] = d->number;
    s->side_ready[side] = 0;
    s->side_layout[side] = layout;
    s->side_cal_step[side] = 0;
    remember_device(s, side, d);
}

static void release_side(Selector* s, int side) {
    s->side_uid[side] = -1;
    s->side_number[side] = 0;
    s->side_ready[side] = 0;
    s->side_layout[side] = s->cfg.default_layout;
    s->side_cal_step[side] = 0;
    s->side_key[side][0] = 0;
    s->side_vid[side] = 0;
    s->side_pid[side] = 0;
}

static void change_layout(Selector* s, int side, int step, int number) {
    char buf[96];
    Str m;
    int layout = (s->side_layout[side] + step + LAYOUT_COUNT) % LAYOUT_COUNT;
    s->side_layout[side] = layout;
    layout_memory_put(s->side_key[side], s->side_vid[side], s->side_pid[side], layout);
    str_init(&m, buf, (int)sizeof(buf));
    str_add(&m, "CONTROL ");
    str_int(&m, number);
    str_add(&m, " LAYOUT ");
    str_add(&m, layout_name(layout));
    str_add(&m, side == 0 ? " P1" : " P2");
    emit(s, buf, 1);
}

static void start_cal(Selector* s, int side, int number) {
    char buf[96];
    Str m;
    s->side_cal_step[side] = 1;
    memset(s->side_cal[side], 0, sizeof(s->side_cal[side]));
    str_init(&m, buf, (int)sizeof(buf));
    str_add(&m, "CONTROL ");
    str_int(&m, number);
    str_add(&m, " CALIBRATE 8 BUTTONS");
    str_add(&m, side == 0 ? " P1" : " P2");
    emit(s, buf, 1);
}

static void record_cal(Selector* s, int side, int code, int number) {
    char buf[96];
    Str m;
    int i, step = s->side_cal_step[side];
    if (step < 1 || step > 8 || !code) return;
    str_init(&m, buf, (int)sizeof(buf));
    for (i = 0; i < step - 1; ++i)
        if (s->side_cal[side][i] == code) {
            str_add(&m, "CONTROL ");
            str_int(&m, number);
            str_add(&m, " CAL ");
            str_int(&m, step);
            str_add(&m, "/8 SAME CODE AS STEP ");
            str_int(&m, i + 1);
            str_add(&m, " (");
            str_add(&m, layout_button_name(code));
            str_add(&m, ") - STEAM SENDS THE SAME CODE");
            emit(s, buf, 1);
            return;
        }
    s->side_cal[side][step - 1] = code;
    str_add(&m, "CONTROL ");
    str_int(&m, number);
    str_add(&m, " CAL ");
    str_int(&m, step);
    str_add(&m, "/8 ");
    str_add(&m, layout_button_name(code));
    emit(s, buf, 1);
    if (step < 8) {
        s->side_cal_step[side] = step + 1;
        return;
    }
    layout_memory_put_cal(s->side_key[side], s->side_vid[side], s->side_pid[side], s->side_cal[side]);
    s->side_cal_step[side] = 0;
    emit(s, side == 0 ? "P1 CALIBRATION SAVED" : "P2 CALIBRATION SAVED", 1);
}

static int find_dev(const SelDevice* devs, int count, int uid) {
    int i;
    for (i = 0; i < count; ++i)
        if (devs[i].uid == uid) return i;
    return -1;
}

static void arm(Selector* s, const SelDevice* devs, int count) {
    int i;
    s->prev_count = 0;
    for (i = 0; i < count && i < SEL_MAX_DEVICES; ++i) {
        s->prev[i].uid = devs[i].uid;
        s->prev[i].nav = devs[i].nav;
        s->prev[i].fight = layout_fight_mask(&devs[i].pad);
        s->prev[i].back = (devs[i].pad.buttons & XB_BACK) != 0;
        s->prev[i].start = (devs[i].pad.buttons & XB_START) != 0;
        s->prev[i].cross = (devs[i].pad.buttons & XB_A) != 0;
        s->prev_count = i + 1;
    }
}

static const SelPrev* prev_of(const Selector* s, const SelDevice* d) {
    int i;
    for (i = 0; i < s->prev_count; ++i)
        if (s->prev[i].uid == d->uid) return &s->prev[i];
    return 0;
}

static unsigned prev_nav(const Selector* s, const SelDevice* d) {
    const SelPrev* p = prev_of(s, d);
    return p ? p->nav : d->nav;
}

/* Calibration listens to all 8 stick buttons (not only A/B navigation).
   SELECT cancels, because CIRCLE is one of the 8 buttons. */
static int cal_step(Selector* s, const SelDevice* d, int side) {
    const SelPrev* p = prev_of(s, d);
    unsigned now = layout_fight_mask(&d->pad);
    unsigned fresh = now & ~(p ? p->fight : now);
    int back = (d->pad.buttons & XB_BACK) != 0;
    int pos;
    for (pos = 0; pos < LAYOUT_POS_COUNT; ++pos) {
        if (!(fresh & (1u << pos))) continue;
        record_cal(s, side, layout_pos_code(pos), d->number);
        return 1;
    }
    if (back && !(p ? p->back : back) && !now) {
        s->side_cal_step[side] = 0;
        side_event(s, "CALIBRATION CANCELLED", side, d->number);
        return 1;
    }
    return 0;
}

static int lay_index(const Selector* s, int uid) {
    int i;
    for (i = 0; i < s->lay_n; ++i)
        if (s->lay_uid[i] == uid) return i;
    return -1;
}

static int lay_slot(Selector* s, const SelDevice* d) {
    int i = lay_index(s, d->uid);
    if (i >= 0) return i;
    if (s->lay_n >= SEL_MAX_DEVICES) return -1;
    i = s->lay_n++;
    s->lay_uid[i] = d->uid;
    s->lay_value[i] = suggested_layout(s, d);
    s->lay_ready[i] = 0;
    return i;
}

static int layout_for_device(const Selector* s, const SelDevice* d) {
    int i = lay_index(s, d->uid);
    if (i >= 0) return s->lay_value[i];
    return suggested_layout(s, d);
}

static void clear_lay_ready(Selector* s) {
    int i;
    for (i = 0; i < s->lay_n; ++i) s->lay_ready[i] = 0;
}

static void change_device_layout(Selector* s, const SelDevice* d, int slot, int step) {
    char buf[96];
    Str m;
    int layout = (s->lay_value[slot] + step + LAYOUT_COUNT) % LAYOUT_COUNT;
    s->lay_value[slot] = layout;
    s->lay_ready[slot] = 0;
    layout_memory_put(d->key, d->vid, d->pid, layout);
    str_init(&m, buf, (int)sizeof(buf));
    str_add(&m, "CONTROL ");
    str_int(&m, d->number);
    str_add(&m, " LAYOUT ");
    str_add(&m, layout_name(layout));
    emit(s, buf, 1);
}

static void process_layouts(Selector* s, const SelDevice* devs, int count) {
    int i;
    for (i = 0; i < count; ++i) {
        const SelDevice* d = &devs[i];
        const SelPrev* p = prev_of(s, d);
        unsigned pressed = d->nav & ~prev_nav(s, d);
        int slot = lay_slot(s, d);
        int cross_edge, start_edge;
        if (slot < 0) continue;
        cross_edge = p && (d->pad.buttons & XB_A) && !p->cross;
        start_edge = p && (d->pad.buttons & XB_START) && !p->start;
        if (pressed & (NAV_UP | NAV_DOWN))
            change_device_layout(s, d, slot, (pressed & NAV_UP) ? -1 : 1);
        if (cross_edge) {
            char buf[96];
            Str m;
            s->lay_ready[slot] = 1;
            layout_memory_put(d->key, d->vid, d->pid, s->lay_value[slot]);
            str_init(&m, buf, (int)sizeof(buf));
            str_add(&m, "CONTROL ");
            str_int(&m, d->number);
            str_add(&m, " LAYOUT SAVED ");
            str_add(&m, layout_name(s->lay_value[slot]));
            emit(s, buf, 1);
        }
        if ((pressed & NAV_CANCEL) && s->lay_ready[slot]) {
            char buf[96];
            Str m;
            s->lay_ready[slot] = 0;
            str_init(&m, buf, (int)sizeof(buf));
            str_add(&m, "CONTROL ");
            str_int(&m, d->number);
            str_add(&m, " LAYOUT UNLOCKED");
            emit(s, buf, 1);
        }
        if (start_edge) {
            set_state(s, S_SELECTING);
            emit(s, "CHOOSE YOUR SIDE", 1);
            return;
        }
    }
}

int sel_dev_layout(const Selector* s, int uid) {
    int i = lay_index(s, uid);
    return i >= 0 ? s->lay_value[i] : -1;
}

int sel_dev_layout_ready(const Selector* s, int uid) {
    int i = lay_index(s, uid);
    return i >= 0 && s->lay_ready[i];
}

static void begin_selection(Selector* s, const SelDevice* devs, int count, int force) {
    char buf[120];
    Str m;
    int humans = s->local_humans <= 1 ? 1 : 2;
    clear_sides(s);
    set_state(s, S_DETECTING);
    s->known_count = count;
    (void)force;
    str_init(&m, buf, (int)sizeof(buf));
    str_add(&m, "DETECTED ");
    str_int(&m, count);
    str_add(&m, " CONTROLS local_humans=");
    str_int(&m, humans);
    if (s->cfg.always_show) str_add(&m, " always_show=1");
    emit(s, buf, 0);
    arm(s, devs, count);
    /* Rule: always open this screen before character select, for 1 or 2 local humans,
       regardless of how many pads are connected. */
    s->passthrough = 0;
    if (s->cfg.layout_first) {
        int i;
        clear_lay_ready(s);
        for (i = 0; i < count; ++i) lay_slot(s, &devs[i]);
        set_state(s, S_LAYOUTS);
        emit(s, "SELECT YOUR BUTTON LAYOUT", 1);
    } else if (humans <= 1) {
        set_state(s, S_SELECTING);
        emit(s, "SELECT YOUR CONTROL (1 LOCAL PLAYER)", 1);
    } else {
        set_state(s, S_SELECTING);
        emit(s, "SELECT YOUR CONTROLS (2 LOCAL PLAYERS)", 1);
    }
    emit(s, "SCREEN OPEN before character select", 0);
}

void sel_init(Selector* s, const SelConfig* cfg, SelLogFn log, void* ctx) {
    memset(s, 0, sizeof(*s));
    s->cfg = *cfg;
    s->log = log;
    s->log_ctx = ctx;
    s->state = S_IDLE;
    s->local_humans = 2;
    clear_sides(s);
}

static int any_claim_edge(const Selector* s, const SelDevice* d) {
    const SelPrev* p = prev_of(s, d);
    unsigned pressed = d->nav & ~prev_nav(s, d);
    unsigned fight_now = layout_fight_mask(&d->pad);
    unsigned fight_edge = fight_now & ~(p ? p->fight : fight_now);
    int start_edge = (d->pad.buttons & XB_START) && !(p && p->start);
    if (pressed & (NAV_LEFT | NAV_RIGHT | NAV_UP | NAV_DOWN | NAV_CONFIRM)) return 1;
    if (fight_edge) return 1;
    if (start_edge) return 1;
    return 0;
}

static void process_presses(Selector* s, const SelDevice* devs, int count) {
    int i;
    int humans = s->local_humans <= 1 ? 1 : 2;
    for (i = 0; i < count; ++i) {
        const SelDevice* d = &devs[i];
        unsigned pressed = d->nav & ~prev_nav(s, d);
        int side = sel_uid_side(s, d->uid);
        if (side >= 0 && s->side_cal_step[side] > 0) {
            cal_step(s, d, side);
            continue;
        }
        if (humans <= 1 && side < 0) {
            if (s->side_uid[0] >= 0) {
                if (any_claim_edge(s, d) || (pressed & NAV_CANCEL)) {
                    char buf[96];
                    Str m;
                    str_init(&m, buf, (int)sizeof(buf));
                    str_add(&m, "CONTROL ");
                    str_int(&m, d->number);
                    str_add(&m, " BLOCKED: SINGLE PLAYER SLOT TAKEN");
                    emit(s, buf, 1);
                }
                continue;
            }
            if (!any_claim_edge(s, d)) continue;
            take_side(s, 0, d, s->cfg.layout_first ? layout_for_device(s, d) : suggested_layout(s, d));
            side_event(s, "ACTIVATED", 0, d->number);
            continue;
        }
        if (!pressed) continue;
        if (side < 0) {
            int want = (pressed & NAV_LEFT) ? 0 : (pressed & NAV_RIGHT) ? 1 : -1;
            if (s->side_uid[0] >= 0 && s->side_uid[1] >= 0) {
                char buf[96];
                Str m;
                str_init(&m, buf, (int)sizeof(buf));
                str_add(&m, "CONTROL ");
                str_int(&m, d->number);
                str_add(&m, " BLOCKED: BOTH SIDES TAKEN");
                emit(s, buf, 1);
                continue;
            }
            if (want < 0) continue;
            if (s->side_uid[want] >= 0) {
                side_event(s, "DENIED: ALREADY TAKEN", want, d->number);
                continue;
            }
            take_side(s, want, d, s->cfg.layout_first ? layout_for_device(s, d) : suggested_layout(s, d));
            side_event(s, "SELECTED", want, d->number);
            continue;
        }
        if (!s->side_ready[side]) {
            if (pressed & NAV_CONFIRM) {
                s->side_ready[side] = 1;
                layout_memory_put(s->side_key[side], s->side_vid[side], s->side_pid[side], s->side_layout[side]);
                side_event(s, "READY", side, d->number);
            } else if (pressed & NAV_CANCEL) {
                release_side(s, side);
                side_event(s, "RELEASED", side, d->number);
            } else if (humans > 1 && (pressed & NAV_LEFT) && side == 1 && s->side_uid[0] < 0) {
                take_side(s, 0, d, s->side_layout[side]);
                release_side(s, 1);
                side_event(s, "MOVED TO", 0, d->number);
            } else if (humans > 1 && (pressed & NAV_RIGHT) && side == 0 && s->side_uid[1] < 0) {
                take_side(s, 1, d, s->side_layout[side]);
                release_side(s, 0);
                side_event(s, "MOVED TO", 1, d->number);
            } else if (!s->cfg.layout_first && (pressed & (NAV_UP | NAV_DOWN))) {
                change_layout(s, side, (pressed & NAV_UP) ? -1 : 1, d->number);
            }
        } else if (pressed & NAV_CANCEL) {
            s->side_ready[side] = 0;
            side_event(s, "CANCELLED READY", side, d->number);
        }
    }
}

static void log_match_start(Selector* s) {
    char buf[120];
    Str m;
    int humans = s->local_humans <= 1 ? 1 : 2;
    str_init(&m, buf, (int)sizeof(buf));
    str_add(&m, "MATCH ");
    str_int(&m, s->match);
    str_add(&m, " START local_humans=");
    str_int(&m, humans);
    str_add(&m, " P1=CONTROL ");
    str_int(&m, s->side_number[0]);
    str_add(&m, " (");
    str_add(&m, layout_name(s->side_layout[0]));
    str_add(&m, ")");
    if (humans > 1) {
        str_add(&m, " P2=CONTROL ");
        str_int(&m, s->side_number[1]);
        str_add(&m, " (");
        str_add(&m, layout_name(s->side_layout[1]));
        str_add(&m, ")");
    } else {
        str_add(&m, " P2=CPU/REMOTE (unassigned)");
    }
    emit(s, buf, 0);
}

void sel_update(Selector* s, const SelDevice* devs, int count, const SelSignals* sig, int dt_ms) {
    int i;
    int humans;
    if (count > SEL_MAX_DEVICES) count = SEL_MAX_DEVICES;

    if (sig->local_humans == 1 || sig->local_humans == 2)
        s->local_humans = sig->local_humans;
    else if (s->local_humans != 1 && s->local_humans != 2)
        s->local_humans = 2;
    humans = s->local_humans <= 1 ? 1 : 2;

    if (!sig->vs_active) {
        if (s->state != S_IDLE) {
            set_state(s, S_RESETTING);
            clear_sides(s);
            s->passthrough = 0;
            set_state(s, S_IDLE);
            emit(s, "SESSION END (left character-select flow)", 0);
        }
        s->vs_was_active = 0;
        arm(s, devs, count);
        return;
    }

    if (!s->vs_was_active) {
        char buf[96];
        Str m;
        s->vs_was_active = 1;
        s->session++;
        s->match = 0;
        str_init(&m, buf, (int)sizeof(buf));
        str_add(&m, "SESSION START local_humans=");
        str_int(&m, humans);
        emit(s, buf, 0);
        begin_selection(s, devs, count, 0);
    }

    if (sig->admin_reset) {
        emit(s, "ADMIN RESET", 1);
        set_state(s, S_RESETTING);
        begin_selection(s, devs, count, 1);
    } else if (sig->admin_bypass && !(s->state == S_PLAYING && s->passthrough)) {
        emit(s, "ADMIN BYPASS: NORMAL GAME INPUT", 1);
        clear_sides(s);
        s->passthrough = 1;
        s->known_count = count;
        set_state(s, S_PLAYING);
    }

    if (sig->match_boundary) {
        if (s->state == S_PLAYING) {
            set_state(s, S_MATCH_FINISHED);
            emit(s, "MATCH FINISHED", 0);
            set_state(s, S_RESETTING);
            begin_selection(s, devs, count, 0);
        } else {
            emit(s, "BOUNDARY IGNORED: NOT PLAYING", 0);
        }
    }

    for (i = 0; i < 2; ++i) {
        if (humans <= 1 && i == 1) {
            if (s->side_uid[1] >= 0) release_side(s, 1);
            continue;
        }
        if (s->side_uid[i] >= 0 && find_dev(devs, count, s->side_uid[i]) < 0) {
            side_event(s, "DISCONNECTED FROM", i, s->side_number[i]);
            release_side(s, i);
            if (s->state == S_LOCKED || s->state == S_PLAYING) {
                set_state(s, S_SELECTING);
                arm(s, devs, count);
                emit(s, "SCREEN REOPEN reason=assigned control disconnected", 0);
            }
        }
    }

    switch (s->state) {
    case S_LAYOUTS:
        process_layouts(s, devs, count);
        break;
    case S_SELECTING:
    case S_CONFIRMING:
        process_presses(s, devs, count);
        if (s->cfg.layout_first) {
            int ready = humans <= 1
                ? (s->side_uid[0] >= 0 && s->side_ready[0])
                : (s->side_uid[0] >= 0 && s->side_uid[1] >= 0 && s->side_ready[0] && s->side_ready[1]);
            if (ready) {
                s->lock_left_ms = s->cfg.lock_delay_ms;
                set_state(s, S_LOCKED);
                emit(s, humans <= 1 ? "PLAYER READY" : "BOTH PLAYERS READY", 1);
            } else if (s->state != S_SELECTING) {
                set_state(s, S_SELECTING);
            }
            break;
        }
        if (humans <= 1) {
            if (s->side_uid[0] >= 0) {
                if (s->side_ready[0]) {
                    s->lock_left_ms = s->cfg.lock_delay_ms;
                    set_state(s, S_LOCKED);
                    emit(s, "PLAYER READY", 1);
                } else {
                    if (s->state != S_CONFIRMING) emit(s, "SELECT YOUR BUTTON LAYOUT", 1);
                    set_state(s, S_CONFIRMING);
                }
            } else {
                set_state(s, S_SELECTING);
            }
            break;
        }
        if (s->side_uid[0] >= 0 && s->side_uid[1] >= 0) {
            if (s->side_ready[0] && s->side_ready[1]) {
                s->lock_left_ms = s->cfg.lock_delay_ms;
                set_state(s, S_LOCKED);
                emit(s, "BOTH PLAYERS READY", 1);
            } else {
                if (s->state != S_CONFIRMING) emit(s, "SELECT YOUR BUTTON LAYOUT", 1);
                set_state(s, S_CONFIRMING);
            }
        } else {
            set_state(s, S_SELECTING);
        }
        break;
    case S_LOCKED:
        s->lock_left_ms -= dt_ms;
        if (s->lock_left_ms <= 0) {
            s->match++;
            set_state(s, S_PLAYING);
            log_match_start(s);
        }
        break;
    case S_PLAYING:
        if (s->passthrough && s->cfg.reselect_on_new_device && count > 2 && count > s->known_count) {
            emit(s, "NEW CONTROL DURING NORMAL INPUT: SELECTION REQUIRED", 1);
            begin_selection(s, devs, count, 1);
        }
        if (s->passthrough) s->known_count = count;
        break;
    default:
        break;
    }

    arm(s, devs, count);
}

int sel_access(const Selector* s) {
    if (s->state == S_IDLE) return ACCESS_PASSTHROUGH;
    if (s->state == S_PLAYING) return s->passthrough ? ACCESS_PASSTHROUGH : ACCESS_ASSIGNED;
    return ACCESS_BLOCK_ALL;
}

int sel_side_uid(const Selector* s, int side) {
    if (side < 0 || side > 1) return -1;
    return s->side_uid[side];
}

int sel_side_state(const Selector* s, int side) {
    if (side < 0 || side > 1 || s->side_uid[side] < 0) return SIDE_AVAILABLE;
    if (s->state == S_LOCKED || s->state == S_PLAYING) return SIDE_LOCKED;
    return s->side_ready[side] ? SIDE_READY : SIDE_SELECTED;
}

int sel_side_layout(const Selector* s, int side) {
    if (side < 0 || side > 1 || s->side_uid[side] < 0) return LAYOUT_CLASSIC;
    return s->side_layout[side];
}

int sel_side_cal_step(const Selector* s, int side) {
    if (side < 0 || side > 1 || s->side_uid[side] < 0) return 0;
    return s->side_cal_step[side];
}

int sel_uid_side(const Selector* s, int uid) {
    if (uid < 0) return -1;
    if (s->side_uid[0] == uid) return 0;
    if (s->side_uid[1] == uid) return 1;
    return -1;
}

int sel_overlay_visible(const Selector* s) {
    return s->state == S_SELECTING || s->state == S_CONFIRMING || s->state == S_LOCKED || s->state == S_DETECTING || s->state == S_LAYOUTS;
}

int sel_local_humans(const Selector* s) {
    return s->local_humans <= 1 ? 1 : 2;
}
