#ifndef LOUD_DASHBOARD_H
#define LOUD_DASHBOARD_H

#include "aroma_font.h"
#include "aroma_incense_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the callbacks used by the curated Loud dashboard example. */
void loud_dashboard_register_callbacks(void);

/* Copies the sandbox's registered text-size fonts so the demo can measure
 * and position text exactly like the renderer does. */
void loud_dashboard_bind_fonts(AromaFont **fonts, const int *sizes, int count);

/* Resets the demo to its first-boot state. Safe before the window exists. */
void loud_dashboard_reset(void);

/* Called after any example mounts. Applies the default page and all
 * data-driven labels; it is a no-op when the mounted markup is not Loud. */
void loud_dashboard_after_mount(IncenseRegistry *registry);

#ifdef __cplusplus
}
#endif

#endif
