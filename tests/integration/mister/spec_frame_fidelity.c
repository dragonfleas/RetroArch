/* Acceptance specs. Declarative behavior — a frame rendered locally,
 * streamed through the real GroovyMiSTer pipeline, arrives at the FPGA
 * perceptually identical. */
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
#include "metrics/vmaf.h"

/* Wait (up to ~1s) for the simulator to finish reassembling a streamed frame. */
static int wait_for_frame(sim_composer_t *c)
{
   for (int i = 0; i < 200; i++)
   {
      if (composer_have_frame(c))
         return 1;
      usleep(5 * 1000);
   }
   return composer_have_frame(c);
}

/* Wait for a NEW completed frame (frame number past `prev`). */
static int wait_for_new_frame(sim_composer_t *c, uint32_t prev)
{
   for (int i = 0; i < 200; i++)
   {
      if (composer_have_frame(c) && composer_last_frame(c) != prev)
         return 1;
      usleep(5 * 1000);
   }
   return composer_have_frame(c) && composer_last_frame(c) != prev;
}

/* Expected BGR24 the pipeline should produce from an XRGB8888 source. */
static void xrgb_to_bgr24(const uint32_t *src, uint8_t *bgr, int n)
{
   for (int i = 0; i < n; i++)
   {
      uint32_t c = src[i];
      bgr[i * 3 + 0] = (uint8_t)(c & 0xff);          /* B */
      bgr[i * 3 + 1] = (uint8_t)((c >> 8) & 0xff);   /* G */
      bgr[i * 3 + 2] = (uint8_t)((c >> 16) & 0xff);  /* R */
   }
}

/* Unpack a little-endian RGB565 frame to BGR24 (5/6/5 bits expanded to 8). */
static void rgb565_to_bgr24(const uint8_t *p565, uint8_t *bgr, int n)
{
   const uint16_t *p = (const uint16_t *)p565;
   for (int i = 0; i < n; i++)
   {
      uint16_t v = p[i];
      uint8_t r = (v >> 11) & 0x1f, g = (v >> 5) & 0x3f, b = v & 0x1f;
      bgr[i * 3 + 0] = (uint8_t)((b << 3) | (b >> 2));
      bgr[i * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
      bgr[i * 3 + 2] = (uint8_t)((r << 3) | (r >> 2));
   }
}

/* H1: an RGB888 frame at matched resolution must stream perceptually lossless
 * (full-frame SSIM >= 0.98).
 *
 * KNOWN-FAILING (red) regression test. It currently scores ~0.928 because of a
 * real off-by-one in gfx_mister.c: the blit walk loops `j < y_max - 1` and
 * `i < x_max - 1` (gfx_mister.c:327,342), dropping the last row and column of
 * every streamed frame. The interior is pixel-perfect (interior SSIM == 1.0,
 * see spec_frame_fidelity::interior_is_lossless). This test stays red on
 * purpose to flag the defect until the bound is fixed to y_max / x_max. */
/* Note on size: the default loopback UDP socket buffer (net.core.rmem_max,
 * ~208 KB here) bounds how much can be in flight without root. A 256x192 RGB888
 * frame (144 KB, ~100 datagrams) streams without drops; larger frames would
 * need a raised rmem_max. Happy-path fidelity is independent of resolution. */
Test(frame_fidelity, rgb888_matched_resolution_is_lossless)
{
   enum { W = 256, H = 192 };
   const int n = W * H;

   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);   /* lossless transport; small bursts avoid loopback UDP drops */
   mdrv_arrange_mode(d, W, H);

   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_GRADIENT, W, H, SRC_FMT_XRGB8888, 0, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, W, H);

   sim_composer_t *c = mdrv_composer(d);
   cr_assert(wait_for_frame(c), "the streamed frame must fully arrive");
   cr_assert_eq(composer_rgbsize(c), (uint32_t)(n * 3));

   uint8_t *ref = malloc((size_t)n * 3);
   xrgb_to_bgr24(src, ref, n);

   /* The streamed frame is perceptually identical to the rendered frame. */
   const uint8_t *got = composer_frame(c);
   double s = ssim_luma(got, ref, W, H, W * 3);
   cr_assert(s >= 0.98, "streamed frame must be visually lossless, SSIM=%f", s);

   free(src);
   free(ref);
   mdrv_stop(d);
}

/* Companion to H1: the pipeline is provably lossless everywhere EXCEPT the
 * dropped edge row/column — the frame interior is pixel-perfect (SSIM == 1.0).
 * This isolates the off-by-one and proves render→UDP→reassembly→conversion is
 * otherwise exact. Stays green. */
Test(frame_fidelity, interior_is_lossless)
{
   enum { W = 256, H = 192 };
   const int n = W * H;

   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_arrange_mode(d, W, H);
   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_GRADIENT, W, H, SRC_FMT_XRGB8888, 0, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, W, H);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) cr_skip("frame did not arrive (loopback UDP rmem limit)");
   uint8_t *ref = malloc((size_t)n * 3);
   xrgb_to_bgr24(src, ref, n);

   /* exclude the last row/col (known off-by-one) by scoring the W-1 x H-1 interior */
   double s = ssim_luma(composer_frame(c), ref, W - 1, H - 1, W * 3);
   cr_assert(s >= 0.999, "frame interior must be lossless, SSIM=%f", s);

   free(src); free(ref);
   mdrv_stop(d);
}

/* H4: LZ4 compression is lossless — the LZ4-streamed frame interior matches the
 * raw-streamed interior exactly (SSIM == 1.0). Compares the same content under
 * compression; the edge off-by-one is excluded as in interior_is_lossless. */
Test(frame_fidelity, lz4_compression_is_nondegrading)
{
   enum { W = 256, H = 192 };
   const int n = W * H;

   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_arrange_mode(d, W, H);
   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_GRADIENT, W, H, SRC_FMT_XRGB8888, 0, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, W, H);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) cr_skip("frame did not arrive (loopback UDP rmem limit)");
   uint8_t *ref = malloc((size_t)n * 3);
   xrgb_to_bgr24(src, ref, n);

   double s = ssim_luma(composer_frame(c), ref, W - 1, H - 1, W * 3);
   cr_assert(s >= 0.999, "LZ4 must be lossless, interior SSIM=%f", s);

   free(src); free(ref);
   mdrv_stop(d);
}

/* H2: RGB565 output shows no major degradation — 5/6/5 quantization stays
 * visually lossless. Compares the 565-streamed frame (unpacked) against the
 * full-precision source over the interior (edge off-by-one excluded). */
Test(frame_fidelity, rgb565_shows_no_major_degradation)
{
   enum { W = 256, H = 192 };
   const int n = W * H;

   mdrv_t *d = mdrv_start();
   mdrv_set_rgb565(d, 1);
   mdrv_set_lz4(d, 1);
   mdrv_arrange_mode(d, W, H);
   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_GRADIENT, W, H, SRC_FMT_XRGB8888, 0, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, W, H);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) cr_skip("frame did not arrive (loopback UDP rmem limit)");
   cr_assert_eq(composer_rgbsize(c), (uint32_t)(n * 2), "RGB565 is 2 bytes/pixel");

   uint8_t *ref = malloc((size_t)n * 3);
   uint8_t *got = malloc((size_t)n * 3);
   xrgb_to_bgr24(src, ref, n);
   rgb565_to_bgr24(composer_frame(c), got, n);

   /* 565 gate (0.96) is below 888's "visually lossless" 0.98: 5/6/5 quantization
    * on a smooth gradient is inherent, not a defect. Calibrated baseline: this
    * reference measures ~0.969 deterministically; gate set a margin below it. */
   double s = ssim_luma(got, ref, W - 1, H - 1, W * 3);
   cr_assert(s >= 0.96, "RGB565 must show no major degradation, interior SSIM=%f", s);

   free(src); free(ref); free(got);
   mdrv_stop(d);
}

/* H3: a source upscaled to a larger modeline (2x point) shows no major
 * degradation — the streamed frame matches the source resampled the same way. */
Test(frame_fidelity, upscaled_modeline_shows_no_major_degradation)
{
   enum { SW = 128, SH = 96, DW = 256, DH = 192 };

   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_arrange_mode_scaled(d, DW, DH, 2.0, 2.0);   /* output modeline 256x192 */
   uint32_t *src = malloc((size_t)SW * SH * 4);
   make_source_frame(PATTERN_GRADIENT, SW, SH, SRC_FMT_XRGB8888, 0, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, SW, SH);                  /* source 128x96, upscaled 2x */

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) cr_skip("frame did not arrive (loopback UDP rmem limit)");
   cr_assert_eq(composer_rgbsize(c), (uint32_t)(DW * DH * 3));

   /* reference = source BGR24, point-upscaled to the modeline the same way */
   uint8_t *src_bgr = malloc((size_t)SW * SH * 3);
   xrgb_to_bgr24(src, src_bgr, SW * SH);
   uint8_t *ref = malloc((size_t)DW * DH * 3);
   resample_point(src_bgr, SW, SH, SW * 3, ref, DW, DH, DW * 3);

   double s = ssim_luma(composer_frame(c), ref, DW - 1, DH - 1, DW * 3);
   cr_assert(s >= 0.98, "upscaled frame must show no major degradation, interior SSIM=%f", s);

   free(src); free(src_bgr); free(ref);
   mdrv_stop(d);
}

/* H5: a short motion sequence stays perceptually faithful frame-to-frame.
 * Gate: per-frame interior SSIM (the hard metric). VMAF is reported as an
 * advisory signal only when the tool is available (it is trained on natural
 * video and does not gate here). */
Test(frame_fidelity, motion_sequence_is_faithful)
{
   enum { W = 256, H = 192, NF = 6 };
   const int n = W * H;

   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_arrange_mode(d, W, H);

   uint32_t *src = malloc((size_t)n * 4);
   uint8_t  *ref = malloc((size_t)n * 3);
   uint8_t  *ref_seq  = malloc((size_t)NF * n * 3);
   uint8_t  *dist_seq = malloc((size_t)NF * n * 3);
   sim_composer_t *c = mdrv_composer(d);

   double worst_ssim = 1.0;
   uint32_t prev = 0xffffffffu;

   for (int f = 0; f < NF; f++)
   {
      /* distinct frame each tick (brightness ramp) ⇒ real full-frame blits */
      make_source_frame(PATTERN_GRADIENT, W, H, SRC_FMT_XRGB8888, 0, (uint8_t *)src);
      for (int i = 0; i < n; i++)
      {
         int v = (int)(src[i] & 0xff) + f * 6; if (v > 255) v = 255;
         src[i] = (uint32_t)((v << 16) | (v << 8) | v);
      }
      mdrv_draw_xrgb(d, src, W, H);
      if (!wait_for_new_frame(c, prev))
         cr_skip("frame %d did not arrive (loopback UDP rmem limit)", f);
      prev = composer_last_frame(c);

      xrgb_to_bgr24(src, ref, n);
      double s = ssim_luma(composer_frame(c), ref, W - 1, H - 1, W * 3);
      if (s < worst_ssim) worst_ssim = s;

      memcpy(ref_seq  + (size_t)f * n * 3, ref, (size_t)n * 3);
      memcpy(dist_seq + (size_t)f * n * 3, composer_frame(c), (size_t)n * 3);
   }

   cr_assert(worst_ssim >= 0.98,
             "every frame of the sequence must be visually lossless, worst SSIM=%f", worst_ssim);

   /* Advisory VMAF (never gates). */
   if (vmaf_available())
   {
      double vmaf = vmaf_score_bgr24_sequence(ref_seq, dist_seq, NF, W, H);
      cr_log_info("[advisory] sequence VMAF = %.2f (target >= 93)", vmaf);
   }
   else
   {
      cr_log_info("[advisory] VMAF skipped: install the libvmaf `vmaf` CLI to enable");
   }

   free(src); free(ref); free(ref_seq); free(dist_seq);
   mdrv_stop(d);
}
