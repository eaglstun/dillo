#ifndef __MACOS_AUTOMATION_HH__
#define __MACOS_AUTOMATION_HH__

#ifdef __APPLE__

/** Register Dillo's native handler for dillo: automation URLs. */
void a_Mac_automation_init(void);

/** Deliver any URL received before the first browser window was ready. */
void a_Mac_automation_ready(void);

#endif /* __APPLE__ */

#endif /* __MACOS_AUTOMATION_HH__ */
