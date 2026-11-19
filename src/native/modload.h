#ifndef KOFVS_MODLOAD_H
#define KOFVS_MODLOAD_H

#include "win.h"

void modload_init(HMODULE self, const char* from);
int modload_selector(void);

#endif
