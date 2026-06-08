/* Link seam: settings singleton. Returns a real (zero-initialised) settings_t
 * so gfx_mister.c reads the actual struct layout; the driver sets the handful
 * of mister_* fields it cares about before connecting. */
#include <configuration.h>

static settings_t g_settings;

settings_t *config_get_ptr(void)
{
   return &g_settings;
}
