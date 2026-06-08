/* Link seam: audio state singleton. output_mister stays 0 (zero-init), so
 * gfx_mister.c skips the audio path entirely. */
#include <audio/audio_driver.h>

static audio_driver_state_t g_audio;

audio_driver_state_t *audio_state_get_ptr(void)
{
   return &g_audio;
}
