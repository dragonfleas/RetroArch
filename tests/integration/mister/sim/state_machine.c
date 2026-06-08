#include "sim/state_machine.h"
#include "sim/protocol.h"
#include "sim/factories.h"
#include "lz4.h"
#include <stdlib.h>
#include <string.h>

struct sim_composer {
   int             have_init;
   int             have_mode;
   sim_init_t      init;
   sim_switchres_t mode;
   uint32_t        rgbsize;

   /* blit reassembly */
   int             receiving;     /* mid-payload */
   uint32_t        expected;      /* payload bytes to collect */
   uint32_t        received;      /* collected so far */
   uint8_t        *recv_buf;      /* incoming payload (raw or compressed) */
   uint32_t        recv_cap;
   int             cur_is_lz4;
   uint32_t        cur_frame;

   uint8_t        *frame;         /* reconstructed rgbsize bytes */
   uint32_t        frame_cap;
   int             have_frame;
   uint32_t        last_frame;
};

static void ensure_cap(uint8_t **buf, uint32_t *cap, uint32_t need)
{
   if (*cap < need)
   {
      free(*buf);
      *buf = malloc(need);
      *cap = need;
   }
}

/* Payload fully collected: produce the reconstructed frame. */
static void finalize_frame(sim_composer_t *c)
{
   ensure_cap(&c->frame, &c->frame_cap, c->rgbsize);
   if (c->cur_is_lz4)
      LZ4_decompress_safe((const char *)c->recv_buf, (char *)c->frame,
                          (int)c->received, (int)c->rgbsize);
   else
      memcpy(c->frame, c->recv_buf, c->rgbsize);  /* raw: payload IS the frame */
   c->have_frame  = 1;
   c->last_frame  = c->cur_frame;
   c->receiving   = 0;
}

static void begin_blit(sim_composer_t *c, const uint8_t *buf, size_t len)
{
   sim_blit_header_t h;
   if (parse_blit_header(buf, len, &h) != 0)
      return;
   c->cur_frame = h.frame;
   if (h.is_dup)
   {
      /* duplicate of the previous frame; nothing new streams */
      c->have_frame = 1;
      c->last_frame = h.frame;
      return;
   }
   c->cur_is_lz4 = h.is_lz4;
   c->expected   = h.is_lz4 ? h.csize : c->rgbsize;
   c->received   = 0;
   ensure_cap(&c->recv_buf, &c->recv_cap, c->expected);
   c->receiving  = 1;
}

sim_composer_t *composer_create(void)
{
   sim_composer_t *c = calloc(1, sizeof(*c));
   return c;
}

void composer_destroy(sim_composer_t *c)
{
   if (!c) return;
   free(c->recv_buf);
   free(c->frame);
   free(c);
}

void composer_feed(sim_composer_t *c, const uint8_t *buf, size_t len)
{
   if (len == 0)
      return;

   /* Mid-blit: these datagrams are raw payload chunks, not commands. */
   if (c->receiving)
   {
      uint32_t take = (uint32_t)len;
      if (c->received + take > c->expected)
         take = c->expected - c->received;
      memcpy(c->recv_buf + c->received, buf, take);
      c->received += take;
      if (c->received >= c->expected)
         finalize_frame(c);
      return;
   }

   switch (buf[0])
   {
      case CMD_INIT:
         if (parse_init(buf, len, &c->init) == 0)
            c->have_init = 1;
         break;
      case CMD_SWITCHRES:
         if (parse_switchres(buf, len, &c->mode) == 0)
         {
            c->have_mode = 1;
            c->rgbsize = compute_rgbsize(&c->mode, c->init.rgb_mode);
         }
         break;
      case CMD_BLIT_FIELD_VSYNC:
         c->have_frame = 0;
         begin_blit(c, buf, len);
         break;
      default:
         break;
   }
}

int composer_is_ready(const sim_composer_t *c)
{
   return c->have_init && c->have_mode;
}

uint16_t composer_width(const sim_composer_t *c)  { return c->mode.hActive; }
uint16_t composer_height(const sim_composer_t *c) { return c->mode.vActive; }
uint32_t composer_rgbsize(const sim_composer_t *c){ return c->rgbsize; }

int composer_have_frame(const sim_composer_t *c)  { return c->have_frame; }
int composer_is_receiving(const sim_composer_t *c) { return c->receiving; }
uint32_t composer_last_frame(const sim_composer_t *c) { return c->last_frame; }
const uint8_t *composer_frame(const sim_composer_t *c)
{
   return c->have_frame ? c->frame : NULL;
}

size_t composer_next_ack(const sim_composer_t *c, uint8_t *out)
{
   return make_ack(out, c->last_frame, ST_HEALTHY);
}
