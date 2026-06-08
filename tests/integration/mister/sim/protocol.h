#ifndef MISTER_SIM_PROTOCOL_H
#define MISTER_SIM_PROTOCOL_H

/* GroovyMiSTer wire-protocol constants, verified against
 * deps/mister/groovymister.{h,cpp}. The simulator speaks this protocol. */

#include <stdint.h>

/* Command opcodes (groovymister.cpp:32-39) */
#define CMD_CLOSE            1
#define CMD_INIT             2
#define CMD_SWITCHRES        3
#define CMD_AUDIO            4
#define CMD_GET_STATUS       5
#define CMD_BLIT_VSYNC       6
#define CMD_BLIT_FIELD_VSYNC 7
#define CMD_GET_VERSION      8

/* RGB modes (RGBModeCode) */
#define RGB_888  0
#define RGB_A888 1
#define RGB_565  2

/* LZ4 modes (Lz4FramesCode) — 0 = raw */
#define LZ4_OFF  0
#define LZ4_ON   1

/* MTU payload per datagram (BUFFER_MTU = 1500 - 28) */
#define MISTER_MTU_PAYLOAD 1472

/* ACK packet (13 bytes) — layout per setFpgaStatus() */
#define ACK_SIZE 13

/* status byte bit positions */
#define ST_VRAM_READY     (1u << 0)
#define ST_VRAM_END_FRAME (1u << 1)
#define ST_VRAM_SYNCED    (1u << 2)
#define ST_VGA_FRAMESKIP  (1u << 3)
#define ST_VGA_VBLANK     (1u << 4)
#define ST_VGA_F1         (1u << 5)
#define ST_AUDIO          (1u << 6)
#define ST_VRAM_QUEUE     (1u << 7)

/* A "green" ACK: VRAM ready, frame fully received, synced. */
#define ST_HEALTHY (ST_VRAM_READY | ST_VRAM_END_FRAME | ST_VRAM_SYNCED)

#endif /* MISTER_SIM_PROTOCOL_H */
