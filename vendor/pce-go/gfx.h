#pragma once
// Changed for the Parrot Cart: the lines marked (cvcart); see VENDORED.md.

#include <stdbool.h>

int gfx_init(void);
void gfx_run(void);
void gfx_term(void);
void gfx_irq(int type);
void gfx_reset(bool hard);
void gfx_latch_context(int force);
void gfx_latch_context_vdc2(int force);
void gfx_bind_vdc1(void);

// Frame-skip: when set, render_lines() skips rasterization and the host
// line callback for the current frame (CPU/VDC/IRQ/collision logic still
// runs). The framebuffer keeps the previous image. Used to hold 60fps
// audio through render-bound scenes by dropping only the visible update.
void gfx_set_skip_render(bool skip);

// (cvcart) see gfx.c: the lines' scroll and control as the frame runs, and
// a frame drawn afterwards from a copy of the VDC's memory
#include "pce.h"
extern void (*gfx_line_hook)(int first_line, int last_line, int scroll_x, int scroll_y, int control);
void gfx_sprite_lines(const sprite_t *spram, uint64_t *sprite_lines);
void gfx_draw_saved(const UWord *regs, const uint16_t *vram, const sprite_t *spram,
	const uint64_t *sprite_lines, uint8_t *screen_buffer, int y1, int y2,
	int scroll_x, int scroll_y, int control);
