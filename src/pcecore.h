// pcecore: pce-go (vendor/pce-go, from pico-pcePlus) behind a few calls, for
// the plugin and the Mac tests. pce-go's headers stay inside pcecore.c
// (they define CPU, MIN and MAX, and names the pico-sdk has too).
//
// The game runs with drawing off, always: a frame is then the HuC6280 and
// little else, and costs the same whether a picture is wanted or not. For
// a picture, pcecore_frame(true) saves what the video chip held as that
// frame's last wanted line went by (the video RAM, the sprite table, the
// registers, and each line's scroll and control), and pcecore_draw() draws
// lines of that saved frame whenever there is time, a few at a call.
//
//   once:        pcecore_start(rom, size, alloc)
//   each frame:  pcecore_input(0, pad); pcecore_frame(want); pcecore_psg(&p)
//   a picture:   after pcecore_frame(true): pcecore_width(), pcecore_height(),
//                pcecore_draw(...) for the lines wanted, in any order
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// the pad's buttons (pce-go's JOY_*)
enum { PCECORE_I = 0x01, PCECORE_II = 0x02, PCECORE_SELECT = 0x04, PCECORE_RUN = 0x08,
       PCECORE_UP = 0x10, PCECORE_RIGHT = 0x20, PCECORE_DOWN = 0x40, PCECORE_LEFT = 0x80 };

#define PCECORE_PITCH    368        // pce-go's XBUF_WIDTH: bytes from a drawn line to the next
#define PCECORE_MAX_W    352        // the widest picture it draws (wider modes: their left 352)
#define PCECORE_MAX_H    242
#define PCECORE_PAD      32         // bytes of scratch pcecore_draw() needs before its first line and after its last
#define PCECORE_BRAM     0x800      // the backup RAM: 2K
#define PCECORE_RAM      (0x2000 + 0x10000 + 2 * 256 * sizeof(void *))   // what pcecore_start() takes from alloc
#define PCECORE_SLOW     (0x2000 + 0x2000)  // and from slow (a HuCard with RAM of its own, Populous: 32K more, when it asks)

// rom: the HuCard's file as it is on the card (512-byte header or not,
// bit-reversed US dump or not: pce-go sorts both out, in place, so the
// memory must be writable). alloc: memory for the console's RAM, video
// RAM and page tables (PCECORE_RAM bytes in all, never freed), NULL when
// there is none left. slow: memory that is seldom touched, which may be
// slow (the PSRAM): the page unmapped addresses read, the backup RAM, a
// HuCard's own RAM. False: not a HuCard pce-go can run, or no memory.
bool pcecore_start(uint8_t *rom, uint32_t size, void *(*alloc)(size_t), void *(*slow)(size_t));
void pcecore_input(int pad, uint8_t buttons);          // pad 0-4 (a multitap's five)
// one frame of the game. snap: save this frame for pcecore_draw(), as the
// line last_line (0: the first the game shows) goes by, or at the frame's
// end if the game shows fewer.
void pcecore_frame(bool snap, int last_line);

// ---- the saved frame ---------------------------------------------------------
int pcecore_width(void);            // its picture's width in pixels (256, 320, 336, 352...; at most PCECORE_MAX_W)
int pcecore_height(void);           // lines the game showed (224, 240...)
// Lines y to y + n - 1 of the saved frame into buf: line y + i at
// buf + i * PCECORE_PITCH, pcecore_width() bytes each, a byte a pixel:
// what pcecore_palette()'s table has for the pixel's colour.
// buf has PCECORE_PAD bytes of scratch before it and after its last line.
// Lines outside 0 to pcecore_height() - 1 are the border's colour.
// snap_top >= 0: the background on its 8-pixel grid, across and down from
// line snap_top (the sprites as they are): cells of a picture cut there
// repeat.
void pcecore_draw(uint8_t *buf, int y, int n, int snap_top);
uint8_t pcecore_border(void);       // the border's colour, as a pixel's byte
// The byte a drawn pixel is for each of the PC Engine's 512 colours (index
// G << 6 | R << 3 | B, 3 bits each): map's 512 bytes, which must stay
// where they are. From the next pixel drawn on, whenever it is called.
void pcecore_palette(const uint8_t *map);

// ---- the sound: the PSG's registers as the frame left them ----------------------
typedef struct {
    uint16_t period;                // 12 bits: a step of the wave every `period` of the 3.58 MHz clock's ticks
    uint8_t control;                // bit 7 on, bit 6 direct D/A, bits 4-0 volume
    uint8_t balance;                // left (high nibble), right
    uint8_t noise;                  // channels 4 and 5: bit 7 noise on, bits 4-0 its rate
    uint8_t wave[32];               // 5 bits each
} pcecore_chan_t;

typedef struct {
    uint8_t volume;                 // the main volume: left (high nibble), right
    uint8_t lfo_freq, lfo_ctrl;
    pcecore_chan_t chan[6];
} pcecore_psg_t;

void pcecore_psg(pcecore_psg_t *p);

// ---- the rest -------------------------------------------------------------------
uint8_t *pcecore_bram(void);        // the backup RAM, PCECORE_BRAM bytes ("HUBM" formatted at the start)
uint32_t pcecore_crc(void);         // the file's CRC-32, as pce-go found it
uint16_t pcecore_pc(void);          // the HuC6280's PC, for a note when something hangs
uint32_t pcecore_banks(void);       // its MMR 4-7, a byte each
// tests: a sum of the console's RAM, video RAM and CPU registers; and n
// samples of the game's own sound (pce-go's PSG), mono, at 44100 Hz (n <= 738)
uint32_t pcecore_state_sum(void);
void pcecore_audio(int16_t *out, int n);
