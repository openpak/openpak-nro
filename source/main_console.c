// Text-mode build of openpak.nro (make UI=console).
//
// Same toggle, no SDL: libnx's console renders straight to the framebuffer, so the NRO is
// a few hundred KB and loads in applet mode, where the SDL build needs the memory of a
// full title takeover.
#include "hosts.h"

#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>

#define CONFIG_DIR  "/switch/openpak"
#define CONFIG_PATH CONFIG_DIR "/server.txt"
// No default address: there is no IP that is right for someone else's network, and a
// plausible-looking wrong one is worse than an obvious blank.
#define UNSET_LABEL "Not set"

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

enum { IT_NETWORK, IT_ADDRESS, IT_REBOOT, IT_COUNT };

static void draw(const char *ip, bool on, int sel, const char *status, bool confirming) {
    const char *labels[IT_COUNT] = {"Network", "Server address", "Reboot console"};
    char values[IT_COUNT][64];
    snprintf(values[IT_NETWORK], sizeof(values[0]), "%s", on ? "OpenPak" : "Nintendo");
    snprintf(values[IT_ADDRESS], sizeof(values[0]), "%s", ip[0] ? ip : UNSET_LABEL);
    values[IT_REBOOT][0] = '\0';

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
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    char ip[64];
    load_ip(ip, sizeof(ip));
    char status[192] = "";
    bool on = openpak_enabled();
    bool confirm_reboot = false;
    int sel = 0;
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
                    snprintf(status, sizeof(status), on ? "Now pointing at %s.  Reboot to apply?"
                                                        : "Nintendo's servers restored.  Reboot to apply?", ip);
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
                consoleInit(NULL);   // the keyboard applet takes the framebuffer with it
                break;
            case IT_REBOOT:
                confirm_reboot = true;
                snprintf(status, sizeof(status), "Reboot the console now?");
                break;
            default:
                break;
            }
        }
        draw(ip, on, sel, status, confirm_reboot);
    }

    consoleExit(NULL);
    return 0;
}
