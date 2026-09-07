#include "logo.h"

static const Color WHITE = {0xff, 0xff, 0xff, 255};

// The OpenPak logo, drawn to the same proportions as logo.svg (512 units): background circle
// with the indigo gradient, an outer ring, a 270-degree spiral arc, and the centre dot.
static const Color LOGO_FROM = {0x63, 0x66, 0xf1, 255};
static const Color LOGO_TO   = {0x43, 0x38, 0xca, 255};

void draw_mark(int x, int y, int size) {
    const float u = size / 512.0f;          // logo units -> pixels
    int cx = x + size / 2, cy = y + size / 2;
    int stroke = (int)(20 * u + 0.5f);
    if (stroke < 2) stroke = 2;

    gfx_circle_gradient(cx, cy, size / 2, LOGO_FROM, LOGO_TO);
    Color ring = {WHITE.r, WHITE.g, WHITE.b, 230};        // opacity .9 in the source
    gfx_ring(cx, cy, (int)(180 * u + 0.5f), stroke, ring);
    // The arc runs from due west round to due south, the way the path in logo.svg does.
    gfx_arc(cx, cy, (int)(96 * u + 0.5f), stroke, 270, 180, ring);
    Color dot = {WHITE.r, WHITE.g, WHITE.b, 242};         // opacity .95
    gfx_circle(cx, cy, (int)(28 * u + 0.5f), dot);
}

