// pcecv: a PC Engine game's picture and sound as the ColecoVision takes
// them. Portable C (no pico-sdk), used by the plugin and by the Mac tests.
//
// The picture goes through nescv (../nes/nescv.h): the console runs
// gbkernel and nescv turns 256 x 192 pictures into its command lists.
//
// A PC Engine picture is usually 256 wide and 224 or 240 lines, sometimes
// 320, 336 or 352 wide, in 512 colours. The ColecoVision shows 256 x 192 in
// 15 colours, two to an 8-pixel row of a cell. So:
//   - down: 192 of the game's lines at 1:1 through a window, in the middle
//     to start with, which the keypad moves (pcecv_move); or all of the
//     lines, squeezed (vfit: about one line in seven left out of 224);
//   - across: a picture wider than 256 squeezed to fit, the start (hfit:
//     one column in four left out of 336, so nothing of the playfield is
//     lost); or 256 of its columns at 1:1 through the window;
//   - a picture smaller than the screen sits in the middle, the border's
//     colour round it;
//   - the 512 colours go to the nearest of the 15 (pcecv_colours).
//
// The game is drawn after its frame, not during it (pcecore.h): a picture
// is a job, pcecv_snapped() to start it and pcecv_work() for each band of
// 8 lines, called while there is time. The bands become cells here
// (nescv_cells), beside the encoder, so the next picture is drawn while
// the last one's cells are still going to the console; pcecv_prepare()
// hands a finished picture over when the encoder can take it.
//
// The sound: the PC Engine has six channels, each a 32-step wave at a
// 12-bit period (the last two can be noise instead, any can be a direct
// D/A, and channel 1 can bend channel 0's pitch). The ColecoVision's
// SN76489 has three square waves and noise. pcecv_psg() gives the three
// tones to the loudest channels that are playing a wave, each at the pitch
// its wave has (a wave holding two or four cycles sounds an octave or two
// up), keeps a channel on its tone while it plays so tones don't swap, and
// the noise to the louder noise channel. Direct D/A (samples, some drums)
// is left out.
//
//   once:                 pcecv_init(v, enc)
//   each TV frame:        pcecv_frame(v);
//                         nescv_tv_frame(enc, psg, n, list)   (the sound of the frame before)
//                         want = pcecv_wants(v); pcecore_frame(want, pcecv_last_line(v));
//                         if (want) pcecv_snapped(v, index);
//                         pcecore_psg(&p); n = pcecv_psg(v, &p, psg);
//   then, while there is time:  pcecv_prepare(v); if (pcecv_busy(v)) pcecv_work(v);
//
// The list goes out before the game's frame runs, with the sound the frame
// before left, and the encoder's start of a picture (nescv_prepare, up to
// 10 ms) is done in the time to spare and never inside nescv_tv_frame():
// so a list takes a millisecond or two to make whatever the game and the
// picture are doing, and is never late for the console.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "nescv.h"
#include "pcecore.h"

#define PCECV_W 256
#define PCECV_H 192
#define PCECV_PSG_MAX 11
#define PCECV_HOLD 8             // frames a silent channel keeps its tone before another may have it

// G << 6 | R << 3 | B (3 bits each) as TMS9918 colours (1-15)
extern const uint8_t pcecv_colours[512];

typedef struct {
    nescv_t *enc;
    uint8_t lut[256];            // a drawn pixel's byte (a TMS9918 colour already) as one of 1-15

    // the view
    bool vfit, hfit;             // all the lines / columns, squeezed (else 1:1 through the window)
    bool tiles;                  // the background on its 8-pixel grid
    int16_t dx, dy;              // the window, from the middle, in pixels (kept in what the picture allows)
    uint8_t border;              // 0, or a colour the border shows instead of the game's

    // the picture in work
    bool busy;                   // bands still to draw
    bool ready;                  // all drawn: waiting for the encoder
    uint8_t band;                // the next band, 0-23
    uint8_t rate;                // bands drawn in the last frame that drew any (how long a picture takes)
    uint8_t frame_bands;
    uint32_t clash;
    uint8_t backdrop;
    uint16_t last_uploads;       // cells the picture before the last one offered sent to the console
    uint32_t uploads_then;
    uint8_t cells[NESCV_CELLS][16];
    uint64_t index;
    int16_t w, h;                // the saved frame's
    int16_t x0, y0;              // the window's corner in it (negative: the border shows)
    int16_t last_h;              // the last frame's height, for pcecv_last_line()

    // the sound
    int8_t voice[3];             // the channel each tone plays (-1: none)
    uint8_t quiet[3];            // frames it has been silent
    int32_t sent_period[3];      // -1: never sent
    int16_t sent_att[4];
    int16_t sent_noise;

    uint32_t pictures, bands;    // stats
} pcecv_t;

void pcecv_init(pcecv_t *v, nescv_t *enc);
// true when a picture would be taken now: the frame to save
bool pcecv_wants(const pcecv_t *v);
// the last of the game's lines the window shows (pcecore_frame's last_line)
int pcecv_last_line(const pcecv_t *v);
// the frame was saved: its picture is now in work
void pcecv_snapped(pcecv_t *v, uint64_t index);
static inline bool pcecv_busy(const pcecv_t *v) { return v->busy; }
// one band of the picture in work, drawn and made into cells
void pcecv_work(pcecv_t *v);
// In the time to spare: the finished picture, if there is one and the
// encoder has none going in, handed over and started there (its cells
// looked up and given slots: up to 10 ms). True if that was done.
bool pcecv_prepare(pcecv_t *v);
// each TV frame, first (it counts the bands a frame gets)
void pcecv_frame(pcecv_t *v);
// the window moved by (dx, dy) pixels, or (0, 0): back to the middle
void pcecv_move(pcecv_t *v, int dx, int dy);

// the PSG bytes that bring the console's chip to the game's sound; returns how many (<= PCECV_PSG_MAX)
int pcecv_psg(pcecv_t *v, const pcecore_psg_t *p, uint8_t *out);
