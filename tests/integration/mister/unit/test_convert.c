/* Color conversion & resample — pure logic. */
#include <criterion/criterion.h>
#include <stdint.h>
#include <string.h>
#include "metrics/compare.h"

#define NEAR(actual, expect, tol) \
   cr_assert(abs((int)(actual) - (int)(expect)) <= (tol), \
             "expected ~%d, got %d", (int)(expect), (int)(actual))

/* C1: known colors convert to expected full-range BT.601 YUV (±1). */
Test(convert, known_colors_to_yuv444p)
{
   /* one pixel each, fed as R,G,B (is_bgr = 0) */
   uint8_t white[3] = {255, 255, 255};
   uint8_t black[3] = {0, 0, 0};
   uint8_t red[3]   = {255, 0, 0};
   uint8_t y, u, v;

   frame_to_yuv444p(white, 1, 1, 3, 0, &y, &u, &v);
   NEAR(y, 255, 1); NEAR(u, 128, 1); NEAR(v, 128, 1);

   frame_to_yuv444p(black, 1, 1, 3, 0, &y, &u, &v);
   NEAR(y, 0, 1); NEAR(u, 128, 1); NEAR(v, 128, 1);

   frame_to_yuv444p(red, 1, 1, 3, 0, &y, &u, &v);
   NEAR(y, 76, 1); NEAR(u, 85, 1); NEAR(v, 255, 1);
}

/* C2: byte-order handling. The same color expressed as BGR (is_bgr=1) and as
 * mirrored RGB (is_bgr=0) must produce identical luma — proving we honor
 * GroovyMiSTer's BGR order rather than silently mislabeling channels. */
Test(convert, bgr_and_rgb_agree_on_luma)
{
   uint8_t bgr[3] = {10, 20, 30};   /* B=10 G=20 R=30 */
   uint8_t rgb[3] = {30, 20, 10};   /* same color, R,G,B order */
   uint8_t yb, yr, u, v;

   frame_to_yuv444p(bgr, 1, 1, 3, 1, &yb, &u, &v);
   frame_to_yuv444p(rgb, 1, 1, 3, 0, &yr, &u, &v);

   cr_assert_eq(yb, yr, "BGR luma %d != RGB luma %d", yb, yr);
}

/* C3: resampling to the same dimensions is the identity (byte-for-byte). */
Test(convert, resample_identity)
{
   uint8_t src[4 * 4 * 3];
   uint8_t dst[4 * 4 * 3];
   for (int i = 0; i < (int)sizeof(src); i++)
      src[i] = (uint8_t)(i * 3 + 1);
   memset(dst, 0, sizeof(dst));

   resample_point(src, 4, 4, 4 * 3, dst, 4, 4, 4 * 3);

   cr_assert_arr_eq(dst, src, sizeof(src), "same-size resample must be identity");
}

/* C4: nearest-neighbour 2x upscale replicates each source pixel into a 2x2
 * block. Use a distinct value per source pixel (encoded in the blue channel). */
Test(convert, resample_nearest_2x)
{
   /* 2x2 source: TL=10, TR=20, BL=30, BR=40 in the blue channel. */
   uint8_t src[2 * 2 * 3] = {
      10,0,0,  20,0,0,
      30,0,0,  40,0,0,
   };
   uint8_t dst[4 * 4 * 3];
   memset(dst, 0, sizeof(dst));

   resample_point(src, 2, 2, 2 * 3, dst, 4, 4, 4 * 3);

   /* blue channel of dst pixel (x,y) */
#define B(x,y) dst[((y) * 4 + (x)) * 3]
   cr_assert_eq(B(0,0), 10); cr_assert_eq(B(1,0), 10);
   cr_assert_eq(B(2,0), 20); cr_assert_eq(B(3,0), 20);
   cr_assert_eq(B(0,2), 30); cr_assert_eq(B(3,2), 40);
   cr_assert_eq(B(1,3), 30); cr_assert_eq(B(2,3), 40);
#undef B
}
