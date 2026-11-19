#ifndef KOFVS_UI_H
#define KOFVS_UI_H

#include "util.h"

#define UI_MAX_DEVICES 16

typedef struct UiDevice {
    int number;
    int side;
    int blocked;
    int layout_id;
    int layout_ready;
    char label[56];
} UiDevice;

typedef struct UiModel {
    char subtitle[64];
    int state;
    int local_humans; /* 1 = one panel, 2 = two panels */
    int side_state[2];
    int side_number[2];
    int side_layout[2];
    int side_cal_step[2];
    unsigned side_held[2];
    int side_act[2];
    char side_label[2][56];
    UiDevice devs[UI_MAX_DEVICES];
    int dev_count;
    char message[96];
    char footer[96];
    char legend[96];
    int layout_first;
} UiModel;

typedef struct UiRect {
    int x1, y1, x2, y2;
    u32 color;
} UiRect;

int ui_build(const UiModel* m, int width, int height, UiRect* out, int max);
int ui_text_rects(const char* text, int x, int y, int px, u32 color, UiRect* out, int max);

/* Snapshot published by Kof2002Bgm.dll (KofBgm_MenuView). Same layout on both sides. */
#define BGM_MENU_MAGIC 0x314E554Du
#define BGM_MENU_ROWS 14

typedef struct BgmMenuView {
    int magic;
    int visible;
    char title[48];
    char crumb[72];
    char status[80];
    char hint[80];
    int page;
    int pages;
    int row_count;
    int selected;
    char row[BGM_MENU_ROWS][56];
    int mark[BGM_MENU_ROWS];
} BgmMenuView;

int ui_music_build(const BgmMenuView* v, int width, int height, UiRect* out, int max);

#endif
