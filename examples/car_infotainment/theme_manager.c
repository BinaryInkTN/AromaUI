#include "theme_manager.h"
#include "app_state.h"
#include "aroma.h"
#include <string.h>

void init_theme(void)
{
    if (!state.initialized) {
        return;
    }
    
    memset(&state.theme, 0, sizeof(AromaTheme));
    
    state.theme = aroma_theme_create_material_blue_dark();
    state.theme.colors.surface = 0xFF000000; // Darker surface color for better contrast
    
    aroma_ui_set_theme(&state.theme);
    state.dark_theme_enabled = false;
}

void apply_theme(bool dark_mode)
{
    state.dark_theme_enabled = dark_mode;
    
    if (dark_mode) {
        AromaTheme new_theme = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_BLUE);
        if (new_theme.colors.primary != 0) {
            state.theme = new_theme;
        }
    } else {
        AromaTheme new_theme = aroma_theme_create_material_blue();

        state.theme = new_theme;
    }
    
    state.theme.enable_shadows = false;
    aroma_ui_set_theme(&state.theme);
}

void toggle_theme(void)
{
    apply_theme(!state.dark_theme_enabled);
}