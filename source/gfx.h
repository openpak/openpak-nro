// Direct framebuffer drawing through libnx — no SDL, no EGL, no mesa. That is what lets the
// NRO load in applet mode, where the GPU stack is not available to homebrew.
#pragma once
#include <stdbool.h>
#include <switch.h>

#define GFX_W 1280
#define GFX_H 720

typedef struct { u8 r, g, b, a; } Color;

bool gfx_init(void);
void gfx_exit(void);
void gfx_begin(void);           // grab the back buffer for this frame
void gfx_end(void);             // hand it to the compositor
void gfx_clear(Color c);
void gfx_pixel(int x, int y, Color c);           // alpha-blended
void gfx_rect(int x, int y, int w, int h, Color c);
void gfx_rounded(int x, int y, int w, int h, int radius, Color c);
void gfx_rounded_outline(int x, int y, int w, int h, int radius, Color c);
void gfx_circle(int cx, int cy, int radius, Color c);
