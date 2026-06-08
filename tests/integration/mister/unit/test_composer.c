/* Simulator L2 scenario composer / reassembly — pure logic.
 * Fed byte buffers directly (no sockets). */
#include <criterion/criterion.h>
#include <stdint.h>
#include <string.h>
#include "sim/protocol.h"
#include "sim/state_machine.h"
#include <stdlib.h>
#include "lz4.h"

/* helpers to assemble the command datagrams the composer consumes */
static size_t init_pkt(uint8_t *p, uint8_t lz4, uint8_t rgb)
{
   p[0] = CMD_INIT; p[1] = lz4; p[2] = 0; p[3] = 0; p[4] = rgb;
   return 5;
}
static size_t switchres_pkt(uint8_t *p, uint16_t w, uint16_t h, uint8_t interlace)
{
   double pclock = 25.0;
   memset(p, 0, 26);
   p[0] = CMD_SWITCHRES;
   memcpy(&p[1], &pclock, 8);
   memcpy(&p[9],  &w, 2);          /* hActive */
   memcpy(&p[17], &h, 2);          /* vActive */
   uint16_t htot = 800, vtot = 525;
   memcpy(&p[15], &htot, 2);
   memcpy(&p[23], &vtot, 2);
   p[25] = interlace;
   return 26;
}

/* E1: after INIT + SWITCHRES the composer is READY and reports geometry. */
Test(composer, ready_after_init_and_switchres)
{
   sim_composer_t *c = composer_create();
   uint8_t pkt[26];

   cr_assert(!composer_is_ready(c), "not ready before any command");

   composer_feed(c, pkt, init_pkt(pkt, LZ4_OFF, RGB_888));
   cr_assert(!composer_is_ready(c), "not ready with init only");

   composer_feed(c, pkt, switchres_pkt(pkt, 640, 480, 0));
   cr_assert(composer_is_ready(c), "ready after init + switchres");

   cr_assert_eq(composer_width(c), 640);
   cr_assert_eq(composer_height(c), 480);
   cr_assert_eq(composer_rgbsize(c), (uint32_t)(640 * 480 * 3));

   composer_destroy(c);
}

/* Feed a payload buffer to the composer sliced into MTU-sized datagrams,
 * exactly as SendStream() chunks it on the wire. */
static void feed_chunked(sim_composer_t *c, const uint8_t *payload, uint32_t n)
{
   uint32_t off = 0;
   while (off < n)
   {
      uint32_t chunk = (n - off >= MISTER_MTU_PAYLOAD) ? MISTER_MTU_PAYLOAD : (n - off);
      composer_feed(c, payload + off, chunk);
      off += chunk;
   }
}

/* E2: a raw RGB888 blit, with its payload split across MTU datagrams, is
 * reassembled byte-for-byte. */
Test(composer, raw_blit_reassembles_byte_exact)
{
   enum { W = 64, H = 48 };
   const uint32_t rgbsize = W * H * 3;   /* 9216 — spans several MTUs */

   sim_composer_t *c = composer_create();
   uint8_t pkt[26];
   composer_feed(c, pkt, init_pkt(pkt, LZ4_OFF, RGB_888));
   composer_feed(c, pkt, switchres_pkt(pkt, W, H, 0));
   cr_assert_eq(composer_rgbsize(c), rgbsize);

   /* deterministic payload */
   uint8_t *payload = malloc(rgbsize);
   for (uint32_t i = 0; i < rgbsize; i++) payload[i] = (uint8_t)(i % 251);

   /* raw blit header: 8 bytes, frame=5 */
   uint8_t hdr[8];
   hdr[0] = CMD_BLIT_FIELD_VSYNC;
   uint32_t frame = 5; memcpy(&hdr[1], &frame, 4);
   hdr[5] = 0; uint16_t vs = 0; memcpy(&hdr[6], &vs, 2);
   composer_feed(c, hdr, 8);

   cr_assert(!composer_have_frame(c), "frame not complete before payload");
   feed_chunked(c, payload, rgbsize);

   cr_assert(composer_have_frame(c), "frame must be complete after payload");
   cr_assert_eq(composer_last_frame(c), 5u);
   cr_assert_eq(memcmp(composer_frame(c), payload, rgbsize), 0,
                "reassembled frame must match the streamed payload byte-for-byte");

   free(payload);
   composer_destroy(c);
}

/* E3: an LZ4-compressed blit is decompressed back to the exact source frame. */
Test(composer, lz4_blit_decompresses_exact)
{
   enum { W = 64, H = 48 };
   const uint32_t rgbsize = W * H * 3;

   sim_composer_t *c = composer_create();
   uint8_t pkt[26];
   composer_feed(c, pkt, init_pkt(pkt, LZ4_ON, RGB_888));
   composer_feed(c, pkt, switchres_pkt(pkt, W, H, 0));

   /* compressible source (runs of repeated bytes) */
   uint8_t *src = malloc(rgbsize);
   for (uint32_t i = 0; i < rgbsize; i++) src[i] = (uint8_t)((i / 64) % 17);

   int cap = LZ4_compressBound((int)rgbsize);
   uint8_t *comp = malloc(cap);
   int csize = LZ4_compress_default((const char *)src, (char *)comp, (int)rgbsize, cap);
   cr_assert(csize > 0, "compression failed");

   /* lz4 blit header: 12 bytes, cSize at [8..11], frame=9 */
   uint8_t hdr[12];
   hdr[0] = CMD_BLIT_FIELD_VSYNC;
   uint32_t frame = 9; memcpy(&hdr[1], &frame, 4);
   hdr[5] = 0; uint16_t vs = 0; memcpy(&hdr[6], &vs, 2);
   uint32_t cs = (uint32_t)csize; memcpy(&hdr[8], &cs, 4);
   composer_feed(c, hdr, 12);

   feed_chunked(c, comp, (uint32_t)csize);

   cr_assert(composer_have_frame(c), "frame must complete");
   cr_assert_eq(composer_last_frame(c), 9u);
   cr_assert_eq(memcmp(composer_frame(c), src, rgbsize), 0,
                "LZ4-decoded frame must match the original source (lossless)");

   free(src); free(comp);
   composer_destroy(c);
}

/* E4: after a completed blit the composer emits a 13-byte healthy ACK that
 * echoes the frame number — satisfying the client's getACK flow control. */
Test(composer, next_ack_echoes_frame_and_is_healthy)
{
   enum { W = 64, H = 48 };
   const uint32_t rgbsize = W * H * 3;

   sim_composer_t *c = composer_create();
   uint8_t pkt[26];
   composer_feed(c, pkt, init_pkt(pkt, LZ4_OFF, RGB_888));
   composer_feed(c, pkt, switchres_pkt(pkt, W, H, 0));

   uint8_t *payload = malloc(rgbsize);
   memset(payload, 0x5a, rgbsize);
   uint8_t hdr[8] = { CMD_BLIT_FIELD_VSYNC };
   uint32_t frame = 123; memcpy(&hdr[1], &frame, 4);
   composer_feed(c, hdr, 8);
   feed_chunked(c, payload, rgbsize);

   uint8_t ack[ACK_SIZE];
   size_t n = composer_next_ack(c, ack);
   cr_assert_eq(n, (size_t)ACK_SIZE);

   uint32_t echo; memcpy(&echo, &ack[0], 4);
   cr_assert_eq(echo, 123u, "ACK must echo last frame, got %u", echo);
   cr_assert((ack[12] & ST_VRAM_READY) && (ack[12] & ST_VRAM_SYNCED)
             && (ack[12] & ST_VRAM_END_FRAME), "ACK must be healthy, got 0x%02x", ack[12]);

   free(payload);
   composer_destroy(c);
}
