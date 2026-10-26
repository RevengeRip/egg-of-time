/* Small pixel font for the runtime's own texts on the 256x192 screens (ui_font.c). Thread-safe. */
#pragma once

#include <stdint.h>

#define UI_FONT_H 8 /* rows per line: capitals rows 0-6, small letters rows 2-6, descenders row 7 */

int ui_text_width(const char *s);
/* Draws text into a 256x192 ABGR8888 screen in colour `color` with a black drop shadow, from column x,
 * top row y (clipped to the screen). */
void ui_draw_text(uint32_t *screen, const char *s, int x, int y, uint32_t color);
/* The same without the shadow. */
void ui_draw_text_flat(uint32_t *screen, const char *s, int x, int y, uint32_t color);
/* ui_draw_text on a 192-row screen `width` columns wide (widescreen). */
void ui_draw_text_in(uint32_t *screen, int width, const char *s, int x, int y, uint32_t color);
