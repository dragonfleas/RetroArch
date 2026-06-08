#ifndef MISTER_TEST_SSIM_H
#define MISTER_TEST_SSIM_H

#include <stdint.h>

/* Structural Similarity Index between two BGR24 frames, computed on luma (Y).
 *
 *   a, b   : pointers to BGR24 pixel data (3 bytes/pixel, B,G,R order)
 *   width  : pixels per row
 *   height : rows
 *   stride : bytes per row (>= width*3)
 *
 * Returns a value in [0, 1]; 1.0 means structurally identical. Both buffers
 * must share dimensions and stride. */
double ssim_luma(const uint8_t *a, const uint8_t *b,
                 int width, int height, int stride);

#endif /* MISTER_TEST_SSIM_H */
