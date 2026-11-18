// pcecore.c: see pcecore.h.
#include "pcecore.h"

#include <string.h>

#include "pce-go.h"
#include "pce.h"
#include "gfx.h"
#include "cd.h"
#include "psg.h"

// ---- what pce-go asks of its host ---------------------------------------------------
int osd_gfx_render_line;
uint8_t *osd_gfx_framebuffer(int width, int height) { (void)width; (void)height; return NULL; }   // (never asked: drawing is off)
void osd_gfx_lines_rendered(int first_line, int last_line) { (void)first_line; (void)last_line; }
void osd_input_read(uint8_t joypads[8]) { (void)joypads; }
void osd_vsync(void) {}
void osd_psg_scanline(void) {}
void osd_psg_sync(int frame_cycle) { (void)frame_cycle; }

// its memory: the vendored files are built with malloc, calloc and free as
// these (-Dmalloc=pce_malloc ...), so the plugin needs no heap. pce_init()
// asks for the RAM, then the video RAM, then the page unmapped addresses
// read (seldom touched: from slow), then its two page tables (calloc);
// what comes after (the backup RAM here, Populous's own RAM) is slow's too.
static void *(*core_alloc)(size_t), *(*core_slow)(size_t);
static bool alloc_failed;
static int mallocs;

static void *alloc_from(void *(*fn)(size_t), size_t n) {
    void *p = fn ? fn(n) : NULL;
    if (!p) alloc_failed = true;
    return p;
}

void *pce_malloc(size_t n) { return alloc_from(mallocs++ < 2 ? core_alloc : core_slow, n); }

void *pce_calloc(size_t k, size_t n) {
    void *p = alloc_from(core_alloc, k * n);
    if (p) memset(p, 0, k * n);
    return p;
}

void pce_free(void *p) { (void)p; }
void *frens_f_malloc(size_t n) { return pce_malloc(n); }
void frens_f_free(void *p) { (void)p; }

// ---- the saved frame ------------------------------------------------------------
typedef struct {
    int16_t sx, sy;
    uint8_t control;
    uint8_t set;
} line_t;

static uint16_t snap_vram[0x8000];
static sprite_t snap_spram[64];
static UWord snap_regs[32];
static uint64_t snap_sprite_lines[XBUF_HEIGHT];
static line_t snap_line[XBUF_HEIGHT];
static int snap_h, snap_w;
static bool snap_want, snap_taken;
static int snap_last;

static void snap_take(void) {
    memcpy(snap_vram, PCE.VRAM, sizeof snap_vram);
    memcpy(snap_regs, PCE.VDC.regs, sizeof snap_regs);
    memcpy(snap_spram, PCE.SPRAM, sizeof snap_spram);
    int w = (snap_regs[HDR].B.l + 1) * 8;
    snap_w = w > PCECORE_MAX_W ? PCECORE_MAX_W : w;
    // A sprite wholly off the right edge is put off the top instead:
    // pce-go would draw its last columns into the next line's first.
    for (int i = 0; i < 64; i++)
        if ((snap_spram[i].x & 0x3FF) - 32 >= snap_w) snap_spram[i].y = 0;
    gfx_sprite_lines(snap_spram, snap_sprite_lines);
    snap_taken = true;
}

static void line_hook(int first, int last, int sx, int sy, int control) {
    if (!snap_want) return;
    if (first < 0) first = 0;
    if (last > XBUF_HEIGHT) last = XBUF_HEIGHT;
    // (the lines after the save too: the game's height, and a test's whole picture)
    for (int y = first; y < last; y++) snap_line[y] = (line_t){(int16_t)sx, (int16_t)sy, (uint8_t)control, 1};
    if (last > snap_h) snap_h = last;
    if (!snap_taken && last > snap_last) snap_take();
}

int pcecore_width(void) { return snap_w; }
int pcecore_height(void) { return snap_h; }

// the border is the sprites' colour 0, which pce-go keeps no pixel byte for
uint8_t pcecore_border(void) {
    const unsigned c = PCE.VCE.regs[0x100].W & 0x1FF;
    return pce_palette_map ? pce_palette_map[c] : (uint8_t)(c >> 1);
}

// pce-go's Palette[], as its VCE writes leave it (pce.c), from the colours as they are now
void pcecore_palette(const uint8_t *map) {
    pce_palette_map = map;
    if (!PCE.VRAM) return;          // (before the start: the game's writes fill it)
    for (unsigned n = 0; n < 512; n++) {
        const unsigned v = PCE.VCE.regs[n].W & 0x1FF;
        const uint8_t c = map ? map[v] : (uint8_t)(v >> 1);
        if (n == 0) for (int i = 0; i < 256; i += 16) PCE.Palette[i] = c;
        else if (n & 15) PCE.Palette[n] = c;
    }
}

void pcecore_draw(uint8_t *buf, int y, int n, int snap_top) {
    const uint8_t border = pcecore_border();
    for (int i = 0; i < n;) {
        const int at = y + i;
        if (at < 0 || at >= snap_h || !snap_line[at].set) {
            memset(buf + i * PCECORE_PITCH, border, (size_t)snap_w);
            i++;
            continue;
        }
        const line_t l = snap_line[at];
        int k = 1;                  // the lines after it that it can be drawn with
        while (i + k < n && at + k < snap_h && snap_line[at + k].set && snap_line[at + k].sx == l.sx &&
               snap_line[at + k].sy == l.sy && snap_line[at + k].control == l.control) k++;
        int sx = l.sx, sy = l.sy;
        if (snap_top >= 0) {        // on the grid: the row at snap_top starts a tile
            sx &= ~7;
            sy -= (sy + snap_top) & 7;
        }
        gfx_draw_saved(snap_regs, snap_vram, snap_spram, snap_sprite_lines, buf - y * PCECORE_PITCH, at, at + k, sx, sy, l.control);
        i += k;
    }
}

// ---- the game ---------------------------------------------------------------------
static uint8_t *bram;

bool pcecore_start(uint8_t *rom, uint32_t size, void *(*alloc)(size_t), void *(*slow)(size_t)) {
    core_alloc = alloc;
    core_slow = slow;
    alloc_failed = false;
    mallocs = 0;
    if (InitPCE(44100, false) || alloc_failed) return false;   // (the rate: pcecore_audio()'s, a test's)
    // the backup RAM (a Tennokoe Bank's, a CD-ROM unit's): 2K at the bottom
    // of page $F7, the rest of the page reading $FF, always there
    bram = pce_malloc(0x2000);
    if (!bram) return false;
    memset(bram, 0xFF, 0x2000);
    memset(bram, 0, PCECORE_BRAM);
    memcpy(bram, "HUBM\x00\x88\x10\x80", 8);   // formatted, empty: its end $8800, the first free byte $8010
    PCE.MemoryMapR[0xF7] = PCE.MemoryMapW[0xF7] = bram;
    if (LoadCard(rom, size) || alloc_failed) return false;
    gfx_line_hook = line_hook;
    gfx_set_skip_render(true);
    return true;
}

void pcecore_input(int pad, uint8_t buttons) {
    if (pad >= 0 && pad < 5) PCE.Joypad.regs[pad] = buttons;
}

void pcecore_frame(bool snap, int last_line) {
    snap_want = snap;
    if (snap) {
        snap_taken = false;
        snap_h = 0;
        snap_last = last_line < 0 ? 0 : last_line;
        for (int y = 0; y < XBUF_HEIGHT; y++) snap_line[y].set = 0;
    }
    pce_run();
    if (snap && !snap_taken) snap_take();   // the game shows fewer lines than that
    snap_want = false;
}

void pcecore_psg(pcecore_psg_t *p) {
    p->volume = PCE.PSG.volume;
    p->lfo_freq = PCE.PSG.lfo_freq;
    p->lfo_ctrl = PCE.PSG.lfo_ctrl;
    for (int i = 0; i < 6; i++) {
        const psg_chan_t *c = &PCE.PSG.chan[i];
        p->chan[i].period = (uint16_t)(c->freq_lsb | (c->freq_msb & 15) << 8);
        p->chan[i].control = c->control;
        p->chan[i].balance = c->balance;
        p->chan[i].noise = i >= 4 ? c->noise_ctrl : 0;
        memcpy(p->chan[i].wave, c->wave_data, 32);
    }
}

uint8_t *pcecore_bram(void) { return bram; }
uint32_t pcecore_crc(void) { return PCE.ROM_CRC; }
uint16_t pcecore_pc(void) { return CPU.PC; }
uint32_t pcecore_banks(void) {
    return (uint32_t)PCE.MMR[4] | (uint32_t)PCE.MMR[5] << 8 | (uint32_t)PCE.MMR[6] << 16 | (uint32_t)PCE.MMR[7] << 24;
}

uint32_t pcecore_state_sum(void) {
    uint32_t s = 2166136261u;
    for (int i = 0; i < 0x2000; i++) s = (s ^ PCE.RAM[i]) * 16777619u;
    for (int i = 0; i < 0x8000; i++) s = (s ^ PCE.VRAM[i]) * 16777619u;
    s = (s ^ CPU.PC) * 16777619u;
    s = (s ^ (uint32_t)(CPU.A | CPU.X << 8 | CPU.Y << 16 | (uint32_t)CPU.P << 24)) * 16777619u;
    return (s ^ CPU.S) * 16777619u;
}

void pcecore_audio(int16_t *out, int n) { psg_update(out, (size_t)n, 0x3F); }
