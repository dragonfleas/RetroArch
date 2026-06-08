#ifndef MISTER_SIM_STATE_MACHINE_H
#define MISTER_SIM_STATE_MACHINE_H

#include <stddef.h>
#include <stdint.h>

/* The scenario composer: the GroovyMiSTer receive state machine. It is fed raw
 * datagrams (command headers, then payload chunks) exactly as they arrive on
 * the wire, and reconstructs the streamed frame. No sockets here — the
 * transport feeds it; unit tests feed it byte buffers directly. */

typedef struct sim_composer sim_composer_t;

sim_composer_t *composer_create(void);
void            composer_destroy(sim_composer_t *c);

/* Feed one datagram (a command header, or a payload chunk during a blit). */
void composer_feed(sim_composer_t *c, const uint8_t *buf, size_t len);

/* True once both CMD_INIT and CMD_SWITCHRES have been received. */
int composer_is_ready(const sim_composer_t *c);

uint16_t composer_width(const sim_composer_t *c);
uint16_t composer_height(const sim_composer_t *c);
uint32_t composer_rgbsize(const sim_composer_t *c);

/* A complete frame has been reassembled (and decompressed, if needed). */
int            composer_have_frame(const sim_composer_t *c);
/* True while collecting a blit's payload (next datagrams are payload chunks,
 * not commands). */
int            composer_is_receiving(const sim_composer_t *c);
/* Pointer to the reconstructed frame (rgbsize bytes); NULL if none yet. */
const uint8_t *composer_frame(const sim_composer_t *c);
/* Frame number of the most recently completed blit. */
uint32_t       composer_last_frame(const sim_composer_t *c);

/* Build the ACK the FPGA would return for the current state into `out`
 * (>= ACK_SIZE bytes): frameEcho = last completed frame, status = healthy.
 * Returns bytes written. */
size_t composer_next_ack(const sim_composer_t *c, uint8_t *out);

#endif /* MISTER_SIM_STATE_MACHINE_H */
