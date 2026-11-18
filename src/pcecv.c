// pcecv.c: see pcecv.h.
#include "pcecv.h"

#include <string.h>

#define C_BLACK 1

// The PC Engine's 512 colours as the TMS9918's 15: test/mkcolours.py makes
// this table (the rule and the exceptions are there) and a chart of it.
// G << 6 | R << 3 | B (3 bits each) -> TMS9918 colour; test/mkcolours.py
const uint8_t pcecv_colours[512] = {
    // green 0: rows red 0-7, columns blue 0-7
    1, 1, 4, 4, 4, 4, 4, 4,
    1, 1, 4, 4, 4, 4, 4, 4,
    6, 6, 1, 4, 4, 4, 4, 4,
    6, 6, 13, 13, 13, 4, 4, 4,
    6, 6, 6, 13, 13, 13, 4, 4,
    6, 6, 6, 13, 13, 13, 13, 13,
    6, 6, 6, 6, 13, 13, 13, 13,
    8, 8, 8, 8, 13, 13, 13, 13,
    // green 1: rows red 0-7, columns blue 0-7
    1, 1, 4, 4, 4, 4, 4, 4,
    1, 1, 1, 4, 4, 4, 4, 4,
    6, 1, 1, 4, 4, 4, 4, 4,
    6, 6, 6, 13, 13, 4, 4, 4,
    6, 6, 6, 13, 13, 13, 4, 4,
    6, 6, 6, 13, 13, 13, 13, 13,
    6, 6, 6, 6, 13, 13, 13, 13,
    8, 8, 8, 8, 8, 13, 13, 13,
    // green 2: rows red 0-7, columns blue 0-7
    12, 12, 1, 4, 4, 4, 4, 4,
    12, 1, 1, 4, 4, 4, 4, 4,
    1, 1, 1, 14, 4, 4, 4, 4,
    6, 6, 14, 14, 13, 4, 4, 4,
    6, 6, 6, 13, 13, 13, 5, 5,
    6, 6, 6, 6, 13, 13, 13, 5,
    6, 6, 6, 6, 13, 13, 13, 13,
    8, 8, 8, 8, 8, 13, 13, 13,
    // green 3: rows red 0-7, columns blue 0-7
    12, 12, 12, 12, 4, 4, 4, 4,
    12, 12, 12, 12, 4, 4, 4, 4,
    12, 12, 14, 14, 5, 5, 4, 4,
    12, 12, 14, 14, 14, 5, 5, 5,
    6, 6, 6, 14, 14, 13, 5, 5,
    6, 6, 6, 6, 13, 13, 13, 5,
    6, 6, 6, 6, 9, 13, 13, 13,
    8, 8, 8, 9, 9, 13, 13, 13,
    // green 4: rows red 0-7, columns blue 0-7
    12, 12, 12, 12, 12, 5, 5, 5,
    12, 12, 12, 12, 12, 5, 5, 5,
    12, 12, 12, 12, 12, 5, 5, 5,
    12, 12, 12, 14, 14, 5, 5, 5,
    10, 10, 10, 14, 14, 14, 5, 5,
    10, 10, 10, 10, 14, 14, 13, 5,
    10, 10, 10, 9, 9, 9, 13, 13,
    9, 9, 9, 9, 9, 9, 13, 13,
    // green 5: rows red 0-7, columns blue 0-7
    12, 12, 12, 12, 12, 7, 7, 7,
    12, 12, 12, 12, 12, 7, 7, 7,
    12, 12, 12, 12, 3, 7, 7, 7,
    12, 12, 12, 12, 3, 7, 7, 7,
    12, 12, 10, 10, 14, 14, 14, 5,
    10, 10, 10, 10, 14, 14, 14, 14,
    10, 10, 10, 11, 11, 14, 14, 14,
    10, 10, 11, 11, 11, 9, 14, 14,
    // green 6: rows red 0-7, columns blue 0-7
    2, 2, 2, 3, 3, 7, 7, 7,
    2, 2, 2, 3, 3, 7, 7, 7,
    2, 2, 2, 3, 3, 7, 7, 7,
    2, 2, 3, 3, 3, 7, 7, 7,
    3, 3, 3, 3, 3, 7, 7, 7,
    3, 10, 10, 10, 11, 14, 14, 14,
    10, 10, 10, 10, 11, 14, 14, 14,
    10, 10, 11, 11, 11, 11, 14, 14,
    // green 7: rows red 0-7, columns blue 0-7
    2, 2, 3, 3, 3, 3, 7, 7,
    2, 2, 3, 3, 3, 3, 7, 7,
    2, 2, 3, 3, 3, 3, 7, 7,
    3, 3, 3, 3, 3, 3, 7, 7,
    3, 3, 3, 3, 3, 3, 7, 7,
    3, 3, 3, 3, 3, 3, 7, 7,
    3, 10, 10, 10, 11, 11, 15, 15,
    10, 10, 11, 11, 11, 11, 15, 15,
};

// ---- the picture ----------------------------------------------------------------------

// the lines one band can need (all of 242 lines squeezed into 192: 11)
#define SPAN 12
static uint8_t draw_buf[PCECORE_PAD + SPAN * PCECORE_PITCH + PCECORE_PAD];
static uint8_t out_buf[8][PCECV_W];

void pcecv_init(pcecv_t *v, nescv_t *enc) {
    memset(v, 0, sizeof *v);
    v->enc = enc;
    v->hfit = true;
    v->last_h = 224;
    // a drawn pixel's byte is its TMS9918 colour already (pcecore_palette)
    pcecore_palette(pcecv_colours);
    for (int i = 0; i < 256; i++) v->lut[i] = (i & 15) ? (uint8_t)(i & 15) : (uint8_t)C_BLACK;
    for (int k = 0; k < 3; k++) { v->voice[k] = -1; v->sent_period[k] = -1; }
    for (int k = 0; k < 4; k++) v->sent_att[k] = -1;
    v->sent_noise = -1;
}

// A picture is worth starting when the encoder will be ready for it about
// when it is drawn: what is left of the last one's cells (about 100 go to
// the console a TV frame) goes in the frames the drawing takes.
bool pcecv_wants(const pcecv_t *v) {
    if (v->busy || v->ready) return false;
    const nescv_t *e = v->enc;
    const int left = e->has_target ? (uint16_t)(e->up_tail - e->up_head) : 0;
    const int rate = v->rate ? v->rate : 8, frames = (NESCV_ROWS + rate - 1) / rate;
    return left <= frames * 100;
}

// the window's top line in a picture h lines high (negative: the picture is shorter than the screen)
static int top_of(const pcecv_t *v, int h) {
    if (h <= PCECV_H) return -((PCECV_H - h) / 2);
    int y = (h - PCECV_H) / 2 + v->dy;
    return y < 0 ? 0 : y > h - PCECV_H ? h - PCECV_H : y;
}

static int left_of(const pcecv_t *v, int w) {
    if (w <= PCECV_W) return -((PCECV_W - w) / 2);
    int x = (w - PCECV_W) / 2 + v->dx;
    return x < 0 ? 0 : x > w - PCECV_W ? w - PCECV_W : x;
}

int pcecv_last_line(const pcecv_t *v) {
    const int h = v->last_h;
    if (h <= PCECV_H || v->vfit) return h - 1;
    return top_of(v, h) + PCECV_H - 1;
}

void pcecv_move(pcecv_t *v, int dx, int dy) {
    if (!dx && !dy) { v->dx = v->dy = 0; return; }
    // kept inside what the last picture allowed, so a key always moves it
    const int h = v->last_h, w = v->w ? v->w : PCECV_W;
    const int my = h > PCECV_H ? (h - PCECV_H + 1) / 2 : 0, mx = w > PCECV_W ? (w - PCECV_W + 1) / 2 : 0;
    int x = v->dx + dx, y = v->dy + dy;
    v->dx = (int16_t)(x < -mx ? -mx : x > mx ? mx : x);
    v->dy = (int16_t)(y < -my ? -my : y > my ? my : y);
}

void pcecv_snapped(pcecv_t *v, uint64_t index) {
    v->w = (int16_t)pcecore_width();
    v->h = (int16_t)pcecore_height();
    if (v->w < 8) v->w = 8;
    v->last_h = v->h > 0 ? v->h : 224;
    v->y0 = (int16_t)top_of(v, v->h);
    v->x0 = (int16_t)left_of(v, v->w);
    v->index = index;
    v->band = 0;
    v->clash = 0;
    v->busy = true;
}

bool pcecv_prepare(pcecv_t *v) {
    nescv_t *e = v->enc;
    if (!v->ready || e->has_target || e->has_latest) return false;
    if (!nescv_offer_cells(e, (const uint8_t (*)[16])v->cells, v->clash, v->index, v->backdrop)) return false;
    v->ready = false;
    v->pictures++;
    v->last_uploads = (uint16_t)(e->stats.uploads - v->uploads_then);
    v->uploads_then = e->stats.uploads;
    nescv_prepare(e);
    return true;
}

void pcecv_frame(pcecv_t *v) {
    if (v->frame_bands) v->rate = v->frame_bands;
    v->frame_bands = 0;
}

void pcecv_work(pcecv_t *v) {
    if (!v->busy) return;
    const int r = v->band, w = v->w, h = v->h;
    const bool vsq = v->vfit && h > PCECV_H, hsq = v->hfit && w > PCECV_W;
    uint8_t *const buf = draw_buf + PCECORE_PAD;
    int first, n;                   // the game's lines this band needs
    if (vsq) {
        first = r * 8 * h / PCECV_H;
        n = (r * 8 + 7) * h / PCECV_H - first + 1;
    } else {
        first = v->y0 + r * 8;
        n = 8;
    }
    // on the grid only at 1:1: a squeezed picture's cells don't repeat anyway
    pcecore_draw(buf, first, n, v->tiles && !vsq && !hsq ? v->y0 : -1);
    if (!vsq && !hsq && v->x0 >= 0 && v->x0 + PCECV_W <= w) {
        v->clash += nescv_cells(v->enc, buf + v->x0, PCECORE_PITCH, v->lut, &v->cells[r * NESCV_COLS]);   // as drawn
    } else {
        const uint8_t border = pcecore_border();
        for (int i = 0; i < 8; i++) {
            const uint8_t *src = buf + (vsq ? (r * 8 + i) * h / PCECV_H - first : i) * PCECORE_PITCH;
            uint8_t *dst = out_buf[i];
            if (hsq) {
                for (int x = 0; x < PCECV_W; x++) dst[x] = src[x * w / PCECV_W];
            } else if (v->x0 >= 0) {
                memcpy(dst, src + v->x0, PCECV_W);
            } else {                // narrower than the screen: in the middle
                memset(dst, border, PCECV_W);
                memcpy(dst - v->x0, src, (size_t)(w < PCECV_W ? w : PCECV_W));
            }
        }
        v->clash += nescv_cells(v->enc, &out_buf[0][0], PCECV_W, v->lut, &v->cells[r * NESCV_COLS]);
    }
    v->bands++;
    if (v->frame_bands < 255) v->frame_bands++;
    if (++v->band == NESCV_ROWS) {
        v->backdrop = v->border ? v->border : v->lut[pcecore_border()];
        v->busy = false;
        v->ready = true;
    }
}

// ---- the sound ------------------------------------------------------------------------

// a wave's level by its peak to peak (0-31), in half dB down from full
static const uint8_t WAVE_DB2[32] = {255, 60, 48, 41, 36, 32, 29, 26, 24, 21, 20, 18, 16, 15, 14, 13,
                                     11,  10, 9,  9,  8,  7,  6,  5,  4,  4,  3,  2,  2,  1,  1,  0};
#define SQUARE_DB2 4             // a square wave of a level is louder than most waves of it: 2 dB down
#define SILENT_DB2 56            // 28 dB down and more: off (the PSG's last step is 28)
#define STEAL_DB2  12            // a waiting channel takes a tone from one 6 dB quieter

typedef struct {
    bool on;
    int db2;                     // half dB down from full
    int n;                       // the SN76489's period (tones)
} chan_t;

// channel c as a tone: its level and the period of a square wave at its pitch
static chan_t tone_of(const pcecore_psg_t *p, int c) {
    const pcecore_chan_t *ch = &p->chan[c];
    chan_t t = {false, 0, 0};
    const int vol = ch->control & 0x1F;
    const int bal = (ch->balance >> 4) > (ch->balance & 15) ? ch->balance >> 4 : ch->balance & 15;
    const int mv = (p->volume >> 4) > (p->volume & 15) ? p->volume >> 4 : p->volume & 15;
    if (!(ch->control & 0x80) || (ch->control & 0x40) || !vol || !bal || !mv) return t;   // off, or direct D/A
    if (c >= 4 && (ch->noise & 0x80)) return t;                                            // noise
    if (c == 1 && (p->lfo_ctrl & 3) && !(p->lfo_ctrl & 0x80)) return t;                    // it is bending channel 0
    if (!ch->period) return t;
    int lo = 31, hi = 0;
    for (int i = 0; i < 32; i++) {
        const int s = ch->wave[i] & 31;
        if (s < lo) lo = s;
        if (s > hi) hi = s;
    }
    if (hi == lo) return t;
    // the cycles the wave holds: 32 over the shortest step it repeats at
    int cycles = 1;
    for (int s = 2; s <= 16; s *= 2) {
        bool same = true;
        for (int i = 0; i < 32 && same; i++) same = ch->wave[i] == ch->wave[(i + s) & 31];
        if (same) { cycles = 32 / s; break; }
    }
    // the wave goes round every 32 * period ticks of 3.58 MHz, a PSG tone
    // every 32 * n: the same number
    int n = (ch->period + cycles / 2) / cycles;
    if (n < 5) return t;                                 // above 22 kHz
    while (n > 1023) n = (n + 1) / 2;                    // below the PSG's lowest: up an octave
    t.db2 = (31 - vol) * 3 + (15 - bal) * 6 + (15 - mv) * 6 + WAVE_DB2[hi - lo] + SQUARE_DB2;
    t.on = t.db2 < SILENT_DB2;
    t.n = n;
    return t;
}

static chan_t noise_of(const pcecore_psg_t *p, int c) {
    const pcecore_chan_t *ch = &p->chan[c];
    chan_t t = {false, 0, 0};
    const int vol = ch->control & 0x1F;
    const int bal = (ch->balance >> 4) > (ch->balance & 15) ? ch->balance >> 4 : ch->balance & 15;
    const int mv = (p->volume >> 4) > (p->volume & 15) ? p->volume >> 4 : p->volume & 15;
    if (!(ch->control & 0x80) || (ch->control & 0x40) || !vol || !bal || !mv || !(ch->noise & 0x80)) return t;
    t.db2 = (31 - vol) * 3 + (15 - bal) * 6 + (15 - mv) * 6 + SQUARE_DB2;
    t.on = t.db2 < SILENT_DB2;
    // The PC Engine's noise steps 3.58 MHz / (64 * (31 - rate)) times a
    // second (1.8 kHz at rate 0, 56 kHz at 30); the PSG's at 6991, 3496 or
    // 1748 Hz: the nearest.
    const int rate = ch->noise & 0x1F, div = rate == 31 ? 1 : 2 * (31 - rate);   // in 32 ticks
    t.n = div <= 12 ? 0 : div <= 24 ? 1 : 2;                                        // (16, 32, 64 are the PSG's)
    return t;
}

static int att_of(int db2) {
    const int a = (db2 + 2) / 4;
    return a > 14 ? 15 : a;
}

int pcecv_psg(pcecv_t *v, const pcecore_psg_t *p, uint8_t *out) {
    chan_t t[6];
    bool has[6] = {false};
    for (int c = 0; c < 6; c++) t[c] = tone_of(p, c);
    // a tone stays with its channel while it plays, and a little after
    for (int k = 0; k < 3; k++) {
        const int c = v->voice[k];
        if (c < 0) continue;
        if (t[c].on) v->quiet[k] = 0;
        else if (++v->quiet[k] > PCECV_HOLD) { v->voice[k] = -1; continue; }
        has[c] = true;
    }
    // the others, loudest first (of two as loud, the higher): a free tone,
    // or the tone of a channel 6 dB quieter
    for (;;) {
        int best = -1;
        for (int c = 0; c < 6; c++) {
            if (!t[c].on || has[c]) continue;
            if (best < 0 || t[c].db2 < t[best].db2 || (t[c].db2 == t[best].db2 && t[c].n < t[best].n)) best = c;
        }
        if (best < 0) break;
        has[best] = true;           // (looked at once)
        int k = -1;
        for (int i = 0; i < 3 && k < 0; i++)
            if (v->voice[i] < 0) k = i;
        if (k < 0) {
            int worst = -1, wdb = 0;
            for (int i = 0; i < 3; i++) {
                const chan_t *o = &t[v->voice[i]];
                const int db = o->on ? o->db2 : SILENT_DB2 + 1;
                if (db > wdb) { wdb = db; worst = i; }
            }
            if (worst >= 0 && wdb >= t[best].db2 + STEAL_DB2) k = worst;
        }
        if (k < 0) continue;
        v->voice[k] = (int8_t)best;
        v->quiet[k] = 0;
    }
    int n = 0;
    int att[4];
    for (int k = 0; k < 3; k++) {
        const int c = v->voice[k];
        att[k] = c >= 0 && t[c].on ? att_of(t[c].db2) : 15;
        if (att[k] == 15 || t[c].n == v->sent_period[k]) continue;
        out[n++] = (uint8_t)(0x80 | k << 5 | (t[c].n & 15));
        out[n++] = (uint8_t)(t[c].n >> 4 & 0x3F);
        v->sent_period[k] = t[c].n;
    }
    const chan_t n4 = noise_of(p, 4), n5 = noise_of(p, 5);
    const chan_t *nz = n4.on && (!n5.on || n4.db2 <= n5.db2) ? &n4 : &n5;
    att[3] = nz->on ? att_of(nz->db2) : 15;
    if (nz->on && (4 | nz->n) != v->sent_noise) {       // white noise at that rate (a write restarts it: only on a change)
        out[n++] = (uint8_t)(0xE0 | 4 | nz->n);
        v->sent_noise = (int16_t)(4 | nz->n);
    }
    for (int k = 0; k < 4; k++) {
        if (att[k] == v->sent_att[k]) continue;
        out[n++] = (uint8_t)(0x90 | k << 5 | att[k]);
        v->sent_att[k] = (int16_t)att[k];
    }
    return n;
}
