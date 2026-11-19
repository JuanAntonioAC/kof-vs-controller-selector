#include "png.h"
#include "win.h"

#define PNG_MAX_W 1280
#define PNG_MAX_H 720
#define RAW_MAX (PNG_MAX_H * (PNG_MAX_W * 3 + 1))

static u8 g_raw[RAW_MAX];
static u8 g_zlib[RAW_MAX + RAW_MAX / 65535 * 5 + 64];
static u32 g_crc_table[256];

static void crc_init(void) {
    u32 n, c, k;
    for (n = 0; n < 256; ++n) {
        c = n;
        for (k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crc_table[n] = c;
    }
}

static u32 crc_update(u32 crc, const u8* data, u32 n) {
    u32 i;
    for (i = 0; i < n; ++i) crc = g_crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put32(u8* p, u32 v) {
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static int write_chunk(HANDLE f, const char* type, const u8* data, u32 n) {
    u8 head[8];
    u8 tail[4];
    DWORD w;
    u32 crc;
    put32(head, n);
    memcpy(head + 4, type, 4);
    crc = crc_update(0xFFFFFFFFu, head + 4, 4);
    crc = crc_update(crc, data, n) ^ 0xFFFFFFFFu;
    put32(tail, crc);
    if (!WriteFile(f, head, 8, &w, 0)) return 0;
    if (n && !WriteFile(f, data, n, &w, 0)) return 0;
    return WriteFile(f, tail, 4, &w, 0);
}

static int write_raw(const wch* path, int width, int height);

int png_write_bgrx(const wch* path, int width, int height, const u8* bits, int pitch) {
    int x, y, step = 1, ow, oh;
    while (width / step > PNG_MAX_W || height / step > PNG_MAX_H) ++step;
    ow = width / step;
    oh = height / step;
    for (y = 0; y < oh; ++y) {
        u8* row = g_raw + (u32)y * ((u32)ow * 3 + 1);
        const u8* src = bits + (i64)y * step * pitch;
        row[0] = 0;
        for (x = 0; x < ow; ++x) {
            const u8* px = src + (i64)x * step * 4;
            row[1 + x * 3] = px[2];
            row[2 + x * 3] = px[1];
            row[3 + x * 3] = px[0];
        }
    }
    return write_raw(path, ow, oh);
}

int png_render_rects(const wch* path, int width, int height, const UiRect* rects, int count) {
    u32 raw_len;
    int r, x, y;
    if (width > PNG_MAX_W || height > PNG_MAX_H) return 0;
    raw_len = (u32)height * ((u32)width * 3 + 1);
    memset(g_raw, 0, raw_len);
    for (r = 0; r < count; ++r) {
        const UiRect* q = &rects[r];
        int x1 = q->x1 < 0 ? 0 : q->x1;
        int y1 = q->y1 < 0 ? 0 : q->y1;
        int x2 = q->x2 > width ? width : q->x2;
        int y2 = q->y2 > height ? height : q->y2;
        u8 cr = (u8)(q->color >> 16), cg = (u8)(q->color >> 8), cb = (u8)q->color;
        for (y = y1; y < y2; ++y) {
            u8* row = g_raw + (u32)y * ((u32)width * 3 + 1) + 1;
            for (x = x1; x < x2; ++x) {
                row[x * 3] = cr;
                row[x * 3 + 1] = cg;
                row[x * 3 + 2] = cb;
            }
        }
    }
    return write_raw(path, width, height);
}

static int write_raw(const wch* path, int width, int height) {
    static const u8 sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    u8 ihdr[13];
    u32 raw_len, pos, z = 0, a = 1, b = 0, i;
    HANDLE f;
    DWORD w;
    crc_init();
    raw_len = (u32)height * ((u32)width * 3 + 1);
    g_zlib[z++] = 0x78;
    g_zlib[z++] = 0x01;
    for (pos = 0; pos < raw_len;) {
        u32 n = raw_len - pos;
        if (n > 65535) n = 65535;
        g_zlib[z++] = (u8)((pos + n == raw_len) ? 1 : 0);
        g_zlib[z++] = (u8)n;
        g_zlib[z++] = (u8)(n >> 8);
        g_zlib[z++] = (u8)~n;
        g_zlib[z++] = (u8)(~n >> 8);
        memcpy(g_zlib + z, g_raw + pos, n);
        z += n;
        pos += n;
    }
    for (i = 0; i < raw_len; ++i) {
        a = (a + g_raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    put32(g_zlib + z, (b << 16) | a);
    z += 4;
    put32(ihdr, (u32)width);
    put32(ihdr + 4, (u32)height);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    f = CreateFileW(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE) return 0;
    WriteFile(f, sig, 8, &w, 0);
    write_chunk(f, "IHDR", ihdr, 13);
    write_chunk(f, "IDAT", g_zlib, z);
    write_chunk(f, "IEND", 0, 0);
    CloseHandle(f);
    return 1;
}
