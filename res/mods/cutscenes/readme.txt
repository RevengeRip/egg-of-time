Egg of Time - cutscene mods
===========================

Put an MP4 video in this folder to replace one of the game's movies. Name it after
the movie it replaces:

    op_ds.mp4       the opening movie
    m1_ds.mp4
    m2_ds.mp4
    m3_ds.mp4
    m4_ds.mp4
    m5_ds.mp4
    m6_ds.mp4
    m7_ds.mp4
    enosp1_ds.mp4
    enosp2_ds.mp4

These are the names of the game's own movie files (/movie/<name>.mods in the ROM).
A movie without a file here plays as usual.

Format
------
- Container: MP4.
- Video: H.264 (AVC) or H.265 (HEVC), 8-bit or 10-bit, any resolution up to 4K
  (3840x2160 or 4096x2160). It is shown over the whole window at its own aspect ratio,
  with black bars where the shapes differ.
- Audio: optional (AAC is the usual choice); played in place of the movie's sound.

The hardware video decoder is used when the system has one for the codec; otherwise
the video is decoded on the CPU, where 4K H.265 needs a fast processor.

Playing
-------
- Replacements play while System Settings > Mods is On (the default).
- START or a click/touch on the bottom screen skips the movie, as in the game.
