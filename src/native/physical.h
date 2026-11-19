#ifndef KOFVS_PHYSICAL_H
#define KOFVS_PHYSICAL_H

#include "registry.h"
#include "dinput_min.h"

#define PHYS_MAX_DI_PROFILES 8

typedef struct PhysDiProfile {
    u16 vid;
    u16 pid;
    int map[32];
} PhysDiProfile;

typedef struct PhysConfig {
    int di_button_map[32];
    PhysDiProfile di_profiles[PHYS_MAX_DI_PROFILES];
    int di_profile_count;
    int deadzone;
    int xinput_caps;
    int use_dinput;
    int enum_interval_ms;
} PhysConfig;

typedef struct PhysDi {
    int used;
    int rejected;
    int seen;
    int range_ok;
    int ig_index;
    int fails;
    const int* map;
    GUID guid;
    void* dev;
    char key[72];
    DevInfo info;
} PhysDi;

typedef struct Phys {
    Registry* reg;
    PhysConfig cfg;
    XInputGetState_fn xget;
    XInputGetCapabilitiesEx_fn xcaps;
    void* di;
    HWND hwnd;
    PhysDi dis[REG_MAX];
    u64 last_enum;
    int force_enum;
    u16 xi_vid[4];
    u16 xi_pid[4];
    int xi_known[4];
    volatile long lock;
    RegLogFn log;
    void* log_ctx;
    int (*extra_enum)(void* ctx, DevInfo* out, int max);
    int (*extra_read)(void* ctx, const DevInfo* d, Pad* out);
    void* extra_ctx;
} Phys;

HMODULE phys_load_system_dinput8(void);
void phys_default_config(PhysConfig* c);
int phys_init(Phys* p, Registry* reg, const PhysConfig* cfg, XInputGetState_fn xget, HWND hwnd, RegLogFn log, void* ctx);
void phys_tick(Phys* p);
void phys_enumerate(Phys* p);
int phys_read_live(Phys* p, int uid, Pad* out);
int phys_peek(Phys* p, int uid, Pad* out);
int phys_peek_all(Phys* p, int* uids, Pad* pads, int max);
int phys_xinput_index(Phys* p, int uid);
int phys_owns_device(Phys* p, void* dev);
void phys_shutdown(Phys* p);
void di_to_pad(const PhysConfig* c, const int* map, const DIJOYSTATE_* js, int range_ok, Pad* p);
const int* phys_di_map(const PhysConfig* c, u16 vid, u16 pid);

#endif
