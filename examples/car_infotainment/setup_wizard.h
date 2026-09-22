#ifndef SETUP_WIZARD_H
#define SETUP_WIZARD_H

/* Android-style first-run setup wizard for the car infotainment example.
 * Shown automatically at boot when no completed setup is stored. Every page
 * is fully functional: values persist via setup_store and take effect
 * immediately (theme, bluetooth, voice flag, device name, wifi).
 */

#include <stdbool.h>

/* Build (once) and show the wizard if setup is not complete. No-op when
 * setup already completed. Must be called after fonts + vehicle view exist. */
void setup_wizard_maybe_show(void);

/* Force (re)show, e.g. to let the user change setup later. */
void setup_wizard_show(void);

bool setup_wizard_is_active(void);

#endif
