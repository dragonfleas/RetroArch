/* Pin the delta path — mister_buffer_delta bytes + the match_delta passed to
 * gmw_blit — which the SSIM specs don't observe (the sim ignores deltas, and
 * match_delta never reaches the wire; captured here via the gmw_blit spy). Each
 * test fills a solid frame and draws twice: first vs the cleared buffer (diff),
 * then vs an identical buffer (match). lz4=0 ⇒ delta_frames on. */
#include <criterion/criterion.h>
#include <stdint.h>
#include <stdlib.h>

#include "driver/mister_stream_driver.h"
#include "seams/spy_gmw_blit.h"

extern char *gmw_get_pBufferBlitDelta(void);

#define FILL_XRGB 0x00C84020u                  /* R=0xC8 G=0x40 B=0x20 */
static const uint8_t FILL_BGR[3] = { 0x20, 0x40, 0xC8 };
static const uint8_t ZERO_BGR[3] = { 0, 0, 0 };

static const uint8_t *delta_buf(void) { return (const uint8_t *)gmw_get_pBufferBlitDelta(); }

static void assert_delta_region(size_t pixels, const uint8_t pat[3], const char *msg)
{
   const uint8_t *b = delta_buf();
   for (size_t p = 0; p < pixels; p++)
      for (int k = 0; k < 3; k++)
         cr_assert_eq(b[p * 3 + k], pat[k], "%s: delta[%zu].%d", msg, p, k);
}

Test(delta_argb8888, diff_then_match)
{
   enum { W = 256, H = 240 };
   const size_t px = (size_t)W * H;
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 0);
   mdrv_arrange_mode(d, W, H);

   uint32_t *src = malloc(px * 4);
   for (size_t i = 0; i < px; i++) src[i] = FILL_XRGB;

   mdrv_draw_xrgb(d, src, W, H);
   cr_assert_eq(spy_gmw_last_match_delta(), 0, "no channel matches the cleared buffer");
   assert_delta_region(px, FILL_BGR, "argb diff");

   mdrv_draw_xrgb(d, src, W, H);
   cr_assert_eq(spy_gmw_last_match_delta(), 3u * W * H, "all channels match an identical frame");
   assert_delta_region(px, ZERO_BGR, "argb match");

   free(src);
   mdrv_stop(d);
}

Test(delta_bgr24, diff_then_match)
{
   enum { W = 200, H = 160 };
   const size_t px = (size_t)W * H;
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 0);
   mdrv_arrange_mode(d, W, H);

   uint32_t *src = malloc(px * 4);
   for (size_t i = 0; i < px; i++) src[i] = FILL_XRGB;   /* solid: invariant to the readback vflip */

   mdrv_draw_hw(d, src, W, H);
   cr_assert_eq(spy_gmw_last_match_delta(), 0, "hw: no channel matches the cleared buffer");
   assert_delta_region(px, FILL_BGR, "bgr24 diff");

   mdrv_draw_hw(d, src, W, H);
   cr_assert_eq(spy_gmw_last_match_delta(), 3u * W * H, "hw: all channels match an identical frame");
   assert_delta_region(px, ZERO_BGR, "bgr24 match");

   free(src);
   mdrv_stop(d);
}

#define FILL_565 0xCC04u                       /* r5=0x19 g6=0x20 b5=0x04 */
static const uint8_t EXP565_BGR[3] = { 0x21, 0x82, 0xCE };  /* 5/6/5 expanded to 8 bits */
Test(delta_rgb565_non565, diff_then_match)
{
   enum { W = 200, H = 160 };
   const size_t px = (size_t)W * H;
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 0);
   mdrv_arrange_mode(d, W, H);

   /* XRGB first latches rgb_mode=RGB888, so the 565 frame hits the non-565 sub-branch */
   uint32_t *xrgb_zeros = calloc(px, 4);
   mdrv_draw_xrgb(d, xrgb_zeros, W, H);

   uint16_t *src = malloc(px * 2);
   for (size_t i = 0; i < px; i++) src[i] = FILL_565;

   mdrv_draw_rgb565(d, src, W, H);
   cr_assert_eq(spy_gmw_last_match_delta(), 0, "565: no expanded channel matches the cleared buffer");
   assert_delta_region(px, EXP565_BGR, "565 diff");

   mdrv_draw_rgb565(d, src, W, H);
   cr_assert_eq(spy_gmw_last_match_delta(), 3u * W * H, "565: all channels match an identical frame");
   assert_delta_region(px, ZERO_BGR, "565 match");

   free(src);
   free(xrgb_zeros);
   mdrv_stop(d);
}

/* 565-mode sub-branch: 2-byte raw write + 2-channel delta (pix_size=2). */
Test(delta_rgb565_raw, diff_then_match)
{
   enum { W = 200, H = 160 };
   const size_t px = (size_t)W * H;
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 0);
   mdrv_set_rgb565(d, 1);
   mdrv_arrange_mode(d, W, H);

   uint16_t *src = malloc(px * 2);
   for (size_t i = 0; i < px; i++) src[i] = FILL_565;   /* 0xCC04 → bytes {0x04,0xCC} */

   mdrv_draw_rgb565(d, src, W, H);
   const uint8_t *db = delta_buf();           /* buffer is allocated during the first draw */
   cr_assert_eq(spy_gmw_last_match_delta(), 0, "565raw: no channel matches the cleared buffer");
   for (size_t p = 0; p < px; p++) {
      cr_assert_eq(db[p*2+0], 0x04, "565raw diff lo @%zu", p);
      cr_assert_eq(db[p*2+1], 0xCC, "565raw diff hi @%zu", p);
   }

   mdrv_draw_rgb565(d, src, W, H);
   cr_assert_eq(spy_gmw_last_match_delta(), 2u * W * H, "565raw: both channels match identical frame");
   for (size_t p = 0; p < px; p++) {
      cr_assert_eq(db[p*2+0], 0, "565raw match lo @%zu", p);
      cr_assert_eq(db[p*2+1], 0, "565raw match hi @%zu", p);
   }

   free(src);
   mdrv_stop(d);
}

/* Two upstream quirks: the delta is always zeroed (so even the diff draw reads
 * zero), and the match compares are unmasked, so on a repeat frame only channel 0
 * matches → match_delta == W*H, not 3*W*H. */
#define FILL_4444 0xABCDu
Test(delta_rgba4444, quirks_pinned)
{
   enum { W = 200, H = 160 };
   const size_t px = (size_t)W * H;
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 0);
   mdrv_arrange_mode(d, W, H);

   uint16_t *menu = malloc(px * 2);
   for (size_t i = 0; i < px; i++) menu[i] = FILL_4444;

   mdrv_draw_menu(d, menu, W, H);
   cr_assert_eq(spy_gmw_last_match_delta(), 0, "4444: nothing matches the cleared buffer");
   assert_delta_region(px, ZERO_BGR, "4444 diff delta is zeroed");

   mdrv_draw_menu(d, menu, W, H);
   cr_assert_eq(spy_gmw_last_match_delta(), (uint32_t)px, "4444: only channel 0 matches");
   assert_delta_region(px, ZERO_BGR, "4444 match delta is zeroed");

   free(menu);
   mdrv_stop(d);
}
