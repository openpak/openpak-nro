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
#include "system.h"
#include "hosts.h"
#include "netfetch.h"
#include "update.h"
#include "news.h"
#include "installtrust.h"
#include "crash.h"
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

// The whole tool is one choice: which network this console talks to. Choosing
// OpenPak installs everything OpenPak needs together — hosts, CA, News patches
// and the store-trust boot package; choosing Nintendo removes all of it.
enum { IT_NINTENDO, IT_OPENPAK, IT_COUNT };
static const char *const item_labels[IT_COUNT] = {"Nintendo", "OpenPak"};

// server.txt when the user set one; otherwise the network profile's server address when
// there is one (so moving the server is not a new NRO), else the built-in one.
static void load_ip(char *ip, size_t len) {
    const char *from_profile = openpak_active_policy()->address;
    snprintf(ip, len, "%s", from_profile[0] ? from_profile : OPENPAK_SERVER);
    FILE *f = fopen(CONFIG_PATH, "rb");
    if (!f) return;                       // never configured: the default stands
    char saved[64] = {0};
    if (fgets(saved, sizeof(saved), f)) {
        saved[strcspn(saved, "\r\n")] = '\0';
        if (saved[0]) snprintf(ip, len, "%s", saved);
    }
    fclose(f);
}

// The console is only told, never changed: the hosts files stay as they are until the
// user selects OpenPak again (docs/signed-ceiling.md, client rule 5).
#define REDIRECTS_CHANGED "OpenPak updated the Switch's network redirects. Re-apply OpenPak and reboot to use them."
static bool redirects_changed(const char *ip) {
    char installed[65], pending[65];
    if (!openpak_installed_digest(installed)) return false;
    openpak_pending_digest(ip, pending);
    return pending[0] && strcmp(installed, pending) != 0;
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

// What the online check at launch had to say ("" = it went through).
static char net_note[128];
// Set after enable when a KIP in /atmosphere/kips takes the place of OpenPak's Loader.
static char loader_note[160];
// What the Album does now, after enable; why override_config.ini was left alone, after either.
#define ALBUM_NOTE "Album now opens the Album — hold R on Album for the Homebrew Menu"
static char album_note[160];
static bool album_ok;

static void render(const char *ip, bool on, int sel, const char *status, bool confirming, bool asking,
                   bool offering) {
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
        gfx_rounded_outline(margin, msg_y, card_w, 56, 12, (confirming || asking || offering) ? WARN : ACCENT);
        txt_draw(margin + 24, msg_y + 16, PX_BODY, TXT_LEFT, INK, "%s", status);
        if (loader_note[0]) txt_draw(margin, msg_y + 76, PX_LABEL, TXT_LEFT, WARN, "%s", loader_note);
        if (album_note[0])
            txt_draw(margin, msg_y + (loader_note[0] ? 102 : 76), PX_LABEL, TXT_LEFT, album_ok ? INK : WARN, "%s", album_note);
    } else {
        txt_draw(margin, msg_y + 16, PX_LABEL, TXT_LEFT, MUTED,
                 "Changes apply at boot — dns_mitm reads the hosts files then.");
        // Say where the rules came from: "built in" means this console is running the
        // frozen fallback, which looks identical from here and is not the same thing.
        const openpak_policy *pol = openpak_active_policy();
        long long ceiling = openpak_ceiling_current()->version;
        char ceil_txt[48];
        if (ceiling) snprintf(ceil_txt, sizeof(ceil_txt), "signed ceiling v%lld", ceiling);
        else snprintf(ceil_txt, sizeof(ceil_txt), "built-in ceiling");
        if (pol->sequence)
            txt_draw(margin, msg_y + 42, PX_LABEL, TXT_LEFT, MUTED,
                     "%d host rules · %s · revision %lld · %s", pol->count,
                     openpak_source_name(pol->source), pol->sequence, ceil_txt);
        else
            txt_draw(margin, msg_y + 42, PX_LABEL, TXT_LEFT, MUTED,
                     "%d host rules · %s · %s", pol->count, openpak_source_name(pol->source), ceil_txt);
        if (net_note[0])
            txt_draw(margin, msg_y + 68, PX_LABEL, TXT_LEFT, MUTED, "%s", net_note);
        if (loader_note[0])
            txt_draw(margin, msg_y + 94, PX_LABEL, TXT_LEFT, WARN, "%s", loader_note);
        if (album_note[0])
            txt_draw(margin, msg_y + (loader_note[0] ? 120 : 94), PX_LABEL, TXT_LEFT, album_ok ? INK : WARN, "%s", album_note);
    }

    // Footer
    gfx_rect(margin, 604, card_w, 1, LINE);
    int x = margin;
    if (asking) {
        x += draw_hint(x, 628, "A", "Send");
        x += draw_hint(x, 628, "B", "Don't send");
        x += draw_hint(x, 628, "X", "Always");
        draw_hint(x, 628, "Y", "Never");
    } else if (offering) {
        x += draw_hint(x, 628, "A", "Update");
        draw_hint(x, 628, "B", "Not now");
    } else if (confirming) {
        x += draw_hint(x, 628, "A", "Reboot now");
        draw_hint(x, 628, "B", "Later");
    } else {
        x += draw_hint(x, 628, "A", "Select");
        draw_hint(x, 628, "B", "Exit");
    }

    gfx_end();
}

int main(int argc, char **argv) {
    // argv[0] is where hbmenu loaded this NRO from: the file an update replaces.
    openpak_update_self(argc > 0 ? argv[0] : NULL);
    romfsInit();          // the CA bundle we ship lives in romfs:/
    plInitialize(PlServiceType_User);
    openpak_report_init("sdl");
    // A display that will not start is saved and offered on the next launch.
    if (!gfx_init()) { openpak_report_failure("startup", NULL, "Could not start the display"); plExit(); return 1; }
    if (!txt_init()) {
        openpak_report_failure("startup", NULL, "Could not load the system font");
        gfx_exit(); plExit(); return 1;
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    char ip[64];
    char status[192] = "";
    bool on = openpak_enabled();
    // Online check first, so what the tool would install is openpak.org's current set.
    render(OPENPAK_SERVER, on, on ? IT_OPENPAK : IT_NINTENDO, "Checking openpak.org for network updates...", false, false, false);
    openpak_network_refresh(net_note, sizeof(net_note));
    load_ip(ip, sizeof(ip));
    bool confirm_reboot = false;
    // The release check ran inside the refresh above; the offer waits behind the report question.
    bool offer_update = openpak_update_tag()[0] != '\0', update_asked = false;
    int sel = on ? IT_OPENPAK : IT_NINTENDO;
    // A console that was on OpenPak and has since been updated to a firmware it does not know is in an
    // unsupported state: warn at launch and point at the fix (select Nintendo, which
    // removes cleanly on any firmware).
    {
        char fw[32] = "";
        if (on && !openpak_firmware_supported(fw, sizeof(fw)))
            snprintf(status, sizeof(status),
                     "Warning: OpenPak is on but this console runs %s, not " OPENPAK_FIRMWARE
                     ". Select Nintendo to remove.", fw);
    }
    // An older OpenPak boot package: 1.11.2 cannot boot firmware 23, and one without the dns.mitm fix
    // lets firmware 23's system services reach Nintendo. Selecting OpenPak again rebuilds it.
    if (on && !status[0]) {
        int age = openpak_package_outdated();
        if (age == 2)
            snprintf(status, sizeof(status), "%s", "OpenPak's boot package is from Atmosphere 1.11.2: install "
                     "Atmosphere 1.12.0 and select OpenPak again before updating the firmware.");
        else if (age == 1)
            snprintf(status, sizeof(status), "%s", "OpenPak's boot package is out of date: select OpenPak again "
                     "and restart.");
    }
    // Reports saved by an earlier run (a crash, a failed setup) are offered now.
    bool asking = openpak_report_offer(status, sizeof(status), "OpenPak saved a problem report last time.");
    if (!status[0] && redirects_changed(ip)) snprintf(status, sizeof(status), "%s", REDIRECTS_CHANGED);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        char err[128] = "";

        if (asking) {
            if (down & (HidNpadButton_A | HidNpadButton_X)) {
                if (down & HidNpadButton_X) openpak_report_set_consent(REPORT_ALWAYS);
                snprintf(status, sizeof(status), "Sending report...");
                render(ip, on, sel, status, false, false, false);
                openpak_report_send_all(status, sizeof(status));
                asking = false;
            } else if (down & HidNpadButton_B) {
                openpak_report_discard_all();
                snprintf(status, sizeof(status), "Report discarded.");
                asking = false;
            } else if (down & HidNpadButton_Y) {
                openpak_report_set_consent(REPORT_NEVER);
                snprintf(status, sizeof(status), "Reports turned off. Nothing was sent.");
                asking = false;
            }
        } else if (offer_update) {
            if (down & HidNpadButton_A) {
                snprintf(status, sizeof(status), "Downloading OpenPak %s...", openpak_update_tag());
                render(ip, on, sel, status, false, false, false);
                char msg[192] = "";
                int done = openpak_update_apply(msg, sizeof(msg));
                offer_update = false;
                snprintf(status, sizeof(status), "%s", msg);
                if (done > 0) {                          // the new build takes over as this exits
                    render(ip, on, sel, status, false, false, false);
                    break;
                }
                if (done < 0) openpak_report_failure("update", NULL, msg);
            } else if (down & HidNpadButton_B) {
                offer_update = false;
                status[0] = '\0';
            }
        } else if (confirm_reboot) {
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
            // OpenPak is built for one firmware; installing on another is refused before a
            // single file is written. Removal (Nintendo) is allowed on any firmware, so a
            // console updated after installing can always clean up.
            char fw[32] = "";
            if (want_openpak && !openpak_firmware_supported(fw, sizeof(fw))) {
                snprintf(status, sizeof(status),
                         "OpenPak requires firmware " OPENPAK_FIRMWARE " — this console runs %s", fw);
                render(ip, on, sel, status, confirm_reboot, asking, offer_update);
                continue;
            }
            // Selecting the network already in use re-applies it rather than refusing: that is
            // how a console picks up a new CA or new host rules after the tool is updated.
            bool again = (want_openpak == on);
            bool applied = want_openpak ? openpak_system_install(err, sizeof(err))
                                        : openpak_system_remove(err, sizeof(err));
            if (applied) {
                if (want_openpak) {
                    int bundles = openpak_ca_install(err, sizeof(err));
                    int patches = openpak_patches_install();
                    // The News module only fetches channels it follows.
                    openpak_news_subscribe();
                    applied = bundles > 0 && patches > 0;
                    if (!applied) snprintf(err, sizeof(err), "Could not finish certificate and system setup");
                } else {
                    openpak_ca_remove(err, sizeof(err));
                    openpak_patches_remove();
                    openpak_news_unsubscribe();
                }
            }
            if (applied) applied = want_openpak ? openpak_enable(ip, err, sizeof(err))
                                               : openpak_disable(err, sizeof(err));
            if (applied) {
                on = want_openpak;
                confirm_reboot = true;
                snprintf(status, sizeof(status), again ? "%s setup updated. Reboot to apply?"
                                                       : "%s selected. Reboot to apply?",
                         want_openpak ? "OpenPak" : "Nintendo");
                // A warning, not a failure: everything else works, only store titles will not launch.
                char kip[64];
                if (want_openpak && openpak_loader_override(kip, sizeof(kip)))
                    snprintf(loader_note, sizeof(loader_note),
                             "Store titles will not launch: /atmosphere/kips/%s replaces OpenPak's Loader.", kip);
                else
                    loader_note[0] = '\0';
                // Never fails the switch either: a file left as it is only gets a note.
                album_ok = want_openpak ? openpak_album_install(album_note, sizeof(album_note))
                                        : openpak_album_remove(album_note, sizeof(album_note));
                if (album_ok) snprintf(album_note, sizeof(album_note), "%s", want_openpak ? ALBUM_NOTE : "");
            } else {
                snprintf(status, sizeof(status), "Failed: %s", err);
                openpak_report_failure(want_openpak ? "install" : "remove", NULL, err);
                asking = openpak_report_offer(status, sizeof(status), status);
            }
        }
        // The update question claims the line once the report question is answered: it is the
        // one prompt that may fix whatever else the line would have said.
        if (offer_update && !asking && !update_asked) {
            snprintf(status, sizeof(status), "OpenPak %s is available. Download and restart?",
                     openpak_update_tag());
            update_asked = true;
        }
        render(ip, on, sel, status, confirm_reboot, asking, offer_update);
    }

    txt_exit();
    gfx_exit();
    plExit();
    romfsExit();
    return 0;
}
