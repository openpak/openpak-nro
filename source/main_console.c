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
#define DEFAULT_IP  "10.0.0.1"

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
    swkbdConfigSetInitialText(&kbd, ip);
    char out[64] = {0};
    Result rc = swkbdShow(&kbd, out, sizeof(out));
    swkbdClose(&kbd);
    if (R_FAILED(rc) || !out[0]) return false;
    snprintf(ip, len, "%s", out);
    return true;
}

static void draw(const char *ip, bool on, const char *status) {
    consoleClear();
    printf("\x1b[1;1H\x1b[36mOpenPak\x1b[0m — network selector for this console\n\n");
    printf("  Routing to     : %s%s\x1b[0m\n", on ? "\x1b[32m" : "\x1b[33m", on ? "OpenPak" : "Nintendo");
    printf("  Server address : %s\n", ip);
    printf("  Host rules     : %d\n\n", openpak_hosts_count);
    printf("  [A] switch to OpenPak    [B] back to Nintendo\n");
    printf("  [X] change address       [+] exit\n\n");
    if (status[0]) printf("  \x1b[33m%s\x1b[0m\n", status);
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
    char status[160] = "";
    bool on = openpak_enabled();
    draw(ip, on, status);

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) break;

        char err[128] = "";
        if (down & HidNpadButton_A) {
            if (openpak_enable(ip, err, sizeof(err)))
                on = true, snprintf(status, sizeof(status), "OpenPak enabled — reboot to apply.");
            else
                snprintf(status, sizeof(status), "Failed: %s", err);
        } else if (down & HidNpadButton_B) {
            if (openpak_disable(err, sizeof(err)))
                on = false, snprintf(status, sizeof(status), "Nintendo restored — reboot to apply.");
            else
                snprintf(status, sizeof(status), "Failed: %s", err);
        } else if (down & HidNpadButton_X) {
            if (prompt_ip(ip, sizeof(ip))) {
                save_ip(ip);
                snprintf(status, sizeof(status), on ? "Address saved — press A to re-apply." : "Address saved.");
            }
            consoleInit(NULL);   // the keyboard applet takes the framebuffer with it
        } else {
            consoleUpdate(NULL);
            continue;
        }
        draw(ip, on, status);
    }

    consoleExit(NULL);
    return 0;
}
