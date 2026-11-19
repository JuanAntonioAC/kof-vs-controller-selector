#ifndef KOFVS_BOUNDARY_H
#define KOFVS_BOUNDARY_H

#include "util.h"

#define BOUNDARY_MAX_PATTERNS 8
#define BOUNDARY_DEFAULT_CONFIRM_MS 500

enum BgmKind {
    BGM_NONE,
    BGM_SELECT,
    BGM_OTHER
};

typedef struct Boundary {
    char folder[48];
    char select[BOUNDARY_MAX_PATTERNS][40];
    int select_count;
    int other_seen;
    int pending;
    /* SELECT after a fight must hold this long without OTHER replacing it.
       Arcade win→next fight flashes PS_N then stage BGM (~200ms); that must NOT reopen the screen. */
    int select_hold;
    u64 select_hold_at;
    int confirm_ms;
    int cancelled;
} Boundary;

void boundary_init(Boundary* b, const char* folder);
void boundary_add_select(Boundary* b, const char* pattern);
int boundary_classify(const Boundary* b, const wch* path);
void boundary_match_started(Boundary* b);
/* Feed a classified BGM open. now = GetTickCount64(). Returns 1 if a select-hold was cancelled. */
int boundary_feed(Boundary* b, int kind, u64 now);
/* Promote a held SELECT into pending once confirm_ms elapses. */
void boundary_poll(Boundary* b, u64 now);
int boundary_take(Boundary* b);

#endif
