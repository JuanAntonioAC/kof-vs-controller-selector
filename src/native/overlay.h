#ifndef KOFVS_OVERLAY_H
#define KOFVS_OVERLAY_H

#include "win.h"
#include "ui.h"

typedef void (*OverlayFrameFn)(void* device, int width, int height);

int overlay_find_present(HWND hwnd, void** out_present, char* err, int errcap);
void* overlay_device_present(void* device);
int overlay_install(void* present_fn, OverlayFrameFn on_frame, char* err, int errcap);
int overlay_install_vtable(OverlayFrameFn on_frame, char* err, int errcap);
void overlay_uninstall(void);
int overlay_draw(void* device, const UiRect* rects, int count);
int overlay_backbuffer_size(void* device, int* w, int* h);
int overlay_snapshot_png(void* device, const wch* path);
extern u32 g_snap_format, g_snap_ms;
u64 overlay_frames(void);
int overlay_readable(const void* addr, u64 size);
void overlay_describe(const void* addr, char* out, int cap);

#endif
