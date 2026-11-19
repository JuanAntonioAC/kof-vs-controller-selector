#ifndef KOFVS_IAT_H
#define KOFVS_IAT_H

#include "win.h"

int iat_patch(HMODULE module, const char* dll, const char* name, int ordinal, void* detour, void** original);
int vtable_patch(void** vtable, int index, void* detour, void** original);
int pe_identity(HMODULE module, u32* timestamp, u32* size_of_image);

#endif
