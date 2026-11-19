#ifndef KOFVS_UIMODEL_H
#define KOFVS_UIMODEL_H

#include "selector.h"
#include "registry.h"
#include "ui.h"

void uimodel_build(UiModel* m, const Selector* s, Registry* r, const char* subtitle, const char* footer);
void sel_devices_from_registry(Registry* r, SelDevice* out, int* count);

#endif
