#ifndef MISTER_STREAM_DRIVER_H
#define MISTER_STREAM_DRIVER_H

#include <stdint.h>
#include "sim/state_machine.h"

/* DRIVER layer: hides mister_set_mode / mister_draw, the simulator lifecycle,
 * and settings wiring behind a small domain vocabulary the specs speak. */

typedef struct mdrv mdrv_t;

/* Start the FPGA simulator on port 32100 and arrange the mister_* settings
 * (loopback IP, raw frames, 1500 MTU). */
mdrv_t *mdrv_start(void);
void    mdrv_stop(mdrv_t *d);

/* Select compression before connecting (0 = raw, 1 = LZ4). Default raw. */
void mdrv_set_lz4(mdrv_t *d, int lz4);

/* Force RGB565 output before connecting (0/1). Default 0 (RGB888). */
void mdrv_set_rgb565(mdrv_t *d, int on);

/* Toggle scanlines (darkens alternate output rows when y_scale >= 2). */
void mdrv_set_scanlines(mdrv_t *d, int on);

/* Toggle interlaced framebuffer mode. */
void mdrv_set_interlaced(mdrv_t *d, int on);

/* Set the display rotation gfx_mister.c reads (ORIENTATION_* 0..3). Default 0. */
void mdrv_set_rotation(mdrv_t *d, unsigned rotation);

/* Arrange a video mode (drives mister_set_mode); width/height in pixels. */
void mdrv_arrange_mode(mdrv_t *d, int w, int h);

/* Arrange a mode with explicit scale factors (e.g. 2.0 to upscale a smaller
 * source to a larger modeline). */
void mdrv_arrange_mode_scaled(mdrv_t *d, int w, int h, double xs, double ys);

/* Arrange an interlaced mode (modeline interlace flag set); pairs with
 * mdrv_set_interlaced to stream fields. */
void mdrv_arrange_mode_interlaced(mdrv_t *d, int w, int h);

/* Stream one XRGB8888 source frame through the real mister_draw pipeline. */
void mdrv_draw_xrgb(mdrv_t *d, const uint32_t *frame, int w, int h);

/* Stream one XRGB8888 frame through the hardware-readback path (:331): marks
 * the frame hw-rendered and supplies it via a read_viewport test double (which
 * yields BGR24). The readback blit reads bottom-up, so the output is a vertical
 * flip of the source. */
void mdrv_draw_hw(mdrv_t *d, const uint32_t *frame, int w, int h);

/* Access the simulator's composer (what the FPGA received). */
sim_composer_t *mdrv_composer(mdrv_t *d);

#endif /* MISTER_STREAM_DRIVER_H */
