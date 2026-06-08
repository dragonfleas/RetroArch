/* The real gfx_mister.c + groovymister sender + scaler, linked against
 * our seams, streaming to the FPGA simulator over loopback. */
#include <criterion/criterion.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "driver/mister_stream_driver.h"
#include "sim/state_machine.h"

extern bool mister_is_connected(void);

/* Links and starts disconnected (own process, fresh statics). */
Test(link, gfx_mister_links_and_starts_disconnected)
{
   cr_assert(!mister_is_connected(), "no connection before any mister_draw");
}

/* G1: arranging a mode and drawing a frame connects to the simulator (the
 * CMD_INIT/ACK handshake) and delivers a CMD_SWITCHRES with the right geometry. */
Test(connect, draw_connects_and_sends_switchres)
{
   mdrv_t *d = mdrv_start();
   mdrv_arrange_mode(d, 640, 480);

   uint32_t *frame = malloc(640 * 480 * 4);
   for (int i = 0; i < 640 * 480; i++) frame[i] = 0x00204060u;
   mdrv_draw_xrgb(d, frame, 640, 480);

   cr_assert(mister_is_connected(), "mister_draw must connect via the CMD_INIT handshake");

   /* let the simulator's receive thread drain the switchres datagram */
   usleep(200 * 1000);
   sim_composer_t *c = mdrv_composer(d);
   cr_assert(composer_is_ready(c), "simulator must receive INIT + SWITCHRES");
   cr_assert_eq(composer_width(c), 640, "switchres geometry width");
   cr_assert_eq(composer_height(c), 480, "switchres geometry height");

   free(frame);
   mdrv_stop(d);
}
