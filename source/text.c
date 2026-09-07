#include "text.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static FT_Library lib;
static FT_Face face;
static int current_px;
static bool ready;

bool txt_init(void) {
    PlFontData fd;
    if (R_FAILED(plGetSharedFontByType(&fd, PlSharedFontType_Standard))) return false;
    if (FT_Init_FreeType(&lib)) return false;
    if (FT_New_Memory_Face(lib, (const FT_Byte *)fd.address, (FT_Long)fd.size, 0, &face)) {
        FT_Done_FreeType(lib);
        return false;
    }
    ready = true;
    return true;
}

void txt_exit(void) {
    if (!ready) return;
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    ready = false;
}

static void use_size(int px) {
    if (px != current_px) {
        FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px);
        current_px = px;
    }
}

int txt_line_height(int px) { return px * 5 / 4; }

// Minimal UTF-8 decode: enough for the Latin text this interface draws, and it degrades to
// one codepoint per byte rather than mangling anything it does not understand.
static const char *next_cp(const char *s, u32 *cp) {
    u8 c = (u8)*s;
    if (c < 0x80)            { *cp = c;                       return s + 1; }
    if ((c & 0xe0) == 0xc0)  { *cp = ((u32)(c & 0x1f) << 6) | ((u8)s[1] & 0x3f);  return s + 2; }
    if ((c & 0xf0) == 0xe0)  { *cp = ((u32)(c & 0x0f) << 12) | (((u8)s[1] & 0x3f) << 6) | ((u8)s[2] & 0x3f); return s + 3; }
    *cp = c;
    return s + 1;
}

static int measure(int px, const char *text) {
    if (!ready) return 0;
    use_size(px);
    int w = 0;
    for (const char *p = text; *p;) {
        u32 cp;
        p = next_cp(p, &cp);
        if (FT_Load_Char(face, cp, FT_LOAD_DEFAULT)) continue;
        w += (int)(face->glyph->advance.x >> 6);
    }
    return w;
}

static int render(int x, int y, int px, Color c, const char *text) {
    if (!ready) return 0;
    use_size(px);
    int pen = x;
    int baseline = y + px;   // y is the top of the line
    for (const char *p = text; *p;) {
        u32 cp;
        p = next_cp(p, &cp);
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) continue;
        FT_GlyphSlot g = face->glyph;
        for (unsigned row = 0; row < g->bitmap.rows; row++) {
            for (unsigned col = 0; col < g->bitmap.width; col++) {
                u8 cov = g->bitmap.buffer[row * g->bitmap.pitch + col];
                if (!cov) continue;
                Color px_c = {c.r, c.g, c.b, (u8)((cov * c.a) / 255)};
                gfx_pixel(pen + g->bitmap_left + (int)col, baseline - g->bitmap_top + (int)row, px_c);
            }
        }
        pen += (int)(g->advance.x >> 6);
    }
    return pen - x;
}

int txt_width(int px, const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    return measure(px, line);
}

int txt_draw(int x, int y, int px, txt_align a, Color c, const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (a == TXT_CENTER) x -= measure(px, line) / 2;
    else if (a == TXT_RIGHT) x -= measure(px, line);
    return render(x, y, px, c, line);
}
