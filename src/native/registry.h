#ifndef KOFVS_REGISTRY_H
#define KOFVS_REGISTRY_H

#include "pad.h"

#define REG_MAX 16

enum DevKind {
    KIND_XINPUT = 1,
    KIND_DINPUT = 2,
    KIND_SIM = 3
};

enum SteamDedupe {
    STEAM_DEDUPE_OFF,
    STEAM_DEDUPE_AUTO,
    STEAM_DEDUPE_ALL
};

typedef struct DevInfo {
    char key[72];
    int kind;
    int xi_index;
    int xinput_path;
    u16 vid;
    u16 pid;
    char name[40];
    void* handle;
} DevInfo;

typedef struct RegPolicy {
    int xbox_via_dinput;
    int steam_dedupe;
} RegPolicy;

/* Steam Input layouts can emit Start/Back from the same physical press as a
   shoulder or trigger (one HID report carries both). A real Start/Select never
   goes down in the very same sample as a shoulder, so such a press is dropped
   until it is released. */
typedef struct PadRide {
    u16 prev;
    u8 prev_sh;
    u16 mask;
} PadRide;

void pad_strip_riding(PadRide* r, Pad* p);

typedef struct RegEntry {
    int used;
    int uid;
    int number;
    int steam_virtual;
    DevInfo info;
    Pad pad;
    unsigned nav;
    int fresh;
    u32 raw_buttons;
    int raw_pov;
    u32 feat;
    u64 feat_ms;
    u32 feat_seq;
    int mirror_of;
    Pad logged;
    PadRide ride;
} RegEntry;

typedef void (*RegLogFn)(void* ctx, const char* text);

#define REG_MIRROR_WINDOW_MS 80
#define REG_MIRROR_MATCHES 3

typedef struct RegPair {
    int score;
    u32 dseq;
    u32 vseq;
} RegPair;

typedef struct Registry {
    RegEntry e[REG_MAX];
    RegPair pair[REG_MAX][REG_MAX];
    int next_uid;
    RegPolicy policy;
    RegLogFn log;
    void* log_ctx;
} Registry;

void reg_init(Registry* r, const RegPolicy* policy, RegLogFn log, void* ctx);
int reg_filter(const RegPolicy* policy, const DevInfo* d, const DevInfo* all, int n, const char** reason);
int reg_sync(Registry* r, const DevInfo* found, int n, void (*on_removed)(void* ctx, const DevInfo* d), void* ctx);
RegEntry* reg_by_uid(Registry* r, int uid);
RegEntry* reg_by_key(Registry* r, const char* key);
int reg_count(const Registry* r);
void reg_label(const RegEntry* e, char* out, int cap);
int reg_is_steam_virtual(const DevInfo* d);
u32 reg_feature(const Pad* p);
int reg_mirror_update(Registry* r, u64 now_ms);

#endif
