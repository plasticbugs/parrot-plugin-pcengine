// pce -- PC Engine / TurboGrafx-16 games on the ColecoVision: pick a .pce
// on the card in the menu and press A. pce-go (from pico-pcePlus) runs it on
// the cart, and pcecv and nescv send each picture and the sound to the
// console as the Game Boy kernel's frame lists. RESET goes back to the menu.
//
//   core 1  serves the kernel and a list a TV frame (cvframes.c)
//   core 0  this loop: for each TV frame the console starts, the next list
//           (the sound of the game's last frame, the picture's next cells),
//           then one frame of the game; then, until the next frame starts,
//           bands of the picture in work; the save when it's due; the pads
//
// The game is never drawn as it runs: that would take longer than a TV
// frame (../FEASIBILITY.md). A frame is saved when a picture is wanted
// (64K of video RAM and a little more) and drawn from the copy a band of 8
// lines at a time in the time each frame leaves, so the game and its sound
// keep their speed and only the number of pictures a second depends on how
// busy the game is. See ../pcecore.h and ../pcecv.h.
//
// Memory: past .bss, up to the launch block, is free (no heap); pce-go's
// memory (90K) comes from there. The HuCard goes in the PSRAM, as the file
// is, and runs from there through the XIP cache (pce-go skips a header and
// turns a US dump's bits round in place).
//
// The controller: joystick = D-pad, left fire = II, right fire = I, # =
// RUN, * = SELECT. Keypad 2 and 8 move the view up and down 8 lines, 4 and
// 6 left and right (a picture wider than the screen shown at 1:1), 5 puts
// it back; 1 switches a wide picture between squeezed to fit (the start)
// and 1:1; 3 switches the lines between 1:1 (the start) and all of them
// squeezed; 0 the scrolling, smooth (the start) or by tiles; 7 and 9 turn
// rapid fire on I and II on and off.
//
// USB game pads (cvpad.h): the first is player 1, with the console's
// controller, the second player 2 (a multitap's second pad). D-pad or left
// stick = D-pad, the right face button = I and the bottom one = II, the top
// one rapid I and the left one rapid II while held, + = RUN, - = SELECT;
// L and R move the view up and down, ZL puts it back, ZR switches the
// scrolling.
//
// The save, next to the HuCard by its name: "<name>.sav", the backup RAM
// (the 2K a CD-ROM unit or a Tennokoe Bank adds, which games that save use
// when it's there), read when the game starts and written a sector at a
// time, only the sectors the game changed (a second after it stops writing
// to it, every ten seconds while it keeps on, and at RESET). A card taken
// out and put back is written to again only if it holds the HuCard where
// it was.
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/xip_cache.h"
#ifdef PCE_CLOCK_KHZ
#include "hardware/clocks.h"
#endif

#include "cvdiag.h"
#include "cvframes.h"
#include "cvpad.h"
#include "cvpsram.h"
#include "cvsd.h"
#include "font8.h"
#include "gbkernel.h"
#include "handoff.h"
#include "pcecv.h"

#define OPEN_US        20000        // serve the console this long before the card's (blocking) start
#define WATCHDOG_MS    4000         // a hang reboots into the menu (with where the HuC6280 was)
#define NOTE_US        1000000      // the menu's note is brought up to date this often
#define CARD_SETTLE_US 500000       // a card put back: in this long before it's mounted
#define TV_US          16600        // a TV frame, a little short: bands are drawn until this long after its start
#define BAND_US        1500         // a band is taken to need this long until one has been timed
#define ROM_MIN        0x2000
#define ROM_MAX        (CVPSRAM_SIZE - 0x10000)
#define SMSCV_W        PCECV_W      // (the message screen's, as the Master System plugin's)

// TMS9918 colours
enum { C_BLACK = 1, C_DBLUE = 4, C_LBLUE = 5, C_CYAN = 7, C_MRED = 8, C_DYELLOW = 10, C_LYELLOW = 11,
       C_LGREEN = 3, C_GRAY = 14, C_WHITE = 15 };

// tests: the time the cart would take (plugin/test/pce_plugin_host.c's clock)
#ifndef PCE_SPEND
#define PCE_SPEND(us)
#endif

extern uint8_t __bss_end__[];       // (the linker's) the free RAM: from here to the launch block
#ifndef PCE_FREE_END                // (test/pce_plugin_host.c has its own)
#define PCE_FREE_END ((uint8_t *)CVL_ADDR)
#endif
#ifndef PCE_ROM_AT                  // where the HuCard is: the PSRAM, through the cache
#define PCE_ROM_AT CVPSRAM_CACHED
#endif

static nescv_t enc;
static pcecv_t cv;
static uint8_t list[NESCV_LIST_MAX] __attribute__((aligned(4)));
static size_t list_len;
static bool list_pending;           // built, not published yet (cvf_publish said wait)
static uint32_t seen_frames;
static uint64_t pic_index;
static fs_t fs;
static fs_file_t file;
static char path[sizeof CVL_BLOCK->path];
static char rom_dir[sizeof CVL_BLOCK->path];  // the HuCard's folder ("" the root)
static char name[33], note[33];
static uint8_t *free_at;

static void feed(void) { if (watchdog_hw->ctrl & WATCHDOG_CTRL_ENABLE_BITS) watchdog_update(); }

static void __attribute__((noreturn)) fail(const char *why, const char *more, const char *menu_note);

static void *take(size_t n) {       // n bytes of the free RAM, zeroed, or NULL (pce-go's memory: taken for good)
    uint8_t *p = (uint8_t *)(((uintptr_t)free_at + 3) & ~(uintptr_t)3);
    if (p > PCE_FREE_END || n > (size_t)(PCE_FREE_END - p)) return NULL;
    free_at = p + n;
    memset(p, 0, n);
    return p;
}

static uint32_t free_left(void) { return (uint32_t)(PCE_FREE_END - free_at); }

// memory that is seldom touched: the PSRAM past the HuCard, through the cache
static uint32_t slow_at;

static void *take_slow(size_t n) {
    uint32_t at = (slow_at + 0xFFF) & ~0xFFFu;
    if (at + n > CVPSRAM_SIZE) return NULL;
    slow_at = at + (uint32_t)n;
    memset(PCE_ROM_AT + at, 0, n);
    return PCE_ROM_AT + at;
}

// ---- the console, a list a TV frame ------------------------------------------------

static void publish(void) {
    if (list_pending && cvf_publish(list, list_len)) list_pending = false;
}

// a new TV frame started since the last call (and the last list is out)
static bool new_frame(void) {
    cvpad_task();
    cvf_task(time_us_32());
    feed();
    publish();
    if (list_pending) return false;
    uint32_t f = cvf_frames();
    if (f == seen_frames) return false;
    seen_frames = f;
    return true;
}

static void send(const uint8_t *psg, int npsg) {
    list_len = nescv_tv_frame(&enc, psg, npsg, list);
    list_pending = true;
    publish();
}

// ---- messages: a picture of text, a band at a time -----------------------------------

typedef struct {
    int16_t x, y;
    uint8_t colour, scale;
    char s[33];
} text_t;

static text_t texts[8];
static int ntexts;
static int bar = -1;                // loading: a bar this many percent full (-1: none)
static bool msg_waiting;
static uint8_t msg_lut[256];

static void add_text(int y, const char *s, uint8_t colour, int scale) {
    if (!s || ntexts == 8) return;
    text_t *t = &texts[ntexts++];
    snprintf(t->s, sizeof t->s, "%s", s);
    int w = (int)strlen(t->s) * 8 * scale;
    t->x = (int16_t)(w < SMSCV_W ? (SMSCV_W - w) / 2 : 0);
    t->y = (int16_t)y;
    t->colour = colour;
    t->scale = (uint8_t)scale;
}

static void fill(uint8_t band[8][SMSCV_W], int r, int x, int y, int w, int h, uint8_t colour) {
    for (int k = 0; k < 8; k++) {
        int py = r * 8 + k;
        if (py >= y && py < y + h) memset(&band[k][x], colour, (size_t)w);
    }
}

static void offer_msg(void) {
    if (!nescv_wants_picture(&enc)) { msg_waiting = true; return; }
    static uint8_t band[8][SMSCV_W];
    for (int r = 0; r < NESCV_ROWS; r++) {
        memset(band, C_BLACK, sizeof band);
        for (int i = 0; i < ntexts; i++) {
            const text_t *t = &texts[i];
            for (int k = 0; k < 8; k++) {
                int gy = r * 8 + k - t->y;
                if (gy < 0 || gy >= 8 * t->scale) continue;
                int x = t->x;
                for (const char *c = t->s; *c; c++, x += 8 * t->scale) {
                    int ch = (unsigned char)*c;
                    if (ch >= 'a' && ch <= 'z') ch -= 32;
                    if (ch < 32 || ch > 95) ch = '?';
                    uint8_t g = font8[ch - 32][gy / t->scale];
                    for (int b = 0; b < 8 * t->scale && x + b < SMSCV_W; b++)
                        if (g >> (7 - b / t->scale) & 1) band[k][x + b] = t->colour;
                }
            }
        }
        if (bar >= 0) {             // loading (the font has no %)
            fill(band, r, 76, 128, 104, 8, C_GRAY);
            fill(band, r, 77, 129, 102, 6, C_BLACK);
            if (bar) fill(band, r, 78, 130, bar, 4, C_WHITE);
        }
        nescv_band(&enc, r, &band[0][0], SMSCV_W, msg_lut);
    }
    nescv_band_done(&enc, ++pic_index, C_BLACK);
    msg_waiting = false;
}

static void message(const char *head, const char *l1, const char *l2) {
    ntexts = 0;
    add_text(48, "PC ENGINE", C_LBLUE, 2);
    if (name[0]) add_text(88, name, C_WHITE, 1);
    add_text(112, head, C_LYELLOW, 1);
    if (bar < 0) {
        add_text(128, l1, C_GRAY, 1);
        add_text(140, l2, C_GRAY, 1);
    }
    add_text(176, "RESET: BACK TO THE MENU", C_DBLUE, 1);
    offer_msg();
}

// serve the console, a list a frame, for us microseconds (forever: 0)
static void serve_for(uint32_t us) {
    uint32_t t0 = time_us_32();
    while (!us || time_us_32() - t0 < us) {
        if (!new_frame()) continue;
        if (msg_waiting) offer_msg();
        send(NULL, 0);
    }
}

static void __attribute__((noreturn)) fail(const char *why, const char *more, const char *menu_note) {
    bar = -1;
    message("IT CAN'T RUN", why, more);
    if (menu_note) cvf_note(menu_note);
    serve_for(0);
    for (;;) tight_loop_contents();
}

static const char *fs_why(int r) {
    switch (r) {
    case FS_EIO:     return cvsd_card_in() ? "THE CARD DIDN'T ANSWER" : "NO CARD IN THE SLOT";
    case FS_ENOFS:   return "NO FAT OR EXFAT ON THE CARD";
    case FS_ENOFILE: return "THE FILE IS EMPTY OR GONE";
    case FS_ECHAIN:  return "ITS CLUSTERS ARE BROKEN";
    case FS_EFRAG:   return "IT'S IN TOO MANY PIECES";
    default:         return "THE CARD'S FILE SYSTEM IS ODD";
    }
}

// ---- the HuCard -----------------------------------------------------------------------

// the file into the PSRAM, a sector between TV frames
static void read_rom(void) {
    static uint32_t sec[128];
    uint32_t n = (uint32_t)file.size, last_pc = 101;
    for (uint32_t at = 0; at < n; at += 512) {
        uint32_t lba = fs_lba(&fs, at / 512);
        if (!lba || !cvsd_read(lba, (uint8_t *)sec)) fail("A CARD READ FAILED", NULL, "PCE: a card read failed");
        if (at + 512 > n) memset((uint8_t *)sec + (n - at), 0xFF, 512 - (n - at));
        volatile uint32_t *d = CVPSRAM_UNCACHED + at / 4;
        for (int i = 0; i < 128; i++) d[i] = sec[i];
        uint32_t pc = (uint32_t)((uint64_t)at * 100 / n);
        if (pc / 5 != last_pc / 5) {
            last_pc = pc;
            bar = (int)pc;
            message("LOADING", NULL, NULL);
        }
        if (new_frame()) {
            if (msg_waiting) offer_msg();
            send(NULL, 0);
        }
    }
    xip_cache_invalidate_all();
    bar = -1;
}

// ---- the save: the backup RAM as "<name>.sav" ---------------------------------------------
#define SAV_QUIET_US   1000000      // no changes for this long: written
#define SAV_EVERY_US   10000000     // ...or changing for this long
#define SAV_RETRY_US   5000000      // after a write failed
#define SAV_LOOK       30           // the backup RAM is looked at for changes every this many frames
#define SAV_SECS       (PCECORE_BRAM / 512)

static uint16_t rom_nm[FS_LNAME], sav_nm[FS_LNAME];
static int rom_nm_n, sav_n;         // 0: no name (unreadable, or too long)
static fs_file_t sav_file;
static uint32_t card_gen = 1;       // cards mounted so far (the .sav is looked for again on a new one)
static uint32_t sav_gen;            // card_gen sav_file is from (0: none)
static uint32_t sav_hash[SAV_SECS]; // each sector as last written (or read)
static uint32_t sav_pending;        // sectors changed, not written yet
static uint32_t sav_change_t, sav_dirty_t, sav_retry_t;
static uint32_t sav_writes, sav_fails;
static char sav_msg[80] = "";       // what the save last did: 25 characters (the menu shows 30 of a note)
static uint8_t sav_buf[512] __attribute__((aligned(4)));

// the card: the one the game came from, mounted (card_watch)
static bool card_here = true, card_tried;
static uint32_t card_in_t;

static uint32_t sec_hash(const uint8_t *p) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 512; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static int with_ext(uint16_t *out, const uint16_t *nm, int stem, const char *ext) {
    size_t e = strlen(ext);
    if (stem + (int)e > 255) return 0;
    memcpy(out, nm, (size_t)stem * 2);
    for (size_t i = 0; i < e; i++) out[stem + (int)i] = (uint16_t)ext[i];
    return stem + (int)e;
}

// the name, and the .sav into the backup RAM (after the start)
static void saves_start(void) {
    uint8_t *bram = pcecore_bram();
    for (int k = 0; k < SAV_SECS; k++) sav_hash[k] = sec_hash(bram + k * 512);
    int n = rom_nm_n = fs_name16(&fs, rom_dir, file.first_clus, rom_nm, FS_LNAME - 1);
    if (n <= 0) { rom_nm_n = 0; snprintf(sav_msg, sizeof sav_msg, "no saves: bad name (%d)", n); return; }
    int stem = n;
    for (int i = 0; i < n; i++) if (rom_nm[i] == '.') stem = i;
    sav_n = with_ext(sav_nm, rom_nm, stem, ".sav");
    fs_file_t f;
    bool made;
    int r = fs_create(&fs, rom_dir, sav_nm, sav_n, 0, &f, &made);
    if (r == FS_ENOFILE) { snprintf(sav_msg, sizeof sav_msg, "no .sav yet"); return; }
    if (r || f.size < PCECORE_BRAM || f.size > 0x10000) {   // never written over: it isn't this plugin's
        snprintf(sav_msg, sizeof sav_msg, r ? ".sav unreadable (%d)" : ".sav odd size: not used", r);
        sav_n = 0;
        return;
    }
    if (fs_open(&fs, &f)) { snprintf(sav_msg, sizeof sav_msg, ".sav won't open: not used"); sav_n = 0; return; }
    // all of it read and looked at before any goes in
    uint32_t lba[SAV_SECS];
    for (uint32_t k = 0; k < SAV_SECS; k++)
        if (!(lba[k] = fs_lba(&fs, k))) { snprintf(sav_msg, sizeof sav_msg, ".sav didn't read: unused"); sav_n = 0; return; }
    if (!cvsd_read(lba[0], sav_buf) || memcmp(sav_buf, "HUBM", 4)) {
        snprintf(sav_msg, sizeof sav_msg, ".sav not backup RAM: kept");
        sav_n = 0;
        return;
    }
    for (uint32_t k = 0; k < SAV_SECS; k++) {
        if (!cvsd_read(lba[k], sav_buf)) {      // half in: the empty one again
            memset(bram, 0, PCECORE_BRAM);
            memcpy(bram, "HUBM\x00\x88\x10\x80", 8);
            snprintf(sav_msg, sizeof sav_msg, ".sav didn't read: unused");
            sav_n = 0;
            return;
        }
        memcpy(bram + k * 512, sav_buf, 512);
    }
    for (int k = 0; k < SAV_SECS; k++) sav_hash[k] = sec_hash(bram + k * 512);
    sav_file = f;
    sav_gen = card_gen;
    snprintf(sav_msg, sizeof sav_msg, "%lu-byte .sav loaded", (unsigned long)f.size);
}

// the .sav, opened for writing: found again on a new card, or made
static bool sav_open(void) {
    if (sav_gen != card_gen) {
        fs_file_t f;
        bool made;
        int r = fs_create(&fs, rom_dir, sav_nm, sav_n, PCECORE_BRAM, &f, &made);
        if (r || f.size < PCECORE_BRAM) {
            snprintf(sav_msg, sizeof sav_msg, r ? ".sav can't be made (%d)" : ".sav on card too small", r);
            if (!r) sav_n = 0;      // someone else's: left alone
            return false;
        }
        if (made) {                 // all of it: the card held anything there
            sav_pending = (1u << SAV_SECS) - 1;
            snprintf(sav_msg, sizeof sav_msg, ".sav made");
        }
        sav_file = f;
        sav_gen = card_gen;
    }
    return fs_open(&fs, &sav_file) == FS_OK;
}

// the sectors the game changed since they were written, into sav_pending
static void sav_look(void) {
    const uint8_t *bram = pcecore_bram();
    for (int k = 0; k < SAV_SECS; k++) {
        uint32_t h = sec_hash(bram + k * 512);
        if (h != sav_hash[k]) {
            sav_hash[k] = h;
            sav_pending |= 1u << k;
        }
    }
}

// write what changed; true when nothing is left
static bool sav_flush(void) {
    if (!sav_n) return true;
    sav_look();
    if (!sav_pending) return true;
    if (!card_here) return false;
    if (!sav_open()) { sav_fails++; return false; }
    const uint8_t *bram = pcecore_bram();
    for (int k = 0; k < SAV_SECS; k++) {
        if (!(sav_pending >> k & 1)) continue;
        memcpy(sav_buf, bram + k * 512, 512);
        if (fs_write_sec(&fs, (uint32_t)k, sav_buf)) { sav_fails++; snprintf(sav_msg, sizeof sav_msg, "the .sav didn't write"); return false; }
        sav_pending &= ~(1u << k);
        feed();
    }
    sav_writes++;
    snprintf(sav_msg, sizeof sav_msg, ".sav written");
    return true;
}

// each TV frame while the game runs: the .sav when it's due
static void saves_service(uint32_t frames, uint32_t now) {
    if (!sav_n) return;
    if (frames % SAV_LOOK == 0) {
        uint32_t before = sav_pending;
        sav_look();
        if (sav_pending != before) {
            sav_change_t = now;
            if (!sav_dirty_t) sav_dirty_t = now | 1;
        }
    }
    bool due = sav_dirty_t && (now - sav_change_t > SAV_QUIET_US || now - sav_dirty_t > SAV_EVERY_US);
    if (!due || (sav_retry_t && (int32_t)(now - sav_retry_t) < 0)) return;
    if (sav_flush()) sav_dirty_t = sav_retry_t = 0;
    else sav_retry_t = (now + SAV_RETRY_US) | 1;
}

// the card out and back: mounted again, and written to only if it's the one
// the game came from (the ROM's name at its first cluster in its folder)
static void card_watch(uint32_t now) {
    if (!cvsd_card_in()) {
        card_here = card_tried = false;
        card_in_t = 0;
        return;
    }
    if (card_here || card_tried) return;
    if (!card_in_t) { card_in_t = now | 1; return; }
    if (now - card_in_t < CARD_SETTLE_US) return;
    card_tried = true;              // once a time it goes in
    if (!cvsd_init() || fs_mount(&fs, cvsd_read) != FS_OK) { snprintf(sav_msg, sizeof sav_msg, "card back: didn't mount"); return; }
    fs_set_writer(&fs, cvsd_write);
    static uint16_t nm[FS_LNAME];
    int n = rom_nm_n ? fs_name16(&fs, rom_dir, file.first_clus, nm, FS_LNAME - 1) : -1;
    if (n != rom_nm_n || memcmp(nm, rom_nm, (size_t)n * 2)) {
        snprintf(sav_msg, sizeof sav_msg, "not the game's card");
        return;
    }
    card_here = true;
    card_gen++;
    snprintf(sav_msg, sizeof sav_msg, "the game's card is back");
}

// ---- the game -----------------------------------------------------------------------

static bool playing;
static bool turbo_i, turbo_ii;      // keypad 7 / 9: rapid fire on
static uint8_t last_key = 0x0F;
static uint32_t last_usb;           // pad 0's buttons, for the presses
static uint32_t late, us_max, list_us_max, shown_at_note, band_us = BAND_US, band_us_max;
static uint8_t psg[PCECV_PSG_MAX];   // the sound the last frame left, for the next list
static int npsg;

static uint8_t pad_from_cv(uint8_t joy, uint8_t fire, uint8_t key) {
    uint8_t p = 0;
    if (!(joy & 1)) p |= PCECORE_UP;
    if (!(joy & 2)) p |= PCECORE_RIGHT;
    if (!(joy & 4)) p |= PCECORE_DOWN;
    if (!(joy & 8)) p |= PCECORE_LEFT;
    if (!(fire & 1)) p |= PCECORE_II;
    if (!(fire & 2)) p |= PCECORE_I;
    if (key == 0x6) p |= PCECORE_RUN;       // #
    if (key == 0x9) p |= PCECORE_SELECT;    // *
    return p;
}

// rapid: the buttons that fire rapidly while held (the frame's count gates them)
static uint8_t pad_from_usb(const gamepad_t *g, uint8_t *rapid) {
    const uint32_t b = g->buttons, d = cvpad_dirs(g);
    uint8_t p = 0;
    if (d & GP_UP) p |= PCECORE_UP;
    if (d & GP_DOWN) p |= PCECORE_DOWN;
    if (d & GP_LEFT) p |= PCECORE_LEFT;
    if (d & GP_RIGHT) p |= PCECORE_RIGHT;
    if (b & GP_EAST) p |= PCECORE_I;
    if (b & GP_SOUTH) p |= PCECORE_II;
    if (b & GP_NORTH) *rapid |= PCECORE_I;
    if (b & GP_WEST) *rapid |= PCECORE_II;
    if (b & GP_PLUS) p |= PCECORE_RUN;
    if (b & GP_MINUS) p |= PCECORE_SELECT;
    return p;
}

// the keypad, on the press
static void view_key(uint8_t key) {
    switch (key) {
    case 0x7: pcecv_move(&cv, 0, -8); break;        // 2
    case 0x1: pcecv_move(&cv, 0, 8); break;         // 8
    case 0x2: pcecv_move(&cv, -8, 0); break;        // 4
    case 0xE: pcecv_move(&cv, 8, 0); break;         // 6
    case 0x3: pcecv_move(&cv, 0, 0); break;         // 5
    case 0xD: cv.hfit = !cv.hfit; break;            // 1
    case 0xC: cv.vfit = !cv.vfit; break;            // 3
    case 0xA: cv.tiles = !cv.tiles; break;          // 0
    case 0x5: turbo_i = !turbo_i; break;            // 7
    case 0xB: turbo_ii = !turbo_ii; break;          // 9
    default: break;
    }
}

// between bands: the console and the pads kept going
static void service(void) {
    cvpad_task();
    cvf_task(time_us_32());
    feed();
    publish();
}

// The encoder's start of a finished picture takes up to 10 ms in one piece.
// Begun later than START_BY_US into a TV frame it could keep the frame
// after next's list waiting, so it is left for the next frame then, three
// times at most (a game that never leaves the time still gets its
// pictures, and a late list now and then).
#define START_BY_US 8000

static void start_picture(uint32_t t0) {
    static uint8_t put_off;
    if (!cv.ready || enc.has_target) return;
    if (time_us_32() - t0 > START_BY_US && put_off < 3) { put_off++; return; }
    put_off = 0;
    if (pcecv_prepare(&cv)) { PCE_SPEND(6000); service(); }
}

static void game_frame(uint32_t frames) {
    uint32_t t0 = time_us_32();
    uint8_t joy, fire, key;
    cvf_input(&joy, &fire, &key);
    if (key != last_key) view_key(key);
    last_key = key;
    uint8_t p0 = pad_from_cv(joy, fire, key), p1 = 0, rapid0 = 0, rapid1 = 0;
    if (turbo_i && (p0 & PCECORE_I)) { p0 &= (uint8_t)~PCECORE_I; rapid0 |= PCECORE_I; }
    if (turbo_ii && (p0 & PCECORE_II)) { p0 &= (uint8_t)~PCECORE_II; rapid0 |= PCECORE_II; }
    gamepad_t g;
    if (cvpad_get(0, &g)) {
        p0 |= pad_from_usb(&g, &rapid0);
        const uint32_t down = g.buttons & ~last_usb;
        if (down & GP_L) pcecv_move(&cv, 0, -8);
        if (down & GP_R) pcecv_move(&cv, 0, 8);
        if (down & GP_ZL) pcecv_move(&cv, 0, 0);
        if (down & GP_ZR) cv.tiles = !cv.tiles;
        last_usb = g.buttons;
    } else last_usb = 0;
    if (cvpad_get(1, &g)) p1 = pad_from_usb(&g, &rapid1);
    if (frames & 2) { p0 |= rapid0; p1 |= rapid1; }  // 15 presses a second
    // both of a pair at once (the console's stick and a pad): neither, as on a real pad
    if ((p0 & (PCECORE_UP | PCECORE_DOWN)) == (PCECORE_UP | PCECORE_DOWN)) p0 &= (uint8_t)~(PCECORE_UP | PCECORE_DOWN);
    if ((p0 & (PCECORE_LEFT | PCECORE_RIGHT)) == (PCECORE_LEFT | PCECORE_RIGHT)) p0 &= (uint8_t)~(PCECORE_LEFT | PCECORE_RIGHT);
    pcecore_input(0, p0);
    pcecore_input(1, p1);

    // the list first: the sound the frame before left, the picture's next
    // cells. A millisecond or two, whatever the game is doing.
    pcecv_frame(&cv);
    send(psg, npsg);
    PCE_SPEND(1200);
    uint32_t us = time_us_32() - t0;
    if (us > list_us_max) list_us_max = us;

    const bool want = pcecv_wants(&cv);
    pcecore_frame(want, pcecv_last_line(&cv));
    PCE_SPEND(want ? 7300 : 7000);
    if (want) pcecv_snapped(&cv, ++pic_index);
    pcecore_psg_t pp;
    pcecore_psg(&pp);
    npsg = pcecv_psg(&cv, &pp, psg);
    us = time_us_32() - t0;
    if (us > us_max) us_max = us;
    service();

    // The time the frame leaves: a finished picture handed to the encoder
    // and started there, then the picture in work, a band at a time, until
    // the next TV frame is about to start.
    start_picture(t0);
    while (pcecv_busy(&cv)) {
        uint32_t t1 = time_us_32();
        if (t1 - t0 + band_us > TV_US || cvf_frames() != seen_frames) break;
        pcecv_work(&cv);
        PCE_SPEND(700);
        uint32_t t2 = time_us_32();
        if (t2 - t1 > band_us_max) band_us_max = t2 - t1;
        band_us = band_us_max + 100;                // (the slowest band so far, and a little)
        service();
        if (!pcecv_busy(&cv)) start_picture(t0);
    }
}

// on the way back to the menu (cvf_on_exit): the game's last changes
static void going_back(void) {
    if (!playing) return;
    bool ok = sav_flush();
    if (!ok || sav_fails) {
        char s[33];
        snprintf(s, sizeof s, "PCE: %.25s", !ok && !card_here ? "card out: .sav not saved" : sav_msg);
        cvf_note(s);
    }
}

// the menu's note, should RESET come: the scrolling (smooth, tiles),
// pictures this second, TV frames the cart was late for, its slowest frame
// (the list and the game's frame, without the drawing), then the slowest
// band; or, once a pad has been in, the longest cvpad_task() in ms; 30
// characters at most, what the menu shows. And in the return block now,
// where the HuC6280 is, should the watchdog reboot into the menu.
static uint32_t mem_left;

static void update_note(void) {
    static uint32_t t_last, late_last;
    uint32_t t = time_us_32();
    if (t - t_last < NOTE_US) return;
    snprintf(note, sizeof note, "PCE stuck: pc %04X b %08lX", pcecore_pc(), (unsigned long)pcecore_banks());
    cvf_note(note);
    cvf_note_now();
    uint32_t shown = enc.stats.shown + enc.stats.same;
    unsigned per_s = MIN(shown - shown_at_note, 99u), dl = MIN(late - late_last, 99u),
             ms = MIN(us_max / 1000, 99u), tenths = us_max / 100 % 10;
    int n = snprintf(note, sizeof note, "PCE %s %u/s %uL %u.%ums ", cv.tiles ? "tiles" : "smooth", per_s, dl, ms, tenths);
    if (cvpad_seen()) {
        unsigned pad = MIN(cvpad_task_max_us() / 10, 999u);
        snprintf(note + n, sizeof note - (size_t)n, "p%u.%02u", pad / 100, pad % 100);
    } else {
        snprintf(note + n, sizeof note - (size_t)n, "b%u", (unsigned)MIN(band_us_max / 10, 999u));
    }
    cvf_note(note);
    t_last = t;
    late_last = late;
    shown_at_note = shown;
    us_max = 0;
}

int main(void) {
    static const cvf_config_t cfg = {gbkernel_bin, GBKERNEL_BIN_LEN, handoff_bin, HANDOFF_BIN_LEN};
#ifdef PCE_CLOCK_KHZ                // (an experiment: see CMakeLists.txt)
    set_sys_clock_khz(PCE_CLOCK_KHZ, false);
#endif
    cvf_start(&cfg);
    cvd_start();                    // a fault, a hang or a panic: what, in the menu's note (cvdiag.h)
    cvd_stage(1);
    cvpad_init();                   // the pads find their way in while the game loads
    nescv_init(&enc);
    pcecv_init(&cv, &enc);
    for (int i = 0; i < 256; i++) msg_lut[i] = (i & 15) ? (uint8_t)(i & 15) : (uint8_t)C_BLACK;
    free_at = __bss_end__;

    const cvl_t *b = cvf_launch();
    if (b && b->path[0]) {          // (the block becomes a return block below: what's needed, copied)
        memcpy(path, b->path, sizeof path);
        const char *slash = strrchr(path, '/');
        snprintf(name, sizeof name, "%.32s", slash ? slash + 1 : path);   // a line of the screen at most
        char *dot = strrchr(name, '.');   // the title without its extension
        if (dot && dot != name) *dot = 0;
        size_t d = slash ? (size_t)(slash - path) : 0;
        memcpy(rom_dir, path, d);
        rom_dir[d] = 0;
    }
    message(path[0] ? "STARTING" : NULL, path[0] ? NULL : "OPEN A .PCE FILE", path[0] ? NULL : "FROM THE MENU");
    serve_for(OPEN_US);
    if (!path[0]) fail("OPEN A .PCE FILE", "FROM THE MENU", NULL);

    int r = cvsd_open_launched(&fs, b, true, &file);   // (blocks up to 1.5 s: the last list runs again meanwhile)
    if (r != FS_OK) fail(fs_why(r), NULL, "PCE: the file couldn't be read");
    cvd_stage(2);
    if (file.size < ROM_MIN || file.size > ROM_MAX) fail("IT ISN'T A HUCARD", "(TOO SMALL OR TOO BIG)", "PCE: not a HuCard");
    if (free_left() < PCECORE_RAM + 64) fail("NO ROOM FOR THE CONSOLE", NULL, "PCE: no room for pce-go");
    if (!cvpsram_init()) fail("THE PSRAM DIDN'T ANSWER", "(THE HUCARD NEEDS IT)", "PCE: the PSRAM didn't answer");
    watchdog_enable(WATCHDOG_MS, true);
    cvd_stage(3);
    read_rom();
    cvd_stage(4);

    message("STARTING", NULL, NULL);
    serve_for(OPEN_US);
    feed();                         // (pce-go sums the file and may turn its bits round: a second for 2.5 MB)
    slow_at = (uint32_t)file.size + 0x2000;   // (pce-go maps a last page whole)
    if (!pcecore_start(PCE_ROM_AT, (uint32_t)file.size, take, take_slow)) fail("IT ISN'T A HUCARD", "PCE-GO CAN RUN", "PCE: pce-go can't run it");
    feed();
    cvd_stage(5);
    mem_left = free_left();
    saves_start();
    cvd_stage(6);
    snprintf(note, sizeof note, "PCE: %.25s", sav_msg);
    cvf_note(note);
    cvf_on_exit(going_back);
    playing = true;

    for (;;) {
        uint32_t before = seen_frames;
        if (!new_frame()) continue;
        if (before && seen_frames - before > 1) late += seen_frames - before - 1;
        game_frame(seen_frames);
        uint32_t now = time_us_32();
        saves_service(seen_frames, now);
        card_watch(now);
        update_note();
    }
}
