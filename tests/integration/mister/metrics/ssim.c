#include "metrics/ssim.h"
#include <stddef.h>

/* Standard SSIM over non-overlapping 8x8 windows, computed on BT.601 luma.
 * Stabilising constants for 8-bit data (L=255): C1=(0.01L)^2, C2=(0.03L)^2. */

#define WIN 8
#define C1 (0.01 * 255.0 * 0.01 * 255.0)
#define C2 (0.03 * 255.0 * 0.03 * 255.0)

static double luma_at(const uint8_t *p, int x, int y, int stride)
{
   const uint8_t *px = p + (size_t)y * stride + (size_t)x * 3;
   /* BGR24 byte order */
   double b = px[0], g = px[1], r = px[2];
   return 0.299 * r + 0.587 * g + 0.114 * b;
}

double ssim_luma(const uint8_t *a, const uint8_t *b,
                 int width, int height, int stride)
{
   double ssim_sum = 0.0;
   int windows = 0;

   for (int by = 0; by < height; by += WIN)
   {
      for (int bx = 0; bx < width; bx += WIN)
      {
         int wx = (bx + WIN <= width)  ? WIN : width  - bx;
         int wy = (by + WIN <= height) ? WIN : height - by;
         double n = (double)(wx * wy);

         double sa = 0, sb = 0;
         for (int y = 0; y < wy; y++)
            for (int x = 0; x < wx; x++)
            {
               sa += luma_at(a, bx + x, by + y, stride);
               sb += luma_at(b, bx + x, by + y, stride);
            }
         double ma = sa / n, mb = sb / n;

         double va = 0, vb = 0, cov = 0;
         for (int y = 0; y < wy; y++)
            for (int x = 0; x < wx; x++)
            {
               double da = luma_at(a, bx + x, by + y, stride) - ma;
               double db = luma_at(b, bx + x, by + y, stride) - mb;
               va  += da * da;
               vb  += db * db;
               cov += da * db;
            }
         va /= n; vb /= n; cov /= n;

         double num = (2.0 * ma * mb + C1) * (2.0 * cov + C2);
         double den = (ma * ma + mb * mb + C1) * (va + vb + C2);
         ssim_sum += num / den;
         windows++;
      }
   }

   return windows ? ssim_sum / windows : 1.0;
}
