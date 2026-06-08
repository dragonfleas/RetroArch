#ifndef MISTER_TEST_COMPARE_H
#define MISTER_TEST_COMPARE_H

#include <stdint.h>

/* Convert a packed 24-bit frame to full-range BT.601 YUV444P planes.
 *
 *   px      : packed 3-bytes/pixel data
 *   is_bgr  : 1 if byte order is B,G,R (GroovyMiSTer); 0 if R,G,B
 *   y,u,v   : output planes, each width*height bytes (caller-allocated)
 *
 * Each plane is written tightly packed (stride == width). */
void frame_to_yuv444p(const uint8_t *px, int width, int height, int stride,
                      int is_bgr, uint8_t *y, uint8_t *u, uint8_t *v);

/* Nearest-neighbour (point) resample of a packed 24-bit frame.
 * Byte order is irrelevant (channels are copied as a unit). */
void resample_point(const uint8_t *src, int sw, int sh, int sstride,
                    uint8_t *dst, int dw, int dh, int dstride);

#endif /* MISTER_TEST_COMPARE_H */
