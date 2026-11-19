#include "util.h"

void str_init(Str* s, char* buf, int cap) {
    s->buf = buf;
    s->cap = cap;
    s->len = 0;
    if (cap > 0) buf[0] = 0;
}

void str_ch(Str* s, char c) {
    if (s->len + 1 >= s->cap) return;
    s->buf[s->len++] = c;
    s->buf[s->len] = 0;
}

void str_add(Str* s, const char* text) {
    while (text && *text) str_ch(s, *text++);
}

void str_addn(Str* s, const char* text, int n) {
    int i;
    for (i = 0; i < n && text[i]; ++i) str_ch(s, text[i]);
}

void str_int(Str* s, long long value) {
    char tmp[24];
    int n = 0;
    unsigned long long v;
    if (value < 0) {
        str_ch(s, '-');
        v = (unsigned long long)(-(value + 1)) + 1;
    } else {
        v = (unsigned long long)value;
    }
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 24);
    while (n) str_ch(s, tmp[--n]);
}

void str_hex(Str* s, unsigned long long value, int digits) {
    static const char hex[] = "0123456789ABCDEF";
    int i;
    for (i = digits - 1; i >= 0; --i) str_ch(s, hex[(value >> (i * 4)) & 15]);
}

int cstr_len(const char* s) {
    int n = 0;
    while (s && s[n]) ++n;
    return n;
}

void cstr_copy(char* dst, int cap, const char* src) {
    int i = 0;
    if (cap <= 0) return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

int cstr_eq(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int lower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int cstr_ieq(const char* a, const char* b) {
    while (*a && lower(*a) == lower(*b)) {
        ++a;
        ++b;
    }
    return lower(*a) == lower(*b);
}

int cstr_find_ci(const char* hay, const char* needle) {
    int i, j;
    if (!hay || !needle) return -1;
    for (i = 0; hay[i]; ++i) {
        for (j = 0; needle[j] && hay[i + j] && lower(hay[i + j]) == lower(needle[j]); ++j) {}
        if (!needle[j]) return i;
    }
    return -1;
}

int wstr_len(const wch* s) {
    int n = 0;
    while (s && s[n]) ++n;
    return n;
}

void wstr_copy(wch* dst, int cap, const wch* src) {
    int i = 0;
    if (cap <= 0) return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

void wstr_cat(wch* dst, int cap, const wch* src) {
    int n = wstr_len(dst);
    wstr_copy(dst + n, cap - n, src);
}

int wstr_find_ci(const wch* hay, const char* needle) {
    int i, j;
    if (!hay || !needle) return -1;
    for (i = 0; hay[i]; ++i) {
        for (j = 0; needle[j] && hay[i + j] && hay[i + j] < 128 && lower((int)hay[i + j]) == lower(needle[j]); ++j) {}
        if (!needle[j]) return i;
    }
    return -1;
}

void wstr_to_ascii(const wch* src, char* dst, int cap) {
    int i = 0;
    if (cap <= 0) return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = (src[i] >= 32 && src[i] < 127) ? (char)src[i] : '?';
        ++i;
    }
    dst[i] = 0;
}

void ascii_to_wstr(const char* src, wch* dst, int cap) {
    int i = 0;
    if (cap <= 0) return;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = (wch)(unsigned char)src[i];
        ++i;
    }
    dst[i] = 0;
}

int parse_int(const char* s, int* out) {
    int sign = 1;
    int value = 0;
    int any = 0;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '-') {
        sign = -1;
        ++s;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        for (;;) {
            int c = *s;
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            value = value * 16 + d;
            any = 1;
            ++s;
        }
    } else {
        while (*s >= '0' && *s <= '9') {
            value = value * 10 + (*s - '0');
            any = 1;
            ++s;
        }
    }
    if (any) *out = value * sign;
    return any;
}
