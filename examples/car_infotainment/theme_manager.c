#include "theme_manager.h"
#include "app_state.h"
#include "aroma.h"

void init_theme(void)
{
    state.theme = aroma_theme_create_material_blue();
    state.theme.enable_shadows = false;
    state.theme.colors.background = aroma_color_blend(
        state.theme.colors.primary, state.theme.colors.background, 0.96f);
    aroma_ui_set_theme(&state.theme);
}

void apply_theme(bool dark_mode)
{
    state.dark_theme_enabled = dark_mode;
    if (dark_mode) {
        state.theme = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_BLUE);
    } else {
        state.theme = aroma_theme_create_high_contrast();
        state.theme.colors.primary = 0xFF2196F3;
        state.theme.colors.primary_dark = 0xFF1976D2;
        state.theme.colors.primary_light = 0xFFBBDEFB;
    }
    aroma_ui_set_theme(&state.theme);
}

void toggle_theme(void)
{
    apply_theme(!state.dark_theme_enabled);
}