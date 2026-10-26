# Egg of Time: A CT DS recomp project

Egg of Time is a **static recompilation** of *Chrono Trigger* for Nintendo DS (USA, `YQUE`) into portable C11,
running natively on Windows, Linux and macOS with SDL3. It is not an emulator. Every function of the game's ARM9
code is translated ahead of time into C and compiled with a normal C compiler. A small runtime provides the parts
of the DS the code talks to.

> **No game data is included.** This repository contains no ROM, no BIOS, no game code and no game assets.
> You must supply your own legally obtained copy of the game. See [Getting started](#getting-started).

## Highlights

- **Static recompiler** (`tools/recomp/`, Python + Capstone)
  - Turns all ~9,000 ARM and Thumb functions (about 563,000 instructions) into one C function each.
  - Function bounds, modes and relocations come from [ds-decomp](https://github.com/AetiasHax/ds-decomp).
  - Handles overlay-aware calls, jump tables and hand-written code the game copies to fast memory (TCM) at run
    time, such as the movie decoder.
  - Every generated function can be re-entered at its call sites and loop heads, which is what makes save states
    possible.
- **ARM7 side emulated at a high level**: no ARM7 code runs.
  - Inter-processor messages (PXI), touch screen, real-time clock, cartridge reads.
  - EEPROM saves, stored as a `.sav` file next to the ROM.
  - A full reimplementation of the NitroSDK sound driver:
    - plays the game's music and sound effects from its own sequence, instrument and wave data;
    - envelopes, LFOs, portamento, priority-based channel allocation;
    - IMA-ADPCM decoding and streamed movie audio;
    - mixed at 48 kHz through SDL3.
- **Threads and interrupts**
  - Each NitroSDK thread runs as a host coroutine (minicoro).
  - Interrupt delivery is scanline-accurate, so the game's mid-frame raster effects (text boxes, HBlank DMA) land
    on the right line.
- **Video**
  - Both 2D engines: text, affine and bitmap backgrounds, sprites, windows, blending, brightness, VRAM banking.
  - The FMV cutscenes (Mobiclip) are decoded by the game's own recompiled decoder.
- **Movie mods**
  - An MP4 file (H.264 or H.265, up to 4K) in `mods/cutscenes/` next to the executable replaces a cutscene,
    shown over the whole window at its own aspect ratio, with its own sound. The build creates the folder, with a
    `readme.txt` listing the file names (`op_ds.mp4` for the opening, ...).
  - Decoded with FFmpeg, on the GPU's video decoder when there is one. START or a touch skips (the opening: any button), as in the
    game.
  - System Settings > Mods turns them on (the default) or off.
- **Music mods**
  - An audio file in `mods/music/` replaces a music track: `BGM_18.ogg` replaces "Chrono Trigger". The build
    creates the folder, with a `readme.txt` listing every track with its title from the game's Music Box.
  - Formats: OGG, Opus, FLAC, MP3, WAV, M4A/AAC, looping between `LOOPSTART` and `LOOPEND` (or `LOOPLENGTH`) tags
    (in samples), else the whole file; or MSU-1 `.pcm` files, with their loop point.
  - The game's fades and volume changes apply to them.
- **Single screen mode** (on by default)
  - Made for devices with one screen: only the main (top) screen is shown unless you change the layout.
  - The boot screens ("Licensed by Nintendo", the Square Enix and Mobiclip logos), which the DS spreads over both
    screens, are laid out on the main screen.
  - The title screen shows the copyright and "Press any button to start"; any button opens the menu, whose
    buttons (New Game...) appear on the main screen.
  - Game Settings fits on the main screen: Battle Mode and Movies, with the highlighted option's description
    under it. Game Mode is left out (always Classic), and SELECT opens System Settings.
  - New games start in the game's own Classic (one-screen) battle mode.
  - Build option `EGG_SINGLE_SCREEN` (`-DEGG_SINGLE_SCREEN=OFF`) keeps the DS's two-screen presentation and
    defaults, for ports to devices with two screens.
- **System Settings** (SELECT on the title menu or the Game Settings screen, or while the game is paused with START; B or SELECT
  to close, which also resumes a paused game)
  - Window mode: windowed, borderless windowed (the default) or fullscreen.
  - Internal resolution: Auto, or 1x (256x192) up to 11x (2816x2112, within 4K).
  - Aspect ratio: Original (4:3), 16:9 (the default), ultrawide (21:9) or Fit screen (the window's shape). The wider ones show
    more of the world while exploring and in battle, without stretching (battle menus and text boxes stay in
    the centred 4:3 area); other screens stay 4:3.
  - Framerate: 30, 60, 90, 120, 144 or unlimited. Above 60, scrolling in the field is smoothed: the picture
    glides with the camera between the game's frames (sub-pixel, evenly paced, a frame of extra delay), while
    the characters the camera follows stay put on screen. Nothing is blended or redrawn: characters' own movement
    and animation stay at the game's 60, as do its timing, sound and input.
  - V-Sync on (the default) or off; graphics filter: Off (Pixel), Sharp (the default), Smooth (edge-smoothing upscale)
    or LCD (DS) (the DS screen's pixel grid).
  - Color: Original (DS), SNES light (the default), SNES moderate or SNES strong. The DS version's colors look
    washed out on a PC display; the SNES levels deepen them towards the SNES look (whole window, menus included).
  - Mods: on (the default) or off: replacement cutscenes and music from `mods/`.
  - Changes apply at once and are remembered in `eggoftime.ini`.
- **Display**
  - Layouts: top screen only, stacked, side by side, or screens swapped.
- **Save states** (F5 / F7)
  - They contain only emulated state, so they keep working after the code is rebuilt and move between Windows and
    Linux.
- **Runs on retail-faithful behaviour**
  - The game's DS Protect anti-piracy checks pass because the runtime behaves like a retail DS.
  - The game code is not patched for this.

## Status

- The game boots, plays the intro movie and title screen, and starts a new game.
- The opening, the Millennial Fair, the first time warp, battles and Game Over have been tested.
- Sound, music and movies work.
- Windows (clang via llvm-mingw) and Linux (gcc, Ubuntu 24.04) are tested. On macOS, CI compiles the runtime
  cleanly (Apple clang, `-Werror`), but a full game build has not been tested there yet.
- Not implemented: the 3D engine (the game draws no 3D in the parts tested), Wi-Fi, microphone.

See [DESIGN.md](DESIGN.md) for the architecture, the problems found along the way and how they were solved.

## Getting started

### Playing (Windows release)

Download the release zip, unzip it anywhere you can write to, start `Egg of Time.exe` and choose your ROM:
*Chrono Trigger* (USA, game code `YQUE`, SHA-1 `11f9e8204e484eab42181b210c9f214584be75bf`). The release
contains no part of the game: on the first start it builds the game from your ROM with the tools it brings
(`kit/`: dsd, Python, clang), which takes a few minutes with a progress bar. Later starts begin at once. The
ROM is kept as `rom/CT.nds`, next to your saves.

### Building from source

### 1. Requirements

- CMake ≥ 3.25 and Ninja.
- A C11 compiler: clang (llvm-mingw on Windows), or gcc.
- Python 3 with Capstone (`pip install capstone`, for the disassembly comments in the generated code).
- SDL3:
  - Windows: the SDL3 development zip.
  - macOS: `brew install sdl3`.
  - Linux: a distro package if available. Otherwise CMake builds SDL 3.4.18 from source; install the
    X11/Wayland and ALSA/PulseAudio development packages first.
- FFmpeg (libavformat, libavcodec, libavutil, libswscale, libswresample), for movie and music mods:
  - Windows: a shared build, e.g. `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip` from
    [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds/releases). The build copies its DLLs next to the
    executable.
  - macOS: `brew install ffmpeg`.
  - Linux: the development packages (Debian/Ubuntu: `libavformat-dev libavcodec-dev libswscale-dev
    libswresample-dev`).
- [ds-decomp (dsd) v0.12.1](https://github.com/AetiasHax/ds-decomp/releases). Place it at `tools/dsd/` or
  anywhere on your `PATH`.

### 2. Add your ROM

Put your own dump of the game, *Chrono Trigger* (USA, game code `YQUE`), here, with exactly this name:

```
rom/CT.nds
```

Expected SHA-1: `11f9e8204e484eab42181b210c9f214584be75bf`.

### 3. Extract it

This creates `extract/`, the game's code and data, which the recompiler reads. It stays on your machine; never
commit it.

```sh
dsd rom extract --rom rom/CT.nds --output-path extract
```

### 4. Build

The build generates the C code from `extract/` and `config/` (the dsd analysis, included in this repository),
then compiles it. The first build takes a few minutes.

- **Linux / macOS**: `scripts/build.sh` (output in `build/`).
- **Windows** (llvm-mingw + SDL3 zip + FFmpeg build):

  ```sh
  cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang "-DCMAKE_PREFIX_PATH=<SDL3>/x86_64-w64-mingw32;<FFmpeg>"
  cmake --build build
  ```

### 5. Run

The game looks for `rom/CT.nds` next to the executable, then in the folder it is started from. Copy your ROM into
`build/rom/` and run `Egg of Time` (`Egg of Time.exe` on Windows) from `build/`; the recompiled code is the
game library next to it (`build/game/`). You can also pass a ROM path:
`"Egg of Time" path/to/rom.nds`. Saves go next to the ROM.

## Controls

| DS | Keyboard | Gamepad |
|---|---|---|
| D-pad | Arrow keys | D-pad or left stick |
| A / B | X / Z | East / South button |
| X / Y | S / A | North / West button |
| L / R | Q / W | Shoulders |
| Start / Select | Enter / Right Shift or Backspace | Start / Back |
| Touch screen | Mouse, when the bottom screen is shown | — |

| Key | Action |
|---|---|
| F2 | Layout: top only / stacked / side by side |
| F3 | Swap the screens |
| F4 | Filter: Pixel / Sharp / Smooth / LCD (DS) |
| F11 | Windowed / full screen |
| F5 / F7 | Save / load state |
| Hold Esc (1 s) | Quit |

## Credits

- [ds-decomp](https://github.com/AetiasHax/ds-decomp) and [dsd-ghidra](https://github.com/AetiasHax/dsd-ghidra)
  by AetiasHax, for the analysis the recompiler builds on.
- [SDL3](https://libsdl.org), [FFmpeg](https://ffmpeg.org) (LGPL, linked dynamically), [minicoro](https://github.com/edubart/minicoro) (bundled in
  `runtime/third_party/`, under its own license) and [Capstone](https://www.capstone-engine.org).
- Hardware details from GBATEK.
- The LCD (DS) filter is based on the [lcd3x shader](https://github.com/libretro/slang-shaders/blob/master/handheld/shaders/lcd3x.slang)
  by Gigaherz (public domain).

## Disclaimers

- This is an unofficial fan project, not affiliated with or endorsed by Square Enix or Nintendo.
  *Chrono Trigger* is a trademark of Square Enix.
- **No copyrighted game data is distributed.** You need your own copy of the game, and the files extracted from it
  (`rom/`, `extract/`, the generated code in `build/`) must not be redistributed.
- **AI assistance:** this project was developed with Claude (Anthropic) through Claude Code. Claude did the
  reverse-engineering analysis and wrote the recompiler, the runtime and the documentation, under the direction
  of the repository owner.
