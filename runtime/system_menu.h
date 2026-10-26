/* System Settings menu (system_menu.c): the runtime's display settings, opened with SELECT from the game's
 * Game Settings screen, the title screen's menu or the paused field. Host thread only. */
#pragma once

#include <stdint.h>

/* Takes this frame's DS keys (keyboard, gamepad, automation) and returns the keys the game gets: none while
 * the menu is open, and none of the keys still held from closing it. */
uint32_t sysmenu_input(uint32_t keys);
int sysmenu_is_open(void);
/* Draws the menu over a 256x192 screen. */
void sysmenu_draw(uint32_t *screen);
