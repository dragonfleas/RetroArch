#include "driver/mister_stream_driver.h"
#include "sim/protocol.h"
#include "sim/udp_transport.h"

#include <libretro.h>
#include <configuration.h>
#include <gfx/video_driver.h>
#include <switchres/switchres_wrapper.h>

#include <stdlib.h>
#include <string.h>

/* Real GroovyMiSTer video-output entry points (defined in gfx/gfx_mister.c). */
extern void mister_set_mode(sr_mode *srm);
extern void mister_draw(video_driver_state_t *video_st, const void *data,
                        unsigned width, unsigned height, size_t pitch);

#define MISTER_PORT 32100

struct mdrv {
   sim_composer_t  *comp;
   sim_transport_t *transport;
};

mdrv_t *mdrv_start(void)
{
   mdrv_t *d = calloc(1, sizeof(*d));

   /* Arrange settings via the same getter gfx_mister.c reads. */
   settings_t *s = config_get_ptr();
   strncpy(s->arrays.mister_ip, "127.0.0.1", sizeof(s->arrays.mister_ip) - 1);
   s->uints.mister_lz4 = LZ4_OFF;
   s->uints.mister_mtu = 1500;
   s->uints.audio_output_sample_rate = 48000;
   s->bools.mister_force_scaler  = false;
   s->bools.mister_force_rgb565  = false;
   s->bools.mister_interlaced_fb = false;
   s->bools.video_frame_delay_auto = false;

   d->comp = composer_create();
   d->transport = transport_start_on_port(d->comp, MISTER_PORT);
   return d;
}

void mdrv_stop(mdrv_t *d)
{
   if (!d) return;
   transport_stop(d->transport);
   composer_destroy(d->comp);
   free(d);
}

void mdrv_set_lz4(mdrv_t *d, int lz4)
{
   (void)d;
   config_get_ptr()->uints.mister_lz4 = (unsigned)lz4;
}

void mdrv_set_rgb565(mdrv_t *d, int on)
{
   (void)d;
   config_get_ptr()->bools.mister_force_rgb565 = on ? true : false;
}

void mdrv_set_scanlines(mdrv_t *d, int on)
{
   (void)d;
   config_get_ptr()->bools.mister_scanlines = on ? true : false;
}

void mdrv_set_interlaced(mdrv_t *d, int on)
{
   (void)d;
   config_get_ptr()->bools.mister_interlaced_fb = on ? true : false;
}

void mdrv_arrange_mode_scaled(mdrv_t *d, int w, int h, double xs, double ys)
{
   (void)d;
   sr_mode m;
   memset(&m, 0, sizeof(m));
   m.width   = w;
   m.height  = h;
   m.refresh = 60;
   m.vfreq   = 60.0;
   m.pclock  = 25175000;   /* ~VGA 640x480@60 dot clock */
   m.hbegin  = w + 16;  m.hend = w + 112;  m.htotal = w + 160;
   m.vbegin  = h + 10;  m.vend = h + 12;   m.vtotal = h + 45;
   m.interlace = 0;
   m.x_scale = xs;      m.y_scale = ys;    m.v_scale = 1.0;
   mister_set_mode(&m);
}

void mdrv_arrange_mode(mdrv_t *d, int w, int h)
{
   mdrv_arrange_mode_scaled(d, w, h, 1.0, 1.0);
}

void mdrv_arrange_mode_interlaced(mdrv_t *d, int w, int h)
{
   (void)d;
   sr_mode m;
   memset(&m, 0, sizeof(m));
   m.width   = w;
   m.height  = h;
   m.refresh = 60;
   m.vfreq   = 60.0;
   m.pclock  = 25175000;
   m.hbegin  = w + 16;  m.hend = w + 112;  m.htotal = w + 160;
   m.vbegin  = h + 10;  m.vend = h + 12;   m.vtotal = h + 45;
   m.interlace = 1;
   m.x_scale = 1.0;     m.y_scale = 1.0;   m.v_scale = 1.0;
   mister_set_mode(&m);
}

void mdrv_draw_xrgb(mdrv_t *d, const uint32_t *frame, int w, int h)
{
   (void)d;
   video_driver_state_t vst;
   memset(&vst, 0, sizeof(vst));
   vst.pix_fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   mister_draw(&vst, frame, (unsigned)w, (unsigned)h, (size_t)w * 4);
}

sim_composer_t *mdrv_composer(mdrv_t *d) { return d->comp; }
