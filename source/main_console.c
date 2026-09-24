// Text-mode build of openpak.nro (make UI=console).
//
// Same toggle, no SDL: libnx's console renders straight to the framebuffer, so the NRO is
// a few hundred KB and loads in applet mode, where the SDL build needs the memory of a
// full title takeover.
#include "ca.h"
#include "system.h"
#include "hosts.h"
#include "netfetch.h"
#include "news.h"
#include "installtrust.h"
#include "crash.h"

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
    swkbdConfigSetInitialText(&kbd, ip);
    char out[64] = {0};
    Result rc = swkbdShow(&kbd, out, sizeof(out));
    swkbdClose(&kbd);
    if (R_FAILED(rc) || !out[0]) return false;
    snprintf(ip, len, "%s", out);
    return true;
}

// dns_mitm reads the hosts files at boot, so offer the reboot here rather than sending the
// user to the power menu.
static void reboot_console(void) {
    if (R_FAILED(spsmInitialize())) return;
    spsmShutdown(true);   // true = reboot
    spsmExit();
}

// The whole tool is one choice: which network this console talks to. OpenPak
// installs everything together — hosts, CA, News patches, the store patch.
enum { IT_NINTENDO, IT_OPENPAK, IT_COUNT };

// What the online check at launch had to say ("" = it went through).
static char net_note[128];

static void draw(const char *ip, bool on, int sel, const char *status, bool confirming, bool asking) {
    const char *labels[IT_COUNT] = {"Nintendo", "OpenPak"};
    char values[IT_COUNT][64];
    for (int i = 0; i < IT_COUNT; i++)
        snprintf(values[i], sizeof(values[0]), "%s", ((i == IT_OPENPAK) == on) ? "Active" : "");

    consoleClear();
    printf("\x1b[1;1H \x1b[36mOpenPak\x1b[0m  network selector%*s%s%s\x1b[0m\n\n",
           28, "", on ? "\x1b[32m" : "\x1b[33m", on ? "[ OPENPAK ]" : "[ NINTENDO ]");

    for (int i = 0; i < IT_COUNT; i++) {
        bool cur = i == sel;
        printf("  %s%s %-22s %s%s\x1b[0m\n",
               cur ? "\x1b[7m" : "", cur ? ">" : " ",
               labels[i], values[i],
               cur ? "" : "");
    }

    const openpak_policy *pol = openpak_active_policy();
    printf("\n  %d host rules redirected (%s", pol->count, openpak_source_name(pol->source));
    if (pol->sequence)
        printf(", revision %lld", pol->sequence);
    if (openpak_ceiling_current()->version) printf(", signed ceiling v%lld", openpak_ceiling_current()->version);
    else printf(", built-in ceiling");
    printf(")\n\n");
    if (net_note[0]) printf("  %s\n\n", net_note);
    if (openpak_policy_problem()[0]) printf("  \x1b[33m%s\x1b[0m\n\n", openpak_policy_problem());
    if (asking)
        printf("  \x1b[33m%s\x1b[0m\n\n  [A] send   [B] don't send   [X] always   [Y] never\n", status);
    else if (confirming)
        printf("  \x1b[33m%s\x1b[0m\n\n  [A] reboot now   [B] later\n", status[0] ? status : "Reboot to apply?");
    else {
        if (status[0]) printf("  \x1b[33m%s\x1b[0m\n\n", status);
        else           printf("  Changes apply at boot.\n\n");
        printf("  [Up/Down] move   [A] select   [B] exit\n");
    }
    consoleUpdate(NULL);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    romfsInit();          // the CA bundle we ship lives in romfs:/
    consoleInit(NULL);
    openpak_report_init("console");
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    char ip[64];
    printf("\n  Checking openpak.org for network updates...\n");
    consoleUpdate(NULL);
    openpak_network_refresh(net_note, sizeof(net_note));
    load_ip(ip, sizeof(ip));
    char status[192] = "";
    bool on = openpak_enabled();
    bool confirm_reboot = false;
    int sel = on ? IT_OPENPAK : IT_NINTENDO;
    {
        // Warn a console that is on OpenPak but has been updated off 22.5.0.
        char fw[32] = "";
        if (on && !openpak_firmware_supported(fw, sizeof(fw)))
            snprintf(status, sizeof(status),
                     "Warning: OpenPak is on but firmware is %s, not " OPENPAK_FIRMWARE
                     ". Select Nintendo to remove.", fw);
    }
    // Reports saved by an earlier run (a crash, a failed setup) are offered now.
    bool asking = openpak_report_offer(status, sizeof(status), "OpenPak saved a problem report last time.");
    if (!status[0] && redirects_changed(ip)) snprintf(status, sizeof(status), "%s", REDIRECTS_CHANGED);
    draw(ip, on, sel, status, confirm_reboot, asking);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (!down) { consoleUpdate(NULL); continue; }

        char err[128] = "";
        if (asking) {
            if (down & (HidNpadButton_A | HidNpadButton_X)) {
                if (down & HidNpadButton_X) openpak_report_set_consent(REPORT_ALWAYS);
                draw(ip, on, sel, "Sending report...", false, false);
                openpak_report_send_all(status, sizeof(status));
            } else if (down & HidNpadButton_B) {
                openpak_report_discard_all();
                snprintf(status, sizeof(status), "Report discarded.");
            } else if (down & HidNpadButton_Y) {
                openpak_report_set_consent(REPORT_NEVER);
                snprintf(status, sizeof(status), "Reports turned off. Nothing was sent.");
            } else { consoleUpdate(NULL); continue; }
            asking = false;
            draw(ip, on, sel, status, confirm_reboot, asking);
            continue;
        }
        if (confirm_reboot) {
            if (down & HidNpadButton_A) reboot_console();
            else if (down & HidNpadButton_B) { confirm_reboot = false; status[0] = '\0'; }
            else { consoleUpdate(NULL); continue; }
            draw(ip, on, sel, status, confirm_reboot, asking);
            continue;
        }

        if (down & (HidNpadButton_Up | HidNpadButton_StickLUp))        sel = (sel + IT_COUNT - 1) % IT_COUNT;
        else if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) sel = (sel + 1) % IT_COUNT;
        else if (down & HidNpadButton_B) break;
        else if (down & HidNpadButton_Plus) break;
        else if (down & HidNpadButton_A) {
            bool want_openpak = (sel == IT_OPENPAK);
            // OpenPak installs only on its target firmware; refuse before writing anything.
            // Removal (Nintendo) works on any firmware so a console can always clean up.
            char fw[32] = "";
            if (want_openpak && !openpak_firmware_supported(fw, sizeof(fw))) {
                snprintf(status, sizeof(status),
                         "OpenPak requires firmware " OPENPAK_FIRMWARE " - this console runs %s", fw);
                draw(ip, on, sel, status, confirm_reboot, asking);
                continue;
            }
            // Re-selecting the active network re-applies it, so an updated CA or host list
            // reaches a console that is already switched over.
            bool again = (want_openpak == on);
            char store_note[96] = "";
            bool applied = want_openpak ? openpak_system_install(err, sizeof(err))
                                        : openpak_system_remove(err, sizeof(err));
            if (applied) {
                if (want_openpak) {
                    int bundles = openpak_ca_install(err, sizeof(err));
                    int patches = openpak_patches_install();
                    // The News module only fetches channels it follows.
                    openpak_news_subscribe();
                    // The store-install patch rides along with OpenPak (best-effort note).
                    openpak_store_install(store_note, sizeof(store_note));
                    applied = bundles > 0 && patches > 0;
                    if (!applied) snprintf(err, sizeof(err), "Could not finish certificate and system setup");
                } else {
                    openpak_ca_remove(err, sizeof(err));
                    openpak_patches_remove();
                    openpak_news_unsubscribe();
                    openpak_store_remove();
                }
            }
            if (applied) applied = want_openpak ? openpak_enable(ip, err, sizeof(err))
                                               : openpak_disable(err, sizeof(err));
            if (applied) {
                on = want_openpak;
                confirm_reboot = true;
                snprintf(status, sizeof(status), again ? "%s setup updated. Reboot to apply?%s%s"
                                                       : "%s selected. Reboot to apply?%s%s",
                         want_openpak ? "OpenPak" : "Nintendo",
                         (want_openpak && store_note[0]) ? "  " : "",
                         want_openpak ? store_note : "");
            } else {
                snprintf(status, sizeof(status), "Failed: %s", err);
                openpak_report_failure(want_openpak ? "install" : "remove", NULL, err);
                asking = openpak_report_offer(status, sizeof(status), status);
            }
        }
        draw(ip, on, sel, status, confirm_reboot, asking);
    }

    consoleExit(NULL);
    romfsExit();
    return 0;
}
