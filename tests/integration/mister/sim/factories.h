#ifndef MISTER_SIM_FACTORIES_H
#define MISTER_SIM_FACTORIES_H

#include <stddef.h>
#include <stdint.h>

/* Build a 13-byte FPGA ACK packet into `out` (must hold ACK_SIZE bytes).
 * Sets frameEcho and the status byte; the vCount/frame fields are zeroed (not
 * used by happy-path flow control). Returns the byte count written (13). */
size_t make_ack(uint8_t *out, uint32_t frame_echo, uint8_t status_bits);

/* Source-frame pixel formats (mirror libretro values). */
#define SRC_FMT_XRGB8888 1
#define SRC_FMT_RGB565   2

typedef enum {
   PATTERN_SOLID = 0,
   PATTERN_GRADIENT,
} sim_pattern_t;

/* Fill `out` with a deterministic synthetic frame in the given format.
 * For PATTERN_SOLID, `color` is the XRGB8888 value (0x00RRGGBB); for RGB565 it
 * is reduced to 16-bit. Returns bytes written (w*h*bpp). */
size_t make_source_frame(sim_pattern_t pat, int w, int h, int fmt,
                         uint32_t color, uint8_t *out);

/* --- inbound command decoders --- */

typedef struct {
   uint8_t lz4;        /* 0 = raw, 1 = lz4 */
   uint8_t rate_code;  /* 0 off, 1 22050, 2 44100, 3 48000 */
   uint8_t chan;
   uint8_t rgb_mode;   /* RGB_888 / RGB_A888 / RGB_565 */
} sim_init_t;

/* Decode a CMD_INIT datagram. Returns 0 on success, -1 on bad opcode/length. */
int parse_init(const uint8_t *buf, size_t len, sim_init_t *out);

typedef struct {
   double   pclock;
   uint16_t hActive, hBegin, hEnd, hTotal;
   uint16_t vActive, vBegin, vEnd, vTotal;
   uint8_t  interlace;
} sim_switchres_t;

/* Decode a 26-byte CMD_SWITCHRES datagram. Returns 0 on success, -1 otherwise. */
int parse_switchres(const uint8_t *buf, size_t len, sim_switchres_t *out);

/* Bytes of pixel payload the client will stream for one (raw) frame at this
 * mode, per the verified CmdSwitchres formula. */
uint32_t compute_rgbsize(const sim_switchres_t *m, uint8_t rgb_mode);

typedef struct {
   uint32_t frame;
   uint8_t  field;
   uint16_t vsync;
   uint8_t  is_lz4;    /* payload is LZ4-compressed */
   uint8_t  is_delta;  /* payload is a delta frame  */
   uint8_t  is_dup;    /* duplicate frame, no payload follows */
   uint32_t csize;     /* compressed payload size (lz4); 0 ⇒ use rgbsize */
} sim_blit_header_t;

/* Decode a CMD_BLIT_FIELD_VSYNC header. The variant is determined by `len`
 * (8 raw / 9 raw-dup / 12 lz4 / 13 lz4-delta). Returns 0 on success. */
int parse_blit_header(const uint8_t *buf, size_t len, sim_blit_header_t *out);

#endif /* MISTER_SIM_FACTORIES_H */
