#ifndef KOFVS_LAYOUT_H
#define KOFVS_LAYOUT_H

#include "pad.h"

/* ButtonLayoutManager: physical 8-button stick positions
     top    0 SQUARE/X   1 TRIANGLE/Y   2 R1/RB   3 L1/LB
     bottom 4 CROSS/A    5 CIRCLE/B     6 R2/RT   7 L2/LT
   Logical attacks: A=LP  B=LK  C=HP  D=HK. START/SELECT keep their own roles. */
enum ButtonLayout {
    LAYOUT_CLASSIC,
    LAYOUT_SAN_FELIPE,
    LAYOUT_LINEAL,
    LAYOUT_PULGAR,
    LAYOUT_ABCD,
    LAYOUT_MEXICO,
    LAYOUT_COUNT
};

#define LAYOUT_F_L1_SELECT 1u
#define LAYOUT_POS_COUNT 8
#define LAYOUT_ACT_NONE 0
#define LAYOUT_ACT_A 1
#define LAYOUT_ACT_B 2
#define LAYOUT_ACT_C 3
#define LAYOUT_ACT_D 4

#define LAYOUT_DI_DEFAULT_COUNT 12
extern const int g_layout_di_default[LAYOUT_DI_DEFAULT_COUNT];

const char* layout_name(int layout);
const char* layout_slug(int layout);
int layout_parse(const char* v);
int layout_button_code(const char* v);
const char* layout_button_name(int code);
char layout_cell_char(int layout, int pos);
int layout_cell_act(int layout, int pos);
int layout_profiles_ok(void);
void layout_set_game_id(const char* id);
void layout_set_steam_standard(int on);
int layout_fill_cal(const char* key, u16 vid, u16 pid, int* cal);
int layout_steam_observe(const char* key, u16 vid, u16 pid, const Pad* p);
void layout_steam_forget(const char* key);
unsigned layout_fight_mask(const Pad* p);
int layout_pos_code(int pos);
int layout_pos_held(const Pad* p, int pos, const int* cal);
int layout_pos_held_in(const Pad* p, int pos, const int* cal, int layout);
int layout_first_fight_code(const Pad* p);
int layout_action_from_pad(int layout, const Pad* p, const int* cal);
void layout_cal_normalize(Pad* p, const int* cal);
void layout_apply(Pad* p, int layout, unsigned flags);
/* After layout_apply: send A/B/C/D on the buttons the game really reads (map[0..3]). */
void layout_remap_attacks(Pad* p, const u16* map);
/* Steam Deck virtual pad. SVC there reads X=LP, Y=LK, RB=HP, A=HK. */
int layout_is_deck_pad(u16 vid, u16 pid);
void layout_apply_deck(Pad* p, int layout);
void layout_pad_text(const Pad* p, char* out, int cap);
void layout_game_text(const Pad* p, char* out, int cap);
int layout_build_game_table(const void* default_table, unsigned flags);
const void* layout_game_table(int layout);
int layout_table_raw(u32 outbits);
int layout_table_raw_of(int layout, u32 outbits);

void layout_memory_reset(void);
void layout_memory_set_path(const wch* path);
void layout_memory_load(void);
void layout_memory_save(void);
int layout_memory_last(const char* key, u16 vid, u16 pid);
void layout_memory_put(const char* key, u16 vid, u16 pid, int layout);
int layout_memory_cal(const char* key, u16 vid, u16 pid, int* cal);
void layout_memory_put_cal(const char* key, u16 vid, u16 pid, const int* cal);

#endif
