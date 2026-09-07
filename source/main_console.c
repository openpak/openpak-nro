// Text-mode build of openpak.nro (make UI=console).
//
// Same toggle, no SDL: libnx's console renders straight to the framebuffer, so the NRO is
// a few hundred KB and loads in applet mode, where the SDL build needs the memory of a
// full title takeover.
#include "ca.h"
#include "hosts.h"

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

// The whole tool is one choice: which network this console talks to.
enum { IT_NINTENDO, IT_OPENPAK, IT_COUNT };

static void draw(const char *ip, bool on, int sel, const char *status, bool confirming) {
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

    printf("\n  %d host rules redirected\n\n", openpak_hosts_count);
    if (confirming)
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
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    char ip[64];
    load_ip(ip, sizeof(ip));
    char status[192] = "";
    bool on = openpak_enabled();
    bool confirm_reboot = false;
    int sel = on ? IT_OPENPAK : IT_NINTENDO;
    draw(ip, on, sel, status, confirm_reboot);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (!down) { consoleUpdate(NULL); continue; }

        char err[128] = "";
        if (confirm_reboot) {
            if (down & HidNpadButton_A) reboot_console();
            else if (down & HidNpadButton_B) { confirm_reboot = false; status[0] = '\0'; }
            else { consoleUpdate(NULL); continue; }
            draw(ip, on, sel, status, confirm_reboot);
            continue;
        }

        if (down & (HidNpadButton_Up | HidNpadButton_StickLUp))        sel = (sel + IT_COUNT - 1) % IT_COUNT;
        else if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) sel = (sel + 1) % IT_COUNT;
        else if (down & HidNpadButton_B) break;
        else if (down & HidNpadButton_Plus) break;
        else if (down & HidNpadButton_A) {
            bool want_openpak = (sel == IT_OPENPAK);
            if (want_openpak == on) {
                snprintf(status, sizeof(status), "Already using %s.", want_openpak ? "OpenPak" : "Nintendo");
            } else if (want_openpak ? openpak_enable(ip, err, sizeof(err))
                                    : openpak_disable(err, sizeof(err))) {
                if (want_openpak) openpak_ca_install(err, sizeof(err));
                else              openpak_ca_remove(err, sizeof(err));
                on = want_openpak;
                confirm_reboot = true;
                snprintf(status, sizeof(status), "Switched to %s.  Reboot to apply?",
                         want_openpak ? "OpenPak" : "Nintendo");
            } else {
                snprintf(status, sizeof(status), "Failed: %s", err);
            }
        }
        draw(ip, on, sel, status, confirm_reboot);
    }

    consoleExit(NULL);
    romfsExit();
    return 0;
}
