/* Movie mods: MP4 replacements for the game's cutscenes (movie.c). */
#pragma once

#include <SDL3/SDL.h>

/* Host thread: while a replacement plays, draws its latest frame over the whole window (letterboxed) and returns
 * 1; returns 0 when none is playing. */
int movie_draw(SDL_Renderer *ren);
