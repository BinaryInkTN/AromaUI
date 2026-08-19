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
    
    state.icon_font = create_font_safe(icon_ttf, icon_ttf_len, 24, "icon_font");
    if (!state.icon_font) success = false;
    
    state.tab_font = create_font_safe(icon_ttf, icon_ttf_len, 32, "tab_font");
    if (!state.tab_font) success = false;
    
    state.clock_font = create_font_safe(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 58, "clock_font");
    if (!state.clock_font) success = false;
    state.huge_icon_font = create_font_safe(icon_ttf, icon_ttf_len, 64, "huge_icon_font");
    if (!state.huge_icon_font) success = false;
    state.clock_pm_am_font = create_font_safe(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 24, "clock_pm_am_font");
    if (!state.clock_pm_am_font) success = false;
    
    state.settings_font = create_font_safe(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 18, "settings_font");
    if (!state.settings_font) success = false;
   
    state.big_icon_font = create_font_safe(icon_ttf, icon_ttf_len, 64, "big_icon_font");
    if (!state.big_icon_font) success = false;

    state.ac_font = create_font_safe(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 36, "ac_font");
    if (!state.ac_font) success = false;
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

    if(state.huge_icon_font) {
        aroma_ui_unload_font(state.huge_icon_font);
        state.huge_icon_font = NULL;
    }

    if(state.big_icon_font) {
        aroma_ui_unload_font(state.big_icon_font);
        state.big_icon_font = NULL;
    }
       if (state.ac_font) {
        aroma_ui_unload_font(state.ac_font);
        state.ac_font = NULL;
    }
}