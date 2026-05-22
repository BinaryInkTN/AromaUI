#include "font_manager.h"
#include "app_state.h"
#include "aroma.h"


void init_fonts(void)
{
    state.ui_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 24);
    state.icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    state.tab_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 128);
    state.clock_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 68);
    state.clock_pm_am_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 24);
    state.settings_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 18);
}

void cleanup_fonts(void)
{
    if (state.ui_font) aroma_ui_unload_font(state.ui_font);
    if (state.icon_font) aroma_ui_unload_font(state.icon_font);
    if (state.tab_font) aroma_ui_unload_font(state.tab_font);
    if (state.clock_font) aroma_ui_unload_font(state.clock_font);
    if (state.clock_pm_am_font) aroma_ui_unload_font(state.clock_pm_am_font);
    if (state.settings_font) aroma_ui_unload_font(state.settings_font);
}