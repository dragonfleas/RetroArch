/* Link seam: video-driver globals gfx_mister.c queries.
 *   - software path (hw_render = false), so read_viewport is never invoked
 *   - ident "test" (not gl/glcore/vulkan), so the GPU resize externs below are
 *     referenced but never called — they exist only to satisfy the linker. */
#include <gfx/video_driver.h>
#include <retroarch.h>

/* hw-render is injected so specs can exercise the readback blit path (:331).
 * Default false (software path). */
static bool g_hw_render = false;
bool video_driver_cached_frame_is_hw_render(void) { return g_hw_render; }
void fake_set_hw_render(bool on)                  { g_hw_render = on; }

const char *video_driver_get_ident(void)          { return "test"; }

/* Rotation is injected here (the only place gfx_mister.c reads it) so specs can
 * exercise the rotated blit branches. Default 0 (ORIENTATION_NORMAL). */
static unsigned int g_rotation = 0;
unsigned int retroarch_get_rotation(void)          { return g_rotation; }
void fake_set_rotation(unsigned int r)             { g_rotation = r; }

void gl2_resize_viewport_and_scaler(video_driver_state_t *video_st, unsigned width,
                                    unsigned height, double x_scale, double y_scale)
{ (void)video_st; (void)width; (void)height; (void)x_scale; (void)y_scale; }

void gl3_resize_viewport_and_scaler(video_driver_state_t *video_st, unsigned width,
                                    unsigned height, double x_scale, double y_scale)
{ (void)video_st; (void)width; (void)height; (void)x_scale; (void)y_scale; }

void vulkan_resize_viewport_and_scaler(video_driver_state_t *video_st, unsigned width,
                                       unsigned height, double x_scale, double y_scale)
{ (void)video_st; (void)width; (void)height; (void)x_scale; (void)y_scale; }

void gl2_restore_viewport_and_scaler(video_driver_state_t *video_st)    { (void)video_st; }
void gl3_restore_viewport_and_scaler(video_driver_state_t *video_st)    { (void)video_st; }
void vulkan_restore_viewport_and_scaler(video_driver_state_t *video_st) { (void)video_st; }
