/* Link seam: menu state singleton. Defaults inactive; fake_set_menu_alive toggles
 * MENU_ST_FLAG_ALIVE to reach the menu (RGBA4444) blit arm. */
#include <menu/menu_driver.h>
#include <menu/menu_defines.h>
#include <stdbool.h>

static struct menu_state g_menu;

struct menu_state *menu_state_get_ptr(void)
{
   return &g_menu;
}

void fake_set_menu_alive(bool on)
{
   if (on)
      g_menu.flags |= MENU_ST_FLAG_ALIVE;
   else
      g_menu.flags &= ~MENU_ST_FLAG_ALIVE;
}
