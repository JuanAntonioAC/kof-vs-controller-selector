#include "iat.h"

static u8* nt_headers(HMODULE module) {
    u8* base = (u8*)module;
    int lfanew;
    if (!base || *(u16*)base != 0x5A4D) return 0;
    lfanew = *(int*)(base + 0x3C);
    if (*(u32*)(base + lfanew) != 0x4550) return 0;
    return base + lfanew;
}

int pe_identity(HMODULE module, u32* timestamp, u32* size_of_image) {
    u8* nt = nt_headers(module);
    if (!nt) return 0;
    *timestamp = *(u32*)(nt + 8);
    *size_of_image = *(u32*)(nt + 24 + 56);
    return 1;
}

int iat_patch(HMODULE module, const char* dll, const char* name, int ordinal, void* detour, void** original) {
    u8* base = (u8*)module;
    u8* nt = nt_headers(module);
    u32 import_rva;
    u8* desc;
    int patched = 0;
    if (!nt) return 0;
    import_rva = *(u32*)(nt + 24 + 120);
    if (!import_rva) return 0;
    for (desc = base + import_rva; *(u32*)(desc + 12); desc += 20) {
        u32 orig_rva = *(u32*)(desc + 0);
        u32 thunk_rva = *(u32*)(desc + 16);
        const char* dll_name = (const char*)(base + *(u32*)(desc + 12));
        u64* orig;
        void** thunk;
        if (dll && !cstr_ieq(dll_name, dll)) continue;
        if (!orig_rva) orig_rva = thunk_rva;
        orig = (u64*)(base + orig_rva);
        thunk = (void**)(base + thunk_rva);
        for (; *orig; ++orig, ++thunk) {
            int match;
            if (*orig & 0x8000000000000000ull) match = name == 0 && (int)(*orig & 0xFFFF) == ordinal;
            else match = name != 0 && cstr_eq((const char*)(base + (u32)*orig + 2), name);
            if (!match) continue;
            if (vtable_patch(thunk, 0, detour, original)) patched = 1;
        }
    }
    return patched;
}

int vtable_patch(void** vtable, int index, void* detour, void** original) {
    DWORD old = 0;
    void** slot = &vtable[index];
    if (*slot == detour) return 1;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return 0;
    if (original && !*original) *original = *slot;
    *slot = detour;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return 1;
}
