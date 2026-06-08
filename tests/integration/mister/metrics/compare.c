#include "metrics/compare.h"
#include <stddef.h>

static uint8_t clamp8(double v)
{
   if (v < 0.0)   return 0;
   if (v > 255.0) return 255;
   return (uint8_t)(v + 0.5);
}

void frame_to_yuv444p(const uint8_t *px, int width, int height, int stride,
                      int is_bgr, uint8_t *y, uint8_t *u, uint8_t *v)
{
   for (int row = 0; row < height; row++)
   {
      const uint8_t *p = px + (size_t)row * stride;
      uint8_t *yo = y + (size_t)row * width;
      uint8_t *uo = u + (size_t)row * width;
      uint8_t *vo = v + (size_t)row * width;
      for (int x = 0; x < width; x++)
      {
         double r, g, b;
         if (is_bgr) { b = p[0]; g = p[1]; r = p[2]; }
         else        { r = p[0]; g = p[1]; b = p[2]; }
         p += 3;
         /* full-range BT.601 */
         yo[x] = clamp8( 0.299 * r + 0.587 * g + 0.114 * b);
         uo[x] = clamp8(-0.169 * r - 0.331 * g + 0.500 * b + 128.0);
         vo[x] = clamp8( 0.500 * r - 0.419 * g - 0.081 * b + 128.0);
      }
   }
}

void resample_point(const uint8_t *src, int sw, int sh, int sstride,
                    uint8_t *dst, int dw, int dh, int dstride)
{
   for (int dy = 0; dy < dh; dy++)
   {
      int sy = (int)((size_t)dy * sh / dh);
      const uint8_t *srow = src + (size_t)sy * sstride;
      uint8_t *drow = dst + (size_t)dy * dstride;
      for (int dx = 0; dx < dw; dx++)
      {
         int sx = (int)((size_t)dx * sw / dw);
         const uint8_t *sp = srow + (size_t)sx * 3;
         uint8_t *dp = drow + (size_t)dx * 3;
         dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
      }
   }
}
