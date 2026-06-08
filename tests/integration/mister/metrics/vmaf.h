#ifndef MISTER_TEST_VMAF_H
#define MISTER_TEST_VMAF_H

#include <stdint.h>

/* VMAF is an *advisory* perceptual metric here (secondary to SSIM): it is
 * trained on natural video and is unreliable on synthetic/pixel-doubled
 * emulator content, so it never gates the build. It also needs an external
 * tool. This helper reports availability and, when present, scores a pair of
 * BGR24 frame sequences by dumping Y4M and invoking the `vmaf` CLI. */

/* 1 if a usable `vmaf` CLI is on PATH, else 0. */
int vmaf_available(void);

/* Score two equal-length BGR24 sequences (advisory). Returns a VMAF score in
 * [0,100], or a negative value if unavailable / on error. `frames` is the
 * number of (ref,dist) frame pairs; each buffer is frames*w*h*3 bytes. */
double vmaf_score_bgr24_sequence(const uint8_t *ref, const uint8_t *dist,
                                 int frames, int w, int h);

#endif /* MISTER_TEST_VMAF_H */
