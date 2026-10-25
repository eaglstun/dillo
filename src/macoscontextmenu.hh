#ifndef __MACOS_CONTEXT_MENU_HH__
#define __MACOS_CONTEXT_MENU_HH__

#ifdef __APPLE__

struct Fl_Menu_Item;

/**
 * Show a hierarchy of FLTK menu items as a native macOS context menu.
 *
 * The returned pointer is one of the items in menu (or a pointed-to
 * submenu), and remains owned by the caller. NULL means the menu was
 * dismissed without choosing anything.
 */
const Fl_Menu_Item *a_Mac_context_menu_popup(const Fl_Menu_Item *menu);

#endif /* __APPLE__ */

#endif /* __MACOS_CONTEXT_MENU_HH__ */
