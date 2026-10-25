#ifndef __MACOSLOCATION_HH__
#define __MACOSLOCATION_HH__

/* Native macOS address-field adapter. The implementation owns the AppKit
 * objects; callers own the opaque handle and must destroy it. */

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*MacLocationActivateCb)(void *data);

void *a_Mac_location_new(void *data, MacLocationActivateCb activate_cb);
void a_Mac_location_free(void *handle);
void a_Mac_location_sync(void *handle, void *fl_window,
                         int x, int y, int w, int h, int visible);
const char *a_Mac_location_get(void *handle);
void a_Mac_location_set(void *handle, const char *text);
void a_Mac_location_focus(void *handle);

#ifdef __cplusplus
}
#endif

#endif /* __MACOSLOCATION_HH__ */
