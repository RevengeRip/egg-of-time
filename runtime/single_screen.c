/* Single screen mode: the boot, title and Game Settings screens, which the DS spreads over both screens, laid
 * out on the main (top) screen, for devices with one screen. Off (EGG_SINGLE_SCREEN=0) the main screen is the
 * top screen.
 *
 * The running scene is the game's own: Scene::run (main 0x0202514c), which every scene runs in, stores the
 * scene's secondary base (scene + 8) at 0x0207088c (func 0x02025d30), where the VBlank handler finds it. That
 * base's vtable names the scene class. Being game memory, it is right after loading a save state too.
 *   - Logos (ov007): first "Licensed by Nintendo" on the bottom screen alone, then Square Enix in the middle
 *     of the top screen and Mobiclip in the middle of the bottom one, on the same plain backdrop. Shown: the
 *     bottom screen while the top one is blank, then both logos stacked.
 *   - Title (ov007): logo and pendulum on the top screen. The bottom screen shows "Touch to Start" (title
 *     state 8), with the copyright once the logo has formed; a touch or START opens the menu (phase 7), the
 *     buttons (New Game...) on the bottom screen. The main screen gets the copyright, "Press any button to
 *     start" (blinking with the game's own prompt) and then the menu buttons, left of the pendulum and
 *     right above the copyright; any button stands in for the touch. In the menu, SELECT also opens the
 *     runtime's menu (not shown on screen).
 *   - Game Settings (ov007, after New Game): the options on the bottom screen, the highlighted one's
 *     description on the top screen. The main screen shows Battle Mode and Movies with the description under
 *     the highlighted one; Game Mode is left out (single screen mode always plays Classic) and SELECT reads
 *     "System settings" instead of the game's "Defaults": it opens the runtime's menu (system_menu.c).
 *   - Name entry (ov012): the character on a patch of grass on the top screen, the keyboard, name and
 *     portrait filling the bottom screen. The main screen shows the top screen while the character appears
 *     and after the name is accepted (the character's animation), the bottom screen while the name is entered.
 *   - Everything else, the intro movie included, shows the top screen as it is; while the field is paused,
 *     with "Press SELECT to open System Settings" at the bottom (the menu opens from there too). */
#include <SDL3/SDL.h>

#include "dispatch.h"
#include "rt.h"
#include "single_screen.h"
#include "ui_font.h"
#include "video.h"

enum { SCENE_OTHER, SCENE_LOGOS, SCENE_TITLE, SCENE_SETTINGS, SCENE_NAMING, SCENE_FIELD };
typedef struct Scene { int kind; uint32_t obj; } Scene;
static uint32_t title_held; /* keys held since before the title started: not a press */
#define TITLE_STATE 0x4a  /* title scene: current state (halfword); +0x48 holds the requested one */
#define TITLE_WAITING 8   /* "Touch to Start", until the phase is the menu's */
#define TITLE_PHASE 0x38b6 /* title scene: phase (halfword): 0 and 3 the logo forming, 5 formed, 7 menu, 0x10 leaving */
#define TITLE_MENU 7      /* a touch before the logo has formed opens it through states 0-0x1e, one after in state 8 */
#define OBJ 0x1000u       /* DISPCNT: sprites on */
#define SETTINGS_ROW 0x3f00       /* Game Settings: cursor row (byte); +0x3f01 the row shown, +0x3f02 the column */
#define SETTINGS_ROW_SHOWN 0x3f01
#define NAMING_STATE 0x4e48 /* name entry: state (word): 0 the character and keyboard fading in, 1 entering, 2 accepted */
#define NAMING_ENTERING 1

#define CURRENT_SCENE 0x0207088cu /* the running scene's secondary base (scene + 8), set by Scene::run */

static Scene current(void) {
    Scene sc = {SCENE_OTHER, 0};
    uint32_t base = rt_r32(CURRENT_SCENE);
    if (!EGG_SINGLE_SCREEN || base >> 24 != 0x02) return sc;
    sc.obj = base - 8;
    uint32_t vt = rt_r32(base); /* the secondary base's vtable */
    if (rt_overlay_resident(7)) {
        if (vt == 0x0212ad68u) sc.kind = SCENE_LOGOS;
        else if (vt == 0x0212aed4u) sc.kind = SCENE_TITLE;
        else if (vt == 0x0212b300u) sc.kind = SCENE_SETTINGS;
    }
    if (vt == 0x02156880u && rt_overlay_resident(12)) sc.kind = SCENE_NAMING;
    if (vt == 0x02115558u && rt_overlay_resident(0)) sc.kind = SCENE_FIELD; /* ov000: towns, maps, battles */
    return sc;
}

/* The bottom screen's engine registers (POWCNT1 bit 15: engine A on the top screen). */
static uint32_t bottom_io(void) { return (io_get16(0x304) & 0x8000) ? 0x1000u : 0; }

static int title_waiting(Scene sc) {
    return sc.kind == SCENE_TITLE && rt_r16(sc.obj + TITLE_STATE) == TITLE_WAITING &&
           rt_r16(sc.obj + TITLE_PHASE) < TITLE_MENU;
}

/* Up/Down in Game Settings: the direction the game gets for the press being held (0: not held). */
static uint32_t settings_up, settings_down;

static uint32_t settings_dir(uint32_t keys, uint32_t key, uint32_t *held_as, int wraps, uint32_t other) {
    if (!(keys & key)) {
        *held_as = 0;
        return keys;
    }
    if (!*held_as) *held_as = wraps ? other : key; /* decided when the press starts, kept while it is held */
    return (keys & ~key) | *held_as;
}

/* Game Settings without its Game Mode row (rows 0 Game Mode, 1 Battle Mode, 2 Movies): the cursor leaves
 * row 0 (a pulsed Down, so the scene sees a fresh press once it takes input), and Up/Down wrap between the
 * two other rows. SELECT, the game's "Defaults", is kept for "System settings" (not implemented yet). */
static uint32_t settings_keys(Scene sc, uint32_t keys) {
    keys &= ~KEY_SELECT;
    int row = rt_r8(sc.obj + SETTINGS_ROW);
    if (row == 0) {
        keys &= ~(KEY_UP | KEY_DOWN);
        return rt_frame_count & 2 ? keys | KEY_DOWN : keys;
    }
    keys = settings_dir(keys, KEY_UP, &settings_up, row == 1, KEY_DOWN);
    return settings_dir(keys, KEY_DOWN, &settings_down, row == 2, KEY_UP);
}

uint32_t ss_game_keys(uint32_t keys) {
    static int seen = SCENE_OTHER;
    Scene sc = current();
    if (sc.kind != seen) { /* a scene started */
        seen = sc.kind;
        title_held = keys;
        settings_up = settings_down = 0;
    }
    if (sc.kind == SCENE_SETTINGS) return settings_keys(sc, keys);
    if (!title_waiting(sc)) return keys;
    title_held &= keys;
    return keys & ~title_held ? keys | KEY_START : keys;
}

/* ---- composition ---- */

#define BLACK 0xFF000000u

/* Copies the non-black pixels of rows [y0, y1) of src into dst, moved by (dx, dy). */
static void overlay(uint32_t *dst, const uint32_t *src, int y0, int y1, int dx, int dy) {
    for (int y = y0; y < y1; y++) {
        int ty = y + dy;
        if (ty < 0 || ty >= SCREEN_H) continue;
        for (int x = 0; x < SCREEN_W; x++) {
            int tx = x + dx;
            uint32_t p = src[y * SCREEN_W + x];
            if (p != BLACK && tx >= 0 && tx < SCREEN_W) dst[ty * SCREEN_W + tx] = p;
        }
    }
}

/* Title layout on the main screen: the free area left of the resting pendulum. */
#define FREE_CX 95       /* centre column */
#define FREE_CY 130      /* centre row */
#define MENU_DX (-32)    /* bottom-screen buttons (centred at column 127) moved into it... */
#define MENU_Y1 165      /* ...ending right above the copyright (main rows 166-188), over the logo's lower part */
#define COPYRIGHT_Y0 156 /* bottom-screen rows holding the copyright (BG2)... */
#define COPYRIGHT_Y1 186
#define COPYRIGHT_DY 6   /* ...moved down to the main screen's last rows */

static void compose_title(uint32_t *main, const uint32_t *top, const uint32_t *bottom) {
    memcpy(main, top, SCREEN_W * SCREEN_H * 4);
    overlay(main, bottom, COPYRIGHT_Y0, COPYRIGHT_Y1, 0, COPYRIGHT_DY);
    Scene sc = current();
    /* States before the waiting one set the bottom screen up, "Touch to Start" included: no menu yet. */
    if (sc.kind == SCENE_TITLE && rt_r16(sc.obj + TITLE_STATE) < TITLE_WAITING) return;
    if (title_waiting(sc)) {
        uint32_t io = bottom_io();
        if (!(io_get32(io) & OBJ)) return; /* the game's prompt blinks by turning its sprites off */
        /* Follow the bottom screen's fade (master brightness, darken mode). */
        uint16_t mb = io_get16(io + 0x6C);
        int f = (mb >> 14) == 2 ? (mb & 31) : 0;
        if (f > 16) f = 16;
        uint32_t v = 255u * (uint32_t)(16 - f) / 16;
        const char *prompt = "Press any button to start";
        ui_draw_text(main, prompt, FREE_CX - ui_text_width(prompt) / 2, FREE_CY - 4, BLACK | v << 16 | v << 8 | v);
        return;
    }
    /* Menu: the buttons, wherever the game puts them above the copyright, in the free area. They don't fit
     * between the logo and the copyright, so they end above the copyright and cover part of the logo. */
    int first = -1, last = -1;
    for (int y = 0; y < COPYRIGHT_Y0; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            if (bottom[y * SCREEN_W + x] != BLACK) {
                if (first < 0) first = y;
                last = y;
                break;
            }
        }
    }
    /* Each row from its first to its last lit pixel, black included: the black outlines of the labels must
     * hide the logo under them. */
    for (int y = first; first >= 0 && y <= last; y++) {
        const uint32_t *src = &bottom[y * SCREEN_W];
        int x0 = 0, x1 = SCREEN_W - 1;
        while (x0 <= x1 && src[x0] == BLACK) x0++;
        while (x1 >= x0 && src[x1] == BLACK) x1--;
        int ty = y + MENU_Y1 - (last + 1);
        if (ty < 0) continue;
        for (int x = x0; x <= x1; x++) {
            if (x + MENU_DX >= 0 && x + MENU_DX < SCREEN_W) main[ty * SCREEN_W + x + MENU_DX] = src[x];
        }
    }
}

/* Game Settings: top screen, the highlighted option's title and description box; bottom screen, the options
 * (Game Mode, Battle Mode, Movies), SELECT "Defaults" / START "Accept" and the footer. The main screen stacks
 * full-width bands of both: header, Battle Mode, Movies with the description box under the highlighted one,
 * the buttons with SELECT relabelled, footer (26 + 27 + 27 + 66 + 20 + 26 = 192 rows). */
typedef struct Band { int screen, y0, h; } Band;
static const Band SET_HEADER = {0, 0, 26}, SET_BATTLE = {1, 58, 27}, SET_MOVIES = {1, 98, 27},
                  SET_DESC = {0, 119, 66}, SET_BUTTONS = {1, 142, 20}, SET_FOOTER = {1, 166, 26};
#define SELECT_LABEL_X0 46 /* the "Defaults" label (x 48-84 of the SELECT button, x 16-119)... */
#define SELECT_LABEL_X1 117
#define SELECT_LABEL_Y0 5  /* ...rows 147-156 of the bottom screen, painted over with... */
#define SELECT_LABEL_Y1 15
#define SELECT_FILL_X 86   /* ...the plain button texture at x 86-115 */
#define SELECT_FILL_W 30

static int put_band(uint32_t *main, int y, Band b, const uint32_t *top, const uint32_t *bottom) {
    memcpy(&main[y * SCREEN_W], &(b.screen ? bottom : top)[b.y0 * SCREEN_W], (size_t)b.h * SCREEN_W * 4);
    return y + b.h;
}

static void compose_settings(Scene sc, uint32_t *main, const uint32_t *top, const uint32_t *bottom) {
    int movies = rt_r8(sc.obj + SETTINGS_ROW_SHOWN) == 2;
    int y = put_band(main, 0, SET_HEADER, top, bottom);
    y = put_band(main, y, SET_BATTLE, top, bottom);
    if (!movies) y = put_band(main, y, SET_DESC, top, bottom);
    y = put_band(main, y, SET_MOVIES, top, bottom);
    if (movies) y = put_band(main, y, SET_DESC, top, bottom);
    int buttons = y;
    y = put_band(main, y, SET_BUTTONS, top, bottom);
    put_band(main, y, SET_FOOTER, top, bottom);
    for (int r = SELECT_LABEL_Y0; r < SELECT_LABEL_Y1; r++) {
        uint32_t *row = &main[(buttons + r) * SCREEN_W];
        for (int x = SELECT_LABEL_X0; x < SELECT_LABEL_X1; x++) {
            row[x] = row[SELECT_FILL_X + (x - SELECT_LABEL_X0) % SELECT_FILL_W];
        }
    }
    ui_draw_text(main, "System settings", SELECT_LABEL_X0 + 2, buttons + 6, 0xFFFFFFFFu);
}

/* Whether a screen is one plain colour. */
static int blank(const uint32_t *px) {
    for (int i = 1; i < SCREEN_W * SCREEN_H; i++) {
        if (px[i] != px[0]) return 0;
    }
    return 1;
}

/* The field's pause (ov000 func 0x0210155c, START): dims both screens and loops until START, with the byte
 * at *(0x0207b520) + 0x407 set to 0xff meanwhile. */
static int field_paused(void) {
    if (!rt_overlay_resident(0)) return 0;
    uint32_t base = rt_r32(0x0207b520u);
    return base >> 24 == 0x02 && rt_r8(base + 0x407) == 0xff;
}

int ss_in_field(void) { return current().kind == SCENE_FIELD; }

static SDL_AtomicInt menu_place;

int ss_menu_place(void) { return SDL_GetAtomicInt(&menu_place); }

int ss_compose(uint32_t *main, const uint32_t *top, const uint32_t *bottom, const uint32_t *wide, int wide_w) {
    Scene sc = current();
    int paused = EGG_SINGLE_SCREEN && sc.kind == SCENE_FIELD && field_paused();
    int title_menu = sc.kind == SCENE_TITLE && rt_r16(sc.obj + TITLE_PHASE) == TITLE_MENU;
    SDL_SetAtomicInt(&menu_place, sc.kind == SCENE_SETTINGS ? SS_MENU_GAME_SETTINGS
                                  : paused                  ? SS_MENU_PAUSE
                                  : title_menu              ? SS_MENU_TITLE
                                                            : SS_MENU_NONE);
    if (sc.kind == SCENE_FIELD) { /* widescreen: the top screen with its margins */
        memcpy(main, wide, (size_t)wide_w * SCREEN_H * 4);
        if (paused) {
            const char *hint = "Press SELECT to open System Settings";
            ui_draw_text_in(main, wide_w, hint, (wide_w - ui_text_width(hint)) / 2, SCREEN_H - UI_FONT_H - 6, 0xFFFFFFFFu);
        }
        return wide_w;
    }
    switch (sc.kind) {
    case SCENE_LOGOS:
        if (blank(top)) {
            memcpy(main, bottom, SCREEN_W * SCREEN_H * 4);
            break;
        }
        /* Square Enix (top rows 85-107) above Mobiclip (bottom rows 50-141), centred together. */
        for (int y = 0; y < SCREEN_H; y++) {
            const uint32_t *src = y < 60 ? &top[(y + 57) * SCREEN_W] : &bottom[(y - 21) * SCREEN_W];
            memcpy(&main[y * SCREEN_W], src, SCREEN_W * 4);
        }
        break;
    case SCENE_TITLE:
        compose_title(main, top, bottom);
        break;
    case SCENE_SETTINGS:
        compose_settings(sc, main, top, bottom);
        break;
    case SCENE_NAMING:
        memcpy(main, rt_r32(sc.obj + NAMING_STATE) == NAMING_ENTERING ? bottom : top, SCREEN_W * SCREEN_H * 4);
        break;
    default:
        memcpy(main, top, SCREEN_W * SCREEN_H * 4);
        break;
    }
    return SCREEN_W;
}
