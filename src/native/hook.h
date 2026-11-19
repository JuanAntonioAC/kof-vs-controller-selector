#ifndef KOFVS_HOOK_H
#define KOFVS_HOOK_H

#include "win.h"

#define HOOK_PATCH_LEN 14

typedef struct InlineHook {
    u8* target;
    u8* trampoline;
    int stolen;
    int chained;
    u8 saved[32];
} InlineHook;

int hook_readable(const void* addr, u64 size);
int hook_jump_dest(const u8* code, u64* dest);
int hook_prologue_len(const u8* code, int need, char* err, int errcap);
int hook_install(InlineHook* h, void* target, void* detour, void** original, char* err, int errcap);
int hook_remove(InlineHook* h);

#endif
