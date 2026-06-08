/* Enabled by -Wl,--wrap=gmw_blit: __wrap_gmw_blit intercepts the call, the real
 * sender is __real_gmw_blit. */
#include "spy_gmw_blit.h"

void __real_gmw_blit(uint32_t frame, uint8_t field, uint16_t vCountSync,
                     uint32_t margin, uint32_t matchDeltaBytes);

static uint32_t g_last_match_delta;
static uint32_t g_blit_count;

void __wrap_gmw_blit(uint32_t frame, uint8_t field, uint16_t vCountSync,
                     uint32_t margin, uint32_t matchDeltaBytes)
{
   g_last_match_delta = matchDeltaBytes;
   g_blit_count++;
   __real_gmw_blit(frame, field, vCountSync, margin, matchDeltaBytes);
}

uint32_t spy_gmw_last_match_delta(void) { return g_last_match_delta; }
uint32_t spy_gmw_blit_count(void)       { return g_blit_count; }
