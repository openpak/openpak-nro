// Host shim: just enough of libnx for gfx.c to compile on a PC, so the logo and layout can be
// rendered to a PNG and looked at without a console. Never linked into the NRO.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef struct { int dummy; } NWindow;
typedef struct { u32 *pixels; u32 stride; } Framebuffer;

#define PIXEL_FORMAT_RGBA_8888 1
#define R_FAILED(x) ((x) != 0)

extern u32 *host_fb;
extern u32 host_stride;

static inline NWindow *nwindowGetDefault(void) { static NWindow w; return &w; }
static inline int framebufferCreate(Framebuffer *fb, NWindow *w, u32 width, u32 height, int fmt, u32 bufs) {
    (void)w; (void)fmt; (void)bufs;
    fb->stride = width * 4;
    fb->pixels = calloc((size_t)width * height, 4);
    host_fb = fb->pixels;
    host_stride = fb->stride;
    return fb->pixels ? 0 : 1;
}
static inline int framebufferMakeLinear(Framebuffer *fb) { (void)fb; return 0; }
static inline void framebufferClose(Framebuffer *fb) { free(fb->pixels); fb->pixels = NULL; }
static inline void *framebufferBegin(Framebuffer *fb, u32 *stride) { *stride = fb->stride; return fb->pixels; }
static inline void framebufferEnd(Framebuffer *fb) { (void)fb; }
