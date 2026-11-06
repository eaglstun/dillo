#ifndef __MACOSAPPEARANCE_HH__
#define __MACOSAPPEARANCE_HH__

/* Feed the macOS light/dark appearance to CSS prefers-color-scheme and
 * re-render affected pages when it changes. Call once at startup. */
void a_Mac_appearance_init(void);

#endif /* __MACOSAPPEARANCE_HH__ */
