#ifndef MISTER_SIM_UDP_TRANSPORT_H
#define MISTER_SIM_UDP_TRANSPORT_H

#include <stdint.h>
#include "sim/state_machine.h"

/* Layer 3: the socket mechanics. Binds a UDP port on 127.0.0.1, receives
 * datagrams on a background thread, feeds them to the composer, and sends the
 * composer's ACK back to the client after CMD_INIT and after each completed
 * blit — satisfying the GroovyMiSTer client's getACK flow control. */

typedef struct sim_transport sim_transport_t;

/* Start listening (ephemeral port). Returns NULL on failure. */
sim_transport_t *transport_start(sim_composer_t *composer);

/* Start listening on a specific port (e.g. 32100, which the GroovyMiSTer
 * client hardcodes). Uses SO_REUSEADDR. Returns NULL on failure. */
sim_transport_t *transport_start_on_port(sim_composer_t *composer, uint16_t port);

/* The bound UDP port (host byte order). */
uint16_t transport_port(const sim_transport_t *t);

/* Stop the background thread and close the socket. */
void transport_stop(sim_transport_t *t);

#endif /* MISTER_SIM_UDP_TRANSPORT_H */
