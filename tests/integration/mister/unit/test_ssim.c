/* SSIM kernel — pure logic. One behavior per test. */
#include <criterion/criterion.h>
#include <stdint.h>
#include <string.h>
#include "metrics/ssim.h"

#define W 64
#define H 48
#define STRIDE (W * 3)

/* B1: two identical frames are structurally identical. */
Test(ssim, identical_frames_score_one)
{
   uint8_t a[H * STRIDE];
   uint8_t b[H * STRIDE];
   memset(a, 0x7f, sizeof(a));
   memcpy(b, a, sizeof(a));

   double s = ssim_luma(a, b, W, H, STRIDE);

   cr_assert(s >= 0.9999, "identical frames must score ~1.0, got %f", s);
}

/* B2: maximally different frames (all-black vs all-white) score near 0. */
Test(ssim, black_vs_white_scores_near_zero)
{
   uint8_t black[H * STRIDE];
   uint8_t white[H * STRIDE];
   memset(black, 0x00, sizeof(black));
   memset(white, 0xff, sizeof(white));

   double s = ssim_luma(black, white, W, H, STRIDE);

   cr_assert(s < 0.05, "black vs white must score near 0, got %f", s);
}

/* Fill a BGR24 buffer with a horizontal luma gradient (gives windows nonzero
 * variance so SSIM is meaningful, unlike a flat fill). */
static void fill_gradient(uint8_t *buf)
{
   for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++)
      {
         uint8_t v = (uint8_t)((x * 255) / (W - 1));
         uint8_t *p = buf + y * STRIDE + x * 3;
         p[0] = p[1] = p[2] = v;
      }
}

/* B3: a frame vs the same frame with small additive noise scores high but
 * strictly below 1.0. */
Test(ssim, small_noise_scores_high_but_below_one)
{
   uint8_t a[H * STRIDE];
   uint8_t b[H * STRIDE];
   fill_gradient(a);
   memcpy(b, a, sizeof(a));

   /* Perturb every pixel by a small, deterministic amount. */
   for (int i = 0; i < H * STRIDE; i++)
   {
      int d = (i % 7) - 3;                 /* -3..+3 */
      int v = (int)b[i] + d;
      b[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
   }

   double s = ssim_luma(a, b, W, H, STRIDE);

   cr_assert(s > 0.9,  "small noise should still score high, got %f", s);
   cr_assert(s < 1.0,  "noise must drop SSIM below 1.0, got %f", s);
}

/* B4: SSIM is computed on luma only. Two frames with very different chroma but
 * (near-)equal luma must score ~1.0 — proving chroma is ignored. Solid blue
 * (BGR [255,0,0]) has luma 0.114*255 = 29.07; a solid gray of value 29 has luma
 * 29. Equal luma, opposite chroma. */
Test(ssim, equal_luma_different_chroma_scores_one)
{
   uint8_t blue[H * STRIDE];
   uint8_t gray[H * STRIDE];
   for (int i = 0; i < H * STRIDE; i += 3)
   {
      blue[i] = 255; blue[i + 1] = 0; blue[i + 2] = 0;   /* B,G,R */
      gray[i] = gray[i + 1] = gray[i + 2] = 29;
   }

   double s = ssim_luma(blue, gray, W, H, STRIDE);

   cr_assert(s > 0.99, "equal-luma frames must score ~1.0 (luma-only), got %f", s);
}
