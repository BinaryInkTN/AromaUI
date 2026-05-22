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
    
    state.theme = aroma_theme_create_material_blue();
    state.theme.enable_shadows = false;
    
    if (state.theme.colors.primary == 0) {
        state.theme.colors.primary = 0xFF2196F3;
    }
    if (state.theme.colors.background == 0) {
        state.theme.colors.background = 0xFF1E1E1E;
    }
    
    state.theme.colors.background = aroma_color_blend(
        state.theme.colors.primary, 
        state.theme.colors.background, 
        0.96f
    );
    
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
        AromaTheme new_theme = aroma_theme_create_high_contrast();
        new_theme.colors.primary = 0xFF2196F3;
        new_theme.colors.primary_dark = 0xFF1976D2;
        new_theme.colors.primary_light = 0xFFBBDEFB;
        state.theme = new_theme;
    }
    
    state.theme.enable_shadows = false;
    aroma_ui_set_theme(&state.theme);
}

void toggle_theme(void)
{
    apply_theme(!state.dark_theme_enabled);
}