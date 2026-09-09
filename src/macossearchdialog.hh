#ifndef __MACOS_SEARCH_DIALOG_HH__
#define __MACOS_SEARCH_DIALOG_HH__

#ifdef __APPLE__

/**
 * Show the native macOS web-search dialog.
 *
 * The returned pointer remains valid until the next call. NULL means the
 * dialog was cancelled. chosenEngine receives an index into engineLabels.
 */
const char *a_Mac_search_dialog(const char *title, const char *prompt,
                                const char *const *engineLabels,
                                int engineCount, int selectedEngine,
                                int *chosenEngine);

#endif /* __APPLE__ */

#endif /* __MACOS_SEARCH_DIALOG_HH__ */
