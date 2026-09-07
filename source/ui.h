// Small drawing helpers over SDL2: rounded rectangles, circles, and text with an
// alignment and a measured width. Enough to lay out a real interface, nothing more.
#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } ui_align;

void ui_fill_rounded(SDL_Renderer *r, SDL_Rect rect, int radius, SDL_Color c);
void ui_stroke_rounded(SDL_Renderer *r, SDL_Rect rect, int radius, SDL_Color c);
void ui_fill_circle(SDL_Renderer *r, int cx, int cy, int radius, SDL_Color c);
int  ui_text_width(TTF_Font *f, const char *text);
// Returns the drawn width, so callers can flow one item after another.
int  ui_text(SDL_Renderer *r, TTF_Font *f, int x, int y, ui_align a, SDL_Color c, const char *fmt, ...);
