#include "ui.h"
#include "selector.h"
#include "layout.h"

typedef struct Glyph {
    char c;
    u8 rows[7];
} Glyph;

static const Glyph g_font[] = {
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
    { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
    { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { ':', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 } },
    { '/', { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
    { ',', { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 } },
    { '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } },
    { ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
    { '[', { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E } },
    { ']', { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E } },
    { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
    { '=', { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 } },
    { '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },
    { '?', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 } },
    { '\'', { 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 } },
    { '>', { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 } },
    { '<', { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 } },
    { '*', { 0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00 } },
    { '#', { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A } },
    { '_', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F } },
    { '&', { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D } },
};

#define COL_BG 0xFF0C0C16u
#define COL_PANEL 0xFF1C1C2Au
#define COL_TEXT 0xFFF0F0F0u
#define COL_DIM 0xFF9090A0u
#define COL_P1 0xFFFF4848u
#define COL_P2 0xFF48A8FFu
#define COL_AVAILABLE 0xFF707084u
#define COL_SELECTED 0xFFF0C000u
#define COL_READY 0xFF20D050u
#define COL_LOCKED 0xFF3C8CFFu
#define COL_BLOCKED 0xFFD04040u

static const u8* glyph_rows(char c) {
    int i;
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (i = 0; i < (int)(sizeof(g_font) / sizeof(g_font[0])); ++i)
        if (g_font[i].c == c) return g_font[i].rows;
    if (c == ' ') return 0;
    return glyph_rows('?');
}

typedef struct Canvas {
    UiRect* out;
    int max;
    int count;
    int ox, oy;
    int k;
} Canvas;

static int sx(const Canvas* cv, int v) { return cv->ox + (v * cv->k) / 256; }
static int sy(const Canvas* cv, int v) { return cv->oy + (v * cv->k) / 256; }

static void push_px(Canvas* cv, int x1, int y1, int x2, int y2, u32 color) {
    UiRect* r;
    if (cv->count >= cv->max || x2 <= x1 || y2 <= y1) return;
    r = &cv->out[cv->count++];
    r->x1 = x1;
    r->y1 = y1;
    r->x2 = x2;
    r->y2 = y2;
    r->color = color;
}

static void box(Canvas* cv, int x, int y, int w, int h, u32 color) {
    push_px(cv, sx(cv, x), sy(cv, y), sx(cv, x + w), sy(cv, y + h), color);
}

static void frame(Canvas* cv, int x, int y, int w, int h, int t, u32 color) {
    box(cv, x, y, w, t, color);
    box(cv, x, y + h - t, w, t, color);
    box(cv, x, y, t, h, color);
    box(cv, x + w - t, y, t, h, color);
}

static int text_width(const char* text, int px) {
    return cstr_len(text) * 6 * px - px;
}

static void text(Canvas* cv, const char* str, int x, int y, int px, u32 color) {
    int i, row, col;
    for (i = 0; str[i]; ++i) {
        const u8* rows = glyph_rows(str[i]);
        int gx = x + i * 6 * px;
        if (!rows) continue;
        for (row = 0; row < 7; ++row) {
            u8 bits = rows[row];
            col = 0;
            while (col < 5) {
                int start;
                if (!(bits & (0x10 >> col))) {
                    ++col;
                    continue;
                }
                start = col;
                while (col < 5 && (bits & (0x10 >> col))) ++col;
                push_px(cv, sx(cv, gx + start * px), sy(cv, y + row * px), sx(cv, gx + col * px), sy(cv, y + (row + 1) * px), color);
            }
        }
    }
}

static void text_center(Canvas* cv, const char* str, int cx, int y, int px, u32 color) {
    text(cv, str, cx - text_width(str, px) / 2, y, px, color);
}

int ui_text_rects(const char* str, int x, int y, int px, u32 color, UiRect* out, int max) {
    Canvas cv;
    cv.out = out;
    cv.max = max;
    cv.count = 0;
    cv.ox = 0;
    cv.oy = 0;
    cv.k = 256;
    text(&cv, str, x, y, px, color);
    return cv.count;
}

static u32 side_color(int st) {
    switch (st) {
    case SIDE_SELECTED: return COL_SELECTED;
    case SIDE_READY: return COL_READY;
    case SIDE_LOCKED: return COL_LOCKED;
    default: return COL_AVAILABLE;
    }
}

static const char* const g_cal_pos_name[8] = {
    "TOP-LEFT", "TOP-2", "TOP-3", "TOP-RIGHT", "BOT-LEFT", "BOT-2", "BOT-3", "BOT-RIGHT"
};

static void stick_grid(Canvas* cv, int layout, unsigned held, int act, int cal_step, int x, int y) {
    int row, col;
    for (row = 0; row < 2; ++row) {
        for (col = 0; col < 4; ++col) {
            int pos = row * 4 + col;
            int cx = x + col * 46;
            int cy = y + row * 26;
            char ch = layout_cell_char(layout, pos);
            int on = (held >> pos) & 1;
            int wait = cal_step == pos + 1;
            int hit_act = on && layout_cell_act(layout, pos) == act && act;
            u32 bg = wait ? COL_SELECTED : (hit_act ? COL_READY : (on ? COL_LOCKED : COL_BG));
            u32 fg = (wait || hit_act || on) ? COL_BG : (ch == '-' ? COL_AVAILABLE : COL_TEXT);
            char t[2];
            box(cv, cx, cy, 42, 22, bg);
            frame(cv, cx, cy, 42, 22, on || wait ? 2 : 1, wait ? COL_SELECTED : (hit_act ? COL_READY : COL_AVAILABLE));
            t[0] = ch;
            t[1] = 0;
            text_center(cv, t, cx + 21, cy + 6, 2, fg);
        }
    }
}

static void panel(Canvas* cv, const UiModel* m, int side, int x) {
    int st = m->side_state[side];
    int layout_screen = m->state == S_CONFIRMING || m->state == S_LOCKED;
    u32 sc = side_color(st);
    int cx = x + 130;
    char buf[64];
    Str s;
    box(cv, x, 56, 260, 196, COL_PANEL);
    frame(cv, x, 56, 260, 196, st == SIDE_AVAILABLE ? 2 : 4, sc);
    text_center(cv, side == 0 ? "PLAYER 1" : "PLAYER 2", cx, 62, 3, side == 0 ? COL_P1 : COL_P2);
    box(cv, x + 20, 86, 220, 18, st == SIDE_AVAILABLE ? COL_BG : sc);
    text_center(cv, sel_side_name(st), cx, 90, 1, st == SIDE_AVAILABLE ? COL_AVAILABLE : COL_BG);
    if (m->side_number[side] > 0) {
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "CONTROL ");
        str_int(&s, m->side_number[side]);
        text_center(cv, buf, cx, 108, 2, COL_TEXT);
        cstr_copy(buf, 41, m->side_label[side]);
        text_center(cv, buf, cx, 126, 1, COL_DIM);
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, st == SIDE_SELECTED ? "< " : "");
        str_add(&s, layout_name(m->side_layout[side]));
        if (st == SIDE_SELECTED) str_add(&s, " >");
        text_center(cv, buf, cx, 138, 2, st == SIDE_SELECTED ? COL_SELECTED : COL_TEXT);
        if (layout_screen || st != SIDE_AVAILABLE)
            stick_grid(cv, m->side_layout[side], m->side_held[side], m->side_act[side], m->side_cal_step[side], x + 38, 154);
    } else {
        text_center(cv, "- - -", cx, 126, 2, COL_DIM);
    }
    if (m->side_cal_step[side] > 0) {
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "CAL ");
        str_int(&s, m->side_cal_step[side]);
        str_add(&s, "/8 PRESS ");
        str_add(&s, g_cal_pos_name[m->side_cal_step[side] - 1]);
        text_center(cv, buf, cx, 210, 1, COL_SELECTED);
        text_center(cv, "SELECT = CANCEL CALIBRATION", cx, 230, 1, COL_DIM);
    } else if (st == SIDE_AVAILABLE)
        text_center(cv, m->local_humans <= 1
            ? "PRESS ANY BUTTON"
            : (m->layout_first
                ? (side == 0 ? "IZQUIERDA PARA P1" : "DERECHA PARA P2")
                : (side == 0 ? "PRESS LEFT TO TAKE P1" : "PRESS RIGHT TO TAKE P2")), cx, 230, 1, COL_TEXT);
    else if (st == SIDE_SELECTED)
        text_center(cv, m->layout_first ? "A=OK  B=SOLTAR" : "UP/DOWN LAYOUT  A=OK  B=BACK  PRESS TO TEST", cx, 230, 1, COL_SELECTED);
    else if (st == SIDE_READY)
        text_center(cv, m->local_humans <= 1 ? "READY" : "WAITING FOR RIVAL  -  B = CANCEL", cx, 230, 1, COL_READY);
    else
        text_center(cv, "[LOCKED]", cx, 230, 1, COL_LOCKED);
}

static int draw_roster(const UiModel* m, Canvas* cv, int y0, int row_h, int show_layout) {
    int i, rows = m->dev_count < 8 ? m->dev_count : 8;
    for (i = 0; i < rows; ++i) {
        const UiDevice* d = &m->devs[i];
        int y = y0 + i * row_h;
        char buf[96];
        Str s;
        const char* tag;
        u32 tag_color;
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "CONTROL ");
        str_int(&s, d->number);
        text(cv, buf, 40, y, 2, COL_TEXT);
        text(cv, d->label, 150, y, 1, COL_DIM);
        if (show_layout && d->layout_id >= 0)
            text(cv, layout_name(d->layout_id), 330, y, 2, d->layout_ready ? COL_READY : COL_SELECTED);
        if (d->side == 0) {
            tag = "P1";
            tag_color = COL_P1;
        } else if (d->side == 1) {
            tag = "P2";
            tag_color = COL_P2;
        } else if (d->layout_ready && m->state == S_LAYOUTS) {
            tag = "LISTO";
            tag_color = COL_READY;
        } else if (d->blocked) {
            tag = "BLOCKED";
            tag_color = COL_BLOCKED;
        } else {
            tag = "FREE";
            tag_color = COL_AVAILABLE;
        }
        text(cv, tag, 520, y, 2, tag_color);
    }
    return rows;
}

static int draw_layout_or_roster(const UiModel* m, Canvas* cv) {
    char buf[96];
    Str s;
    int y;
    int layouts = m->state == S_LAYOUTS;
    text_center(cv, layouts ? "SELECT YOUR BUTTON LAYOUT" : "SELECT YOUR CONTROLLER", 320, 8, 3, COL_TEXT);
    text_center(cv, m->subtitle, 320, 34, 1, COL_DIM);
    if (!layouts) {
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "P1 ");
        if (m->side_number[0] > 0) {
            str_add(&s, "CONTROL ");
            str_int(&s, m->side_number[0]);
            if (m->side_layout[0] >= 0) {
                str_add(&s, " ");
                str_add(&s, layout_name(m->side_layout[0]));
            }
        } else str_add(&s, "- - -");
        text(cv, buf, 40, 50, 2, COL_P1);
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "P2 ");
        if (m->side_number[1] > 0) {
            str_add(&s, "CONTROL ");
            str_int(&s, m->side_number[1]);
            if (m->side_layout[1] >= 0) {
                str_add(&s, " ");
                str_add(&s, layout_name(m->side_layout[1]));
            }
        } else str_add(&s, "- - -");
        text(cv, buf, 340, 50, 2, COL_P2);
        y = 78;
    } else {
        text_center(cv, "ARRIBA/ABAJO CAMBIA   A GUARDA   START ELIGE P1/P2", 320, 50, 1, COL_SELECTED);
        y = 70;
    }
    draw_roster(m, cv, y, 28, 1);
    if (m->legend[0]) text_center(cv, m->legend, 320, 308, 1, COL_DIM);
    text_center(cv, m->message, 320, 324, 1, COL_SELECTED);
    text_center(cv, m->footer, 320, 342, 1, COL_DIM);
    return cv->count;
}

int ui_build(const UiModel* m, int width, int height, UiRect* out, int max) {
    Canvas cv;
    int kx, ky, i, rows, max_rows;
    int layout_screen = m->state == S_CONFIRMING || m->state == S_LOCKED;
    char buf[96];
    Str s;
    cv.out = out;
    cv.max = max;
    cv.count = 0;
    kx = width * 256 / 640;
    ky = height * 256 / 360;
    cv.k = kx < ky ? kx : ky;
    if (cv.k < 64) cv.k = 64;
    cv.ox = (width - 640 * cv.k / 256) / 2;
    cv.oy = (height - 360 * cv.k / 256) / 2;

    push_px(&cv, 0, 0, width, height, COL_BG);
    if (m->state == S_LAYOUTS)
        return draw_layout_or_roster(m, &cv);
    text_center(&cv, m->state == S_LOCKED ? "LOCKED"
        : (layout_screen ? "SELECT YOUR BUTTON LAYOUT"
            : (m->local_humans <= 1 ? "SELECT YOUR CONTROL" : "SELECT YOUR CONTROLS")), 320, 10, 3, COL_TEXT);
    text_center(&cv, m->subtitle, 320, 38, 1, COL_DIM);
    box(&cv, 40, 50, 560, 2, COL_AVAILABLE);
    if (m->local_humans <= 1) {
        panel(&cv, m, 0, 190);
    } else {
        panel(&cv, m, 0, 40);
        panel(&cv, m, 1, 340);
    }

    str_init(&s, buf, (int)sizeof(buf));
    str_add(&s, "CONTROLS DETECTED: ");
    str_int(&s, m->dev_count);
    text(&cv, buf, 40, 258, 1, COL_TEXT);
    max_rows = layout_screen ? 4 : 6;
    rows = m->dev_count < max_rows ? m->dev_count : max_rows;
    for (i = 0; i < rows; ++i) {
        const UiDevice* d = &m->devs[i];
        int y = 270 + i * 11;
        u32 tag_color;
        const char* tag;
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "CONTROL ");
        str_int(&s, d->number);
        text(&cv, buf, 40, y, 1, COL_TEXT);
        text(&cv, d->label, 120, y, 1, COL_DIM);
        if (d->side == 0) {
            tag = "P1";
            tag_color = COL_P1;
        } else if (d->side == 1) {
            tag = "P2";
            tag_color = COL_P2;
        } else if (d->blocked) {
            tag = "BLOCKED";
            tag_color = COL_BLOCKED;
        } else {
            tag = "FREE";
            tag_color = COL_AVAILABLE;
        }
        text(&cv, tag, 560, y, 1, tag_color);
    }
    if (m->dev_count > max_rows) {
        str_init(&s, buf, (int)sizeof(buf));
        str_add(&s, "+");
        str_int(&s, m->dev_count - max_rows);
        str_add(&s, " MORE");
        text(&cv, buf, 40, 270 + max_rows * 11, 1, COL_DIM);
    }
    if (m->legend[0]) text_center(&cv, m->legend, 320, 322, 1, COL_DIM);
    text_center(&cv, m->message, 320, 336, 1, COL_SELECTED);
    text_center(&cv, m->footer, 320, 348, 1, COL_DIM);
    return cv.count;
}

int ui_music_build(const BgmMenuView* v, int width, int height, UiRect* out, int max) {
    Canvas cv;
    int kx, ky, i, y;
    char page[32];
    if (!v || v->magic != BGM_MENU_MAGIC || !v->visible) return 0;
    cv.out = out;
    cv.max = max;
    cv.count = 0;
    kx = width * 256 / 640;
    ky = height * 256 / 360;
    cv.k = kx < ky ? kx : ky;
    if (cv.k < 64) cv.k = 64;
    cv.ox = (width - 640 * cv.k / 256) / 2;
    cv.oy = (height - 360 * cv.k / 256) / 2;
    box(&cv, 70, 4, 500, 352, COL_BG);
    frame(&cv, 70, 4, 500, 352, 2, COL_SELECTED);
    text_center(&cv, v->title[0] ? v->title : "SELECT MUSIC", 320, 12, 3, COL_SELECTED);
    text_center(&cv, v->crumb, 320, 40, 1, COL_DIM);
    y = 56;
    for (i = 0; i < v->row_count && i < BGM_MENU_ROWS; ++i) {
        char line[64];
        u32 col = COL_TEXT;
        line[0] = 0;
        if (i == v->selected) {
            box(&cv, 86, y - 1, 468, 16, 0xFF782418u);
            col = 0xFFBEECFF;
        }
        if (v->mark[i] == 2) {
            line[0] = '<';
            line[1] = ' ';
            line[2] = 0;
            col = i == v->selected ? col : COL_DIM;
        } else if (v->mark[i] == 1) {
            line[0] = '>';
            line[1] = ' ';
            line[2] = 0;
            col = i == v->selected ? col : COL_P2;
        }
        {
            int n = 0;
            while (line[n]) ++n;
            {
                int k = 0;
                while (v->row[i][k] && n < 60) line[n++] = v->row[i][k++];
                line[n] = 0;
            }
        }
        text(&cv, line, 96, y, 2, col);
        y += 16;
    }
    {
        int p = v->page, q = v->pages, n = 0;
        if (p < 0) p = 0;
        if (q < 1) q = 1;
        if (p > 99) p = 99;
        if (q > 99) q = 99;
        page[n++] = 'P';
        page[n++] = ' ';
        page[n++] = (char)('0' + (p / 10) % 10);
        page[n++] = (char)('0' + p % 10);
        page[n++] = '/';
        page[n++] = (char)('0' + (q / 10) % 10);
        page[n++] = (char)('0' + q % 10);
        page[n] = 0;
    }
    text_center(&cv, page, 320, 292, 1, COL_DIM);
    text_center(&cv, v->status, 320, 312, 1, COL_SELECTED);
    text_center(&cv, v->hint, 320, 332, 1, COL_DIM);
    return cv.count;
}
