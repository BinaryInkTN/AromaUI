#include "font_manager.h"
#include "app_state.h"
#include "aroma.h"
#include <stdio.h>




static AromaFont* create_font_safe(const unsigned char *data, unsigned int len, int size, const char *name)
{
    if (!data || len == 0 || size <= 0) {
        fprintf(stderr, "FONT ERROR: Invalid parameters for %s\n", name);
        return NULL;
    }
    
    AromaFont *font = aroma_font_create_from_memory((unsigned char *)data, len, size);
    if (!font) {
        fprintf(stderr, "FONT ERROR: Failed to create %s (size %d)\n", name, size);
    }
    
    return font;
}

bool init_fonts(void)
{
    bool success = true;
    
    
    state.ui_font = create_font_safe(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 24, "ui_font");
    if (!state.ui_font) success = false;
    
    state.icon_font =     state.ui_font;

    
    state.tab_font =     state.ui_font;

    state.clock_font =    state.ui_font;
    
    state.clock_pm_am_font =    state.ui_font;

    
    state.settings_font =     state.ui_font;

    
    return success;
}

void cleanup_fonts(void)
{
    if (state.settings_font) {
        aroma_ui_unload_font(state.settings_font);
        state.settings_font = NULL;
    }
    
    if (state.clock_pm_am_font) {
        aroma_ui_unload_font(state.clock_pm_am_font);
        state.clock_pm_am_font = NULL;
    }
    
    if (state.clock_font) {
        aroma_ui_unload_font(state.clock_font);
        state.clock_font = NULL;
    }
    
    if (state.tab_font) {
        aroma_ui_unload_font(state.tab_font);
        state.tab_font = NULL;
    }
    
    if (state.icon_font) {
        aroma_ui_unload_font(state.icon_font);
        state.icon_font = NULL;
    }
    
    if (state.ui_font) {
        aroma_ui_unload_font(state.ui_font);
        state.ui_font = NULL;
    }
}