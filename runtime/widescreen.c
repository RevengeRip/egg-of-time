/* Widescreen for the field: fills the background map columns the widened view shows beyond the DS screen.
 *
 * The field keeps each background in a 512x256 map used as a ring (64 x 32 tiles, scrolled with BGxHOFS /
 * BGxVOFS), and the game only keeps the 34 columns around the 4:3 view current: when the camera moves one tile,
 * the scroll handler (ov004 0x0213bcb0 and its counterparts) generates the entering column from the room map into
 * a staging buffer (per layer: ov004 0x02137144, 0x02137734, 0x02137c4c) and the frame's upload copies it into the
 * ring. The other columns still hold whatever was there before. So before each frame the runtime writes the
 * columns of the margins itself, from the same data, the way the game would:
 *   - field scroll state *(0x020708e0): +4 camera column, +8 camera row (world tiles; the view is 32 x 24), map
 *     masks per layer at +0x1a (column, row: metatiles), ring origin per layer at +0x8e (column, row): the ring
 *     slot of the camera's tile;
 *   - room map *(0x0207b518): metatile ids, rows 0x100 apart, layer L at +0x1000 + 0x40 * L (64 per row), a 9th
 *     id bit at +0x10c0 (layer 0: bit 0, layer 1: bit 1);
 *   - metatiles *(*(0x020708d4) + 0xc) (layers 0-1) or + 0x10 (layer 2): 2x2 tiles of 3 bytes: tile number (byte 0
 *     and bits 0-1 of byte 1), flips (byte 1 bits 2-3), palette (byte 1 high nibble), priority (byte 2);
 *   - targets (checked against the game's own columns in the Millennial Fair, every cell): layer 2 -> BG0 with its
 *     palette remapped through the table at 0x02058714; layer 0 -> BG2 and layer 1 -> BG3 with the palette as it is;
 *     priority tiles of either go to BG1 instead (layer 1's first); a target without a tile gets entry 0.
 * A layer is filled only when its BG scroll agrees with its ring origin (a layer scrolling on its own, or a map
 * that is not 512x256, is left alone).
 * Battles (ov003 in the slot of the field's ov004) keep the room on screen but reuse its map for their own data,
 * and the field puts it back a moment after the battle ends, so the margins are filled from a copy of the room
 * map taken while the field ran: during the battle and after it, until the map matches the copy again (or the
 * room changes, or 10 s pass). Save states keep the copy. */
#include "dispatch.h"
#include "rt.h"
#include "single_screen.h"
#include "video.h"
#include "widescreen.h"

#define SCROLL 0x020708e0u
#define TILESETS 0x020708d4u
#define ROOM 0x0207b518u
#define PAL_REMAP 0x02058714u

static int ram_ptr(uint32_t p) { return p >> 24 == 0x02; }

/* The room map's metatile rows (room + 0x1000: 0x100 a row) as the field last had them. */
static struct {
    uint8_t rows[0x100 * 0x100];
    uint32_t of;       /* the room map they were copied from (0: none) */
    uint8_t masks[6];  /* and its map masks (the room) */
    int32_t after;     /* frames since a battle ended while the map still differs (0: no battle) */
} room_copy;

static void copy_room_rows(uint32_t scroll, uint32_t room) {
    uint8_t masks[6];
    int rows = 0;
    for (int i = 0; i < 6; i++) masks[i] = (uint8_t)rt_r8(scroll + 0x1a + i);
    for (int L = 0; L < 3; L++) rows = masks[2 * L + 1] + 1 > rows ? masks[2 * L + 1] + 1 : rows;
    uint32_t n = (uint32_t)rows * 0x100;
    if (room_copy.of == room && !memcmp(room_copy.masks, masks, 6)) {
        if (rt_overlay_resident(3)) { /* a battle: keep the field's */
            room_copy.after = 1;
            return;
        }
        if (room_copy.after) {
            uint32_t i = 0;
            while (i < n && room_copy.rows[i] == rt_r8(room + 0x1000 + i)) i++;
            if (i < n && room_copy.after++ < 600) return; /* not put back yet */
            room_copy.after = 0;
        }
    }
    for (uint32_t i = 0; i < n; i++) room_copy.rows[i] = (uint8_t)rt_r8(room + 0x1000 + i);
    room_copy.of = room;
    memcpy(room_copy.masks, masks, 6);
    room_copy.after = 0;
}

void ws_state(RtStateIO *s) {
    if (!rt_state_optional(s, "ws_room_copy", &room_copy, sizeof room_copy) && rt_state_loading(s)) room_copy.of = 0;
}

/* Packed tile (b0 | b1 << 8 | b2 << 16) of layer L at world tile (c, r). */
static uint32_t room_tile(uint32_t scroll, uint32_t tilesets, int L, int c, int r) {
    uint32_t i = ((uint32_t)(c >> 1) & rt_r8(scroll + 0x1a + 2 * L)) + (((uint32_t)(r >> 1) & rt_r8(scroll + 0x1b + 2 * L)) << 8);
    uint32_t id = room_copy.rows[0x40 * L + i];
    if (L < 2) id += ((room_copy.rows[0xc0 + i] >> L) & 1) << 8;
    uint32_t t = rt_r32(tilesets + (L < 2 ? 0xc : 0x10)) + (id * 4 + (c & 1) + 2 * (r & 1)) * 3;
    return rt_r8(t) | rt_r8(t + 1) << 8 | rt_r8(t + 2) << 16;
}

static uint16_t map_entry(int L, uint32_t t) {
    uint32_t b0 = t & 0xFF, b1 = (t >> 8) & 0xFF, pal = b1 >> 4;
    if (L == 2) pal = rt_r8(PAL_REMAP + pal);
    return (uint16_t)(b0 | (b1 & 3) << 8 | ((b1 >> 2) & 1) << 10 | ((b1 >> 3) & 1) << 11 | pal << 12);
}

static void put(int bg, int slot, int row, uint16_t v) {
    uint32_t dispcnt = io_get32(0), cnt = io_get16(0x08 + 2 * bg);
    uint32_t a = 0x06000000u + ((dispcnt >> 27) & 7) * 0x10000u + ((cnt >> 8) & 0x1F) * 0x800u + (uint32_t)(slot >> 5) * 0x800u +
                 (uint32_t)row * 64 + (uint32_t)(slot & 31) * 2;
    uint8_t *p = video_vram_ptr(a);
    if (p) memcpy(p, &v, 2);
}

/* The BG agrees with layer L's ring origin and is a 512x256 text map. */
static int bg_follows(int bg, uint32_t scroll, int L) {
    uint16_t cnt = io_get16(0x08 + 2 * bg);
    return (cnt >> 14) == 1 && ((io_get16(0x10 + 4 * bg) >> 3) & 63) == rt_r8(scroll + 0x8e + 2 * L) &&
           ((io_get16(0x12 + 4 * bg) >> 3) & 31) == rt_r8(scroll + 0x8f + 2 * L);
}

/* The sprite renderer's view (NitroSDK G2D surface of the top screen, 0x02058070: top-left x, y, then width,
 * height, 20.12 fixed point; normally 0, -1, 256, 192). Sprites outside it are culled, and inside it they are
 * placed relative to its top-left. In the field it is widened to the margins, so characters beyond the 4:3 view
 * are drawn; their OAM X then counts from the left margin (ws_sprite_origin). */
#define G2D_VIEW 0x02058070u
#define FX(v) ((uint32_t)((v) * 4096))

/* The field camera's top-left in the room, in pixels (0x020760f8: x, y; BG scroll moves with it). */
#define CAMERA 0x020760f8u

int ws_camera(int *x, int *y) {
    if (!ss_in_field()) return 0;
    *x = (int16_t)rt_r16(CAMERA);
    *y = (int16_t)rt_r16(CAMERA + 2);
    return 1;
}

int ws_sprite_origin(void) { /* the view as widened here, recognised by its shape (also after a state load) */
    int32_t x = (int32_t)rt_r32(G2D_VIEW);
    return x < 0 && x >= -VIDEO_MARGIN_MAX * 4096 && rt_r32(G2D_VIEW + 8) == FX(SCREEN_W) - 2 * (uint32_t)x ? -x / 4096 : 0;
}

/* Widens the view to margin columns a side; 0 puts back a view widened here (and leaves any other alone). */
static void set_sprite_view(int margin) {
    if (ws_sprite_origin() == margin || (!margin && !ws_sprite_origin())) return;
    rt_w32(G2D_VIEW, FX(-margin));
    rt_w32(G2D_VIEW + 8, FX(SCREEN_W + 2 * margin));
}

/* The field scene's renderer surface (scene secondary base *(0x0207088c) + 0xd748, view rect first). Its own
 * view is screen space: 0, 0, 1024 wide (no culling); the scene's draw (main 0x02041940) saves it, switches to
 * the G2D view above for world sprites (characters, gauges, damage numbers) and puts it back for the UI drawn in
 * screen coordinates (battle menus and their text, the command cursor). In the field that view starts at the
 * left margin, so the UI keeps its 4:3 place, centred. */
#define CURRENT_SCENE 0x0207088cu
#define SCENE_SURFACE 0xd748u

static void set_screen_view(int margin) {
    uint32_t base = rt_r32(CURRENT_SCENE);
    if (!ram_ptr(base)) return;
    uint32_t s = base + SCENE_SURFACE;
    int32_t x = (int32_t)rt_r32(s);
    if (rt_r32(s + 4) || rt_r32(s + 8) != FX(1024) || x > 0 || x < -VIDEO_MARGIN_MAX * 4096) return; /* not its own view */
    rt_w32(s, FX(-margin));
}

static int field_margin; /* this frame's margin columns in the field, 0 elsewhere */

/* Field object visibility (ov004 0x02130dbc, r0 = object): true while the object's tile lies within the view
 * columns *(SCROLL)+4 - 4 .. *(SCROLL)+6 + 4 (clamped to the room) and the matching rows. Those two fields are
 * the camera's own (map streaming uses them too), so they are widened by the margins only for the call. */
static void game_object_visible(CPU *cpu) { rt_call_wrapped(cpu, "ov004", 0x02130dbcu); } /* the original */
void rt_ws_object_visible(CPU *cpu) {
    uint32_t scroll = rt_r32(SCROLL);
    int extra = (field_margin + 7) >> 3; /* tiles */
    if (!extra || !ram_ptr(scroll) || cpu->resume_pc) {
        game_object_visible(cpu);
        return;
    }
    uint32_t left = rt_r16(scroll + 4), right = rt_r16(scroll + 6);
    rt_w16(scroll + 4, left > (uint32_t)extra ? left - (uint32_t)extra : 0);
    rt_w16(scroll + 6, right + (uint32_t)extra);
    game_object_visible(cpu);
    rt_w16(scroll + 4, left);
    rt_w16(scroll + 6, right);
}

void ws_fill_margins(int margin) {
    int field = margin > 0 && ss_in_field();
    field_margin = field ? margin : 0;
    set_sprite_view(field_margin);
    if (ss_in_field()) set_screen_view(field_margin);
    if (!field) return;
    uint32_t scroll = rt_r32(SCROLL), room = rt_r32(ROOM), tilesets = rt_r32(TILESETS);
    if (!ram_ptr(scroll) || !ram_ptr(room) || !ram_ptr(tilesets)) return;
    int ok0 = bg_follows(2, scroll, 0), ok1 = bg_follows(3, scroll, 1), ok2 = bg_follows(0, scroll, 2);
    int okhi = ok0 && ok1 && bg_follows(1, scroll, 0);
    if (!ok0 && !ok1 && !ok2) return;
    copy_room_rows(scroll, room);
    int camc = rt_r16(scroll + 4), camr = rt_r16(scroll + 8);
    int fine = io_get16(0x10) & 7, vfine = io_get16(0x12) & 7; /* BG0: every filled layer agrees on the tile */
    int left = camc + ((fine - margin) >> 3), right = camc + ((fine + SCREEN_W - 1 + margin) >> 3);
    int bottom = camr + ((vfine + SCREEN_H - 1) >> 3);
    for (int c = left; c <= right; c++) {
        if (c >= camc && c < camc + 32) continue; /* the game's own columns */
        for (int r = camr; r <= bottom; r++) {
            uint32_t t0 = room_tile(scroll, tilesets, 0, c, r), t1 = room_tile(scroll, tilesets, 1, c, r);
            int pri0 = (t0 >> 16) & 0xFF, pri1 = (t1 >> 16) & 0xFF;
            for (int L = 0; L < 3; L++) {
                static const int bg_of[3] = {2, 3, 0};
                int ok = L == 0 ? ok0 : L == 1 ? ok1 : ok2;
                if (!ok) continue;
                int slot = (rt_r8(scroll + 0x8e + 2 * L) + c - camc) & 63, row = (rt_r8(scroll + 0x8f + 2 * L) + r - camr) & 31;
                uint32_t t = L == 0 ? t0 : L == 1 ? t1 : room_tile(scroll, tilesets, 2, c, r);
                put(bg_of[L], slot, row, L == 0 && pri0 ? 0 : L == 1 && pri1 ? 0 : map_entry(L, t));
                if (L == 0 && okhi) put(1, slot, row, pri1 ? map_entry(1, t1) : pri0 ? map_entry(0, t0) : 0);
            }
        }
    }
}
