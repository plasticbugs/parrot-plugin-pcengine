# pce-go (pico-pcePlus), vendored

- Upstream: https://github.com/PicoPlus-devel/pico-pcePlus, `pce-go/`,
  commit `125a3223bb8040f6bb677209fba6df8551d8686a`. Copied 2026-11-17.
- Origin: PCE-GO by Alex Duchesne and Macs75 (retro-go), from HuExpress
  by Alex von Gluck IV, from Hu-Go! by Zeograd (Olivier Jolly) and
  others, from Bero's PCE emulator (`CREDITS`). pico-pcePlus (the
  PicoPlus-devel port, after Frank Hoedemakers' Pico emulators) adds the
  SuperGrafx, the CD-ROM, sample-accurate sound and fixes for single
  games.
- Licences:
  - pico-pcePlus as a whole: GPL-3 (`LICENSE-pico-pcePlus`).
  - `pce-go/`: `COPYING` is the GPL-2 text. The files carry no notice of
    their own; HuExpress and Hu-Go! were GPL-2 or later.
- How the plugin goes out: free, as a separate download, never with the
  cart. PCE.CVP, built from these files with our own code (pcecore, pcecv,
  nescv, the SDK it links), is GPL-3 as a whole. Each release carries:
  - the credits: PCE-GO by Alex Duchesne, HuExpress, Hu-Go! and Bero's PCE
    emulator before it, and pico-pcePlus;
  - a link to all of that source.

## Kept

`pce-go.c/.h`, `pce.c/.h`, `gfx.c/.h`, `h6280.c/.h`, `h6280_instr.h`,
`h6280_dbg.h`, `psg.c/.h`, `config.h`, `cd.h`, `cd_chd.h` (the last two
for their declarations only), `COPYING`, `CREDITS`.

## Left out

- `cd.c`, `cd_chd.c`, and `external/libchdr`: the CD-ROM. It needs a
  System Card image, CD sound and ADPCM, which the ColecoVision can't play.
  `../../port/nocd.c` answers the core's calls with "no drive".
- `docs/`, `ports/`, `CMakeLists.txt`, and all of pico-pcePlus outside
  `pce-go/`: `main.cpp` (HDMI, I2S, the menu, FatFS, its pad drivers),
  `pico_shared/`, `pico_lib/`, the board files and builds.

## Changes

Every change is marked `(cvcart)` in the source, and each changed file
says so at its top; everything else is byte for byte upstream's. `git diff
65f02f5 -- firmware/revc/pce/vendor/pce-go` shows them all.

- **`h6280_instr.h`, `h6280.c`: a wait loop skipped.** The eight
  conditional branches and `BRA` go through `branch_taken()`. A taken
  branch back over one instruction that only reads memory and sets flags
  (`LDA`, `LDX`, `LDY`, `BIT`, `CMP`, `CPX`, `CPY`, zero page or absolute
  outside the I/O page), or a `BBR`/`BBS` to itself, is a game waiting for
  its interrupt. So is `INC $hhll / CMP $zz / BEQ` (Military Madness
  counts in RAM while it waits; the count gets the turns skipped). The core takes interrupts only between `h6280_run()`
  calls, so once the loop has gone round whole inside a call nothing can
  end it before the call does, and `H6280_IDLE(n)` adds the cycles of the
  turns left in one step. The game runs the same cycle for cycle:
  `test/run.sh` runs every game both ways and compares RAM, video RAM and
  CPU at each frame. `-DH6280_NO_IDLE` builds the core without it.
- **`gfx.c`, `gfx.h`: a frame drawn after it has run.**
  - `gfx_line_hook`: called from `render_lines()` for each run of lines
    with its scroll and control, drawn or not.
  - `gfx_draw_saved()`, `gfx_sprite_lines()`: lines of a frame drawn from a
    copy of the registers, video RAM and sprite table, with upstream's own
    `draw_tiles()` and `draw_sprites()`.
  - `PCE_NO_SGX`: `render_lines()` without its call to the SuperGrafx
    renderer, and `gfx_run()` without the second chip's sprite lists, so
    the linker drops that code and its buffers (14K and 4K).
- **`pce.c`, `pce.h`: `pce_palette_map`.** A table of 512 bytes the two
  palette writes take a pixel's byte from, in place of the colour's top 8
  bits (which lose the last bit of blue). The plugin's is the TMS9918
  colour for each PC Engine colour, so a drawn pixel is a ColecoVision
  colour already. `NULL`: as upstream.
- **`cd.h`: `CD_MAX_TRACKS_`** can be set from outside. The plugin builds
  with 1, which takes 13K off the CD state the core still declares.

## Building it

- `-Dmalloc=pce_malloc -Dcalloc=pce_calloc -Dfree=pce_free` for the
  vendored files: the core's memory comes from `../../pcecore.c`, which
  takes it from the plugin's free RAM (and the PSRAM for what is seldom
  touched). The plugin has no heap.
- `-DPCE_NO_SGX -DCD_MAX_TRACKS_=1`.
- The core calls out to `osd_*()` and `frens_f_malloc()` /
  `frens_f_free()` (`../../pcecore.c`) and to the CD-ROM's functions
  (`../../port/nocd.c`). `pce-go.c` includes `ff.h` for its `LoadDisc()`:
  `../../port/ff.h` is a stand-in.
- `-w`: its own warnings are left alone.
