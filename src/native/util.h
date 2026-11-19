#ifndef KOFVS_UTIL_H
#define KOFVS_UTIL_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed short i16;
typedef int i32;
typedef long long i64;
typedef unsigned short wch;

void* memcpy(void* dst, const void* src, unsigned long long n);
void* memset(void* dst, int value, unsigned long long n);

typedef struct Str {
    char* buf;
    int cap;
    int len;
} Str;

void str_init(Str* s, char* buf, int cap);
void str_add(Str* s, const char* text);
void str_addn(Str* s, const char* text, int n);
void str_ch(Str* s, char c);
void str_int(Str* s, long long value);
void str_hex(Str* s, unsigned long long value, int digits);

int cstr_len(const char* s);
void cstr_copy(char* dst, int cap, const char* src);
int cstr_eq(const char* a, const char* b);
int cstr_ieq(const char* a, const char* b);
int cstr_find_ci(const char* hay, const char* needle);
int wstr_len(const wch* s);
void wstr_copy(wch* dst, int cap, const wch* src);
void wstr_cat(wch* dst, int cap, const wch* src);
int wstr_find_ci(const wch* hay, const char* needle);
void wstr_to_ascii(const wch* src, char* dst, int cap);
void ascii_to_wstr(const char* src, wch* dst, int cap);
int parse_int(const char* s, int* out);

#endif
