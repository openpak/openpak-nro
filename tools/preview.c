// Renders the logo (and a colour swatch strip) to a PNG so the drawing code can be reviewed
// on a PC: cc -Itools/hostshim -Isource -o /tmp/preview tools/preview.c source/gfx.c source/logo.c
#include "gfx.h"
#include "logo.h"

#include <stdio.h>
#include <string.h>
#include <zlib.h>

u32 *host_fb;
u32 host_stride;

static void put_be32(unsigned char *p, u32 v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }

static void write_png(const char *path, const u32 *px, int w, int h) {
    // raw scanlines, each prefixed with filter byte 0
    unsigned long raw_len = (unsigned long)h * (1 + w * 3);
    unsigned char *raw = malloc(raw_len);
    unsigned long o = 0;
    for (int y = 0; y < h; y++) {
        raw[o++] = 0;
        for (int x = 0; x < w; x++) {
            u32 v = px[y * w + x];
            raw[o++] = v & 0xff; raw[o++] = (v >> 8) & 0xff; raw[o++] = (v >> 16) & 0xff;
        }
    }
    unsigned long comp_len = compressBound(raw_len);
    unsigned char *comp = malloc(comp_len);
    compress2(comp, &comp_len, raw, raw_len, 9);

    FILE *f = fopen(path, "wb");
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    unsigned char ihdr[25] = {0};
    put_be32(ihdr, 13); memcpy(ihdr + 4, "IHDR", 4);
    put_be32(ihdr + 8, (u32)w); put_be32(ihdr + 12, (u32)h);
    ihdr[16] = 8; ihdr[17] = 2;   // 8-bit RGB
    put_be32(ihdr + 21, (u32)crc32(0, ihdr + 4, 17));
    fwrite(ihdr, 1, 25, f);

    unsigned char *idat = malloc(comp_len + 12);
    put_be32(idat, (u32)comp_len); memcpy(idat + 4, "IDAT", 4);
    memcpy(idat + 8, comp, comp_len);
    put_be32(idat + 8 + comp_len, (u32)crc32(0, idat + 4, (unsigned)(4 + comp_len)));
    fwrite(idat, 1, comp_len + 12, f);

    unsigned char iend[12] = {0};
    memcpy(iend + 4, "IEND", 4);
    put_be32(iend + 8, (u32)crc32(0, iend + 4, 4));
    fwrite(iend, 1, 12, f);
    fclose(f);
    free(raw); free(comp); free(idat);
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "/tmp/openpak-logo.png";
    gfx_init();
    gfx_begin();
    Color paper = {0x10, 0x11, 0x14, 255};
    gfx_clear(paper);
    draw_mark(40, 40, 256);        // large, to inspect the curves
    draw_mark(340, 40, 96);
    draw_mark(470, 40, 56);        // the size the header actually uses
    draw_mark(560, 40, 32);
    gfx_end();
    write_png(out, host_fb, GFX_W, GFX_H);
    printf("wrote %s\n", out);
    return 0;
}
