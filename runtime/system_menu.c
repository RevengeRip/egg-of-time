/* System Settings menu: window mode, internal resolution, aspect ratio, framerate, V-Sync, graphics filter,
 * mods (display.c holds and applies them). SELECT opens it from the game's Game Settings screen (the button
 * single screen mode relabels "System settings"), the title screen's menu or while the field is paused; B or
 * SELECT closes it, and so does the game leaving that place (the title idling into the intro, say). Closed
 * from the pause, it also presses START for the game, which ends the pause. Up/Down pick a setting,
 * Left/Right change it (held: repeats); changes apply at once.
 *
 * The look follows the game's Game Settings screen, drawn by the runtime so it is the same wherever the menu
 * opens: a striped dark backdrop, light bevelled bars (header, footer), dark bevelled buttons, the
 * highlighted row's label lit and its value framed in the game's cursor teal. */
#include <SDL3/SDL.h>

#include "display.h"
#include "rt.h"
#include "single_screen.h"
#include "system_menu.h"
#include "ui_font.h"
#include "video.h"

#define REPEAT_DELAY_MS 400
#define REPEAT_MS 110
#define RESUME_MS 100 /* START held for the game after closing from the pause */

static const char *const descriptions[SET_COUNT] = {
    [SET_WINDOW] = "How the game window is shown.",
    [SET_RESOLUTION] = "Size the screens are drawn at.",
    [SET_ASPECT] = "Wider shows more of the world.",
    [SET_FRAMERATE] = "Frames shown per second. Game: 60.",
    [SET_VSYNC] = "Waits for the display: no tearing.",
    [SET_FILTER] = "How the pixels are scaled up.",
    [SET_COLOR] = "Deepens colors toward the SNES.",
    [SET_MODS] = "Replacement movies and music (mods).",
};

static int place, cursor; /* place: where it was opened (SS_MENU_*), SS_MENU_NONE when closed */
static uint32_t prev_keys, blocked; /* blocked: held since the menu closed, kept from the game until released */
static Uint64 repeat_at, resume_until;

uint32_t sysmenu_input(uint32_t keys) {
    uint32_t pressed = keys & ~prev_keys;
    Uint64 now = SDL_GetTicks();
    prev_keys = keys;
    if (place == SS_MENU_NONE) {
        if ((pressed & KEY_SELECT) && (place = ss_menu_place()) != SS_MENU_NONE) {
            cursor = 0;
            return 0;
        }
        blocked &= keys;
        return (keys & ~blocked) | (now < resume_until ? KEY_START : 0);
    }
    uint32_t dirs = keys & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT), act = pressed;
    if (pressed & dirs) {
        repeat_at = now + REPEAT_DELAY_MS;
    } else if (dirs && now >= repeat_at) {
        act |= dirs;
        repeat_at = now + REPEAT_MS;
    }
    int left = ss_menu_place() != place; /* the game moved on */
    if ((act & (KEY_B | KEY_SELECT)) || left) {
        if (place == SS_MENU_PAUSE && !left) resume_until = now + RESUME_MS;
        place = SS_MENU_NONE;
        blocked = keys;
        return 0;
    }
    if (act & KEY_UP) cursor = (cursor + SET_COUNT - 1) % SET_COUNT;
    if (act & KEY_DOWN) cursor = (cursor + 1) % SET_COUNT;
    if (act & (KEY_LEFT | KEY_RIGHT)) {
        int n = display_setting_count(cursor);
        display_setting_set(cursor, (display_setting_get(cursor) + ((act & KEY_RIGHT) ? 1 : n - 1)) % n);
    }
    return 0;
}

int sysmenu_is_open(void) { return place != SS_MENU_NONE; }

/* ---- drawing ---- */

typedef struct Rect { int x, y, w, h; } Rect;
typedef struct Rgb { int r, g, b; } Rgb;
/* A bevelled panel as the game draws them: outer edge, bright then dark rim, inner line, and a noisy fill
 * shading from top to bottom (colours measured on Game Settings). */
typedef struct Style { Rgb edge, rim_lit, rim_shade, inner_lit, inner_shade, fill_top, fill_bottom; int noise; } Style;
static const Style BAR = {{57, 66, 74}, {206, 206, 214}, {57, 66, 74}, {165, 165, 173}, {99, 107, 115},
                          {123, 132, 140}, {57, 66, 74}, 16};
static const Style BUTTON = {{8, 16, 16}, {99, 99, 99}, {8, 16, 16}, {82, 82, 82}, {49, 49, 57},
                             {57, 62, 66}, {28, 34, 36}, 9};
static const Style BUTTON_LIT = {{49, 49, 49}, {214, 214, 214}, {49, 49, 49}, {181, 181, 181}, {140, 140, 140},
                                 {140, 140, 148}, {90, 99, 107}, 16};

static uint32_t rgb(int r, int g, int b) {
    r = SDL_clamp(r, 0, 255), g = SDL_clamp(g, 0, 255), b = SDL_clamp(b, 0, 255);
    return 0xFF000000u | (uint32_t)b << 16 | (uint32_t)g << 8 | (uint32_t)r;
}

static uint32_t noise(int x, int y) { /* fixed per pixel, so the texture does not shimmer */
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void panel(uint32_t *s, Rect r, const Style *st) {
    for (int y = 0; y < r.h; y++) {
        for (int x = 0; x < r.w; x++) {
            int left = x, top = y, right = r.w - 1 - x, bottom = r.h - 1 - y;
            int d = SDL_min(SDL_min(left, top), SDL_min(right, bottom));
            int lit = SDL_min(left, top) <= SDL_min(right, bottom);
            Rgb c;
            if (d == 0) c = st->edge;
            else if (d == 1) c = lit ? st->rim_lit : st->rim_shade;
            else if (d == 2) c = lit ? st->inner_lit : st->inner_shade;
            else {
                int n = (int)(noise(r.x + x, r.y + y) % (uint32_t)(2 * st->noise + 1)) - st->noise;
                c.r = st->fill_top.r + (st->fill_bottom.r - st->fill_top.r) * y / r.h + n;
                c.g = st->fill_top.g + (st->fill_bottom.g - st->fill_top.g) * y / r.h + n;
                c.b = st->fill_top.b + (st->fill_bottom.b - st->fill_top.b) * y / r.h + n;
            }
            s[(r.y + y) * SCREEN_W + r.x + x] = rgb(c.r, c.g, c.b);
        }
    }
}

static void frame(uint32_t *s, Rect r, uint32_t color) {
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            if (x - r.x < 2 || r.x + r.w - x <= 2 || y - r.y < 2 || r.y + r.h - y <= 2) s[y * SCREEN_W + x] = color;
        }
    }
}

/* The game's B button sign: a white disc with a dark B. */
static void b_sign(uint32_t *s, int cx, int cy) {
    for (int y = -8; y <= 8; y++) {
        for (int x = -8; x <= 8; x++) {
            int d = x * x + y * y;
            if (d <= 64) s[(cy + y) * SCREEN_W + cx + x] = d > 49 ? rgb(16, 16, 16) : rgb(240, 240, 240);
        }
    }
    ui_draw_text_flat(s, "B", cx - 1, cy - 3, rgb(24, 24, 24));
}

#define WHITE rgb(255, 255, 255)
#define GREY rgb(176, 176, 176)
#define YELLOW rgb(248, 240, 88) /* the game's chosen value */
#define TEAL rgb(24, 198, 173)   /* the game's cursor frame */

#define ROW_Y 27
#define ROW_PITCH 17
#define ROW_H 16

void sysmenu_draw(uint32_t *s) {
    for (int y = 0; y < SCREEN_H; y++) { /* backdrop: the game's brushed dark stripes */
        for (int x = 0; x < SCREEN_W; x++) s[y * SCREEN_W + x] = (y & 1) ? rgb(24, 24, 24) : rgb(16, 16, 16);
    }
    panel(s, (Rect){0, 0, 256, 24}, &BAR);
    const char *title = "System Settings";
    ui_draw_text(s, title, (SCREEN_W - ui_text_width(title)) / 2, 8, WHITE);

    for (int i = 0; i < SET_COUNT; i++) {
        int y = ROW_Y + i * ROW_PITCH, sel = i == cursor;
        Rect lr = {6, y, 112, ROW_H}, vr = {122, y, 128, ROW_H};
        panel(s, lr, sel ? &BUTTON_LIT : &BUTTON);
        panel(s, vr, &BUTTON);
        if (sel) frame(s, vr, TEAL);
        ui_draw_text(s, display_setting_title(i), lr.x + 6, y + 4, sel ? WHITE : GREY);
        const char *v = display_setting_label(i, display_setting_get(i));
        ui_draw_text(s, v, vr.x + (vr.w - ui_text_width(v)) / 2, y + 4, sel ? YELLOW : WHITE);
        if (sel) {
            ui_draw_text(s, "<", vr.x + 5, y + 4, WHITE);
            ui_draw_text(s, ">", vr.x + vr.w - 8, y + 4, WHITE);
        }
    }

    panel(s, (Rect){0, 166, 222, 26}, &BAR);
    ui_draw_text(s, descriptions[cursor], 8, 175, WHITE);
    panel(s, (Rect){224, 166, 32, 26}, &BAR);
    b_sign(s, 240, 179);
}
