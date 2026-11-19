#ifndef KOFVS_PAD_H
#define KOFVS_PAD_H

#include "util.h"

typedef struct Pad {
    u16 buttons;
    u8 lt;
    u8 rt;
    i16 lx;
    i16 ly;
    i16 rx;
    i16 ry;
} Pad;

#define XB_UP 0x0001u
#define XB_DOWN 0x0002u
#define XB_LEFT 0x0004u
#define XB_RIGHT 0x0008u
#define XB_START 0x0010u
#define XB_BACK 0x0020u
#define XB_LTHUMB 0x0040u
#define XB_RTHUMB 0x0080u
#define XB_LB 0x0100u
#define XB_RB 0x0200u
#define XB_A 0x1000u
#define XB_B 0x2000u
#define XB_X 0x4000u
#define XB_Y 0x8000u

#define PHYS_LT 0x10000
#define PHYS_RT 0x20000

unsigned pad_nav(const Pad* p);
int pad_active(const Pad* p);

#endif
