// FreeType text straight into the framebuffer, using the console's own shared font so the
// NRO ships no assets and matches the system UI.
#pragma once
#include "gfx.h"

typedef enum { TXT_LEFT, TXT_CENTER, TXT_RIGHT } txt_align;

bool txt_init(void);
void txt_exit(void);
int  txt_width(int px, const char *fmt, ...);   // measured advance, in pixels
// Draws at (x, y) with y the TOP of the line; returns the width drawn.
int  txt_draw(int x, int y, int px, txt_align a, Color c, const char *fmt, ...);
int  txt_line_height(int px);
