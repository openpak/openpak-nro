// openpak.nro — flips a CFW Switch between Nintendo's servers and OpenPak's.
//
// Enable writes a marked block of dns_mitm host entries; disable removes it and leaves
// the console exactly as it was. The server address is kept in /switch/openpak/server.txt
// and edited with the system keyboard.
//
// Written from scratch against libnx/SDL2: no code from any other homebrew.
#include "hosts.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>
#include <stdarg.h>

#define CONFIG_DIR  "/switch/openpak"
#define CONFIG_PATH CONFIG_DIR "/server.txt"
#define DEFAULT_IP  "10.0.0.1"

static const SDL_Color WHITE = {236, 239, 244, 255};
static const SDL_Color DIM   = {140, 148, 164, 255};
static const SDL_Color GREEN = {110, 200, 140, 255};
static const SDL_Color AMBER = {230, 180, 90, 255};

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

// System keyboard, so the address can be changed without a PC.
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

static void draw_text(SDL_Renderer *r, TTF_Font *f, int x, int y, SDL_Color c, const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    SDL_Surface *s = TTF_RenderUTF8_Blended(f, line, c);
    if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_Rect dst = {x, y, s->w, s->h};
    SDL_FreeSurface(s);
    if (t) {
        SDL_RenderCopy(r, t, NULL, &dst);
        SDL_DestroyTexture(t);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    romfsInit();
    plInitialize(PlServiceType_User);
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    TTF_Init();
    SDL_GameControllerOpen(0);

    // The console's own font: nothing to ship, and it matches the system UI.
    PlFontData font_data;
    TTF_Font *font = NULL, *big = NULL;
    if (R_SUCCEEDED(plGetSharedFontByType(&font_data, PlSharedFontType_Standard))) {
        font = TTF_OpenFontRW(SDL_RWFromMem(font_data.address, font_data.size), 1, 26);
        big  = TTF_OpenFontRW(SDL_RWFromMem(font_data.address, font_data.size), 1, 44);
    }

    SDL_Window *win = SDL_CreateWindow("openpak", 0, 0, 1280, 720, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

    char ip[64];
    load_ip(ip, sizeof(ip));
    char status[160] = "";
    bool on = openpak_enabled();

    while (appletMainLoop()) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type != SDL_CONTROLLERBUTTONDOWN) continue;
            char err[128] = "";
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_A:
                if (openpak_enable(ip, err, sizeof(err))) {
                    on = true;
                    snprintf(status, sizeof(status), "OpenPak enabled — reboot the console to apply.");
                } else {
                    snprintf(status, sizeof(status), "Failed: %s", err);
                }
                break;
            case SDL_CONTROLLER_BUTTON_B:
                if (openpak_disable(err, sizeof(err))) {
                    on = false;
                    snprintf(status, sizeof(status), "Nintendo restored — reboot the console to apply.");
                } else {
                    snprintf(status, sizeof(status), "Failed: %s", err);
                }
                break;
            case SDL_CONTROLLER_BUTTON_X:
                if (prompt_ip(ip, sizeof(ip))) {
                    save_ip(ip);
                    snprintf(status, sizeof(status), on ? "Address saved — press A to re-apply." : "Address saved.");
                }
                break;
            case SDL_CONTROLLER_BUTTON_START:
                goto done;
            default:
                break;
            }
        }

        SDL_SetRenderDrawColor(ren, 16, 18, 24, 255);
        SDL_RenderClear(ren);
        if (big && font) {
            draw_text(ren, big, 80, 70, WHITE, "OpenPak");
            draw_text(ren, font, 80, 140, DIM, "Network selector for this console");

            draw_text(ren, font, 80, 240, DIM, "Currently routing to");
            draw_text(ren, big, 80, 275, on ? GREEN : AMBER, on ? "OpenPak" : "Nintendo");
            draw_text(ren, font, 80, 345, DIM, "Server address");
            draw_text(ren, font, 80, 380, WHITE, "%s", ip);
            draw_text(ren, font, 80, 430, DIM, "%d host rules", openpak_hosts_count);

            draw_text(ren, font, 80, 540, WHITE, "A  switch to OpenPak      B  back to Nintendo");
            draw_text(ren, font, 80, 578, WHITE, "X  change address         +  exit");
            if (status[0]) draw_text(ren, font, 80, 640, AMBER, "%s", status);
        }
        SDL_RenderPresent(ren);
    }

done:
    if (font) TTF_CloseFont(font);
    if (big) TTF_CloseFont(big);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();
    plExit();
    romfsExit();
    return 0;
}
