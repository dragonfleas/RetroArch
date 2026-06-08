#include "sim/udp_transport.h"
#include "sim/protocol.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

struct sim_transport {
   int             fd;
   uint16_t        port;
   sim_composer_t *composer;
   pthread_t       thread;
   volatile int    running;
};

static void *recv_loop(void *arg)
{
   sim_transport_t *t = (sim_transport_t *)arg;
   uint8_t buf[2048];

   while (t->running)
   {
      struct sockaddr_in src;
      socklen_t slen = sizeof(src);
      ssize_t n = recvfrom(t->fd, buf, sizeof(buf), 0,
                           (struct sockaddr *)&src, &slen);
      if (n <= 0)
         continue;  /* timeout (poll the running flag) or error */

      int was_receiving = composer_is_receiving(t->composer);
      int had_frame     = composer_have_frame(t->composer);

      composer_feed(t->composer, buf, (size_t)n);

      /* ACK after CMD_INIT, and whenever a blit just completed — the two
       * points where the client blocks in getACK. */
      int is_init        = (!was_receiving && buf[0] == CMD_INIT);
      int frame_complete = (!had_frame && composer_have_frame(t->composer));

      if (is_init || frame_complete)
      {
         uint8_t ack[ACK_SIZE];
         size_t alen = composer_next_ack(t->composer, ack);
         sendto(t->fd, ack, alen, 0, (struct sockaddr *)&src, slen);
      }
   }
   return NULL;
}

sim_transport_t *transport_start(sim_composer_t *composer)
{
   return transport_start_on_port(composer, 0);  /* ephemeral */
}

sim_transport_t *transport_start_on_port(sim_composer_t *composer, uint16_t port)
{
   sim_transport_t *t = calloc(1, sizeof(*t));
   if (!t)
      return NULL;
   t->composer = composer;

   t->fd = socket(AF_INET, SOCK_DGRAM, 0);
   if (t->fd < 0) { free(t); return NULL; }

   int one = 1;
   setsockopt(t->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
   /* generous receive buffer: a full raw frame is ~900 KB across ~600 datagrams */
   int rcvbuf = 8 * 1024 * 1024;
   setsockopt(t->fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

   struct sockaddr_in addr;
   memset(&addr, 0, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
   addr.sin_port = htons(port);
   if (bind(t->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
   {
      close(t->fd); free(t); return NULL;
   }

   struct sockaddr_in bound;
   socklen_t blen = sizeof(bound);
   getsockname(t->fd, (struct sockaddr *)&bound, &blen);
   t->port = ntohs(bound.sin_port);

   /* short receive timeout so the thread can observe `running` and exit */
   struct timeval tv = { .tv_sec = 0, .tv_usec = 100 * 1000 };
   setsockopt(t->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

   t->running = 1;
   if (pthread_create(&t->thread, NULL, recv_loop, t) != 0)
   {
      close(t->fd); free(t); return NULL;
   }
   return t;
}

uint16_t transport_port(const sim_transport_t *t) { return t->port; }

void transport_stop(sim_transport_t *t)
{
   if (!t)
      return;
   t->running = 0;
   pthread_join(t->thread, NULL);
   close(t->fd);
   free(t);
}
