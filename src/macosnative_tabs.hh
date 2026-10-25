#ifndef __MACOSNATIVE_TABS_HH__
#define __MACOSNATIVE_TABS_HH__

void a_Mac_tabs_configure(void *fl_window);
void a_Mac_tabs_add(void *source_fl_window, void *new_fl_window, int focus);
void a_Mac_tabs_remove(void *fl_window);
void a_Mac_tabs_select_relative(void *fl_window, int direction);
void a_Mac_tabs_select_index(void *fl_window, int index);
void a_Mac_tabs_set_title(void *fl_window, const char *title);
int a_Mac_tabs_close_selected(void);
int a_Mac_tabs_is_front(void *fl_window);
int a_Mac_tabs_is_selected(void *fl_window);

#endif
