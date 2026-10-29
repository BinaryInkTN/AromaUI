#ifndef LOUD_DASHBOARD_H
#define LOUD_DASHBOARD_H

#include "aroma_font.h"
#include "aroma_incense_loader.h"

#ifdef __cplusplus
extern "C" {
#endif


void loud_dashboard_register_callbacks(void);


void loud_dashboard_bind_fonts(AromaFont **fonts, const int *sizes, int count);


void loud_dashboard_reset(void);


void loud_dashboard_after_mount(IncenseRegistry *registry);

#ifdef __cplusplus
}
#endif

#endif
