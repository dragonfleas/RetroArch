/* Edge regression guards: the blit walk once looped `j < y_max - 1` /
 * `i < x_max - 1`, dropping the last output row and column of every frame; the
 * hw-readback formula (:331) separately dropped output row 0. These tests pin
 * the populated edge on each distinct index formula the shared bound feeds, so
 * a reintroduced off-by-one is caught on every branch. Each uses a SOLID fill: a
 * solid frame is identical under any rotation, so the expected edge pixel is
 * just the fill color regardless of the path — a dropped edge shows as cleared
 * (black). See docs/mister-frame-drop-finding.md.
 *
 * Row vs column are asserted separately (cr_expect, non-fatal) so a partial
 * regression (one bound) is reported per-axis; note they share the corner pixel
 * (H-1, W-1), which needs both bounds correct.
 *
 * Interior correctness for these branches is characterized green in
 * spec_cfg_permutations.c, spec_rotation_fidelity.c, and spec_hwrender_fidelity.c. */
#include <criterion/criterion.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sim/protocol.h"
#include "sim/factories.h"
#include "sim/state_machine.h"
#include "driver/mister_stream_driver.h"

#define FILL_XRGB 0x00C84020u           /* R=0xC8 G=0x40 B=0x20, all non-zero */

static int wait_for_frame(sim_composer_t *c)
{
   for (int i = 0; i < 200; i++)
   {
      if (composer_have_frame(c)) return 1;
      usleep(5 * 1000);
   }
   return composer_have_frame(c);
}

/* Expected on-wire bytes for the fill color in the output format. */
static void fill_bgr24(uint8_t out[3])
{
   out[0] = FILL_XRGB & 0xff;            /* B */
   out[1] = (FILL_XRGB >> 8) & 0xff;     /* G */
   out[2] = (FILL_XRGB >> 16) & 0xff;    /* R */
}
static uint16_t fill_rgb565(void)
{
   uint8_t r = (FILL_XRGB >> 16) & 0xff, g = (FILL_XRGB >> 8) & 0xff, b = FILL_XRGB & 0xff;
   return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* True if pixel `px` (bpp bytes) in `frame` matches `expect` within tol. */
static int px_matches(const uint8_t *frame, int idx, int bpp, const uint8_t *expect, int tol)
{
   for (int k = 0; k < bpp; k++)
   {
      int d = (int)frame[idx * bpp + k] - (int)expect[k];
      if (d < 0) d = -d;
      if (d > tol) return 0;
   }
   return 1;
}

/* Last column (x = W-1) over the given rows [y0, H) stepping `ystep`. */
static int col_populated(const uint8_t *frame, int W, int H, int bpp,
                         const uint8_t *expect, int tol, int y0, int ystep)
{
   for (int y = y0; y < H; y += ystep)
      if (!px_matches(frame, y * W + (W - 1), bpp, expect, tol)) return 0;
   return 1;
}
/* Last row (y = H-1) over all columns. */
static int row_populated(const uint8_t *frame, int W, int H, int bpp,
                         const uint8_t *expect, int tol)
{
   for (int x = 0; x < W; x++)
      if (!px_matches(frame, (H - 1) * W + x, bpp, expect, tol)) return 0;
   return 1;
}

/* All four border edges. Orientation-agnostic: a rotation moves the dropped
 * edge to a different side, so the rotated-path tests assert the whole border
 * (which side the off-by-one clears depends on orientation). */
static int border_populated(const uint8_t *frame, int W, int H, int bpp,
                            const uint8_t *expect, int tol)
{
   for (int x = 0; x < W; x++)
      if (!px_matches(frame, x, bpp, expect, tol) ||
          !px_matches(frame, (H - 1) * W + x, bpp, expect, tol)) return 0;
   for (int y = 0; y < H; y++)
      if (!px_matches(frame, y * W, bpp, expect, tol) ||
          !px_matches(frame, y * W + (W - 1), bpp, expect, tol)) return 0;
   return 1;
}

/* ORIENTATION_* (gfx/video_defines.h). */
#define ORI_VERTICAL        1
#define ORI_FLIPPED_ROTATED 3

/* Stream a solid square frame under `rotation` and assert its whole border is
 * populated (a solid frame is identical under rotation, so the border must be
 * the fill color; the off-by-one currently clears one side). N >= 200 to clear
 * the mister_set_mode size floor. */
static void assert_rotated_border_populated(unsigned rotation, const char *name)
{
   enum { N = 200 };
   const int n = N * N;
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_set_rotation(d, rotation);
   mdrv_arrange_mode(d, N, N);

   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_SOLID, N, N, SRC_FMT_XRGB8888, FILL_XRGB, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, N, N);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { free(src); mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }

   uint8_t expect[3]; fill_bgr24(expect);
   cr_expect(border_populated(composer_frame(c), N, N, 3, expect, 0),
             "%s: a border edge not populated (:328/:342 regressed)", name);

   free(src);
   mdrv_stop(d);
}

/* A2: RGB565 path (:334) — the last row/column must carry the fill color, not
 * the cleared black left by the off-by-one. */
Test(edge_fidelity, rgb565_edge_is_populated)
{
   enum { W = 200, H = 160 };
   const int n = W * H;
   mdrv_t *d = mdrv_start();
   mdrv_set_rgb565(d, 1);
   mdrv_set_lz4(d, 1);
   mdrv_arrange_mode(d, W, H);

   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_SOLID, W, H, SRC_FMT_XRGB8888, FILL_XRGB, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, W, H);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { free(src); mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }
   cr_assert_eq(composer_rgbsize(c), (uint32_t)(n * 2), "RGB565 is 2 bytes/pixel");

   uint16_t e16 = fill_rgb565();
   uint8_t expect[2] = { (uint8_t)(e16 & 0xff), (uint8_t)(e16 >> 8) };
   const uint8_t *got = composer_frame(c);

   cr_expect(row_populated(got, W, H, 2, expect, 0), "565: last row not populated (:328 regressed)");
   cr_expect(col_populated(got, W, H, 2, expect, 0, 0, 1), "565: last column not populated (:342 regressed)");

   free(src);
   mdrv_stop(d);
}

/* A3: scanlines path (:334 + :344 blanking). With a 2x upscale the geometric
 * last output row is an odd (blanked) scanline, so the detectable edge defect is
 * the last COLUMN on the even (non-blanked) rows — currently cleared black. */
Test(edge_fidelity, scanlines_edge_column_is_populated)
{
   enum { SW = 100, SH = 80, DW = 200, DH = 160 };
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_set_scanlines(d, 1);
   mdrv_arrange_mode_scaled(d, DW, DH, 2.0, 2.0);

   uint32_t *src = malloc((size_t)SW * SH * 4);
   make_source_frame(PATTERN_SOLID, SW, SH, SRC_FMT_XRGB8888, FILL_XRGB, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, SW, SH);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { free(src); mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }

   uint8_t expect[3]; fill_bgr24(expect);
   /* even rows only (odd rows are intentionally blanked scanlines) */
   cr_expect(col_populated(composer_frame(c), DW, DH, 3, expect, 0, 0, 2),
             "scanlines: last column dropped on non-blanked rows (fixed by :342)");

   free(src);
   mdrv_stop(d);
}

/* A4: interlaced path (:334, one half-height field per blit). The same bound
 * drops the field's last row/column. */
Test(edge_fidelity, interlaced_field_edge_is_populated)
{
   enum { W = 200, H = 160, FH = H / 2 };
   const int n = W * H;
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_set_interlaced(d, 1);
   mdrv_arrange_mode_interlaced(d, W, H);

   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_SOLID, W, H, SRC_FMT_XRGB8888, FILL_XRGB, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, W, H);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { free(src); mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }
   cr_assert_eq(composer_rgbsize(c), (uint32_t)(W * FH * 3), "interlaced blit is one half-height field");

   uint8_t expect[3]; fill_bgr24(expect);
   const uint8_t *got = composer_frame(c);
   cr_expect(row_populated(got, W, FH, 3, expect, 0), "interlaced: field last row not populated (:328 regressed)");
   cr_expect(col_populated(got, W, FH, 3, expect, 0, 0, 1), "interlaced: field last column not populated (:342 regressed)");

   free(src);
   mdrv_stop(d);
}

/* B1: VERTICAL path (:337) edge. */
Test(edge_fidelity, vertical_edge_is_populated)
{
   assert_rotated_border_populated(ORI_VERTICAL, "vertical");
}

/* B3: FLIPPED_ROTATED path (:340) edge. */
Test(edge_fidelity, flipped_rotated_edge_is_populated)
{
   assert_rotated_border_populated(ORI_FLIPPED_ROTATED, "flipped_rotated");
}

/* C1: hardware-readback path (:331) edge. This path had its OWN off-by-one
 * (height/r_step - j dropped output row 0); fixed with a -1. Solid border is
 * invariant to the readback vertical flip, so border_populated guards it. */
Test(edge_fidelity, hw_render_edge_is_populated)
{
   enum { W = 200, H = 160 };
   const int n = W * H;
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_arrange_mode(d, W, H);

   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_SOLID, W, H, SRC_FMT_XRGB8888, FILL_XRGB, (uint8_t *)src);
   mdrv_draw_hw(d, src, W, H);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { free(src); mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }

   uint8_t expect[3]; fill_bgr24(expect);
   cr_expect(border_populated(composer_frame(c), W, H, 3, expect, 0),
             "hw-render: a border edge not populated (:331 / :328 / :342 regressed)");

   free(src);
   mdrv_stop(d);
}
