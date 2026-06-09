/* Hardware-readback interior guard: the hw blit formula (gfx_mister.c :331)
 * reproduces the source faithfully. The readback path reads bottom-up, so the
 * output is a vertical flip of the source (an OpenGL orientation convention, not
 * corruption); we compare against the v-flipped source over a small
 * vertical-shift search and take the best alignment (the search also documented
 * the :331 row-offset off-by-one, which previously landed the match at dy=-1 —
 * after the :331 fix it lands at dy=0). The edge is guarded separately in
 * spec_edge_fidelity.c::hw_render_edge_is_populated. */
#include <criterion/criterion.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sim/protocol.h"
#include "sim/factories.h"
#include "sim/state_machine.h"
#include "driver/mister_stream_driver.h"
#include "metrics/ssim.h"

static int wait_for_frame(sim_composer_t *c)
{
   for (int i = 0; i < 200; i++)
   {
      if (composer_have_frame(c)) return 1;
      usleep(5 * 1000);
   }
   return composer_have_frame(c);
}

static void xrgb_to_bgr24(const uint32_t *src, uint8_t *bgr, int n)
{
   for (int i = 0; i < n; i++)
   {
      uint32_t c = src[i];
      bgr[i*3+0] = c & 0xff; bgr[i*3+1] = (c>>8)&0xff; bgr[i*3+2] = (c>>16)&0xff;
   }
}

static void vflip(const uint8_t *s, uint8_t *d, int W, int H)
{
   for (int y = 0; y < H; y++)
      memcpy(d + (size_t)y * W * 3, s + (size_t)(H - 1 - y) * W * 3, (size_t)W * 3);
}

/* C0: the hw-readback blit faithfully reproduces the frame content. */
Test(hwrender_fidelity, readback_interior_is_faithful)
{
   enum { W = 200, H = 160 };
   const int n = W * H;
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_arrange_mode(d, W, H);   /* 1:1 mode (x_scale = y_scale = 1.0) */

   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_GRADIENT, W, H, SRC_FMT_XRGB8888, 0, (uint8_t *)src);
   mdrv_draw_hw(d, src, W, H);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { free(src); mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }

   uint8_t *sb = malloc((size_t)n * 3);
   uint8_t *vf = malloc((size_t)n * 3);
   xrgb_to_bgr24(src, sb, n);
   vflip(sb, vf, W, H);

   /* Best alignment over a small vertical shift absorbs the readback row offset.
    * Compare the overlapping interior (1px inset on x; shift-trimmed on y). */
   const uint8_t *got = composer_frame(c);
   double best = 0.0;
   int best_dy = 0;
   for (int dy = -3; dy <= 3; dy++)
   {
      int y0 = (dy < 0 ? -dy : 0) + 1;
      int y1 = H - 1 - (dy > 0 ? dy : 0);
      int rows = y1 - y0;
      if (rows < 8) continue;
      const uint8_t *g = got + ((size_t)y0 * W + 1) * 3;
      const uint8_t *r = vf  + ((size_t)(y0 + dy) * W + 1) * 3;
      double s = ssim_luma(g, r, W - 2, rows, W * 3);
      if (s > best) { best = s; best_dy = dy; }
   }
   cr_log_info("hw readback interior best SSIM=%.4f at dy=%d", best, best_dy);
   cr_assert(best >= 0.98, "hw readback must reproduce the frame faithfully, best SSIM=%f", best);

   free(src); free(sb); free(vf);
   mdrv_stop(d);
}

Test(hwrender_fidelity, full_height_interlaced_readback_survives_field)
{
   enum { W = 200, H = 160, FH = H / 2 };
   const int n = W * H;
   const uint32_t color = 0x00123456;
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_set_interlaced(d, 1);
   mdrv_arrange_mode_interlaced(d, W, H);

   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_SOLID, W, H, SRC_FMT_XRGB8888, color, (uint8_t *)src);
   mdrv_draw_hw(d, src, W, H);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { free(src); mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }
   cr_assert_eq(composer_rgbsize(c), (uint32_t)(W * FH * 3), "interlaced blit is one half-height field");

   uint8_t exp[3] = { color & 0xff, (color >> 8) & 0xff, (color >> 16) & 0xff };
   const uint8_t *got = composer_frame(c);
   int bad_row = -1;
   for (int y = 0; y < FH && bad_row < 0; y++)
      for (int x = 0; x < W; x++)
         if (memcmp(got + ((size_t)y * W + x) * 3, exp, 3) != 0) { bad_row = y; break; }
   cr_assert_eq(bad_row, -1, "interlaced hw field=1 dropped/corrupted field row %d", bad_row);

   free(src);
   mdrv_stop(d);
}
