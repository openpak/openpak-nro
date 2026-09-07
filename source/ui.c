#include "ui.h"
#include <stdarg.h>
#include <stdio.h>

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

void ui_fill_circle(SDL_Renderer *r, int cx, int cy, int radius, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = (int)(SDL_sqrt((double)(radius * radius - dy * dy)) + 0.5);
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

void ui_fill_rounded(SDL_Renderer *r, SDL_Rect rect, int radius, SDL_Color c) {
    if (radius * 2 > rect.w) radius = rect.w / 2;
    if (radius * 2 > rect.h) radius = rect.h / 2;
    fill_rect(r, rect.x + radius, rect.y, rect.w - 2 * radius, rect.h, c);
    fill_rect(r, rect.x, rect.y + radius, radius, rect.h - 2 * radius, c);
    fill_rect(r, rect.x + rect.w - radius, rect.y + radius, radius, rect.h - 2 * radius, c);
    ui_fill_circle(r, rect.x + radius, rect.y + radius, radius, c);
    ui_fill_circle(r, rect.x + rect.w - radius - 1, rect.y + radius, radius, c);
    ui_fill_circle(r, rect.x + radius, rect.y + rect.h - radius - 1, radius, c);
    ui_fill_circle(r, rect.x + rect.w - radius - 1, rect.y + rect.h - radius - 1, radius, c);
}

// A 1px border, drawn as a filled rounded rect one pixel larger behind nothing — cheap and
// good enough at this size. ponytail: no anti-aliasing; at 1280x720 the corners read fine.
void ui_stroke_rounded(SDL_Renderer *r, SDL_Rect rect, int radius, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(r, rect.x + radius, rect.y, rect.x + rect.w - radius, rect.y);
    SDL_RenderDrawLine(r, rect.x + radius, rect.y + rect.h - 1, rect.x + rect.w - radius, rect.y + rect.h - 1);
    SDL_RenderDrawLine(r, rect.x, rect.y + radius, rect.x, rect.y + rect.h - radius);
    SDL_RenderDrawLine(r, rect.x + rect.w - 1, rect.y + radius, rect.x + rect.w - 1, rect.y + rect.h - radius);
}

int ui_text_width(TTF_Font *f, const char *text) {
    int w = 0, h = 0;
    if (f) TTF_SizeUTF8(f, text, &w, &h);
    return w;
}

int ui_text(SDL_Renderer *r, TTF_Font *f, int x, int y, ui_align a, SDL_Color c, const char *fmt, ...) {
    if (!f) return 0;
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    SDL_Surface *s = TTF_RenderUTF8_Blended(f, line, c);
    if (!s) return 0;
    int w = s->w;
    if (a == ALIGN_CENTER) x -= w / 2;
    else if (a == ALIGN_RIGHT) x -= w;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_Rect dst = {x, y, s->w, s->h};
    SDL_FreeSurface(s);
    if (t) {
        SDL_RenderCopy(r, t, NULL, &dst);
        SDL_DestroyTexture(t);
    }
    return w;
}
