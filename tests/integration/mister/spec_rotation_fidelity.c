/* Rotation interior characterization: prove the rotated blit formulas
 * (gfx_mister.c :337 VERTICAL, :340 FLIPPED_ROTATED) reproduce the source as a
 * faithful 90-degree rotation BEFORE the edge fix touches their shared loop
 * bound. Uses a directional gradient (varies mostly in x) so a real rotation is
 * distinguishable from a no-op. Handedness (CW vs CCW) is an implementation
 * detail, so we accept whichever 90-degree rotation matches — the same
 * "accept-the-parity-the-pipeline-picks" approach used for interlaced fields.
 *
 * Interior only (W-1 x H-1): the last row/column are dropped by the known
 * off-by-one (characterized red in spec_edge_fidelity.c) and excluded here. */
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

/* ORIENTATION_* (gfx/video_defines.h): NORMAL=0, VERTICAL=1, FLIPPED=2,
 * FLIPPED_ROTATED=3. */
#define ORI_VERTICAL        1
#define ORI_FLIPPED         2
#define ORI_FLIPPED_ROTATED 3

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

/* Square N x N BGR24 rotations. */
static void rot90_cw(const uint8_t *s, uint8_t *d, int N)
{
   for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++)
         memcpy(d + (y*N + x)*3, s + ((N-1-x)*N + y)*3, 3);
}
static void rot90_ccw(const uint8_t *s, uint8_t *d, int N)
{
   for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++)
         memcpy(d + (y*N + x)*3, s + (x*N + (N-1-y))*3, 3);
}

/* Stream a square gradient under `rotation`; assert the interior is a faithful
 * 90-degree rotation of the source (either handedness), and that the unrotated
 * source scores strictly lower (i.e. rotation actually happened). */
static void assert_rotated_interior(unsigned rotation, const char *name)
{
   /* N >= 200: mister_set_mode rejects modes narrower than 200 (gfx_mister.c:674).
    * Square keeps the rot90 reference simple (rot_width == rot_height). */
   enum { N = 200 };
   const int n = N * N;
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_set_rotation(d, rotation);
   mdrv_arrange_mode(d, N, N);

   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_GRADIENT, N, N, SRC_FMT_XRGB8888, 0, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, N, N);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { free(src); mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }

   uint8_t *sb  = malloc((size_t)n * 3);
   uint8_t *cw  = malloc((size_t)n * 3);
   uint8_t *ccw = malloc((size_t)n * 3);
   xrgb_to_bgr24(src, sb, n);
   rot90_cw(sb, cw, N);
   rot90_ccw(sb, ccw, N);

   /* Compare a 1px-inset interior (rows/cols 1..N-2): a rotation moves the
    * dropped edge to a different border (VERTICAL drops the top row + right
    * column, not bottom-right), so insetting all four sides excludes the known
    * off-by-one regardless of orientation. */
   const int off = (N + 1) * 3;   /* skip first row and first column */
   const uint8_t *got = composer_frame(c);
   double s_cw  = ssim_luma(got + off, cw  + off, N-2, N-2, N*3);
   double s_ccw = ssim_luma(got + off, ccw + off, N-2, N-2, N*3);
   double s_id  = ssim_luma(got + off, sb  + off, N-2, N-2, N*3);
   double best  = s_cw > s_ccw ? s_cw : s_ccw;
   cr_log_info("%s interior SSIM: cw=%.4f ccw=%.4f identity=%.4f", name, s_cw, s_ccw, s_id);

   cr_assert(best >= 0.98, "%s must reproduce a faithful 90-degree rotation, best SSIM=%f", name, best);
   cr_assert(best > s_id + 0.01, "%s output must be rotated, not a no-op (best=%f id=%f)", name, best, s_id);

   free(src); free(sb); free(cw); free(ccw);
   mdrv_stop(d);
}

/* B0: VERTICAL (rotation 1, formula :337) interior is a faithful rotation. */
Test(rotation_fidelity, vertical_interior_is_faithful_rotation)
{
   assert_rotated_interior(ORI_VERTICAL, "vertical");
}

/* B2: FLIPPED_ROTATED (rotation 3, formula :340) interior is a faithful
 * rotation (the opposite handedness from VERTICAL). */
Test(rotation_fidelity, flipped_rotated_interior_is_faithful_rotation)
{
   assert_rotated_interior(ORI_FLIPPED_ROTATED, "flipped_rotated");
}

/* B4: FLIPPED (rotation 2) has rotation & 1 == 0, so the blit takes the NORMAL
 * formula (:334) with normal stepping — mister_draw does not rotate it. This
 * documents that FLIPPED shares the normal path's edge fate (the H1 / last
 * row+column off-by-one), rather than being a distinct index path. So the
 * interior matches the identity source, not a 90-degree rotation. */
Test(rotation_fidelity, flipped_uses_normal_formula_identity_interior)
{
   enum { N = 200 };
   const int n = N * N;
   mdrv_t *d = mdrv_start();
   mdrv_set_lz4(d, 1);
   mdrv_set_rotation(d, ORI_FLIPPED);
   mdrv_arrange_mode(d, N, N);

   uint32_t *src = malloc((size_t)n * 4);
   make_source_frame(PATTERN_GRADIENT, N, N, SRC_FMT_XRGB8888, 0, (uint8_t *)src);
   mdrv_draw_xrgb(d, src, N, N);

   sim_composer_t *c = mdrv_composer(d);
   if (!wait_for_frame(c)) { free(src); mdrv_stop(d); cr_skip("frame did not arrive (rmem)"); }

   uint8_t *sb = malloc((size_t)n * 3);
   xrgb_to_bgr24(src, sb, n);
   const int off = (N + 1) * 3;
   double s = ssim_luma(composer_frame(c) + off, sb + off, N-2, N-2, N*3);
   cr_assert(s >= 0.999, "FLIPPED must use the normal formula (identity interior), SSIM=%f", s);

   free(src); free(sb);
   mdrv_stop(d);
}
