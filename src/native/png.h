#ifndef KOFVS_PNG_H
#define KOFVS_PNG_H

#include "ui.h"

int png_render_rects(const wch* path, int width, int height, const UiRect* rects, int count);
int png_write_bgrx(const wch* path, int width, int height, const u8* bits, int pitch);

#endif
