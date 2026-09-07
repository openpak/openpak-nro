#include "gfx.h"
#include <string.h>

static Framebuffer fb;
static u32 *buf;
static u32 stride_px;
static bool ready;

bool gfx_init(void) {
    NWindow *win = nwindowGetDefault();
    if (R_FAILED(framebufferCreate(&fb, win, GFX_W, GFX_H, PIXEL_FORMAT_RGBA_8888, 2))) return false;
    if (R_FAILED(framebufferMakeLinear(&fb))) { framebufferClose(&fb); return false; }
    ready = true;
    return true;
}

void gfx_exit(void) {
    if (ready) framebufferClose(&fb);
    ready = false;
}

void gfx_begin(void) {
    u32 stride = 0;
    buf = (u32 *)framebufferBegin(&fb, &stride);
    stride_px = stride / sizeof(u32);
}

void gfx_end(void) {
    framebufferEnd(&fb);
    buf = NULL;
}

// RGBA_8888 in memory is 0xAABBGGRR.
static inline u32 pack(u8 r, u8 g, u8 b, u8 a) {
    return (u32)r | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24);
}

void gfx_pixel(int x, int y, Color c) {
    if (!buf || x < 0 || y < 0 || x >= GFX_W || y >= GFX_H || c.a == 0) return;
    u32 *p = &buf[y * stride_px + x];
    if (c.a == 255) {
        *p = pack(c.r, c.g, c.b, 255);
        return;
    }
    u32 d = *p;
    u8 dr = d & 0xff, dg = (d >> 8) & 0xff, db = (d >> 16) & 0xff;
    u16 a = c.a, ia = 255 - a;
    *p = pack((u8)((c.r * a + dr * ia) / 255), (u8)((c.g * a + dg * ia) / 255),
              (u8)((c.b * a + db * ia) / 255), 255);
}

void gfx_clear(Color c) {
    if (!buf) return;
    u32 v = pack(c.r, c.g, c.b, 255);
    for (int y = 0; y < GFX_H; y++) {
        u32 *row = &buf[y * stride_px];
        for (int x = 0; x < GFX_W; x++) row[x] = v;
    }
}

void gfx_rect(int x, int y, int w, int h, Color c) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) gfx_pixel(x + i, y + j, c);
}

void gfx_circle(int cx, int cy, int radius, Color c) {
    for (int dy = -radius; dy <= radius; dy++) {
        int span = 0;
        while ((span + 1) * (span + 1) + dy * dy <= radius * radius) span++;
        for (int dx = -span; dx <= span; dx++) gfx_pixel(cx + dx, cy + dy, c);
    }
}

void gfx_rounded(int x, int y, int w, int h, int radius, Color c) {
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    gfx_rect(x + radius, y, w - 2 * radius, h, c);
    gfx_rect(x, y + radius, radius, h - 2 * radius, c);
    gfx_rect(x + w - radius, y + radius, radius, h - 2 * radius, c);
    gfx_circle(x + radius, y + radius, radius, c);
    gfx_circle(x + w - radius - 1, y + radius, radius, c);
    gfx_circle(x + radius, y + h - radius - 1, radius, c);
    gfx_circle(x + w - radius - 1, y + h - radius - 1, radius, c);
}

void gfx_rounded_outline(int x, int y, int w, int h, int radius, Color c) {
    for (int i = x + radius; i < x + w - radius; i++) { gfx_pixel(i, y, c); gfx_pixel(i, y + h - 1, c); }
    for (int j = y + radius; j < y + h - radius; j++) { gfx_pixel(x, j, c); gfx_pixel(x + w - 1, j, c); }
    // corner arcs
    for (int t = 0; t <= 90; t++) {
        double rad = t * 3.14159265 / 180.0;
        int dx = (int)(radius * (1 - __builtin_cos(rad)) + 0.5);
        int dy = (int)(radius * (1 - __builtin_sin(rad)) + 0.5);
        gfx_pixel(x + dx, y + dy, c);
        gfx_pixel(x + w - 1 - dx, y + dy, c);
        gfx_pixel(x + dx, y + h - 1 - dy, c);
        gfx_pixel(x + w - 1 - dx, y + h - 1 - dy, c);
    }
}
