// openpak.nro — flips a CFW Switch between Nintendo's servers and OpenPak's.
//
// Selecting Network writes a marked block of dns_mitm host rules (or takes it back out) and
// then offers the reboot that makes it live. The address lives in /switch/openpak/server.txt
// and is edited with the system keyboard.
//
// Drawn straight to the framebuffer with FreeType text: no SDL, no EGL, so it loads in applet
// mode as well as under title takeover. Palette follows openpak.org's dark theme.
//
// Written from scratch against libnx: no code from any other homebrew.
#include "gfx.h"
#include "ca.h"
#include "hosts.h"
#include "logo.h"
#include "text.h"

#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>

#define CONFIG_DIR  "/switch/openpak"
#define CONFIG_PATH CONFIG_DIR "/server.txt"
// OpenPak's server, baked in: the point of this tool is that nobody has to look an address
// up. Override at build time with -DOPENPAK_SERVER=\"1.2.3.4\", or on the console with X.
#ifndef OPENPAK_SERVER
#define OPENPAK_SERVER "145.241.199.19"
#endif

// openpak.org's dark theme.
static const Color PAPER   = {0x10, 0x11, 0x14, 255};
static const Color RAISED  = {0x1c, 0x1e, 0x24, 255};
static const Color SELECT  = {0x23, 0x2a, 0x3d, 255};
static const Color LINE    = {0x26, 0x28, 0x2e, 255};
static const Color INK     = {0xe9, 0xea, 0xee, 255};
static const Color STRONG  = {0xff, 0xff, 0xff, 255};
static const Color MUTED   = {0x8a, 0x8e, 0x99, 255};
static const Color ACCENT  = {0x2f, 0x56, 0xd0, 255};
static const Color OK      = {0x3d, 0xc0, 0x7c, 255};
static const Color OK_BG   = {0x14, 0x2c, 0x20, 255};
static const Color WARN    = {0xe0, 0xa8, 0x50, 255};
static const Color WARN_BG = {0x2c, 0x24, 0x14, 255};

#define PX_DISPLAY 46
#define PX_TITLE   34
#define PX_BODY    24
#define PX_LABEL   19

// The whole tool is one choice: which network this console talks to.
enum { IT_NINTENDO, IT_OPENPAK, IT_COUNT };
static const char *const item_labels[IT_COUNT] = {"Nintendo", "OpenPak"};

static void load_ip(char *ip, size_t len) {
    snprintf(ip, len, "%s", OPENPAK_SERVER);
    FILE *f = fopen(CONFIG_PATH, "rb");
    if (!f) return;                       // never configured: the built-in address stands
    char saved[64] = {0};
    if (fgets(saved, sizeof(saved), f)) {
        saved[strcspn(saved, "\r\n")] = '\0';
        if (saved[0]) snprintf(ip, len, "%s", saved);
    }
    fclose(f);
}

static void save_ip(const char *ip) {
    mkdir(CONFIG_DIR, 0777);
    FILE *f = fopen(CONFIG_PATH, "wb");
    if (!f) return;
    fprintf(f, "%s\n", ip);
    fclose(f);
}

// Kept for the config file and the build-time define; the interface never asks.
__attribute__((unused)) static bool prompt_ip(char *ip, size_t len) {
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

// dns_mitm reads the hosts files at boot, so the tool offers the reboot itself rather than
// sending the user to the power menu.
static void reboot_console(void) {
    if (R_FAILED(spsmInitialize())) return;
    spsmShutdown(true);
    spsmExit();
}

// A button glyph and its caption, returning the width consumed so hints can flow.
static int draw_hint(int x, int y, const char *glyph, const char *caption) {
    const int r = 18;
    gfx_circle(x + r, y + r, r, RAISED);
    gfx_circle(x + r, y + r, r, RAISED);
    txt_draw(x + r, y + r - PX_LABEL / 2 - 3, PX_LABEL, TXT_CENTER, INK, "%s", glyph);
    int w = txt_draw(x + r * 2 + 12, y + r - PX_BODY / 2 - 2, PX_BODY, TXT_LEFT, MUTED, "%s", caption);
    return r * 2 + 12 + w + 38;
}

static void draw_item(int x, int y, int w, int h, bool selected,
                      const char *label, const char *value, Color value_color) {
    if (selected) {
        gfx_rounded(x, y, w, h, 12, SELECT);
        gfx_rounded(x, y + 10, 4, h - 20, 2, ACCENT);
    }
    txt_draw(x + 26, y + h / 2 - PX_BODY / 2 - 2, PX_BODY, TXT_LEFT, selected ? STRONG : INK, "%s", label);
    if (value && value[0])
        txt_draw(x + w - 26, y + h / 2 - PX_BODY / 2 - 2, PX_BODY, TXT_RIGHT, value_color, "%s", value);
}

static void render(const char *ip, bool on, int sel, const char *status, bool confirming) {
    const int margin = 72;
    const int card_w = GFX_W - margin * 2;

    gfx_begin();
    gfx_clear(PAPER);

    // Header
    draw_mark(margin, 48, 56);
    txt_draw(margin + 74, 50, PX_TITLE, TXT_LEFT, STRONG, "OpenPak");
    txt_draw(margin + 76, 92, PX_LABEL, TXT_LEFT, MUTED, "Network selector");

    const char *pill = on ? "OPENPAK" : "NINTENDO";
    int pill_w = txt_width(PX_LABEL, "%s", pill) + 40;
    gfx_rounded(GFX_W - margin - pill_w, 58, pill_w, 40, 20, on ? OK_BG : WARN_BG);
    gfx_rounded_outline(GFX_W - margin - pill_w, 58, pill_w, 40, 20, on ? OK : WARN);
    txt_draw(GFX_W - margin - pill_w / 2, 68, PX_LABEL, TXT_CENTER, on ? OK : WARN, "%s", pill);

    gfx_rect(margin, 132, card_w, 1, LINE);

    // The list
    const int row_h = 72;
    const int card_y = 164, card_h = row_h * IT_COUNT + 24;
    gfx_rounded(margin, card_y, card_w, card_h, 18, RAISED);
    gfx_rounded_outline(margin, card_y, card_w, card_h, 18, LINE);

    for (int i = 0; i < IT_COUNT; i++) {
        bool active = (i == IT_OPENPAK) == on;
        int y = card_y + 12 + i * row_h;
        draw_item(margin + 12, y, card_w - 24, row_h, i == sel, item_labels[i],
                  active ? "Active" : "", active ? (on ? OK : WARN) : MUTED);
        if (i + 1 < IT_COUNT) gfx_rect(margin + 38, y + row_h, card_w - 76, 1, LINE);
    }

    // Message line
    int msg_y = card_y + card_h + 26;
    if (status[0]) {
        gfx_rounded(margin, msg_y, card_w, 56, 12, RAISED);
        gfx_rounded_outline(margin, msg_y, card_w, 56, 12, confirming ? WARN : ACCENT);
        txt_draw(margin + 24, msg_y + 16, PX_BODY, TXT_LEFT, INK, "%s", status);
    } else {
        txt_draw(margin, msg_y + 16, PX_LABEL, TXT_LEFT, MUTED,
                 "Changes apply at boot — dns_mitm reads the hosts files then.");
    }

    // Footer
    gfx_rect(margin, 604, card_w, 1, LINE);
    int x = margin;
    if (confirming) {
        x += draw_hint(x, 628, "A", "Reboot now");
        draw_hint(x, 628, "B", "Later");
    } else {
        x += draw_hint(x, 628, "A", "Select");
        draw_hint(x, 628, "B", "Exit");
    }

    gfx_end();
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    romfsInit();          // the CA bundle we ship lives in romfs:/
    plInitialize(PlServiceType_User);
    if (!gfx_init()) { plExit(); return 1; }
    if (!txt_init()) { gfx_exit(); plExit(); return 1; }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    char ip[64];
    load_ip(ip, sizeof(ip));
    char status[192] = "";
    bool on = openpak_enabled();
    bool confirm_reboot = false;
    int sel = on ? IT_OPENPAK : IT_NINTENDO;

    while (appletMainLoop()) {
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
            break;
        } else if (down & HidNpadButton_A) {
            bool want_openpak = (sel == IT_OPENPAK);
            // Selecting the network already in use re-applies it rather than refusing: that is
            // how a console picks up a new CA or new host rules after the tool is updated.
            bool again = (want_openpak == on);
            if (want_openpak ? openpak_enable(ip, err, sizeof(err))
                             : openpak_disable(err, sizeof(err))) {
                // Host rules alone are not enough: the browser keeps its own CA bundle, and
                // without ours the link page never loads.
                int bundles = 0;
                if (want_openpak) bundles = openpak_ca_install(err, sizeof(err));
                else              bundles = openpak_ca_remove(err, sizeof(err));
                on = want_openpak;
                confirm_reboot = true;
                if (want_openpak && !openpak_browser_patch_present()) {
                    // Say so plainly: everything else can be right and the link page will still
                    // never load without the browser's own patch.
                    snprintf(status, sizeof(status),
                             "Applied (%d CA files), but the browser patch is missing — the link page will not load.",
                             bundles);
                } else {
                    snprintf(status, sizeof(status), again ? "%s re-applied (%d CA files).  Reboot to apply?"
                                                           : "Switched to %s (%d CA files).  Reboot to apply?",
                             item_labels[sel], bundles);
                }
            } else {
                snprintf(status, sizeof(status), "Failed: %s", err);
            }
        }
        render(ip, on, sel, status, confirm_reboot);
    }

    txt_exit();
    gfx_exit();
    plExit();
    romfsExit();
    return 0;
}
