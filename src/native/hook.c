#include "hook.h"

#define MEM_RESERVE_ 0x2000u
#define PAGE_EXECUTE_READ_ 0x20u
#define TH32CS_SNAPTHREAD_ 4u
#define THREAD_SUSPEND_RESUME_ 0x0002u
#define THREAD_GET_CONTEXT_ 0x0008u
#define THREAD_QUERY_INFORMATION_ 0x0040u
#define CONTEXT_CONTROL_AMD64_ 0x00100001u
#define CONTEXT_SIZE_ 0x4D0
#define CONTEXT_FLAGS_OFS_ 0x30
#define CONTEXT_RIP_OFS_ 0xF8

typedef struct THREADENTRY32_ {
    DWORD dwSize;
    DWORD cntUsage;
    DWORD th32ThreadID;
    DWORD th32OwnerProcessID;
    LONG tpBasePri;
    LONG tpDeltaPri;
    DWORD dwFlags;
} THREADENTRY32_;

__declspec(dllimport) void* WINAPI VirtualAlloc(void* addr, u64 size, DWORD type, DWORD protect);
__declspec(dllimport) int WINAPI FlushInstructionCache(HANDLE process, const void* addr, u64 size);
__declspec(dllimport) HANDLE WINAPI CreateToolhelp32Snapshot(DWORD flags, DWORD pid);
__declspec(dllimport) int WINAPI Thread32First(HANDLE snap, THREADENTRY32_* te);
__declspec(dllimport) int WINAPI Thread32Next(HANDLE snap, THREADENTRY32_* te);
__declspec(dllimport) HANDLE WINAPI OpenThread(DWORD access, int inherit, DWORD tid);
__declspec(dllimport) DWORD WINAPI SuspendThread(HANDLE thread);
__declspec(dllimport) DWORD WINAPI ResumeThread(HANDLE thread);
__declspec(dllimport) int WINAPI GetThreadContext(HANDLE thread, void* ctx);

#define CURRENT_PROCESS_ ((HANDLE)(long long)-1)
#define MEM_FREE_ 0x10000u
#define MAX_THREADS_ 512

int hook_readable(const void* addr, u64 size) {
    MEMORY_BASIC_INFORMATION_ mi;
    if (!addr || VirtualQuery(addr, &mi, sizeof(mi)) == 0) return 0;
    if (mi.State != MEM_COMMIT_ || mi.Protect == 0 || (mi.Protect & (PAGE_NOACCESS_ | PAGE_GUARD_))) return 0;
    return (const u8*)addr + size <= (const u8*)mi.BaseAddress + mi.RegionSize;
}

static void err_bytes(Str* s, const u8* code, int at) {
    int i;
    str_add(s, " at +");
    str_int(s, at);
    str_add(s, " bytes:");
    for (i = 0; i < 16; ++i) {
        str_ch(s, ' ');
        str_hex(s, code[i], 2);
    }
}

static int modrm_len(const u8* p, int* rip_rel) {
    u8 m = p[0];
    int mod = m >> 6, rm = m & 7, n = 1;
    *rip_rel = 0;
    if (mod == 3) return 1;
    if (rm == 4) {
        u8 sib = p[1];
        ++n;
        if (mod == 0 && (sib & 7) == 5) n += 4;
    } else if (mod == 0 && rm == 5) {
        *rip_rel = 1;
        return n + 4;
    }
    if (mod == 1) n += 1;
    else if (mod == 2) n += 4;
    return n;
}

int hook_prologue_len(const u8* code, int need, char* err, int errcap) {
    Str s;
    int i = 0;
    str_init(&s, err, errcap);
    if (code[0] == 0xE9 || code[0] == 0xEB || (code[0] == 0xFF && code[1] == 0x25) || (code[0] == 0x48 && code[1] == 0xB8)) {
        str_add(&s, "function already starts with a jump (hooked by another module)");
        err_bytes(&s, code, 0);
        return 0;
    }
    while (i < need) {
        int start = i, rip = 0;
        u8 b = code[i];
        if ((b & 0xF0) == 0x40) b = code[++i];
        if (b >= 0x50 && b <= 0x57) {
            i += 1;
        } else if (b == 0x89 || b == 0x8B || b == 0x8D || b == 0x31 || b == 0x33 || b == 0x85 ||
                   b == 0x3B || b == 0x39 || b == 0x2B || b == 0x29 || b == 0x03 || b == 0x01 ||
                   b == 0x23 || b == 0x0B || b == 0x09 || b == 0x21) {
            i += 1 + modrm_len(code + i + 1, &rip);
        } else if (b == 0x83) {
            i += 1 + modrm_len(code + i + 1, &rip) + 1;
        } else if (b == 0x81 || b == 0xC7) {
            i += 1 + modrm_len(code + i + 1, &rip) + 4;
        } else {
            str_add(&s, "unsupported instruction in prologue");
            err_bytes(&s, code, start);
            return 0;
        }
        if (rip) {
            str_add(&s, "RIP-relative instruction in prologue");
            err_bytes(&s, code, start);
            return 0;
        }
    }
    return i;
}

int hook_jump_dest(const u8* code, u64* dest) {
    if (code[0] == 0xE9) {
        i32 rel = (i32)((u32)code[1] | ((u32)code[2] << 8) | ((u32)code[3] << 16) | ((u32)code[4] << 24));
        *dest = (u64)(code + 5) + (u64)(i64)rel;
        return 5;
    }
    if (code[0] == 0xFF && code[1] == 0x25) {
        i32 rel = (i32)((u32)code[2] | ((u32)code[3] << 8) | ((u32)code[4] << 16) | ((u32)code[5] << 24));
        const u8* slot = code + 6 + rel;
        if (!hook_readable(slot, 8)) return 0;
        *dest = *(const u64*)slot;
        return 6;
    }
    return 0;
}

static u8* alloc_near(const u8* target) {
    MEMORY_BASIC_INFORMATION_ mi;
    const u64 gran = 0x10000, range = 0x70000000;
    u64 lo = (u64)target > range ? (u64)target - range : gran;
    u64 hi = (u64)target + range;
    u64 a = ((u64)target & ~(gran - 1));
    while (a > lo) {
        if (VirtualQuery((void*)a, &mi, sizeof(mi)) == 0) break;
        if (mi.State == MEM_FREE_) {
            u64 top = ((u64)mi.BaseAddress + mi.RegionSize - gran) & ~(gran - 1);
            if (top >= (u64)mi.BaseAddress && top > lo) {
                void* p = VirtualAlloc((void*)top, 0x1000, MEM_COMMIT_ | MEM_RESERVE_, PAGE_EXECUTE_READWRITE);
                if (p) return (u8*)p;
            }
        }
        if ((u64)mi.BaseAddress < gran) break;
        a = ((u64)mi.BaseAddress - 1) & ~(gran - 1);
    }
    a = ((u64)target + gran) & ~(gran - 1);
    while (a < hi) {
        if (VirtualQuery((void*)a, &mi, sizeof(mi)) == 0) break;
        if (mi.State == MEM_FREE_) {
            u64 base = ((u64)mi.BaseAddress + gran - 1) & ~(gran - 1);
            if (base < (u64)mi.BaseAddress + mi.RegionSize) {
                void* p = VirtualAlloc((void*)base, 0x1000, MEM_COMMIT_ | MEM_RESERVE_, PAGE_EXECUTE_READWRITE);
                if (p) return (u8*)p;
            }
        }
        a = ((u64)mi.BaseAddress + mi.RegionSize + gran - 1) & ~(gran - 1);
    }
    return 0;
}

static void write_abs_jmp(u8* at, const void* dest) {
    u64 d = (u64)dest;
    int k;
    at[0] = 0xFF;
    at[1] = 0x25;
    at[2] = at[3] = at[4] = at[5] = 0;
    for (k = 0; k < 8; ++k) at[6 + k] = (u8)(d >> (8 * k));
}

static int suspend_others(HANDLE* list, int max) {
    THREADENTRY32_ te;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD_, 0);
    DWORD pid = GetCurrentProcessId(), self = GetCurrentThreadId();
    int n = 0;
    if (snap == INVALID_HANDLE) return -1;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid && te.th32ThreadID != self && n < max) {
                HANDLE t = OpenThread(THREAD_SUSPEND_RESUME_ | THREAD_GET_CONTEXT_ | THREAD_QUERY_INFORMATION_, 0, te.th32ThreadID);
                if (t) {
                    if (SuspendThread(t) != (DWORD)-1) list[n++] = t;
                    else CloseHandle(t);
                }
            }
            te.dwSize = sizeof(te);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return n;
}

static void resume_all(HANDLE* list, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        ResumeThread(list[i]);
        CloseHandle(list[i]);
    }
}

static int any_thread_inside(HANDLE* list, int n, const u8* lo, int len) {
    static __declspec(align(16)) u8 ctx[CONTEXT_SIZE_];
    int i;
    for (i = 0; i < n; ++i) {
        u64 rip;
        memset(ctx, 0, sizeof(ctx));
        *(DWORD*)(ctx + CONTEXT_FLAGS_OFS_) = CONTEXT_CONTROL_AMD64_;
        if (!GetThreadContext(list[i], ctx)) continue;
        rip = *(u64*)(ctx + CONTEXT_RIP_OFS_);
        if (rip > (u64)lo && rip < (u64)lo + (u64)len) return 1;
    }
    return 0;
}

static int patch_code(u8* target, const u8* bytes, int len, char* err, int errcap) {
    static HANDLE threads[MAX_THREADS_];
    Str s;
    int attempt;
    str_init(&s, err, errcap);
    for (attempt = 0; attempt < 50; ++attempt) {
        DWORD old = 0;
        int n = suspend_others(threads, MAX_THREADS_);
        if (n < 0) {
            str_add(&s, "CreateToolhelp32Snapshot failed");
            return 0;
        }
        if (any_thread_inside(threads, n, target, len)) {
            resume_all(threads, n);
            Sleep(1);
            continue;
        }
        if (!VirtualProtect(target, (u64)len, PAGE_EXECUTE_READWRITE, &old)) {
            resume_all(threads, n);
            str_add(&s, "VirtualProtect failed");
            return 0;
        }
        memcpy(target, bytes, (u64)len);
        VirtualProtect(target, (u64)len, old, &old);
        FlushInstructionCache(CURRENT_PROCESS_, target, (u64)len);
        resume_all(threads, n);
        return 1;
    }
    str_add(&s, "a thread stayed inside the patched bytes");
    return 0;
}

int hook_install(InlineHook* h, void* target, void* detour, void** original, char* err, int errcap) {
    u8 patch[32];
    u8* t = (u8*)target;
    int len, k;
    u64 prev = 0;
    memset(h, 0, sizeof(*h));
    if (hook_jump_dest(t, &prev)) {
        u8* relay = alloc_near(t);
        i64 rel;
        if (!relay) {
            Str s;
            str_init(&s, err, errcap);
            str_add(&s, "no free memory within +-1.75 GB for the relay jump");
            return 0;
        }
        write_abs_jmp(relay, detour);
        write_abs_jmp(relay + 16, (void*)prev);
        FlushInstructionCache(CURRENT_PROCESS_, relay, 32);
        rel = (i64)(relay - (t + 5));
        patch[0] = 0xE9;
        for (k = 0; k < 4; ++k) patch[1 + k] = (u8)((u64)rel >> (8 * k));
        memcpy(h->saved, t, 5);
        h->stolen = 5;
        h->target = t;
        h->trampoline = relay + 16;
        h->chained = 1;
        *original = h->trampoline;
        if (!patch_code(t, patch, 5, err, errcap)) {
            h->target = 0;
            return 0;
        }
        return 1;
    }
    len = hook_prologue_len(t, HOOK_PATCH_LEN, err, errcap);
    if (len <= 0) return 0;
    if (len > 24) {
        Str s;
        str_init(&s, err, errcap);
        str_add(&s, "prologue too long");
        return 0;
    }
    h->trampoline = (u8*)VirtualAlloc(0, 64, MEM_COMMIT_ | MEM_RESERVE_, PAGE_EXECUTE_READWRITE);
    if (!h->trampoline) {
        Str s;
        str_init(&s, err, errcap);
        str_add(&s, "VirtualAlloc for trampoline failed");
        return 0;
    }
    memcpy(h->trampoline, t, (u64)len);
    write_abs_jmp(h->trampoline + len, t + len);
    FlushInstructionCache(CURRENT_PROCESS_, h->trampoline, 64);
    memcpy(h->saved, t, (u64)len);
    h->stolen = len;
    h->target = t;
    write_abs_jmp(patch, detour);
    for (k = HOOK_PATCH_LEN; k < len; ++k) patch[k] = 0x90;
    *original = h->trampoline;
    if (!patch_code(t, patch, len, err, errcap)) {
        h->target = 0;
        return 0;
    }
    return 1;
}

int hook_remove(InlineHook* h) {
    char err[64];
    int ok;
    if (!h->target) return 0;
    ok = patch_code(h->target, h->saved, h->stolen, err, (int)sizeof(err));
    h->target = 0;
    return ok;
}
