/* UDP transport — a single behavioral boundary test (infrastructure,
 * so we test the observable behavior, not internals). */
#include <criterion/criterion.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "sim/protocol.h"
#include "sim/state_machine.h"
#include "sim/udp_transport.h"

/* F1: a CMD_INIT sent to the transport is delivered to the composer, and the
 * transport replies with a 13-byte ACK (the handshake the client blocks on).
 * A following CMD_SWITCHRES then drives the composer to READY. */
Test(transport, init_is_delivered_and_acked)
{
   sim_composer_t *comp = composer_create();
   sim_transport_t *t = transport_start(comp);
   cr_assert_not_null(t, "transport must start");
   uint16_t port = transport_port(t);
   cr_assert_neq(port, 0, "transport must bind a port");

   int cli = socket(AF_INET, SOCK_DGRAM, 0);
   cr_assert(cli >= 0);
   struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
   setsockopt(cli, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

   struct sockaddr_in dst;
   memset(&dst, 0, sizeof(dst));
   dst.sin_family = AF_INET;
   dst.sin_port = htons(port);
   dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

   uint8_t init[5] = { CMD_INIT, LZ4_OFF, 0, 0, RGB_888 };
   ssize_t sent = sendto(cli, init, sizeof(init), 0,
                         (struct sockaddr *)&dst, sizeof(dst));
   cr_assert_eq(sent, 5);

   uint8_t ack[32];
   ssize_t got = recv(cli, ack, sizeof(ack), 0);
   cr_assert_eq(got, (ssize_t)ACK_SIZE, "client must receive a 13-byte ACK, got %zd", got);

   /* now drive to READY with a switchres */
   uint8_t sr[26];
   memset(sr, 0, sizeof(sr));
   sr[0] = CMD_SWITCHRES;
   uint16_t w = 640, h = 480, htot = 800, vtot = 525;
   double pclock = 25.0;
   memcpy(&sr[1], &pclock, 8);
   memcpy(&sr[9], &w, 2); memcpy(&sr[15], &htot, 2);
   memcpy(&sr[17], &h, 2); memcpy(&sr[23], &vtot, 2);
   sendto(cli, sr, sizeof(sr), 0, (struct sockaddr *)&dst, sizeof(dst));

   /* give the receive thread a moment to process the switchres */
   usleep(100 * 1000);
   cr_assert(composer_is_ready(comp), "composer should be READY after init+switchres");

   close(cli);
   transport_stop(t);
   composer_destroy(comp);
}
