// openpak.nro — flips a CFW Switch between Nintendo's servers and OpenPak's.
//
// Enable writes a marked block of dns_mitm host entries; disable removes it and leaves the
// console exactly as it was. The address lives in /switch/openpak/server.txt and is edited
// with the system keyboard. Palette and shapes follow openpak.org's dark theme so the
// console tool and the site read as one product.
//
// Written from scratch against libnx/SDL2: no code from any other homebrew.
#include "hosts.h"
#include "ui.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>

#define CONFIG_DIR  "/switch/openpak"
#define CONFIG_PATH CONFIG_DIR "/server.txt"
#define DEFAULT_IP  "10.0.0.1"

#define W 1280
#define H 720

// openpak.org's dark theme.
static const SDL_Color PAPER   = {0x10, 0x11, 0x14, 255};
static const SDL_Color RAISED  = {0x1c, 0x1e, 0x24, 255};
static const SDL_Color LINE    = {0x26, 0x28, 0x2e, 255};
static const SDL_Color INK     = {0xe9, 0xea, 0xee, 255};
static const SDL_Color STRONG  = {0xff, 0xff, 0xff, 255};
static const SDL_Color MUTED   = {0x8a, 0x8e, 0x99, 255};
static const SDL_Color ACCENT  = {0x2f, 0x56, 0xd0, 255};
static const SDL_Color OK      = {0x3d, 0xc0, 0x7c, 255};
static const SDL_Color OK_BG   = {0x14, 0x2c, 0x20, 255};
static const SDL_Color WARN    = {0xe0, 0xa8, 0x50, 255};
static const SDL_Color WARN_BG = {0x2c, 0x24, 0x14, 255};

typedef struct { TTF_Font *display, *title, *body, *label, *mono; } Fonts;

static void load_ip(char *ip, size_t len) {
    snprintf(ip, len, "%s", DEFAULT_IP);
    FILE *f = fopen(CONFIG_PATH, "rb");
    if (!f) return;
    if (fgets(ip, (int)len, f)) ip[strcspn(ip, "\r\n")] = '\0';
    fclose(f);
    if (!ip[0]) snprintf(ip, len, "%s", DEFAULT_IP);
}

static void save_ip(const char *ip) {
    mkdir(CONFIG_DIR, 0777);
    FILE *f = fopen(CONFIG_PATH, "wb");
    if (!f) return;
    fprintf(f, "%s\n", ip);
    fclose(f);
}

static bool prompt_ip(char *ip, size_t len) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, "OpenPak server address");
    swkbdConfigSetSubText(&kbd, "IP or hostname of your OpenPak server");
    swkbdConfigSetInitialText(&kbd, ip);
    char out[64] = {0};
    Result rc = swkbdShow(&kbd, out, sizeof(out));
    swkbdClose(&kbd);
    if (R_FAILED(rc) || !out[0]) return false;
    snprintf(ip, len, "%s", out);
    return true;
}

// A change only takes effect once dns_mitm re-reads the hosts files, which happens at boot,
// so the tool offers the reboot itself rather than sending the user to the power menu.
static void reboot_console(void) {
    if (R_FAILED(spsmInitialize())) return;
    spsmShutdown(true);   // true = reboot
    spsmExit();
}

// The site's icon: a rounded square with a white P.
static void draw_mark(SDL_Renderer *r, Fonts *f, int x, int y, int size) {
    SDL_Rect box = {x, y, size, size};
    ui_fill_rounded(r, box, size / 4, ACCENT);
    ui_text(r, f->title, x + size / 2, y + size / 2 - 24, ALIGN_CENTER, STRONG, "P");
}

// A Switch-style button glyph: dark circle, letter, then its caption.
static int draw_hint(SDL_Renderer *r, Fonts *f, int x, int y, const char *glyph, const char *caption) {
    const int radius = 19;
    ui_fill_circle(r, x + radius, y + radius, radius, RAISED);
    ui_fill_circle(r, x + radius, y + radius, radius - 1, RAISED);
    ui_text(r, f->label, x + radius, y + radius - 15, ALIGN_CENTER, INK, "%s", glyph);
    int w = ui_text(r, f->body, x + radius * 2 + 12, y + radius - 16, ALIGN_LEFT, MUTED, "%s", caption);
    return radius * 2 + 12 + w + 40;
}

// A list row: label on the left, current value on the right, highlighted when selected.
static void draw_item(SDL_Renderer *r, Fonts *f, SDL_Rect row, bool selected,
                      const char *label, const char *value, SDL_Color value_color) {
    if (selected) {
        ui_fill_rounded(r, row, 12, (SDL_Color){0x23, 0x2a, 0x3d, 255});
        SDL_Rect bar = {row.x, row.y + 10, 4, row.h - 20};
        ui_fill_rounded(r, bar, 2, ACCENT);
    }
    ui_text(r, f->body, row.x + 26, row.y + row.h / 2 - 18, ALIGN_LEFT, selected ? STRONG : INK, "%s", label);
    if (value[0])
        ui_text(r, f->body, row.x + row.w - 26, row.y + row.h / 2 - 18, ALIGN_RIGHT, value_color, "%s", value);
}

typedef struct {
    const char *label;
    char value[64];
    SDL_Color color;
} Item;

static void render(SDL_Renderer *r, Fonts *f, Item *items, int count, int sel,
                   bool on, const char *status, bool confirming) {
    SDL_SetRenderDrawColor(r, PAPER.r, PAPER.g, PAPER.b, 255);
    SDL_RenderClear(r);

    const int margin = 72;
    const int card_w = W - margin * 2;

    // Header
    draw_mark(r, f, margin, 48, 56);
    ui_text(r, f->title, margin + 74, 50, ALIGN_LEFT, STRONG, "OpenPak");
    ui_text(r, f->label, margin + 76, 92, ALIGN_LEFT, MUTED, "Network selector");

    // State pill, top right
    const char *pill = on ? "OPENPAK" : "NINTENDO";
    int pill_w = ui_text_width(f->label, pill) + 40;
    SDL_Rect pill_box = {W - margin - pill_w, 58, pill_w, 42};
    ui_fill_rounded(r, pill_box, 21, on ? OK_BG : WARN_BG);
    ui_stroke_rounded(r, pill_box, 21, on ? OK : WARN);
    ui_text(r, f->label, pill_box.x + pill_box.w / 2, pill_box.y + 9, ALIGN_CENTER, on ? OK : WARN, "%s", pill);

    SDL_SetRenderDrawColor(r, LINE.r, LINE.g, LINE.b, 255);
    SDL_RenderDrawLine(r, margin, 132, W - margin, 132);

    // The list
    SDL_Rect card = {margin, 164, card_w, 72 * count + 24};
    ui_fill_rounded(r, card, 18, RAISED);
    ui_stroke_rounded(r, card, 18, LINE);
    for (int i = 0; i < count; i++) {
        SDL_Rect row = {card.x + 12, card.y + 12 + i * 72, card_w - 24, 72};
        draw_item(r, f, row, i == sel, items[i].label, items[i].value, items[i].color);
        if (i + 1 < count) {
            SDL_SetRenderDrawColor(r, LINE.r, LINE.g, LINE.b, 255);
            SDL_RenderDrawLine(r, row.x + 26, row.y + row.h, row.x + row.w - 26, row.y + row.h);
        }
    }

    // Message line
    int msg_y = card.y + card.h + 28;
    if (status[0]) {
        SDL_Rect toast = {margin, msg_y, card_w, 56};
        ui_fill_rounded(r, toast, 12, RAISED);
        ui_stroke_rounded(r, toast, 12, confirming ? WARN : ACCENT);
        ui_text(r, f->body, toast.x + 24, toast.y + 14, ALIGN_LEFT, INK, "%s", status);
    } else {
        ui_text(r, f->label, margin, msg_y + 14, ALIGN_LEFT, MUTED,
                "Changes apply at boot — dns_mitm reads the hosts files then.");
    }

    // Footer
    SDL_SetRenderDrawColor(r, LINE.r, LINE.g, LINE.b, 255);
    SDL_RenderDrawLine(r, margin, 604, W - margin, 604);
    int x = margin;
    if (confirming) {
        x += draw_hint(r, f, x, 630, "A", "Reboot now");
        draw_hint(r, f, x, 630, "B", "Later");
    } else {
        x += draw_hint(r, f, x, 630, "A", "Select");
        draw_hint(r, f, x, 630, "B", "Exit");
    }
    SDL_RenderPresent(r);
}

static TTF_Font *open_font(PlFontData *d, int size) {
    return TTF_OpenFontRW(SDL_RWFromMem(d->address, d->size), 1, size);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    plInitialize(PlServiceType_User);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    if (TTF_Init() != 0) { SDL_Quit(); return 1; }

    // Input comes from libnx, not SDL: SDL_CONTROLLER_BUTTON_A is the *bottom* button in the
    // Xbox layout it models, which on a Switch pad is physically B. Reading the pad directly
    // means A is the button with an A on it.
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    SDL_Window *win = SDL_CreateWindow("openpak", 0, 0, W, H, 0);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : NULL;
    // Without a renderer there is nothing to say and no way to say it; leaving quietly beats
    // dereferencing NULL and handing the user a crash report.
    if (!ren) { TTF_Quit(); SDL_Quit(); plExit(); return 1; }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    Fonts f = {0};
    PlFontData fd;
    if (R_SUCCEEDED(plGetSharedFontByType(&fd, PlSharedFontType_Standard))) {
        f.display = open_font(&fd, 52);
        f.title   = open_font(&fd, 38);
        f.body    = open_font(&fd, 26);
        f.label   = open_font(&fd, 21);
    }

    char ip[64];
    load_ip(ip, sizeof(ip));
    char status[192] = "";
    bool on = openpak_enabled();
    bool confirm_reboot = false;
    int sel = 0;

    enum { IT_NETWORK, IT_ADDRESS, IT_REBOOT, IT_COUNT };
    Item items[IT_COUNT] = {
        {"Network", "", INK},
        {"Server address", "", INK},
        {"Reboot console", "", MUTED},
    };

    while (appletMainLoop()) {
        snprintf(items[IT_NETWORK].value, sizeof(items[IT_NETWORK].value), "%s", on ? "OpenPak" : "Nintendo");
        items[IT_NETWORK].color = on ? OK : WARN;
        snprintf(items[IT_ADDRESS].value, sizeof(items[IT_ADDRESS].value), "%s", ip);

        SDL_PumpEvents();          // keep SDL's video side alive; input is read below
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        char err[128] = "";

        if (confirm_reboot) {
            if (down & HidNpadButton_A) reboot_console();
            else if (down & HidNpadButton_B) { confirm_reboot = false; status[0] = '\0'; }
        } else if (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) {
            sel = (sel + IT_COUNT - 1) % IT_COUNT;
        } else if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) {
            sel = (sel + 1) % IT_COUNT;
        } else if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
            goto done;
        } else if (down & HidNpadButton_A) {
            switch (sel) {
            case IT_NETWORK:
                // Applied on selection; the reboot that makes it live is all that is left to ask.
                if (on ? openpak_disable(err, sizeof(err)) : openpak_enable(ip, err, sizeof(err))) {
                    on = !on;
                    confirm_reboot = true;
                    if (on) snprintf(status, sizeof(status), "Now pointing at %s.  Reboot to apply?", ip);
                    else    snprintf(status, sizeof(status), "Nintendo's servers restored.  Reboot to apply?");
                } else {
                    snprintf(status, sizeof(status), "Failed: %s", err);
                }
                break;
            case IT_ADDRESS:
                if (prompt_ip(ip, sizeof(ip))) {
                    save_ip(ip);
                    snprintf(status, sizeof(status), on ? "Address saved — select Network to re-apply."
                                                        : "Address saved.");
                }
                break;
            case IT_REBOOT:
                confirm_reboot = true;
                snprintf(status, sizeof(status), "Reboot the console now?");
                break;
            default:
                break;
            }
        }

        render(ren, &f, items, IT_COUNT, sel, on, status, confirm_reboot);
    }

done:
    if (f.display) TTF_CloseFont(f.display);
    if (f.title) TTF_CloseFont(f.title);
    if (f.body) TTF_CloseFont(f.body);
    if (f.label) TTF_CloseFont(f.label);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    plExit();
    return 0;
}
