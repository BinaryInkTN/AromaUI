#include "font_manager.h"
#include "app_state.h"
#include "aroma.h"

extern unsigned char aroma_ubuntu_ttf[];
extern unsigned int aroma_ubuntu_ttf_len;
extern unsigned char icon_ttf[];
extern unsigned int icon_ttf_len;

void init_fonts(void)
{
    // Initialize all font pointers to NULL first
    state.ui_font = NULL;
    state.icon_font = NULL;
    state.tab_font = NULL;
    state.clock_font = NULL;
    state.clock_pm_am_font = NULL;
    state.settings_font = NULL;
    
    // Create fonts with proper error checking
    state.ui_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 24);
    if (!state.ui_font) return;
    
    state.icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    if (!state.icon_font) return;
    
    state.tab_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 128);
    if (!state.tab_font) return;
    
    state.clock_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 68);
    if (!state.clock_font) return;
    
    state.clock_pm_am_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 24);
    if (!state.clock_pm_am_font) return;
    
    state.settings_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 18);
}

void cleanup_fonts(void)
{
    if (state.ui_font) {
        aroma_ui_unload_font(state.ui_font);
        state.ui_font = NULL;
    }
    if (state.icon_font) {
        aroma_ui_unload_font(state.icon_font);
        state.icon_font = NULL;
    }
    if (state.tab_font) {
        aroma_ui_unload_font(state.tab_font);
        state.tab_font = NULL;
    }
    if (state.clock_font) {
        aroma_ui_unload_font(state.clock_font);
        state.clock_font = NULL;
    }
    if (state.clock_pm_am_font) {
        aroma_ui_unload_font(state.clock_pm_am_font);
        state.clock_pm_am_font = NULL;
    }
    if (state.settings_font) {
        aroma_ui_unload_font(state.settings_font);
        state.settings_font = NULL;
    }
}