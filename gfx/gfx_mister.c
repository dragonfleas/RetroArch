#include <retroarch.h>
#include <verbosity.h>
#include <string/stdstring.h>
#include <gfx/gfx_mister.h>
#include <gfx/video_frame.h>
#include <audio/audio_driver.h>
#include <switchres/switchres_wrapper.h>
#include <mister/groovymister_wrapper.h>
#ifdef HAVE_MENU
#include <menu/menu_driver.h>
#endif

#define MAX_BUFFER_WIDTH 720 //1024
#define MAX_BUFFER_HEIGHT 576 //768

#define RGB888  0
#define RGBA888 1
#define RGB565  2

typedef struct mister_video_info
{
   bool is_error;
   bool is_connected;
   uint32_t frame;
   uint8_t  field;
   uint16_t width;
   uint16_t height;
   double vfreq;
   uint8_t interlaced;
   uint32_t line_time; //usec
   uint32_t frame_time; //usec
   uint8_t rgb_mode;
   bool delta_frames;
} mister_video_t;

union
{
   const uint8_t *u8;
   const uint16_t *u16;
   const uint32_t *u32;
} u;


extern void gl2_resize_viewport_and_scaler(video_driver_state_t *video_st, unsigned width, unsigned height, double x_scale, double y_scale);
extern void gl3_resize_viewport_and_scaler(video_driver_state_t *video_st, unsigned width, unsigned height, double x_scale, double y_scale);
extern void vulkan_resize_viewport_and_scaler(video_driver_state_t *video_st, unsigned width, unsigned height, double x_scale, double y_scale);
extern void gl2_restore_viewport_and_scaler(video_driver_state_t *video_st);
extern void gl3_restore_viewport_and_scaler(video_driver_state_t *video_st);
extern void vulkan_restore_viewport_and_scaler(video_driver_state_t *video_st);

static void mister_init(const char* mister_host, uint8_t compression, uint32_t sound_rate, uint8_t sound_channels, uint8_t pix_fmt);
static void mister_switchres(sr_mode *srm);
static void mister_resize_viewport(video_driver_state_t *video_st, unsigned width, unsigned height);
static void mister_restore_viewport(video_driver_state_t *video_st);

static mister_video_t mister_video;
static sr_mode mister_mode;
static gmw_fpgaStatus status;
static char *menu_buffer = 0;
static unsigned menu_width = 0;
static unsigned menu_height = 0;
static bool modeline_active = 0;
static bool must_clear_buffer = 0;
static bool mode_switch_pending = 0;
static bool vp_resize_pending = 0;
static bool prev_menu_state = 0;
static struct scaler_ctx *scaler;
static uint8_t *mister_buffer = 0;
static uint8_t *mister_buffer_delta = 0;
static uint8_t *audio_buffer = 0;
static uint8_t *convert_buffer = 0;
static uint8_t *scaled_buffer = 0;
static uint8_t *hardware_buffer = 0;


bool mister_is_connected()
{
   return mister_video.is_connected;
}

void mister_set_menu_buffer(char *frame, unsigned width, unsigned height)
{
   menu_buffer = frame;
   menu_width = width;
   menu_height = height;
}

/* Branchless delta: d = v - fb is 0 exactly when v == fb, so no match branch. */
static inline uint32_t blit_row_argb_delta(uint8_t *fb, uint8_t *delta, const uint32_t *src,
                                           uint32_t x_max, int s_step, uint32_t c, int c_step)
{
   uint32_t md = 0;
   for (uint32_t i = 0; i < x_max; i += s_step)
   {
      uint32_t p = src[i];
      uint8_t db = (uint8_t)((uint8_t)(p >>  0) - fb[c + 0]);
      uint8_t dg = (uint8_t)((uint8_t)(p >>  8) - fb[c + 1]);
      uint8_t dr = (uint8_t)((uint8_t)(p >> 16) - fb[c + 2]);
      delta[c + 0] = db; md += (db == 0);
      delta[c + 1] = dg; md += (dg == 0);
      delta[c + 2] = dr; md += (dr == 0);
      fb[c + 0] = p; fb[c + 1] = p >> 8; fb[c + 2] = p >> 16;
      c += c_step;
   }
   return md;
}

static inline uint32_t blit_row_argb_nodelta(uint8_t *fb, const uint32_t *src,
                                             uint32_t x_max, int s_step, uint32_t c, int c_step)
{
   for (uint32_t i = 0; i < x_max; i += s_step)
   {
      uint32_t p = src[i];
      fb[c + 0] = p; fb[c + 1] = p >> 8; fb[c + 2] = p >> 16;
      c += c_step;
   }
   return 0;
}

static inline uint32_t blit_row_bgr24_delta(uint8_t *fb, uint8_t *delta, const uint8_t *src,
                                            uint32_t x_max, int s_step, uint32_t c, int c_step)
{
   uint32_t md = 0;
   for (uint32_t i = 0; i < x_max; i += s_step)
   {
      const uint8_t *p = &src[i * 3];
      uint8_t db = (uint8_t)(p[0] - fb[c + 0]);
      uint8_t dg = (uint8_t)(p[1] - fb[c + 1]);
      uint8_t dr = (uint8_t)(p[2] - fb[c + 2]);
      delta[c + 0] = db; md += (db == 0);
      delta[c + 1] = dg; md += (dg == 0);
      delta[c + 2] = dr; md += (dr == 0);
      fb[c + 0] = p[0]; fb[c + 1] = p[1]; fb[c + 2] = p[2];
      c += c_step;
   }
   return md;
}

static inline uint32_t blit_row_bgr24_nodelta(uint8_t *fb, const uint8_t *src,
                                              uint32_t x_max, int s_step, uint32_t c, int c_step)
{
   for (uint32_t i = 0; i < x_max; i += s_step)
   {
      const uint8_t *p = &src[i * 3];
      fb[c + 0] = p[0]; fb[c + 1] = p[1]; fb[c + 2] = p[2];
      c += c_step;
   }
   return 0;
}

static inline uint32_t blit_row_565exp_delta(uint8_t *fb, uint8_t *delta, const uint16_t *src,
                                             uint32_t x_max, int s_step, uint32_t c, int c_step)
{
   uint32_t md = 0;
   for (uint32_t i = 0; i < x_max; i += s_step)
   {
      uint16_t pixel = src[i];
      uint8_t r5 = (pixel >> 11) & 0x1f, g6 = (pixel >> 5) & 0x3f, b5 = pixel & 0x1f;
      uint8_t b = (uint8_t)((b5 << 3) | (b5 >> 2));
      uint8_t g = (uint8_t)((g6 << 2) | (g6 >> 4));
      uint8_t r = (uint8_t)((r5 << 3) | (r5 >> 2));
      uint8_t db = (uint8_t)(b - fb[c + 0]);
      uint8_t dg = (uint8_t)(g - fb[c + 1]);
      uint8_t dr = (uint8_t)(r - fb[c + 2]);
      delta[c + 0] = db; md += (db == 0);
      delta[c + 1] = dg; md += (dg == 0);
      delta[c + 2] = dr; md += (dr == 0);
      fb[c + 0] = b; fb[c + 1] = g; fb[c + 2] = r;
      c += c_step;
   }
   return md;
}

static inline uint32_t blit_row_565exp_nodelta(uint8_t *fb, const uint16_t *src,
                                               uint32_t x_max, int s_step, uint32_t c, int c_step)
{
   for (uint32_t i = 0; i < x_max; i += s_step)
   {
      uint16_t pixel = src[i];
      uint8_t r5 = (pixel >> 11) & 0x1f, g6 = (pixel >> 5) & 0x3f, b5 = pixel & 0x1f;
      fb[c + 0] = (uint8_t)((b5 << 3) | (b5 >> 2));
      fb[c + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
      fb[c + 2] = (uint8_t)((r5 << 3) | (r5 >> 2));
      c += c_step;
   }
   return 0;
}

static inline uint32_t blit_row_565raw_delta(uint8_t *fb, uint8_t *delta, const uint16_t *src,
                                             uint32_t x_max, int s_step, uint32_t c, int c_step)
{
   uint32_t md = 0;
   for (uint32_t i = 0; i < x_max; i += s_step)
   {
      uint16_t pixel = src[i];
      uint8_t d0 = (uint8_t)((uint8_t)(pixel & 0xff) - fb[c + 0]);
      uint8_t d1 = (uint8_t)((uint8_t)(pixel >> 8)   - fb[c + 1]);
      delta[c + 0] = d0; md += (d0 == 0);
      delta[c + 1] = d1; md += (d1 == 0);
      *(uint16_t *)&fb[c] = pixel;
      c += c_step;
   }
   return md;
}

static inline uint32_t blit_row_565raw_nodelta(uint8_t *fb, const uint16_t *src,
                                               uint32_t x_max, int s_step, uint32_t c, int c_step)
{
   for (uint32_t i = 0; i < x_max; i += s_step)
   {
      *(uint16_t *)&fb[c] = src[i];
      c += c_step;
   }
   return 0;
}

/* Preserves two upstream quirks: unmasked compares (pixel>>4/pixel>>0 vs truncated
 * bytes) and the compute-then-zero delta (delta is always zeroed regardless). */
static inline uint32_t blit_row_rgba4444(uint8_t *fb, uint8_t *delta, const uint16_t *src,
                                         uint32_t x_max, int s_step, uint32_t c, int c_step,
                                         bool delta_frames)
{
   uint32_t md = 0;
   for (uint32_t i = 0; i < x_max; i += s_step)
   {
      uint16_t pixel = src[i];
      if (delta_frames)
      {
         if ((pixel >> 8) == fb[c + 0]) { md++; delta[c + 0] = 0; }
         else delta[c + 0] = (uint8_t)(pixel >> 8) - (uint8_t)fb[c + 0];
         if ((pixel >> 4) == fb[c + 1]) { md++; delta[c + 1] = 0; }
         else delta[c + 1] = (uint8_t)(pixel >> 4) - (uint8_t)fb[c + 1];
         if ((pixel >> 0) == fb[c + 2]) { md++; delta[c + 2] = 0; }
         else delta[c + 2] = (uint8_t)(pixel >> 0) - (uint8_t)fb[c + 2];
      }
      delta[c + 0] = 0; delta[c + 1] = 0; delta[c + 2] = 0;
      fb[c + 0] = (pixel >> 8); fb[c + 1] = (pixel >> 4); fb[c + 2] = (pixel >> 0);
      c += c_step;
   }
   return md;
}

static inline uint32_t blit_row_blank_delta(uint8_t *fb, uint8_t *delta,
                                            uint32_t x_max, int s_step, uint32_t c, int c_step)
{
   uint32_t md = 0;
   for (uint32_t i = 0; i < x_max; i += s_step)
   {
      md += 3;
      delta[c + 0] = 0; delta[c + 1] = 0; delta[c + 2] = 0;
      fb[c + 0] = 0; fb[c + 1] = 0; fb[c + 2] = 0;
      c += c_step;
   }
   return md;
}

static inline uint32_t blit_row_blank_nodelta(uint8_t *fb,
                                              uint32_t x_max, int s_step, uint32_t c, int c_step)
{
   for (uint32_t i = 0; i < x_max; i += s_step)
   {
      fb[c + 0] = 0; fb[c + 1] = 0; fb[c + 2] = 0;
      c += c_step;
   }
   return 0;
}

void mister_draw(video_driver_state_t *video_st, const void *data, unsigned width, unsigned height, size_t pitch)
{
   settings_t *settings  = config_get_ptr();
   audio_driver_state_t *audio_st  = audio_state_get_ptr();
   uint8_t field = 0;
   uint8_t format = 0;
   bool menu_on = false;
   bool stretched = false;
   bool is_hw_rendered = (data == RETRO_HW_FRAME_BUFFER_VALID
                           && video_driver_cached_frame_is_hw_render())
                      || (settings->bools.mister_force_scaler
                           && !(menu_state_get_ptr()->flags & MENU_ST_FLAG_ALIVE));

   // Initialize MiSTer if required
   if (!mister_video.is_connected)
      mister_init(settings->arrays.mister_ip, settings->uints.mister_lz4, settings->uints.audio_output_sample_rate, 2, video_st->pix_fmt);

   // Send audio first
   if (audio_st->output_mister && audio_st->output_mister_samples)
      mister_audio();

   // Check if we need to mode switch
   if (mode_switch_pending)
   {
      mister_switchres(&mister_mode);

      //video_driver_set_size(mister_mode.width, mister_mode.height);
      vp_resize_pending = true;
      must_clear_buffer = true;
   }

   if (!modeline_active)
      return;

   // Get pixel format
   if (video_st->pix_fmt == RETRO_PIXEL_FORMAT_XRGB8888)
      format = SCALER_FMT_ARGB8888;

   else if (video_st->pix_fmt == RETRO_PIXEL_FORMAT_RGB565)
      format = SCALER_FMT_RGB565;

   else
      // Unsupported pixel format
      return;

   // Get menu bitmap dimensions
   #ifdef HAVE_MENU
   if (menu_state_get_ptr()->flags & MENU_ST_FLAG_ALIVE && menu_buffer != 0)
   {
      menu_on = true;
      width = menu_width;
      height = menu_height;
      pitch = width * sizeof(uint16_t);
      format = SCALER_FMT_RGBA4444;
      data = menu_buffer;
   }
   else
      menu_buffer = 0;

   if (prev_menu_state != menu_on)
   {
      prev_menu_state = menu_on;
      must_clear_buffer = true;
   }
   #endif

   // Ignore bogus frames
   if (data == 0 || width <= 64 || height <= 64 || width > 1000)
      return;

   if (vp_resize_pending)
   {
      /* Only the hw-readback path needs the host viewport shrunk to the
       * modeline; software cores and the menu must keep the full-window
       * viewport so they render on the host as well as on MiSTer. */
      if (is_hw_rendered)
         mister_resize_viewport(video_st, width, height);
      else
         mister_restore_viewport(video_st);
      vp_resize_pending = false;
   }

   // Get RGB buffer if hw rendered
   if (is_hw_rendered)
   {
      if (video_st->current_video->read_viewport
            && video_st->current_video->read_viewport(video_st->data, hardware_buffer, false))
         pitch = mister_video.width * 3;

      else return;

      format = SCALER_FMT_BGR24;
      data = hardware_buffer;
   }

   if (pitch == 0)
      return;

   // Scale frame
   double x_scale, y_scale;
   if (menu_on)
   {
      x_scale = (double)mister_mode.width / (double)width;
      y_scale = (double)mister_mode.height / (double)height;
      stretched = x_scale != floor(x_scale) || y_scale != floor(y_scale);
   }
   else
   {
      x_scale = (retroarch_get_rotation() & 1) ? mister_mode.y_scale : mister_mode.x_scale;
      y_scale = (retroarch_get_rotation() & 1) ? mister_mode.x_scale : mister_mode.y_scale;
      stretched = mister_mode.is_stretched;
   }

   if (is_hw_rendered)
   {
      width *= x_scale;
      height *= y_scale;
   }
   else if (x_scale != 1.0 || y_scale != 1.0)
   {
      uint32_t scaler_width  = round(width * x_scale);
      uint32_t scaler_height = round(height * y_scale);
      uint32_t scaler_pitch  = scaler_width * sizeof(uint32_t);

      if (  width  != (uint32_t)scaler->in_width
         || height != (uint32_t)scaler->in_height
         || format != scaler->in_fmt
         || pitch  != (uint32_t)scaler->in_stride
         || scaler_width  != (uint32_t)scaler->out_width
         || scaler_height != (uint32_t)scaler->out_height)
      {
         scaler->scaler_type = stretched ? SCALER_TYPE_BILINEAR : SCALER_TYPE_POINT;
         scaler->in_fmt    = format;
         scaler->in_width  = width;
         scaler->in_height = height;
         scaler->in_stride = pitch;

         scaler->out_width  = scaler_width;
         scaler->out_height = scaler_height;
         scaler->out_stride = scaler_pitch;

         scaler_ctx_gen_filter(scaler);
      }

      scaler_ctx_scale_direct(scaler, scaled_buffer, data);

      width  = scaler->out_width;
      height = scaler->out_height;
      pitch  = scaler->out_stride;
      format = scaler->out_fmt;
      data = scaled_buffer;
   }

   // Clear frame buffer if required
   if (must_clear_buffer)
   {
      must_clear_buffer = false;
      memset(mister_buffer, 0, MAX_BUFFER_WIDTH * MAX_BUFFER_HEIGHT * 3);
      memset(convert_buffer, 0, MAX_BUFFER_WIDTH * MAX_BUFFER_HEIGHT * 4);
   }

   // Compute borders and clipping
   uint32_t rotation = retroarch_get_rotation();
   uint32_t rot_width = (rotation & 1) && !menu_on ? height : width;
   uint32_t rot_height = (rotation & 1) && !menu_on ?  width : height;

   uint32_t x_start = mister_video.width > rot_width ? (mister_video.width - rot_width) / 2 : 0;
   uint32_t x_crop = mister_video.width < rot_width ? rot_width - mister_video.width : 0;
   uint32_t x_max = rot_width - x_crop;
   uint32_t y_start = mister_video.height > rot_height ? (mister_video.height - rot_height) / 2 : 0;
   uint32_t y_crop = mister_video.height < rot_height ? (rot_height - mister_video.height) : 0;
   uint32_t y_max = rot_height - y_crop;

   if (mister_video.interlaced)
   {
      y_start /= 2;
      y_crop /= 2;

      if (!(rotation & 1) || menu_on)
         y_max /= 2;
   }

   if ((rotation & 1) && !menu_on)
   {
      uint32_t tmp;
      tmp = x_max;
      x_max = y_max;
      y_max = tmp;
   }

   // Get first pixel address from our RGB source
   if (mister_video.interlaced)
      mister_video.field = !status.vgaF1 ^ ((mister_video.frame - status.frame) % 2);
   else
      mister_video.field = status.vgaF1;

   field = mister_video.field;
   u.u8 = data;
   u.u8 += (field + (y_crop / 2)) * pitch + (x_crop / 2);

   // Get target frame buffer
   mister_buffer = (uint8_t*)gmw_get_pBufferBlit(field);
   uint8_t *fb = mister_video.rgb_mode == RGB565 && format != SCALER_FMT_RGB565 ? convert_buffer : mister_buffer;

   if ((mister_video.rgb_mode == RGB565 && format != SCALER_FMT_RGB565))
      mister_video.delta_frames = false;

   // Compute steps to walk through the source & target bitmaps
   uint32_t pix_size = (mister_video.rgb_mode == RGB565 && format == SCALER_FMT_RGB565) ? 2 : 3;
   uint32_t c = 0;
   int c_step = pix_size;
   int s_step = 1;
   int r_step = 1;

   bool scanlines = settings->bools.mister_scanlines && y_scale >= 2.0;

   if (menu_on || is_hw_rendered)
      r_step = mister_video.interlaced ? 2 : 1;

   else switch (rotation)
   {
      case ORIENTATION_NORMAL:
      case ORIENTATION_FLIPPED:
         c_step = pix_size;
         r_step = mister_video.interlaced ? 2 : 1;
         break;

      case ORIENTATION_VERTICAL:
         c_step = -mister_video.width * pix_size;
         s_step = mister_video.interlaced ? 2 : 1;
         break;

      case ORIENTATION_FLIPPED_ROTATED:
         c_step = +mister_video.width * pix_size;
         s_step = mister_video.interlaced ? 2 : 1;
         break;
   }

   uint32_t match_delta = 0;
   bool delta_frames = mister_video.delta_frames;

   /* Format/scanline/delta dispatch hoisted to per-row; inner loops stay tight. */
   for (uint32_t j = 0; j < y_max; j++)
   {
      if (is_hw_rendered)
         c = (mister_video.width * (mister_video.height / r_step - y_start - field - j - 1) + x_start) * pix_size;

      else if (menu_on || !(rotation & 1))
         c = ((j + y_start) * mister_video.width + x_start) * pix_size;

      else if (rotation == ORIENTATION_VERTICAL)
         c = (mister_video.width * (mister_video.height / s_step - y_start - 1) + j + x_start) * pix_size;

      else if (rotation == ORIENTATION_FLIPPED_ROTATED)
         c = (mister_video.width * (y_start + 1) - j - x_start - 1) * pix_size;

      if (scanlines && (j % 2))
         match_delta += delta_frames
            ? blit_row_blank_delta(fb, mister_buffer_delta, x_max, s_step, c, c_step)
            : blit_row_blank_nodelta(fb, x_max, s_step, c, c_step);

      else switch (format)
      {
         case SCALER_FMT_ARGB8888:
            match_delta += delta_frames
               ? blit_row_argb_delta(fb, mister_buffer_delta, u.u32, x_max, s_step, c, c_step)
               : blit_row_argb_nodelta(fb, u.u32, x_max, s_step, c, c_step);
            break;

         case SCALER_FMT_BGR24:
            match_delta += delta_frames
               ? blit_row_bgr24_delta(fb, mister_buffer_delta, u.u8, x_max, s_step, c, c_step)
               : blit_row_bgr24_nodelta(fb, u.u8, x_max, s_step, c, c_step);
            break;

         case SCALER_FMT_RGB565:
            if (mister_video.rgb_mode == RGB565)
               match_delta += delta_frames
                  ? blit_row_565raw_delta(fb, mister_buffer_delta, u.u16, x_max, s_step, c, c_step)
                  : blit_row_565raw_nodelta(fb, u.u16, x_max, s_step, c, c_step);
            else
               match_delta += delta_frames
                  ? blit_row_565exp_delta(fb, mister_buffer_delta, u.u16, x_max, s_step, c, c_step)
                  : blit_row_565exp_nodelta(fb, u.u16, x_max, s_step, c, c_step);
            break;

         case SCALER_FMT_RGBA4444:
            match_delta += blit_row_rgba4444(fb, mister_buffer_delta, u.u16, x_max, s_step, c, c_step, delta_frames);
            break;
      }

      u.u8 += pitch * r_step;
   }

   // Convert to RGB565 if required
   if (mister_video.rgb_mode == RGB565 && format != SCALER_FMT_RGB565)
      conv_bgr24_rgb565(mister_buffer, fb, mister_video.width, mister_video.height, mister_video.width, mister_video.width * 3);

   // Compute sync scanline based on frame delay
   int mister_vsync = 1;
   if (settings->bools.video_frame_delay_auto && video_st->frame_delay_effective > 0)
      mister_vsync = height / (16 / video_st->frame_delay_effective);

   else if (settings->uints.video_frame_delay > 0)
      mister_vsync = height / (16 / settings->uints.video_frame_delay);

   mister_video.frame++;

   // Resync if required
   gmw_getStatus(&status);

   if (status.frame > mister_video.frame)
      mister_video.frame = status.frame + 1;

   // Blit to MiSTer
   gmw_blit(mister_video.frame, mister_video.field, mister_vsync, 0, match_delta);
}


static void mister_init(const char* mister_host, uint8_t compression, uint32_t sound_rate, uint8_t sound_channels, uint8_t pix_fmt)
{
   if (mister_video.is_error)
     return;

   settings_t *settings  = config_get_ptr();
   mister_video.frame = 0;
   mister_video.width = 0;
   mister_video.height = 0;
   mister_video.line_time = 0;
   mister_video.frame_time = 0;
   mister_video.interlaced = 0;
   mister_video.rgb_mode = (pix_fmt == RETRO_PIXEL_FORMAT_RGB565 || settings->bools.mister_force_rgb565) ? RGB565 : RGB888;
   mister_video.delta_frames = (compression % 2 == 0) ? true : false;

   RARCH_LOG("[MiSTer] Sending CMD_INIT... lz4 %d sound_rate %d sound_chan %d rgb_mode %d mtu %d\n", compression, sound_rate, sound_channels, mister_video.rgb_mode, settings->uints.mister_mtu);
   if (gmw_init(mister_host, compression, sound_rate, sound_channels, mister_video.rgb_mode, settings->uints.mister_mtu) < 0)
   {
      mister_video.is_error = true;
      mister_video.is_connected = false;
      RARCH_LOG("[MiSTer] CMD_INIT... failed\n");
   }
   else
   {
      mister_video.is_connected = true;
   }

   // Allocate buffers
   mister_buffer = (uint8_t*)gmw_get_pBufferBlit(0);
   mister_buffer_delta = (uint8_t*)gmw_get_pBufferBlitDelta();
   audio_buffer = (uint8_t*)gmw_get_pBufferAudio();
   convert_buffer = (uint8_t*)malloc(MAX_BUFFER_WIDTH * MAX_BUFFER_HEIGHT * 4);
   scaled_buffer = (uint8_t*)malloc(MAX_BUFFER_WIDTH * MAX_BUFFER_HEIGHT * 4);
   hardware_buffer = (uint8_t*)malloc(MAX_BUFFER_WIDTH * MAX_BUFFER_HEIGHT * 4);

   // Create scaler
   scaler = (struct scaler_ctx*)calloc(1, sizeof(*scaler));

   // Get status to gather info from server
   gmw_getStatus(&status);

   mode_switch_pending = 1;
}


void mister_audio(void)
{
   audio_driver_state_t *audio_st  = audio_state_get_ptr();

   uint16_t audio_bytes = audio_st->output_mister_samples * 2;
   memcpy(audio_buffer, audio_st->output_mister_samples_conv_buf, audio_bytes);
   audio_st->output_mister_samples = 0;

   if (status.audio)
      gmw_audio(audio_bytes);
}

void mister_sync(void)
{
   if (!mister_video.is_connected)
      return;

   gmw_waitSync();
}

int mister_diff_time_raster(void)
{
   if (!mister_video.is_connected)
      return 0;

   return gmw_diffTimeRaster();
}

void mister_close(void)
{
   if (!mister_video.is_connected)
      return;

   RARCH_LOG("[MiSTer] Sending CMD_CLOSE...\n");

   gmw_close();

   mister_video.is_connected = 0;
   modeline_active = 0;

   free(convert_buffer);
   free(scaled_buffer);
   free(hardware_buffer);
   mister_buffer = 0;
   audio_buffer = 0;
   scaled_buffer = 0;
   hardware_buffer = 0;

   scaler_ctx_gen_reset(scaler);
   free(scaler);
   scaler = 0;
}


void mister_set_mode(sr_mode *srm)
{
   // Filter out too small modes (hack: these shouldn't reach here)
   if (srm->width < 200 || srm->height < 160)
      return;

   // Check if mode is the same as previous
   //if (memcmp(&mister_mode, srm, sizeof(sr_mode)) == 0)
   //   return;

   // otherwise store it
   memcpy(&mister_mode, srm, sizeof(sr_mode));

   // Signal mode switch pending
   mode_switch_pending = 1;
}

static void mister_switchres(sr_mode *srm)
{
   settings_t *settings  = config_get_ptr();

   if (srm == 0)
      return;

   /* Guard against the degenerate zero-initialised mode armed at init.
    * Activating a 0x0 mode sets x_scale/y_scale to 0, which produces a
    * 0-dimension scaler and crashes the software frame path. Wait for the
    * real switchres that follows. */
   if (srm->width == 0 || srm->height == 0)
   {
      mode_switch_pending = 0;
      return;
   }

   RARCH_LOG("[MiSTer] Video_SetSwitchres - (result %dx%d@%f) - x=%.4f y=%.4f stretched(%d)\n", srm->width, srm->height,srm->vfreq, srm->x_scale, srm->y_scale, srm->is_stretched);
   RARCH_LOG("[MiSTer] Sending CMD_SWITCHRES...\n");

   gmw_switchres((double)srm->pclock / 1000000.0, srm->width, srm->hbegin, srm->hend, srm->htotal, srm->height, srm->vbegin, srm->vend, srm->vtotal, srm->interlace ? (settings->bools.mister_interlaced_fb ? 1 : 2) : 0);

   mister_video.width = srm->width;
   mister_video.height = srm->height;
   mister_video.vfreq = srm->refresh;
   mister_video.interlaced = settings->bools.mister_interlaced_fb ? srm->interlace : 0;

   double px = (double) srm->pclock / 1000000.0;
   mister_video.line_time = round((double) srm->htotal * (1 / px)); //in usec, time to raster 1 line
   mister_video.frame_time = mister_video.line_time * srm->vtotal;

   if (srm->interlace)
   {
      mister_video.field = 0;
      mister_video.frame_time = mister_video.frame_time >> 1;
   }

   modeline_active = 1;
   mode_switch_pending = 0;
}


static void mister_resize_viewport(video_driver_state_t *video_st, unsigned width, unsigned height)
{
   double x_scale, y_scale;
   x_scale = (retroarch_get_rotation() & 1) ? mister_mode.y_scale : mister_mode.x_scale;
   y_scale = (retroarch_get_rotation() & 1) ? mister_mode.x_scale : mister_mode.y_scale;

   if (string_is_equal(video_driver_get_ident(), "gl"))
      gl2_resize_viewport_and_scaler(video_st, width, height, x_scale, y_scale);

   else if (string_is_equal(video_driver_get_ident(), "glcore"))
      gl3_resize_viewport_and_scaler(video_st, width, height, x_scale, y_scale);

#ifdef HAVE_VULKAN
   else if (string_is_equal(video_driver_get_ident(), "vulkan"))
      vulkan_resize_viewport_and_scaler(video_st, width, height, x_scale, y_scale);
#endif
}


/* Host viewport restore for the non-readback path. The readback (hw cores)
 * shrinks the host viewport to the modeline so read_viewport yields a native
 * frame; software cores and the menu feed MiSTer from their own scaler, so the
 * host viewport must stay at the real window size to draw there too. */
static void mister_restore_viewport(video_driver_state_t *video_st)
{
   if (string_is_equal(video_driver_get_ident(), "gl"))
      gl2_restore_viewport_and_scaler(video_st);

   else if (string_is_equal(video_driver_get_ident(), "glcore"))
      gl3_restore_viewport_and_scaler(video_st);

#ifdef HAVE_VULKAN
   else if (string_is_equal(video_driver_get_ident(), "vulkan"))
      vulkan_restore_viewport_and_scaler(video_st);
#endif
}
