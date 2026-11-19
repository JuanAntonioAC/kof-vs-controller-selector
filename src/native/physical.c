#include "physical.h"
#include "layout.h"

const GUID IID_IDirectInput8W_ = { 0xBF798031, 0x483A, 0x4DA2, { 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00 } };
const GUID IID_IDirectInput8A_ = { 0xBF798030, 0x483A, 0x4DA2, { 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00 } };
const GUID GUID_SysKeyboard_ = { 0x6F1D2B61, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

static const GUID GUID_XAxis_ = { 0xA36D02E0, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID GUID_YAxis_ = { 0xA36D02E1, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID GUID_ZAxis_ = { 0xA36D02E2, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID GUID_RxAxis_ = { 0xA36D02F4, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID GUID_RyAxis_ = { 0xA36D02F5, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID GUID_RzAxis_ = { 0xA36D02E3, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID GUID_Slider_ = { 0xA36D02E4, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
static const GUID GUID_POV_ = { 0xA36D02F2, 0xC9F3, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

#define AXIS_T (DIDFT_AXIS | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL)
#define POV_T (DIDFT_POV | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL)
#define BTN_T (DIDFT_BUTTON | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL)
#define BTN(n) { 0, 48 + (n), BTN_T, 0 }

static DIOBJECTDATAFORMAT_ g_joy_objects[44] = {
    { &GUID_XAxis_, 0, AXIS_T, DIDOI_ASPECTPOSITION },
    { &GUID_YAxis_, 4, AXIS_T, DIDOI_ASPECTPOSITION },
    { &GUID_ZAxis_, 8, AXIS_T, DIDOI_ASPECTPOSITION },
    { &GUID_RxAxis_, 12, AXIS_T, DIDOI_ASPECTPOSITION },
    { &GUID_RyAxis_, 16, AXIS_T, DIDOI_ASPECTPOSITION },
    { &GUID_RzAxis_, 20, AXIS_T, DIDOI_ASPECTPOSITION },
    { &GUID_Slider_, 24, AXIS_T, DIDOI_ASPECTPOSITION },
    { &GUID_Slider_, 28, AXIS_T, DIDOI_ASPECTPOSITION },
    { &GUID_POV_, 32, POV_T, 0 },
    { &GUID_POV_, 36, POV_T, 0 },
    { &GUID_POV_, 40, POV_T, 0 },
    { &GUID_POV_, 44, POV_T, 0 },
    BTN(0), BTN(1), BTN(2), BTN(3), BTN(4), BTN(5), BTN(6), BTN(7),
    BTN(8), BTN(9), BTN(10), BTN(11), BTN(12), BTN(13), BTN(14), BTN(15),
    BTN(16), BTN(17), BTN(18), BTN(19), BTN(20), BTN(21), BTN(22), BTN(23),
    BTN(24), BTN(25), BTN(26), BTN(27), BTN(28), BTN(29), BTN(30), BTN(31),
};

const DIDATAFORMAT_ g_dfJoystick = { sizeof(DIDATAFORMAT_), sizeof(DIOBJECTDATAFORMAT_), DIDF_ABSAXIS, sizeof(DIJOYSTATE_), 44, g_joy_objects };

static void plog(Phys* p, const char* text) {
    if (p->log) p->log(p->log_ctx, text);
}

static void lock(Phys* p) {
    while (_InterlockedCompareExchange(&p->lock, 1, 0) != 0) Sleep(0);
}

static int try_lock(Phys* p) {
    return _InterlockedCompareExchange(&p->lock, 1, 0) == 0;
}

static void unlock(Phys* p) {
    _InterlockedExchange(&p->lock, 0);
}

/* True when the game folder ships King of Moves as dinput8.dll (~8 MB). */
static int game_dinput8_is_kom(void) {
    wch game_di[300];
    u32 n = GetModuleFileNameW(0, game_di, 280);
    HANDLE hf;
    DWORD lo, hi;
    int i;
    if (n == 0 || n >= 280) return 0;
    i = (int)n;
    while (i > 0 && game_di[i - 1] != '\\' && game_di[i - 1] != '/') --i;
    game_di[i] = 0;
    wstr_cat(game_di, 300, L"dinput8.dll");
    hf = CreateFileW(game_di, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hf == INVALID_HANDLE) return 0;
    hi = 0;
    lo = GetFileSize(hf, &hi);
    CloseHandle(hf);
    return (hi != 0 || lo > 1000000u) ? 1 : 0;
}

HMODULE phys_load_system_dinput8(void) {
    wch path[300];
    u32 n;
    HMODULE existing = GetModuleHandleW(L"dinput8.dll");
    if (existing) return existing;

    /* Proton WINEDLLOVERRIDES=dinput8=n,b makes ANY LoadLibrary("dinput8")
       pull the game-folder KoM DLL. If we do that from xinput1_4 before the
       EXE finishes mapping, KoM skips stage3d/cset (no "applied 85 patch sites")
       and a later second attach logs "unexpected bytes". Wait for the game. */
    if (game_dinput8_is_kom()) return 0;

    n = GetSystemDirectoryW(path, 260);
    if (n == 0 || n >= 260) return 0; /* never bare LoadLibrary(L"dinput8.dll") */
    wstr_cat(path, 300, L"\\dinput8.dll");
    return LoadLibraryW(path);
}

static void phys_try_attach_dinput(Phys* p) {
    HMODULE dm;
    DirectInput8Create_fn create;
    HRESULT hr;
    if (!p || p->di || !p->cfg.use_dinput) return;
    dm = phys_load_system_dinput8();
    if (!dm) return;
    create = (DirectInput8Create_fn)GetProcAddress(dm, "DirectInput8Create");
    hr = create ? create(GetModuleHandleW(0), DI_VERSION, &IID_IDirectInput8W_, &p->di, 0) : -1;
    if (hr < 0 || !p->di) {
        p->di = 0;
        plog(p, "DIRECTINPUT ATTACH FAILED (will retry)");
    } else {
        plog(p, "DIRECTINPUT READY (own IDirectInput8W instance)");
    }
}

void phys_default_config(PhysConfig* c) {
    int i;
    memset(c, 0, sizeof(*c));
    for (i = 0; i < LAYOUT_DI_DEFAULT_COUNT; ++i) c->di_button_map[i] = g_layout_di_default[i];
    c->deadzone = 8000;
    c->xinput_caps = 1;
    c->use_dinput = 1;
    c->enum_interval_ms = 1500;
}

static void guid_key(const char* prefix, const GUID* g, char* out, int cap) {
    Str s;
    int i;
    str_init(&s, out, cap);
    str_add(&s, prefix);
    str_hex(&s, g->Data1, 8);
    str_ch(&s, '-');
    str_hex(&s, g->Data2, 4);
    str_ch(&s, '-');
    str_hex(&s, g->Data3, 4);
    str_ch(&s, '-');
    for (i = 0; i < 8; ++i) str_hex(&s, g->Data4[i], 2);
}

static int guid_eq(const GUID* a, const GUID* b) {
    const u8* x = (const u8*)a;
    const u8* y = (const u8*)b;
    int i;
    for (i = 0; i < 16; ++i)
        if (x[i] != y[i]) return 0;
    return 1;
}

int phys_init(Phys* p, Registry* reg, const PhysConfig* cfg, XInputGetState_fn xget, HWND hwnd, RegLogFn log, void* ctx) {
    memset(p, 0, sizeof(*p));
    p->reg = reg;
    p->cfg = *cfg;
    p->xget = xget;
    p->hwnd = hwnd;
    p->log = log;
    p->log_ctx = ctx;
    p->force_enum = 1;
    if (cfg->xinput_caps) {
        wch path[300];
        HMODULE x;
        u32 n = GetSystemDirectoryW(path, 260);
        if (n > 0 && n < 260) {
            wstr_cat(path, 300, L"\\XInput1_4.dll");
            x = LoadLibraryW(path);
            if (x) p->xcaps = (XInputGetCapabilitiesEx_fn)GetProcAddress(x, (const char*)(u64)108);
        }
        plog(p, p->xcaps ? "XINPUT DETAILS: XInputGetCapabilitiesEx available" : "XINPUT DETAILS: not available (no VID/PID for XInput pads)");
    }
    if (cfg->use_dinput) {
        if (game_dinput8_is_kom() && !GetModuleHandleW(L"dinput8.dll")) {
            plog(p, "DIRECTINPUT DEFER (KoM is dinput8 — wait for game so stage3d can arm)");
        } else {
            phys_try_attach_dinput(p);
            if (!p->di) plog(p, "DIRECTINPUT INIT PENDING");
        }
    }
    return 1;
}

typedef struct EnumCtx {
    Phys* p;
    DIDEVICEINSTANCEW_ list[REG_MAX];
    int count;
} EnumCtx;

static int WINAPI enum_cb(const DIDEVICEINSTANCEW_* inst, void* ctx) {
    EnumCtx* e = (EnumCtx*)ctx;
    if (e->count < REG_MAX) e->list[e->count++] = *inst;
    return DIENUM_CONTINUE;
}

static PhysDi* di_find(Phys* p, const GUID* g) {
    int i;
    for (i = 0; i < REG_MAX; ++i)
        if (p->dis[i].used && guid_eq(&p->dis[i].guid, g)) return &p->dis[i];
    return 0;
}

static void di_release(PhysDi* d) {
    if (d->dev) {
        VT(d->dev, DID_Unacquire, DID_Simple_fn)(d->dev);
        VT(d->dev, DID_Release, Release_fn)(d->dev);
    }
    memset(d, 0, sizeof(*d));
}

static int hex_digit(wch c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void di_open(Phys* p, const DIDEVICEINSTANCEW_* inst) {
    PhysDi* d = 0;
    DIPROPGUIDANDPATH_ gp;
    DIPROPRANGE_ range;
    HRESULT hr;
    int i;
    char buf[200];
    Str s;
    for (i = 0; i < REG_MAX && !d; ++i)
        if (!p->dis[i].used) d = &p->dis[i];
    if (!d) return;
    memset(d, 0, sizeof(*d));
    d->used = 1;
    d->guid = inst->guidInstance;
    guid_key("DI:", &inst->guidInstance, d->key, (int)sizeof(d->key));
    cstr_copy(d->info.key, (int)sizeof(d->info.key), d->key);
    d->info.kind = KIND_DINPUT;
    d->info.xi_index = -1;
    d->ig_index = -1;
    d->info.vid = (u16)(inst->guidProduct.Data1 & 0xFFFF);
    d->info.pid = (u16)(inst->guidProduct.Data1 >> 16);
    wstr_to_ascii(inst->tszProductName, d->info.name, (int)sizeof(d->info.name));
    d->map = phys_di_map(&p->cfg, d->info.vid, d->info.pid);
    hr = VT(p->di, DI8_CreateDevice, DI8_CreateDevice_fn)(p->di, &inst->guidInstance, &d->dev, 0);
    if (hr < 0 || !d->dev) {
        d->dev = 0;
        d->rejected = 1;
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "DI CREATE FAILED ");
        str_add(&s, d->info.name);
        plog(p, buf);
        return;
    }
    memset(&gp, 0, sizeof(gp));
    gp.diph.dwSize = sizeof(gp);
    gp.diph.dwHeaderSize = sizeof(DIPROPHEADER_);
    gp.diph.dwHow = DIPH_DEVICE;
    if (VT(d->dev, DID_GetProperty, DID_Property_fn)(d->dev, DIPROP_GUIDANDPATH, &gp.diph) >= 0) {
        char path[200];
        int ig = wstr_find_ci(gp.wszPath, "IG_");
        d->info.xinput_path = ig >= 0;
        if (ig >= 0) {
            int hi = hex_digit(gp.wszPath[ig + 3]), lo = hex_digit(gp.wszPath[ig + 4]);
            if (hi >= 0 && lo >= 0) d->ig_index = hi * 16 + lo;
        }
        wstr_to_ascii(gp.wszPath, path, (int)sizeof(path));
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "DI PATH ");
        str_add(&s, path);
        plog(p, buf);
    }
    if (d->info.xinput_path) {
        VT(d->dev, DID_Release, Release_fn)(d->dev);
        d->dev = 0;
        d->rejected = 1;
        return;
    }
    VT(d->dev, DID_SetDataFormat, DID_SetDataFormat_fn)(d->dev, &g_dfJoystick);
    if (p->hwnd) VT(d->dev, DID_SetCooperativeLevel, DID_SetCooperativeLevel_fn)(d->dev, p->hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    memset(&range, 0, sizeof(range));
    range.diph.dwSize = sizeof(range);
    range.diph.dwHeaderSize = sizeof(DIPROPHEADER_);
    range.diph.dwHow = DIPH_DEVICE;
    range.lMin = -32768;
    range.lMax = 32767;
    d->range_ok = VT(d->dev, DID_SetProperty, DID_Property_fn)(d->dev, DIPROP_RANGE, &range.diph) >= 0;
    VT(d->dev, DID_Acquire, DID_Simple_fn)(d->dev);
    d->info.handle = d->dev;
    str_init(&s, buf, (int)sizeof(buf));
    str_add(&s, "DI OPEN ");
    str_add(&s, d->info.name);
    str_add(&s, d->range_ok ? " (axes ok)" : " (axis range not settable: axes ignored)");
    str_add(&s, d->map == p->cfg.di_button_map ? " buttons: di_button_N map" : " buttons: di_map_VVVV_PPPP profile");
    plog(p, buf);
}

const int* phys_di_map(const PhysConfig* c, u16 vid, u16 pid) {
    int i;
    for (i = 0; i < c->di_profile_count; ++i)
        if (c->di_profiles[i].vid == vid && c->di_profiles[i].pid == pid) return c->di_profiles[i].map;
    return c->di_button_map;
}

static void on_removed(void* ctx, const DevInfo* info) {
    Phys* p = (Phys*)ctx;
    int i;
    layout_steam_forget(info->key);
    if (info->kind != KIND_DINPUT) return;
    for (i = 0; i < REG_MAX; ++i)
        if (p->dis[i].used && cstr_eq(p->dis[i].key, info->key)) di_release(&p->dis[i]);
}

void phys_enumerate(Phys* p) {
    static EnumCtx ec;
    DevInfo found[REG_MAX];
    DevInfo accepted[REG_MAX];
    int nfound = 0, naccepted = 0, i;
    XINPUT_STATE_ st;
    /* Attach DI once KoM/dinput8 is in-process (Proton-safe). */
    if (p->cfg.use_dinput && !p->di) phys_try_attach_dinput(p);
    if (p->xget) {
        for (i = 0; i < 4 && nfound < REG_MAX; ++i) {
            DevInfo* d;
            if (p->xget((DWORD)i, &st) != ERROR_SUCCESS_) {
                p->xi_known[i] = 0;
                continue;
            }
            if (!p->xi_known[i]) {
                XINPUT_CAPABILITIES_EX_ caps;
                p->xi_vid[i] = 0;
                p->xi_pid[i] = 0;
                memset(&caps, 0, sizeof(caps));
                if (p->xcaps && p->xcaps(1, (DWORD)i, 0, &caps) == ERROR_SUCCESS_) {
                    p->xi_vid[i] = caps.VendorId;
                    p->xi_pid[i] = caps.ProductId;
                }
                p->xi_known[i] = 1;
            }
            d = &found[nfound++];
            memset(d, 0, sizeof(*d));
            d->kind = KIND_XINPUT;
            d->xi_index = i;
            d->vid = p->xi_vid[i];
            d->pid = p->xi_pid[i];
            d->key[0] = 'X';
            d->key[1] = 'I';
            d->key[2] = ':';
            d->key[3] = (char)('0' + i);
            d->key[4] = 0;
        }
    }
    if (p->di) {
        ec.p = p;
        ec.count = 0;
        VT(p->di, DI8_EnumDevices, DI8_EnumDevices_fn)(p->di, DI8DEVCLASS_GAMECTRL, enum_cb, &ec, DIEDFL_ATTACHEDONLY);
        for (i = 0; i < REG_MAX; ++i) p->dis[i].seen = 0;
        for (i = 0; i < ec.count; ++i) {
            PhysDi* d = di_find(p, &ec.list[i].guidInstance);
            if (!d) {
                di_open(p, &ec.list[i]);
                d = di_find(p, &ec.list[i].guidInstance);
            }
            if (!d) continue;
            d->seen = d->fails <= 30;
            if (d->seen && !d->rejected && d->dev && nfound < REG_MAX) found[nfound++] = d->info;
        }
        for (i = 0; i < nfound; ++i) {
            int j;
            if (found[i].kind != KIND_XINPUT || found[i].vid != 0) continue;
            for (j = 0; j < REG_MAX; ++j) {
                const PhysDi* d = &p->dis[j];
                if (d->used && d->seen && d->ig_index == found[i].xi_index && d->info.vid == 0x28DE) {
                    found[i].vid = d->info.vid;
                    found[i].pid = d->info.pid;
                    p->xi_vid[found[i].xi_index] = d->info.vid;
                    p->xi_pid[found[i].xi_index] = d->info.pid;
                    break;
                }
            }
        }
    }
    if (p->extra_enum) nfound += p->extra_enum(p->extra_ctx, found + nfound, REG_MAX - nfound);
    for (i = 0; i < nfound; ++i) {
        const char* reason = 0;
        if (reg_filter(&p->reg->policy, &found[i], found, nfound, &reason)) {
            accepted[naccepted++] = found[i];
        } else {
            char buf[200];
            Str s;
            str_init(&s, buf, (int)sizeof(buf));
            str_add(&s, "SKIP ");
            str_add(&s, found[i].key);
            str_ch(&s, ' ');
            str_add(&s, found[i].name);
            str_add(&s, ": ");
            str_add(&s, reason);
            if (!reg_by_key(p->reg, found[i].key)) plog(p, buf);
        }
    }
    lock(p);
    reg_sync(p->reg, accepted, naccepted, on_removed, p);
    for (i = 0; i < REG_MAX; ++i)
        if (p->dis[i].used && !p->dis[i].seen && p->di) di_release(&p->dis[i]);
    unlock(p);
    p->last_enum = GetTickCount64();
    p->force_enum = 0;
}

static int clamp16(LONG v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int)v;
}

void di_to_pad(const PhysConfig* c, const int* map, const DIJOYSTATE_* js, int range_ok, Pad* p) {
    int b;
    DWORD pov = js->rgdwPOV[0];
    memset(p, 0, sizeof(*p));
    for (b = 0; b < 32; ++b) {
        int m;
        if (!(js->rgbButtons[b] & 0x80)) continue;
        m = map[b];
        if (m == PHYS_LT) p->lt = 255;
        else if (m == PHYS_RT) p->rt = 255;
        else p->buttons |= (u16)m;
    }
    if ((pov & 0xFFFF) != 0xFFFF) {
        DWORD a = pov % 36000;
        if (a >= 29250 || a <= 6750) p->buttons |= XB_UP;
        if (a >= 2250 && a <= 15750) p->buttons |= XB_RIGHT;
        if (a >= 11250 && a <= 24750) p->buttons |= XB_DOWN;
        if (a >= 20250 && a <= 33750) p->buttons |= XB_LEFT;
    }
    if (range_ok) {
        int x = clamp16(js->lX);
        int y = clamp16(-js->lY);
        if (x > -c->deadzone && x < c->deadzone) x = 0;
        if (y > -c->deadzone && y < c->deadzone) y = 0;
        p->lx = (i16)x;
        p->ly = (i16)y;
    }
}

static int read_device(Phys* p, RegEntry* e, Pad* out);

static int read_entry(Phys* p, RegEntry* e, Pad* out) {
    if (!read_device(p, e, out)) return 0;
    if (layout_steam_observe(e->info.key, e->info.vid, e->info.pid, out)) {
        char buf[120];
        Str s;
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "STEAM PAD ");
        str_add(&s, e->info.key);
        str_add(&s, " SENDS RB: STANDARD STEAM MAP (R1=RB R2=RT L1=LB L2=LT)");
        plog(p, buf);
    }
    pad_strip_riding(&e->ride, out);
    return 1;
}

static int read_device(Phys* p, RegEntry* e, Pad* out) {
    if (e->info.kind == KIND_XINPUT) {
        XINPUT_STATE_ st;
        if (!p->xget || p->xget((DWORD)e->info.xi_index, &st) != ERROR_SUCCESS_) return 0;
        out->buttons = st.wButtons;
        out->lt = st.bLeftTrigger;
        out->rt = st.bRightTrigger;
        out->lx = st.sThumbLX;
        out->ly = st.sThumbLY;
        out->rx = st.sThumbRX;
        out->ry = st.sThumbRY;
        return 1;
    } else if (e->info.kind == KIND_SIM) {
        return p->extra_read ? p->extra_read(p->extra_ctx, &e->info, out) : 0;
    } else {
        int i, b;
        for (i = 0; i < REG_MAX; ++i) {
            PhysDi* d = &p->dis[i];
            DIJOYSTATE_ js;
            HRESULT hr;
            if (!d->used || !d->dev || !cstr_eq(d->key, e->info.key)) continue;
            VT(d->dev, DID_Poll, DID_Simple_fn)(d->dev);
            memset(&js, 0, sizeof(js));
            hr = VT(d->dev, DID_GetDeviceState, DID_GetDeviceState_fn)(d->dev, sizeof(js), &js);
            if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
                VT(d->dev, DID_Acquire, DID_Simple_fn)(d->dev);
                hr = VT(d->dev, DID_GetDeviceState, DID_GetDeviceState_fn)(d->dev, sizeof(js), &js);
            }
            if (hr < 0) {
                d->fails++;
                return 0;
            }
            d->fails = 0;
            di_to_pad(&p->cfg, d->map ? d->map : p->cfg.di_button_map, &js, d->range_ok, out);
            e->raw_buttons = 0;
            for (b = 0; b < 32; ++b)
                if (js.rgbButtons[b] & 0x80) e->raw_buttons |= 1u << b;
            e->raw_pov = (js.rgdwPOV[0] & 0xFFFF) == 0xFFFF ? -1 : (int)js.rgdwPOV[0];
            return 1;
        }
        return 0;
    }
}

void phys_tick(Phys* p) {
    int i, lost = 0;
    u64 now = GetTickCount64();
    if ((p->force_enum && now - p->last_enum >= 250) || now - p->last_enum >= (u64)p->cfg.enum_interval_ms) phys_enumerate(p);
    lock(p);
    for (i = 0; i < REG_MAX; ++i) {
        RegEntry* e = &p->reg->e[i];
        Pad pad;
        if (!e->used) continue;
        memset(&pad, 0, sizeof(pad));
        if (read_entry(p, e, &pad)) {
            e->pad = pad;
            e->nav = pad_nav(&pad);
        } else {
            memset(&e->pad, 0, sizeof(e->pad));
            e->nav = 0;
            lost = 1;
        }
    }
    reg_mirror_update(p->reg, now);
    unlock(p);
    if (lost) p->force_enum = 1;
}

int phys_read_live(Phys* p, int uid, Pad* out) {
    RegEntry* e;
    int ok = 0;
    if (!try_lock(p)) {
        e = reg_by_uid(p->reg, uid);
        if (!e) return 0;
        *out = e->pad;
        return 1;
    }
    e = reg_by_uid(p->reg, uid);
    if (e) {
        ok = read_entry(p, e, out);
        if (ok) e->pad = *out;
    }
    unlock(p);
    return ok;
}

int phys_peek(Phys* p, int uid, Pad* out) {
    RegEntry* e;
    int ok = 0;
    lock(p);
    e = reg_by_uid(p->reg, uid);
    if (e) {
        *out = e->pad;
        ok = 1;
    }
    unlock(p);
    return ok;
}

int phys_peek_all(Phys* p, int* uids, Pad* pads, int max) {
    int i, n = 0;
    lock(p);
    for (i = 0; i < REG_MAX && n < max; ++i) {
        RegEntry* e = &p->reg->e[i];
        if (!e->used || e->mirror_of) continue;
        uids[n] = e->uid;
        pads[n] = e->pad;
        ++n;
    }
    unlock(p);
    return n;
}

int phys_xinput_index(Phys* p, int uid) {
    RegEntry* e = reg_by_uid(p->reg, uid);
    if (!e || e->info.kind != KIND_XINPUT) return -1;
    return e->info.xi_index;
}

int phys_owns_device(Phys* p, void* dev) {
    int i;
    for (i = 0; i < REG_MAX; ++i)
        if (p->dis[i].used && p->dis[i].dev == dev) return 1;
    return 0;
}

void phys_shutdown(Phys* p) {
    int i;
    lock(p);
    for (i = 0; i < REG_MAX; ++i)
        if (p->dis[i].used) di_release(&p->dis[i]);
    if (p->di) VT(p->di, DID_Release, Release_fn)(p->di);
    p->di = 0;
    unlock(p);
}
