#include "metrics/vmaf.h"
#include "metrics/compare.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The vmaf binary: $VMAF_BIN if set (e.g. a local build), else `vmaf` on PATH. */
static const char *vmaf_bin(void)
{
   const char *b = getenv("VMAF_BIN");
   return (b && *b) ? b : "vmaf";
}

int vmaf_available(void)
{
   char cmd[600];
   snprintf(cmd, sizeof(cmd), "%s --version >/dev/null 2>&1", vmaf_bin());
   return system(cmd) == 0;
}

/* Dump a BGR24 sequence as a YUV444P Y4M file (the format libvmaf consumes). */
static int dump_y4m(const char *path, const uint8_t *seq, int frames, int w, int h)
{
   FILE *f = fopen(path, "wb");
   if (!f)
      return -1;
   fprintf(f, "YUV4MPEG2 W%d H%d F25:1 Ip A1:1 C444\n", w, h);
   uint8_t *y = malloc((size_t)w * h);
   uint8_t *u = malloc((size_t)w * h);
   uint8_t *v = malloc((size_t)w * h);
   for (int fr = 0; fr < frames; fr++)
   {
      const uint8_t *frame = seq + (size_t)fr * w * h * 3;
      frame_to_yuv444p(frame, w, h, w * 3, 1 /*BGR*/, y, u, v);
      fputs("FRAME\n", f);
      fwrite(y, 1, (size_t)w * h, f);
      fwrite(u, 1, (size_t)w * h, f);
      fwrite(v, 1, (size_t)w * h, f);
   }
   free(y); free(u); free(v);
   fclose(f);
   return 0;
}

/* Pull pooled_metrics.vmaf.mean out of the vmaf --json output. */
static double parse_pooled_vmaf(const char *json_path)
{
   FILE *f = fopen(json_path, "rb");
   if (!f)
      return -1.0;
   fseek(f, 0, SEEK_END);
   long sz = ftell(f);
   fseek(f, 0, SEEK_SET);
   char *buf = malloc((size_t)sz + 1);
   if (!buf) { fclose(f); return -1.0; }
   size_t rd = fread(buf, 1, (size_t)sz, f);
   buf[rd] = '\0';
   fclose(f);

   double score = -1.0;
   char *pm = strstr(buf, "\"pooled_metrics\"");
   char *v  = pm ? strstr(pm, "\"vmaf\"") : NULL;
   char *m  = v  ? strstr(v,  "\"mean\"") : NULL;
   char *c  = m  ? strchr(m, ':') : NULL;
   if (c)
      score = atof(c + 1);
   free(buf);
   return score;
}

double vmaf_score_bgr24_sequence(const uint8_t *ref, const uint8_t *dist,
                                 int frames, int w, int h)
{
   if (!vmaf_available())
      return -1.0;  /* advisory metric unavailable; caller skips */

   const char *refp = "/tmp/mister_vmaf_ref.y4m";
   const char *distp = "/tmp/mister_vmaf_dist.y4m";
   const char *outp = "/tmp/mister_vmaf_out.json";
   if (dump_y4m(refp, ref, frames, w, h) != 0 ||
       dump_y4m(distp, dist, frames, w, h) != 0)
      return -1.0;

   char cmd[800];
   snprintf(cmd, sizeof(cmd),
            "%s -r %s -d %s --json -o %s -q >/dev/null 2>&1",
            vmaf_bin(), refp, distp, outp);
   if (system(cmd) != 0)
      return -1.0;

   return parse_pooled_vmaf(outp);
}
