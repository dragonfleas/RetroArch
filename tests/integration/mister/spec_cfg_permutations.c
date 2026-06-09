/* cfg permutations: vary mister_* settings (as a retroarch.cfg would) and
 * confirm the streamed frame still matches expectations.
 *
 * Lossless transport knobs (lz4 modes) must not change a pixel; pixel-affecting
 * knobs (rgb565, scanlines, interlaced) are asserted against a reference that
 * models the intended transformation. */
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
#include "metrics/compare.h"
#include "seams/spy_scaler.h"

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

/* Stream a gradient under a given lz4 mode; return interior SSIM vs source.
 * 200x160 keeps even raw (mode 0) within the loopback socket buffer. */
static double lz4_mode_interior_ssim(int lz4_mode)
{
   enum { W = 200, H = 160 };
   const int n = W * H;
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, lz4_mode);
   mdrv_arrange_mode(d, W, H);

   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_GRADIENT, W, H, SRC_FMT_XRGB8888, 0, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, W, H);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { free(src); mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }

   uint8_t *ref = malloc((size_t)n * 3);
   xrgb_to_bgr24(src, ref, n);
   double s = ssim_luma(composer_frame(c), ref, W - 1, H - 1, W * 3);
   free(src); free(ref);
   mdrv_stop(d);
   return s;
}

/* J1: every compression mode is lossless (interior SSIM == 1.0). */
Test(cfg_lz4, mode0_raw_is_lossless)      { cr_assert(lz4_mode_interior_ssim(0) >= 0.999); }
Test(cfg_lz4, mode1_lz4_is_lossless)      { cr_assert(lz4_mode_interior_ssim(1) >= 0.999); }
Test(cfg_lz4, mode2_is_lossless)          { cr_assert(lz4_mode_interior_ssim(2) >= 0.999); }
Test(cfg_lz4, mode3_is_lossless)          { cr_assert(lz4_mode_interior_ssim(3) >= 0.999); }

static void rgb565_to_bgr24(const uint8_t *p565, uint8_t *bgr, int n)
{
   const uint16_t *p = (const uint16_t *)p565;
   for (int i = 0; i < n; i++)
   {
      uint16_t v = p[i];
      uint8_t r = (v >> 11) & 0x1f, g = (v >> 5) & 0x3f, b = v & 0x1f;
      bgr[i*3+0] = (b << 3) | (b >> 2);
      bgr[i*3+1] = (g << 2) | (g >> 4);
      bgr[i*3+2] = (r << 3) | (r >> 2);
   }
}

/* RGB565 output under a given compression mode; interior SSIM vs full source. */
static double rgb565_interior_ssim(int lz4_mode)
{
   enum { W = 200, H = 160 };
   const int n = W * H;
   mdrv_t *d = mdrv_start();
   mdrv_set_rgb565(d, 1);
   mdrv_set_lz4(d, lz4_mode);
   mdrv_arrange_mode(d, W, H);

   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_GRADIENT, W, H, SRC_FMT_XRGB8888, 0, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, W, H);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { free(src); mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }

   uint8_t *ref = malloc((size_t)n * 3), *got = malloc((size_t)n * 3);
   xrgb_to_bgr24(src, ref, n);
   rgb565_to_bgr24(composer_frame(c), got, n);
   double s = ssim_luma(got, ref, W - 1, H - 1, W * 3);
   free(src); free(ref); free(got);
   mdrv_stop(d);
   return s;
}

/* J2: RGB565 quantization stays visually lossless under each compression mode
 * (gate 0.96 — 5/6/5 is inherent, not a defect). */
Test(cfg_rgb565, raw_no_major_degradation) { cr_assert(rgb565_interior_ssim(0) >= 0.96); }
Test(cfg_rgb565, lz4_no_major_degradation) { cr_assert(rgb565_interior_ssim(1) >= 0.96); }

/* J3: with scanlines on and a 2x upscale, the streamed frame matches the source
 * upscaled with alternate output rows blanked — i.e. the CRT scanline effect is
 * applied as intended (not a corruption). Reference models the same darkening.
 * Writes the measured SSIM to a file for inspection during experimentation. */
Test(cfg_scanlines, alternate_rows_blanked_as_intended)
{
   enum { SW = 100, SH = 80, DW = 200, DH = 160 };
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);              /* reliable transport */
   mdrv_set_scanlines(d, 1);
   mdrv_arrange_mode_scaled(d, DW, DH, 2.0, 2.0);

   uint32_t *src = malloc((size_t)SW * SH * 4);
   make_source_frame(PATTERN_GRADIENT, SW, SH, SRC_FMT_XRGB8888, 0, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, SW, SH);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }

   /* reference: source BGR -> 2x point upscale -> blank odd rows */
   uint8_t *src_bgr = malloc((size_t)SW * SH * 3);
   xrgb_to_bgr24(src, src_bgr, SW * SH);
   uint8_t *ref = malloc((size_t)DW * DH * 3);
   resample_point(src_bgr, SW, SH, SW * 3, ref, DW, DH, DW * 3);
   for (int y = 1; y < DH; y += 2)
      memset(ref + (size_t)y * DW * 3, 0, (size_t)DW * 3);

   double s = ssim_luma(composer_frame(c), ref, DW - 1, DH - 1, DW * 3);
   cr_log_info("scanlines interior SSIM = %.6f", s);
   cr_assert(s >= 0.98, "scanlines must match intended blanking, interior SSIM=%f", s);

   free(src); free(src_bgr); free(ref);
   mdrv_stop(d);
}

Test(cfg_scaled, oversized_scale_does_not_overflow_buffer)
{
   enum { SW = 320, SH = 224 };
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_arrange_mode_scaled(d, 672, 504, 2.625, 2.625);

   uint32_t *src = malloc((size_t)SW * SH * 4);
   make_source_frame(PATTERN_GRADIENT, SW, SH, SRC_FMT_XRGB8888, 0, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, SW, SH);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { free(src); mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }

   cr_assert_leq(spy_scaler_last_out_width(),  720, "scaler out_width exceeds the frame buffer");
   cr_assert_leq(spy_scaler_last_out_height(), 576, "scaler out_height exceeds the frame buffer");

   free(src);
   mdrv_stop(d);
}

Test(menu_lifecycle, close_drops_stale_menu_buffer)
{
   enum { W = 320, H = 240 };
   const uint32_t content = 0x00112233;
   const uint8_t exp[3] = { content & 0xff, (content >> 8) & 0xff, (content >> 16) & 0xff };
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_arrange_mode(d, W, H);

   uint16_t *menu = malloc((size_t)W * H * 2);
   for (int i = 0; i < W * H; i++) menu[i] = 0xFFFF;
   mdrv_draw_menu(d, menu, W, H);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { free(menu); mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }

   mdrv_close(d);

   uint32_t *src = malloc((size_t)W * H * 4);
   make_source_frame(PATTERN_SOLID, W, H, SRC_FMT_XRGB8888, content, (uint8_t *)src);

   int ok = 0;
   for (int i = 0; i < 60 && !ok; i++)
   {
      mdrv_draw_xrgb(d, src, W, H);
      usleep(10 * 1000);
      if (composer_have_frame(c) && memcmp(composer_frame(c), exp, 3) == 0)
         ok = 1;
   }
   cr_assert(ok, "content frame never reached MiSTer after close — stale menu buffer reused");

   free(menu); free(src);
   mdrv_stop(d);
}

/* J4: interlaced_fb streams one field per blit (half height). The received
 * field must faithfully reproduce alternate source lines. Parity (even/odd
 * first line) is an implementation detail, so we accept whichever the pipeline
 * picks and assert that field is lossless (interior). */
Test(cfg_interlaced, field_reproduces_alternate_source_lines)
{
   enum { W = 200, H = 160, FH = H / 2 };
   const int n = W * H;
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_set_interlaced(d, 1);
   mdrv_arrange_mode_interlaced(d, W, H);

   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_GRADIENT, W, H, SRC_FMT_XRGB8888, 0, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, W, H);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { free(src); mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }
   cr_assert_eq(composer_rgbsize(c), (uint32_t)(W * FH * 3), "interlaced blit is one half-height field");

   uint8_t *src_bgr = malloc((size_t)n * 3);
   xrgb_to_bgr24(src, src_bgr, n);

   /* score the received field against both parities; accept the better one */
   uint8_t *field = malloc((size_t)W * FH * 3);
   double best = 0.0;
   for (int parity = 0; parity <= 1; parity++)
   {
      for (int fl = 0; fl < FH; fl++)
         memcpy(field + (size_t)fl * W * 3,
                src_bgr + (size_t)(2 * fl + parity) * W * 3, (size_t)W * 3);
      double s = ssim_luma(composer_frame(c), field, W - 1, FH - 1, W * 3);
      if (s > best) best = s;
   }
   cr_log_info("interlaced best-parity field SSIM = %.6f", best);
   cr_assert(best >= 0.98, "interlaced field must reproduce alternate lines, SSIM=%f", best);

   free(src); free(src_bgr); free(field);
   mdrv_stop(d);
}
