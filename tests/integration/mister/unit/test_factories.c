/* Simulator L1 factories & decoders — pure logic. */
#include <criterion/criterion.h>
#include <stdint.h>
#include <string.h>
#include "sim/protocol.h"
#include "sim/factories.h"

/* D1: make_ack produces a 13-byte packet whose frameEcho and status byte
 * round-trip at the verified offsets ([0..3] frame, [12] status). */
Test(factories, make_ack_layout)
{
   uint8_t buf[ACK_SIZE];
   size_t n = make_ack(buf, 7u, ST_HEALTHY);

   cr_assert_eq(n, (size_t)ACK_SIZE, "ACK must be 13 bytes, got %zu", n);

   uint32_t frame_echo;
   memcpy(&frame_echo, &buf[0], 4);
   cr_assert_eq(frame_echo, 7u, "frameEcho mismatch: %u", frame_echo);
   cr_assert_eq(buf[12], (uint8_t)ST_HEALTHY, "status byte mismatch: 0x%02x", buf[12]);
}

/* D2: a SOLID XRGB8888 source frame is the requested color in every pixel,
 * with the correct size/pitch. */
Test(factories, make_source_frame_solid_xrgb8888)
{
   enum { W = 64, H = 48 };
   uint32_t pixels[W * H];
   size_t n = make_source_frame(PATTERN_SOLID, W, H, SRC_FMT_XRGB8888,
                                0x00112233u, (uint8_t *)pixels);

   cr_assert_eq(n, (size_t)(W * H * 4), "expected %d bytes, got %zu", W * H * 4, n);
   cr_assert_eq(pixels[0],         0x00112233u, "first pixel wrong: 0x%08x", pixels[0]);
   cr_assert_eq(pixels[W * H - 1], 0x00112233u, "last pixel wrong: 0x%08x", pixels[W * H - 1]);
}

/* D3: parse_init decodes a hand-built 5-byte CMD_INIT. */
Test(factories, parse_init_decodes_fields)
{
   uint8_t pkt[5] = { CMD_INIT, LZ4_ON, 3 /*48000*/, 2 /*stereo*/, RGB_888 };
   sim_init_t got;

   cr_assert_eq(parse_init(pkt, sizeof(pkt), &got), 0, "valid CMD_INIT should parse");
   cr_assert_eq(got.lz4, LZ4_ON);
   cr_assert_eq(got.rate_code, 3);
   cr_assert_eq(got.chan, 2);
   cr_assert_eq(got.rgb_mode, RGB_888);

   uint8_t bad[5] = { CMD_SWITCHRES, 0, 0, 0, 0 };
   cr_assert_neq(parse_init(bad, sizeof(bad), &got), 0, "wrong opcode must be rejected");
   cr_assert_neq(parse_init(pkt, 4, &got), 0, "short packet must be rejected");
}

/* Assemble a 26-byte CMD_SWITCHRES packet at the verified offsets. */
static void build_switchres(uint8_t *p, double pclock, uint16_t hAct, uint16_t hBeg,
                            uint16_t hEnd, uint16_t hTot, uint16_t vAct, uint16_t vBeg,
                            uint16_t vEnd, uint16_t vTot, uint8_t interlace)
{
   p[0] = CMD_SWITCHRES;
   memcpy(&p[1],  &pclock, 8);
   memcpy(&p[9],  &hAct, 2); memcpy(&p[11], &hBeg, 2);
   memcpy(&p[13], &hEnd, 2); memcpy(&p[15], &hTot, 2);
   memcpy(&p[17], &vAct, 2); memcpy(&p[19], &vBeg, 2);
   memcpy(&p[21], &vEnd, 2); memcpy(&p[23], &vTot, 2);
   p[25] = interlace;
}

/* D4: parse_switchres recovers geometry; compute_rgbsize matches the verified
 * formula across pixel modes and halves on interlace==1. */
Test(factories, parse_switchres_and_rgbsize)
{
   uint8_t pkt[26];
   build_switchres(pkt, 25.175, 640, 656, 752, 800, 480, 490, 492, 525, 0);

   sim_switchres_t m;
   cr_assert_eq(parse_switchres(pkt, sizeof(pkt), &m), 0);
   cr_assert_eq(m.hActive, 640);
   cr_assert_eq(m.vActive, 480);
   cr_assert_eq(m.hTotal, 800);
   cr_assert_eq(m.vTotal, 525);
   cr_assert_eq(m.interlace, 0);

   cr_assert_eq(compute_rgbsize(&m, RGB_888),  (uint32_t)(640 * 480 * 3));
   cr_assert_eq(compute_rgbsize(&m, RGB_565),  (uint32_t)(640 * 480 * 2));
   cr_assert_eq(compute_rgbsize(&m, RGB_A888), (uint32_t)(640 * 480 * 4));

   /* interlace==1 halves the per-field payload */
   uint8_t ipkt[26];
   build_switchres(ipkt, 13.5, 720, 0, 0, 858, 480, 0, 0, 525, 1);
   sim_switchres_t mi;
   cr_assert_eq(parse_switchres(ipkt, sizeof(ipkt), &mi), 0);
   cr_assert_eq(compute_rgbsize(&mi, RGB_888), (uint32_t)(720 * 480 * 3) / 2);
}

/* Assemble the common 8-byte blit header prefix. */
static void build_blit_prefix(uint8_t *p, uint32_t frame, uint8_t field, uint16_t vsync)
{
   p[0] = CMD_BLIT_FIELD_VSYNC;
   memcpy(&p[1], &frame, 4);
   p[5] = field;
   memcpy(&p[6], &vsync, 2);
}

/* D5: parse_blit_header decodes each of the four header variants. */
Test(factories, parse_blit_header_variants)
{
   uint8_t p[13];
   sim_blit_header_t h;

   /* raw, full frame: 8 bytes */
   build_blit_prefix(p, 42, 0, 311);
   cr_assert_eq(parse_blit_header(p, 8, &h), 0);
   cr_assert_eq(h.frame, 42u); cr_assert_eq(h.field, 0); cr_assert_eq(h.vsync, 311);
   cr_assert(!h.is_lz4 && !h.is_delta && !h.is_dup);
   cr_assert_eq(h.csize, 0u);

   /* raw duplicate: 9 bytes, [8]=0x01 */
   build_blit_prefix(p, 43, 1, 0);
   p[8] = 0x01;
   cr_assert_eq(parse_blit_header(p, 9, &h), 0);
   cr_assert(h.is_dup); cr_assert_eq(h.field, 1);

   /* lz4, non-delta: 12 bytes, cSize at [8..11] */
   build_blit_prefix(p, 44, 0, 0);
   uint32_t csize = 12345; memcpy(&p[8], &csize, 4);
   cr_assert_eq(parse_blit_header(p, 12, &h), 0);
   cr_assert(h.is_lz4 && !h.is_delta); cr_assert_eq(h.csize, 12345u);

   /* lz4 delta: 13 bytes, cSizeDelta at [8..11], [12]=0x01 */
   build_blit_prefix(p, 45, 0, 0);
   uint32_t cd = 999; memcpy(&p[8], &cd, 4); p[12] = 0x01;
   cr_assert_eq(parse_blit_header(p, 13, &h), 0);
   cr_assert(h.is_lz4 && h.is_delta); cr_assert_eq(h.csize, 999u);

   /* wrong opcode rejected */
   p[0] = CMD_INIT;
   cr_assert_neq(parse_blit_header(p, 8, &h), 0);
}
