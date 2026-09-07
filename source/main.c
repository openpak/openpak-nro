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
#include "hosts.h"
#include "text.h"

#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>

#define CONFIG_DIR  "/switch/openpak"
#define CONFIG_PATH CONFIG_DIR "/server.txt"
// No default address: there is no IP that is right for someone else's network, and a
// plausible-looking wrong one is worse than an obvious blank.
#define UNSET_LABEL "Not set"

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

enum { IT_NETWORK, IT_ADDRESS, IT_REBOOT, IT_COUNT };
static const char *const item_labels[IT_COUNT] = {"Network", "Server address", "Reboot console"};

static void load_ip(char *ip, size_t len) {
    ip[0] = '\0';
    FILE *f = fopen(CONFIG_PATH, "rb");
    if (!f) return;
    if (fgets(ip, (int)len, f)) ip[strcspn(ip, "\r\n")] = '\0';
    fclose(f);
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

// dns_mitm reads the hosts files at boot, so the tool offers the reboot itself rather than
// sending the user to the power menu.
static void reboot_console(void) {
    if (R_FAILED(spsmInitialize())) return;
    spsmShutdown(true);
    spsmExit();
}

// The site's icon: a rounded square with a white P.
static void draw_mark(int x, int y, int size) {
    gfx_rounded(x, y, size, size, size / 4, ACCENT);
    txt_draw(x + size / 2, y + size / 4 - 2, PX_TITLE, TXT_CENTER, STRONG, "P");
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

    char value[64];
    for (int i = 0; i < IT_COUNT; i++) {
        Color vc = INK;
        value[0] = '\0';
        if (i == IT_NETWORK) {
            snprintf(value, sizeof(value), "%s", on ? "OpenPak" : "Nintendo");
            vc = on ? OK : WARN;
        } else if (i == IT_ADDRESS) {
            snprintf(value, sizeof(value), "%s", ip[0] ? ip : UNSET_LABEL);
            vc = ip[0] ? INK : WARN;
        }
        int y = card_y + 12 + i * row_h;
        draw_item(margin + 12, y, card_w - 24, row_h, i == sel, item_labels[i], value, vc);
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
    int sel = 0;

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
            switch (sel) {
            case IT_NETWORK:
                if (!on && !ip[0]) {
                    sel = IT_ADDRESS;
                    snprintf(status, sizeof(status), "Set the server address first.");
                    break;
                }
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
        render(ip, on, sel, status, confirm_reboot);
    }

    txt_exit();
    gfx_exit();
    plExit();
    return 0;
}
