#ifndef MISTER_SPY_GMW_BLIT_H
#define MISTER_SPY_GMW_BLIT_H

#include <stdint.h>

/* Records match_delta passed to gmw_blit — the one delta-path value that never
 * reaches the wire, so specs can't otherwise observe it. */
uint32_t spy_gmw_last_match_delta(void);
uint32_t spy_gmw_blit_count(void);

#endif /* MISTER_SPY_GMW_BLIT_H */
