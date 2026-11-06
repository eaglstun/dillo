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

/**
 * Same, but drop the menu at (x, y) in fl_window's coordinates, e.g. just
 * below a toolbar button, the way native toolbar menus open.
 */
const Fl_Menu_Item *a_Mac_context_menu_popup_at(const Fl_Menu_Item *menu,
                                                void *fl_window, int x, int y);

#endif /* __APPLE__ */

#endif /* __MACOS_CONTEXT_MENU_HH__ */
