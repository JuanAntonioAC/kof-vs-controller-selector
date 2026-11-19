#include "registry.h"
#include "selector.h"

#define VID_VALVE 0x28DEu
#define VID_SONY 0x054Cu
#define VID_NINTENDO 0x057Eu

unsigned pad_nav(const Pad* p) {
    unsigned nav = 0;
    if ((p->buttons & XB_LEFT) || p->lx < -20000) nav |= NAV_LEFT;
    if ((p->buttons & XB_RIGHT) || p->lx > 20000) nav |= NAV_RIGHT;
    if ((p->buttons & XB_UP) || p->ly > 20000) nav |= NAV_UP;
    if ((p->buttons & XB_DOWN) || p->ly < -20000) nav |= NAV_DOWN;
    if (p->buttons & (XB_A | XB_START)) nav |= NAV_CONFIRM;
    /* KOF '98 on Steam Deck: the stick's L1/R2 send SELECT together with a
       trigger. Only a bare SELECT means "back". */
    if ((p->buttons & XB_B) || ((p->buttons & XB_BACK) && p->lt <= 30 && p->rt <= 30)) nav |= NAV_CANCEL;
    return nav;
}

int pad_active(const Pad* p) {
    return p->buttons || p->lt > 30 || p->rt > 30 || p->lx < -20000 || p->lx > 20000 || p->ly < -20000 || p->ly > 20000;
}

int reg_is_steam_virtual(const DevInfo* d) {
    return d->vid == VID_VALVE;
}

void reg_init(Registry* r, const RegPolicy* policy, RegLogFn log, void* ctx) {
    memset(r, 0, sizeof(*r));
    r->policy = *policy;
    r->next_uid = 1;
    r->log = log;
    r->log_ctx = ctx;
}

int reg_filter(const RegPolicy* policy, const DevInfo* d, const DevInfo* all, int n, const char** reason) {
    int i;
    *reason = 0;
    if (d->kind == KIND_XINPUT && policy->xbox_via_dinput) {
        *reason = "XINPUT DISABLED: XBOX PADS READ THROUGH DIRECTINPUT";
        return 0;
    }
    if (d->kind == KIND_DINPUT && d->xinput_path && !policy->xbox_via_dinput) {
        *reason = "DUPLICATE OF AN XINPUT DEVICE (IG_ PATH)";
        return 0;
    }
    if (d->kind == KIND_DINPUT && !policy->xbox_via_dinput && (d->vid || d->pid)) {
        for (i = 0; i < n; ++i) {
            if (all[i].kind == KIND_XINPUT && all[i].vid == d->vid && all[i].pid == d->pid) {
                *reason = "DUPLICATE OF AN XINPUT DEVICE (SAME VID:PID)";
                return 0;
            }
        }
    }
    if (d->kind == KIND_DINPUT && policy->steam_dedupe != STEAM_DEDUPE_OFF && !reg_is_steam_virtual(d)) {
        int steam = 0;
        for (i = 0; i < n; ++i)
            if (all[i].kind == KIND_XINPUT && reg_is_steam_virtual(&all[i])) steam = 1;
        if (steam) {
            if (policy->steam_dedupe == STEAM_DEDUPE_ALL) {
                *reason = "HIDDEN: STEAM INPUT VIRTUAL PAD PRESENT";
                return 0;
            }
            if (d->vid == VID_SONY || d->vid == VID_NINTENDO || d->vid == VID_VALVE) {
                *reason = "HIDDEN: LIKELY WRAPPED BY STEAM INPUT";
                return 0;
            }
        }
    }
    return 1;
}

RegEntry* reg_by_uid(Registry* r, int uid) {
    int i;
    for (i = 0; i < REG_MAX; ++i)
        if (r->e[i].used && r->e[i].uid == uid) return &r->e[i];
    return 0;
}

RegEntry* reg_by_key(Registry* r, const char* key) {
    int i;
    for (i = 0; i < REG_MAX; ++i)
        if (r->e[i].used && cstr_eq(r->e[i].info.key, key)) return &r->e[i];
    return 0;
}

int reg_count(const Registry* r) {
    int i, n = 0;
    for (i = 0; i < REG_MAX; ++i)
        if (r->e[i].used) ++n;
    return n;
}

static int lowest_free_number(const Registry* r) {
    int number, i;
    for (number = 1; number <= REG_MAX; ++number) {
        int taken = 0;
        for (i = 0; i < REG_MAX; ++i)
            if (r->e[i].used && r->e[i].number == number) taken = 1;
        if (!taken) return number;
    }
    return REG_MAX + 1;
}

static void reg_log_entry(Registry* r, const char* what, const RegEntry* e) {
    char buf[200];
    char label[64];
    Str s;
    if (!r->log) return;
    reg_label(e, label, (int)sizeof(label));
    str_init(&s, buf, (int)sizeof(buf));
    str_add(&s, what);
    str_add(&s, " CONTROL ");
    str_int(&s, e->number);
    str_add(&s, " uid=");
    str_int(&s, e->uid);
    str_add(&s, " ");
    str_add(&s, label);
    str_add(&s, " key=");
    str_add(&s, e->info.key);
    r->log(r->log_ctx, buf);
}

static void reset_pairs(Registry* r, int slot) {
    int k;
    for (k = 0; k < REG_MAX; ++k) {
        memset(&r->pair[slot][k], 0, sizeof(RegPair));
        memset(&r->pair[k][slot], 0, sizeof(RegPair));
    }
}

static void unhide_mirrors_of(Registry* r, int uid) {
    int k;
    for (k = 0; k < REG_MAX; ++k) {
        RegEntry* m = &r->e[k];
        if (!m->used || m->mirror_of != uid) continue;
        m->mirror_of = 0;
        m->number = lowest_free_number(r);
        reset_pairs(r, k);
        reg_log_entry(r, "DUPLICATE TWIN GONE, SHOWN AGAIN:", m);
    }
}

int reg_sync(Registry* r, const DevInfo* found, int n, void (*on_removed)(void* ctx, const DevInfo* d), void* ctx) {
    int i, j, changes = 0;
    for (i = 0; i < REG_MAX; ++i) {
        int present = 0;
        if (!r->e[i].used) continue;
        for (j = 0; j < n; ++j)
            if (cstr_eq(found[j].key, r->e[i].info.key)) present = 1;
        if (!present) {
            reg_log_entry(r, "REMOVED", &r->e[i]);
            if (on_removed) on_removed(ctx, &r->e[i].info);
            r->e[i].used = 0;
            unhide_mirrors_of(r, r->e[i].uid);
            ++changes;
        }
    }
    for (j = 0; j < n; ++j) {
        RegEntry* e = reg_by_key(r, found[j].key);
        if (e) {
            e->info.handle = found[j].handle;
            continue;
        }
        for (i = 0; i < REG_MAX; ++i) {
            if (r->e[i].used) continue;
            e = &r->e[i];
            memset(e, 0, sizeof(*e));
            e->info = found[j];
            e->number = lowest_free_number(r);
            e->uid = r->next_uid++;
            e->steam_virtual = reg_is_steam_virtual(&found[j]);
            e->raw_pov = -1;
            e->used = 1;
            e->fresh = 1;
            reset_pairs(r, i);
            reg_log_entry(r, "ADDED", e);
            ++changes;
            break;
        }
    }
    return changes;
}

static u8 shoulders(const Pad* p) {
    u8 s = 0;
    if (p->buttons & XB_LB) s |= 1;
    if (p->buttons & XB_RB) s |= 2;
    if (p->lt > 0x3F) s |= 4;
    if (p->rt > 0x3F) s |= 8;
    return s;
}

void pad_strip_riding(PadRide* r, Pad* p) {
    static const u16 sys[2] = { XB_START, XB_BACK };
    u8 sh = shoulders(p);
    u8 sh_new = (u8)(sh & ~r->prev_sh);
    int i;
    for (i = 0; i < 2; ++i) {
        u16 b = sys[i];
        if (!(p->buttons & b)) r->mask &= (u16)~b;
        else if (!(r->prev & b) && sh_new) r->mask |= b;
    }
    r->prev = p->buttons;
    r->prev_sh = sh;
    p->buttons &= (u16)~r->mask;
}

u32 reg_feature(const Pad* p) {
    u32 dirs = 0, count = 0;
    u16 b = (u16)(p->buttons & ~(XB_UP | XB_DOWN | XB_LEFT | XB_RIGHT));
    if ((p->buttons & XB_UP) || p->ly > 20000) dirs |= 1;
    if ((p->buttons & XB_DOWN) || p->ly < -20000) dirs |= 2;
    if ((p->buttons & XB_LEFT) || p->lx < -20000) dirs |= 4;
    if ((p->buttons & XB_RIGHT) || p->lx > 20000) dirs |= 8;
    while (b) {
        count += b & 1u;
        b >>= 1;
    }
    if (p->lt > 0x3F) ++count;
    if (p->rt > 0x3F) ++count;
    return dirs | (count << 4);
}

static int is_candidate_twin(const RegEntry* d) {
    return d->used && d->info.kind == KIND_DINPUT && !d->steam_virtual && !d->mirror_of;
}

/* A DirectInput device whose input changes always land within the window of the same
   change on an XInput device (e.g. a PlayStation-mode stick that Steam Input also exposes
   as a virtual XInput pad) is the same physical controller: it is hidden. */
int reg_mirror_update(Registry* r, u64 now_ms) {
    int i, j, found = 0;
    for (i = 0; i < REG_MAX; ++i) {
        RegEntry* e = &r->e[i];
        u32 f;
        if (!e->used) continue;
        f = reg_feature(&e->pad);
        if (f != e->feat) {
            e->feat = f;
            e->feat_ms = now_ms;
            e->feat_seq++;
        }
    }
    for (i = 0; i < REG_MAX; ++i) {
        RegEntry* d = &r->e[i];
        if (!is_candidate_twin(d)) continue;
        for (j = 0; j < REG_MAX; ++j) {
            RegEntry* v = &r->e[j];
            RegPair* pr = &r->pair[i][j];
            int dnew, vnew;
            u64 gap;
            if (!v->used || v->info.kind != KIND_XINPUT || !v->steam_virtual) continue;
            dnew = d->feat_seq != pr->dseq;
            vnew = v->feat_seq != pr->vseq;
            gap = d->feat_ms > v->feat_ms ? d->feat_ms - v->feat_ms : v->feat_ms - d->feat_ms;
            if (dnew && vnew && d->feat == v->feat && gap <= REG_MIRROR_WINDOW_MS) {
                pr->score++;
                pr->dseq = d->feat_seq;
                pr->vseq = v->feat_seq;
            } else if (dnew && now_ms - d->feat_ms > REG_MIRROR_WINDOW_MS) {
                pr->score = 0;
                pr->dseq = d->feat_seq;
            } else if (vnew && now_ms - v->feat_ms > REG_MIRROR_WINDOW_MS) {
                pr->score = 0;
                pr->vseq = v->feat_seq;
            }
            if (pr->score >= REG_MIRROR_MATCHES) {
                char buf[240];
                char a[64], b[64];
                Str s;
                reg_label(d, a, (int)sizeof(a));
                reg_label(v, b, (int)sizeof(b));
                str_init(&s, buf, (int)sizeof(buf));
                str_add(&s, "DUPLICATE BY INPUT: CONTROL ");
                str_int(&s, d->number);
                str_add(&s, " (");
                str_add(&s, a);
                str_add(&s, ") changes together with CONTROL ");
                str_int(&s, v->number);
                str_add(&s, " (");
                str_add(&s, b);
                str_add(&s, "): same physical controller, hidden");
                if (r->log) r->log(r->log_ctx, buf);
                d->mirror_of = v->uid;
                d->number = 0;
                ++found;
                break;
            }
        }
    }
    return found;
}

void reg_label(const RegEntry* e, char* out, int cap) {
    Str s;
    str_init(&s, out, cap);
    if (e->info.kind == KIND_XINPUT) {
        str_add(&s, "XINPUT ");
        str_int(&s, e->info.xi_index + 1);
    } else if (e->info.kind == KIND_SIM) {
        str_add(&s, "SIMULATED");
    } else {
        str_add(&s, "DINPUT");
    }
    if (e->steam_virtual) str_add(&s, " STEAM VIRTUAL");
    if (e->info.name[0]) {
        str_ch(&s, ' ');
        str_add(&s, e->info.name);
    }
    if (e->info.vid || e->info.pid) {
        str_ch(&s, ' ');
        str_hex(&s, e->info.vid, 4);
        str_ch(&s, ':');
        str_hex(&s, e->info.pid, 4);
    }
}
