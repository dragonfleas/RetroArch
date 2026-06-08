/* Link seam: menu state singleton. flags stays 0, so MENU_ST_FLAG_ALIVE is
 * never set and gfx_mister.c treats the menu as inactive. */
#include <menu/menu_driver.h>

static struct menu_state g_menu;

struct menu_state *menu_state_get_ptr(void)
{
   return &g_menu;
}
