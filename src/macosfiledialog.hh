#ifndef __MACOS_FILE_DIALOG_HH__
#define __MACOS_FILE_DIALOG_HH__

#ifdef __APPLE__

/**
 * Show the native macOS save panel.
 *
 * The returned pointer remains valid until the next call. NULL means the
 * panel was cancelled.
 */
const char *a_Mac_save_file_dialog(const char *title, const char *pattern,
                                   const char *fname);

#endif /* __APPLE__ */

#endif /* __MACOS_FILE_DIALOG_HH__ */
