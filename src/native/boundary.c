#include "boundary.h"

static void normalize(const char* src, char* dst, int cap) {
    int i = 0;
    while (src[i] && i + 1 < cap) {
        char c = src[i];
        if (c == '\\') c = '/';
        dst[i] = c;
        ++i;
    }
    dst[i] = 0;
}

void boundary_init(Boundary* b, const char* folder) {
    memset(b, 0, sizeof(*b));
    normalize(folder, b->folder, (int)sizeof(b->folder));
    b->confirm_ms = BOUNDARY_DEFAULT_CONFIRM_MS;
}

void boundary_add_select(Boundary* b, const char* pattern) {
    if (b->select_count >= BOUNDARY_MAX_PATTERNS || !pattern[0]) return;
    normalize(pattern, b->select[b->select_count], (int)sizeof(b->select[0]));
    b->select_count++;
}

int boundary_classify(const Boundary* b, const wch* path) {
    char ascii[300];
    int i;
    if (!path || !b->folder[0]) return BGM_NONE;
    wstr_to_ascii(path, ascii, (int)sizeof(ascii));
    for (i = 0; ascii[i]; ++i)
        if (ascii[i] == '\\') ascii[i] = '/';
    if (cstr_find_ci(ascii, b->folder) < 0) return BGM_NONE;
    for (i = 0; i < b->select_count; ++i)
        if (cstr_find_ci(ascii, b->select[i]) >= 0) return BGM_SELECT;
    return BGM_OTHER;
}

void boundary_match_started(Boundary* b) {
    b->other_seen = 0;
    b->pending = 0;
    b->select_hold = 0;
    b->select_hold_at = 0;
    b->cancelled = 0;
}

int boundary_feed(Boundary* b, int kind, u64 now) {
    b->cancelled = 0;
    if (kind == BGM_OTHER) {
        b->other_seen = 1;
        if (b->select_hold) {
            b->select_hold = 0;
            b->select_hold_at = 0;
            b->cancelled = 1;
            return 1;
        }
    } else if (kind == BGM_SELECT && b->other_seen) {
        b->other_seen = 0;
        if (b->confirm_ms <= 0) {
            b->pending = 1;
            b->select_hold = 0;
            b->select_hold_at = 0;
        } else {
            b->select_hold = 1;
            b->select_hold_at = now ? now : 1;
            b->pending = 0;
        }
    }
    return 0;
}

void boundary_poll(Boundary* b, u64 now) {
    if (!b->select_hold || b->confirm_ms <= 0) return;
    if (!now) now = b->select_hold_at + (u64)b->confirm_ms;
    if (now - b->select_hold_at >= (u64)b->confirm_ms) {
        b->select_hold = 0;
        b->select_hold_at = 0;
        b->pending = 1;
    }
}

int boundary_take(Boundary* b) {
    int p = b->pending;
    b->pending = 0;
    return p;
}
