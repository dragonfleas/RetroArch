#include "sim/factories.h"
#include "sim/protocol.h"
#include <string.h>

size_t make_ack(uint8_t *out, uint32_t frame_echo, uint8_t status_bits)
{
   uint16_t zero16 = 0;
   uint32_t zero32 = 0;
   memcpy(&out[0],  &frame_echo, 4);   /* frameEcho  */
   memcpy(&out[4],  &zero16,     2);   /* vCountEcho */
   memcpy(&out[6],  &zero32,     4);   /* frame      */
   memcpy(&out[10], &zero16,     2);   /* vCount     */
   out[12] = status_bits;              /* status     */
   return ACK_SIZE;
}

static uint16_t xrgb8888_to_rgb565(uint32_t c)
{
   uint32_t r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
   return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* XRGB8888 value of a pattern at (x,y). For GRADIENT, a gray ramp varying with
 * x (and a slight y tilt) so the frame has real structure for SSIM. */
static uint32_t pattern_pixel(sim_pattern_t pat, int x, int y, int w, int h, uint32_t color)
{
   if (pat == PATTERN_GRADIENT)
   {
      int v = ((x * 200) / (w > 1 ? w - 1 : 1)) + ((y * 55) / (h > 1 ? h - 1 : 1));
      if (v > 255) v = 255;
      return (uint32_t)((v << 16) | (v << 8) | v);
   }
   return color;  /* PATTERN_SOLID */
}

size_t make_source_frame(sim_pattern_t pat, int w, int h, int fmt,
                         uint32_t color, uint8_t *out)
{
   if (fmt == SRC_FMT_RGB565)
   {
      uint16_t *p = (uint16_t *)out;
      for (int y = 0; y < h; y++)
         for (int x = 0; x < w; x++)
            p[y * w + x] = xrgb8888_to_rgb565(pattern_pixel(pat, x, y, w, h, color));
      return (size_t)w * h * 2;
   }
   /* default: XRGB8888 */
   uint32_t *p = (uint32_t *)out;
   for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
         p[y * w + x] = pattern_pixel(pat, x, y, w, h, color);
   return (size_t)w * h * 4;
}

int parse_init(const uint8_t *buf, size_t len, sim_init_t *out)
{
   if (len != 5 || buf[0] != CMD_INIT)
      return -1;
   out->lz4       = buf[1];
   out->rate_code = buf[2];
   out->chan      = buf[3];
   out->rgb_mode  = buf[4];
   return 0;
}

int parse_switchres(const uint8_t *buf, size_t len, sim_switchres_t *out)
{
   if (len != 26 || buf[0] != CMD_SWITCHRES)
      return -1;
   memcpy(&out->pclock,  &buf[1],  8);
   memcpy(&out->hActive, &buf[9],  2);
   memcpy(&out->hBegin,  &buf[11], 2);
   memcpy(&out->hEnd,    &buf[13], 2);
   memcpy(&out->hTotal,  &buf[15], 2);
   memcpy(&out->vActive, &buf[17], 2);
   memcpy(&out->vBegin,  &buf[19], 2);
   memcpy(&out->vEnd,    &buf[21], 2);
   memcpy(&out->vTotal,  &buf[23], 2);
   out->interlace = buf[25];
   return 0;
}

uint32_t compute_rgbsize(const sim_switchres_t *m, uint8_t rgb_mode)
{
   uint32_t px = (uint32_t)m->hActive * m->vActive;
   uint32_t bytes = (rgb_mode == RGB_A888) ? (px << 2)
                  : (rgb_mode == RGB_565)  ? (px << 1)
                                           : (px * 3);
   if (m->interlace == 1)
      bytes >>= 1;
   return bytes;
}

int parse_blit_header(const uint8_t *buf, size_t len, sim_blit_header_t *out)
{
   if (buf[0] != CMD_BLIT_FIELD_VSYNC)
      return -1;

   memset(out, 0, sizeof(*out));
   memcpy(&out->frame, &buf[1], 4);
   out->field = buf[5];
   memcpy(&out->vsync, &buf[6], 2);

   switch (len)
   {
      case 8:  /* raw, full frame */
         return 0;
      case 9:  /* raw duplicate */
         out->is_dup = (buf[8] == 0x01);
         return 0;
      case 12: /* lz4, non-delta */
         out->is_lz4 = 1;
         memcpy(&out->csize, &buf[8], 4);
         return 0;
      case 13: /* lz4 delta */
         out->is_lz4 = 1;
         out->is_delta = (buf[12] == 0x01);
         memcpy(&out->csize, &buf[8], 4);
         return 0;
      default:
         return -1;
   }
}
