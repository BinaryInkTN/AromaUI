/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "core/aroma_style.h"
#include <string.h>

static AromaTheme g_global_theme;
static bool g_theme_initialized = false;

static uint8_t color_clamp(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

uint32_t aroma_color_adjust(uint32_t color, float factor) {
    uint8_t r, g, b;
    aroma_color_extract_rgb(color, &r, &g, &b);

    int new_r = (int)r + (int)(255.0f * factor);
    int new_g = (int)g + (int)(255.0f * factor);
    int new_b = (int)b + (int)(255.0f * factor);

    return aroma_color_rgb(color_clamp(new_r), color_clamp(new_g), color_clamp(new_b));
}

uint32_t aroma_color_blend(uint32_t color1, uint32_t color2, float blend) {
    uint8_t r1, g1, b1;
    uint8_t r2, g2, b2;

    aroma_color_extract_rgb(color1, &r1, &g1, &b1);
    aroma_color_extract_rgb(color2, &r2, &g2, &b2);

    uint8_t r = (uint8_t)(r1 * (1.0f - blend) + r2 * blend);
    uint8_t g = (uint8_t)(g1 * (1.0f - blend) + g2 * blend);
    uint8_t b = (uint8_t)(b1 * (1.0f - blend) + b2 * blend);

    return aroma_color_rgb(r, g, b);
}

uint32_t aroma_color_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t aroma_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b | ((uint32_t)a << 24);
}

void aroma_color_extract_rgb(uint32_t color, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (r) *r = (uint8_t)((color >> 16) & 0xFF);
    if (g) *g = (uint8_t)((color >> 8) & 0xFF);
    if (b) *b = (uint8_t)(color & 0xFF);
}

AromaTheme aroma_theme_create_default(void) {
    AromaTheme theme;
    memset(&theme, 0, sizeof(AromaTheme));

    theme.colors.primary = 0x6750A4;
    theme.colors.primary_dark = 0x21005D;
    theme.colors.primary_light = 0xEADDFF;
    theme.colors.secondary = 0x625B71;
    theme.colors.background = 0xFFFBFE;
    theme.colors.surface = 0xFFFBFE;
    theme.colors.text_primary = 0x1C1B1F;
    theme.colors.text_secondary = 0x49454F;
    theme.colors.border = 0x79747E;
    theme.colors.error = 0xB3261E;

    theme.spacing.padding = 16;
    theme.spacing.margin = 8;
    theme.spacing.border_radius = 12;
    theme.spacing.border_width = 1;
    theme.spacing.shadow_offset = 2;

    theme.typography.font_size = 12;
    theme.typography.line_height = 16;
    theme.typography.letter_spacing = 0;
    theme.typography.font_color = theme.colors.text_primary;
    theme.typography.font_name = "system";

    theme.transition_duration_ms = 150;
    theme.enable_shadows = true;

    return theme;
}

AromaTheme aroma_theme_create_dark(void) {
    AromaTheme theme;

    theme.colors.primary = 0xD0BCFF;
    theme.colors.primary_dark = 0x381E72;
    theme.colors.primary_light = 0x4F378B;
    theme.colors.secondary = 0xCCC2DC;
    theme.colors.background = 0x141218;
    theme.colors.surface = 0x1D1B20;
    theme.colors.text_primary = 0xE6E1E5;
    theme.colors.text_secondary = 0xCAC4D0;
    theme.colors.border = 0x938F99;
    theme.colors.error = 0xF2B8B5;

    theme.spacing.padding = 16;
    theme.spacing.margin = 8;
    theme.spacing.border_radius = 12;
    theme.spacing.border_width = 1;
    theme.spacing.shadow_offset = 2;

    theme.typography.font_size = 12;
    theme.typography.line_height = 16;
    theme.typography.letter_spacing = 0;
    theme.typography.font_color = theme.colors.text_primary;
    theme.typography.font_name = "system";

    theme.transition_duration_ms = 150;
    theme.enable_shadows = false;

    return theme;
}

AromaTheme aroma_theme_create_high_contrast(void) {
    AromaTheme theme;

    theme.colors.primary = 0x0000FF;
    theme.colors.primary_dark = 0x000080;
    theme.colors.primary_light = 0x6666FF;
    theme.colors.secondary = 0xFF0000;
    theme.colors.background = 0xFFFFFF;
    theme.colors.surface = 0xF0F0F0;
    theme.colors.text_primary = 0x000000;
    theme.colors.text_secondary = 0x404040;
    theme.colors.border = 0x000000;
    theme.colors.error = 0xFF0000;

    theme.spacing.padding = 16;
    theme.spacing.margin = 12;
    theme.spacing.border_radius = 4;
    theme.spacing.border_width = 2;
    theme.spacing.shadow_offset = 3;

    theme.typography.font_size = 16;
    theme.typography.line_height = 24;
    theme.typography.letter_spacing = 0;
    theme.typography.font_color = theme.colors.text_primary;
    theme.typography.font_name = "system";

    theme.transition_duration_ms = 100;
    theme.enable_shadows = false;

    return theme;
}

AromaTheme aroma_theme_create_high_contrast_dark(void) {
    AromaTheme theme;

    theme.colors.primary = 0xFFFF00;
    theme.colors.primary_dark = 0xCCCC00;
    theme.colors.primary_light = 0xFFFF80;
    theme.colors.secondary = 0x00FFFF;
    theme.colors.background = 0x000000;
    theme.colors.surface = 0x1A1A1A;
    theme.colors.text_primary = 0xFFFFFF;
    theme.colors.text_secondary = 0xCCCCCC;
    theme.colors.border = 0xFFFFFF;
    theme.colors.error = 0xFF0000;

    theme.spacing.padding = 16;
    theme.spacing.margin = 12;
    theme.spacing.border_radius = 4;
    theme.spacing.border_width = 3;
    theme.spacing.shadow_offset = 0;

    theme.typography.font_size = 18;
    theme.typography.line_height = 24;
    theme.typography.letter_spacing = 1;
    theme.typography.font_color = theme.colors.text_primary;
    theme.typography.font_name = "system";

    theme.transition_duration_ms = 100;
    theme.enable_shadows = false;

    return theme;
}

AromaTheme aroma_theme_create_material_black(void) {
    AromaTheme theme;

    theme.colors.primary = 0xBB86FC;
    theme.colors.primary_dark = 0x3700B3;
    theme.colors.primary_light = 0x6200EE;
    theme.colors.secondary = 0x03DAC6;
    theme.colors.background = 0x000000;
    theme.colors.surface = 0x121212;
    theme.colors.text_primary = 0xFFFFFF;
    theme.colors.text_secondary = 0xB3B3B3;
    theme.colors.border = 0x2A2A2A;
    theme.colors.error = 0xCF6679;

    theme.spacing.padding = 16;
    theme.spacing.margin = 8;
    theme.spacing.border_radius = 12;
    theme.spacing.border_width = 1;
    theme.spacing.shadow_offset = 2;

    theme.typography.font_size = 12;
    theme.typography.line_height = 16;
    theme.typography.letter_spacing = 0;
    theme.typography.font_color = theme.colors.text_primary;
    theme.typography.font_name = "system";

    theme.transition_duration_ms = 150;
    theme.enable_shadows = false;

    return theme;
}

AromaTheme aroma_theme_create_custom(void) {
    AromaTheme theme;
    memset(&theme, 0, sizeof(AromaTheme));
    return theme;
}

AromaTheme aroma_theme_create_material_preset(AromaMaterialThemePreset preset) {
    AromaTheme theme = aroma_theme_create_default();

    switch (preset) {
        case AROMA_THEME_MATERIAL_BLUE:
            theme.colors.primary = 0x1A73E8;
            theme.colors.primary_dark = 0x174EA6;
            theme.colors.primary_light = 0xD2E3FC;
            theme.colors.secondary = 0x5F6368;
            break;
        case AROMA_THEME_MATERIAL_TEAL:
            theme.colors.primary = 0x00796B;
            theme.colors.primary_dark = 0x004D40;
            theme.colors.primary_light = 0xB2DFDB;
            theme.colors.secondary = 0x455A64;
            break;
        case AROMA_THEME_MATERIAL_GREEN:
            theme.colors.primary = 0x2E7D32;
            theme.colors.primary_dark = 0x1B5E20;
            theme.colors.primary_light = 0xC8E6C9;
            theme.colors.secondary = 0x546E7A;
            break;
        case AROMA_THEME_MATERIAL_ORANGE:
            theme.colors.primary = 0xFB8C00;
            theme.colors.primary_dark = 0xEF6C00;
            theme.colors.primary_light = 0xFFE0B2;
            theme.colors.secondary = 0x6D4C41;
            break;
        case AROMA_THEME_MATERIAL_PINK:
            theme.colors.primary = 0xD81B60;
            theme.colors.primary_dark = 0xAD1457;
            theme.colors.primary_light = 0xF8BBD0;
            theme.colors.secondary = 0x6A1B9A;
            break;
        case AROMA_THEME_MATERIAL_PURPLE:
        default:
            break;
    }

    return theme;
}

AromaTheme aroma_theme_create_material_preset_dark(AromaMaterialThemePreset preset) {
    AromaTheme theme = aroma_theme_create_dark();

    switch (preset) {
        case AROMA_THEME_MATERIAL_BLUE:
            theme.colors.primary = 0x8AB4F8;
            theme.colors.primary_dark = 0x669DF6;
            theme.colors.primary_light = 0x4285F4;
            theme.colors.secondary = 0x9AA0A6;
            break;
        case AROMA_THEME_MATERIAL_TEAL:
            theme.colors.primary = 0x4DB6AC;
            theme.colors.primary_dark = 0x26A69A;
            theme.colors.primary_light = 0x80CBC4;
            theme.colors.secondary = 0x90A4AE;
            break;
        case AROMA_THEME_MATERIAL_GREEN:
            theme.colors.primary = 0x81C995;
            theme.colors.primary_dark = 0x66BB6A;
            theme.colors.primary_light = 0xA5D6A7;
            theme.colors.secondary = 0xB0BEC5;
            break;
        case AROMA_THEME_MATERIAL_ORANGE:
            theme.colors.primary = 0xFFB74D;
            theme.colors.primary_dark = 0xFF9800;
            theme.colors.primary_light = 0xFFCC80;
            theme.colors.secondary = 0xBCAAA4;
            break;
        case AROMA_THEME_MATERIAL_PINK:
            theme.colors.primary = 0xF48FB1;
            theme.colors.primary_dark = 0xEC407A;
            theme.colors.primary_light = 0xF8BBD0;
            theme.colors.secondary = 0xCE93D8;
            break;
        case AROMA_THEME_MATERIAL_PURPLE:
            theme.colors.primary = 0xD0BCFF;
            theme.colors.primary_dark = 0xBA68C8;
            theme.colors.primary_light = 0xE1BEE7;
            theme.colors.secondary = 0xC5CAE9;
            break;
    }

    return theme;
}

AromaTheme aroma_theme_create_material_blue(void) {
    return aroma_theme_create_material_preset(AROMA_THEME_MATERIAL_BLUE);
}

AromaTheme aroma_theme_create_material_teal(void) {
    return aroma_theme_create_material_preset(AROMA_THEME_MATERIAL_TEAL);
}

AromaTheme aroma_theme_create_material_green(void) {
    return aroma_theme_create_material_preset(AROMA_THEME_MATERIAL_GREEN);
}

AromaTheme aroma_theme_create_material_orange(void) {
    return aroma_theme_create_material_preset(AROMA_THEME_MATERIAL_ORANGE);
}

AromaTheme aroma_theme_create_material_pink(void) {
    return aroma_theme_create_material_preset(AROMA_THEME_MATERIAL_PINK);
}

AromaTheme aroma_theme_create_material_blue_dark(void) {
    return aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_BLUE);
}

AromaTheme aroma_theme_create_material_teal_dark(void) {
    return aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_TEAL);
}

AromaTheme aroma_theme_create_material_green_dark(void) {
    return aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_GREEN);
}

AromaTheme aroma_theme_create_material_orange_dark(void) {
    return aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_ORANGE);
}

AromaTheme aroma_theme_create_material_pink_dark(void) {
    return aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_PINK);
}

void aroma_theme_set_global(const AromaTheme* theme) {
    if (theme) {
        memcpy(&g_global_theme, theme, sizeof(AromaTheme));
        g_theme_initialized = true;
    }
}

AromaTheme aroma_theme_get_global(void) {
    if (!g_theme_initialized) {
        g_global_theme = aroma_theme_create_default();
        g_theme_initialized = true;
    }
    return g_global_theme;
}

AromaStyle aroma_style_create_from_theme(const AromaTheme* theme) {
    AromaStyle style;
    memset(&style, 0, sizeof(AromaStyle));

    if (!theme) {
        AromaTheme default_theme = aroma_theme_get_global();
        theme = &default_theme;
    }

    style.idle_color = theme->colors.surface;
    style.hover_color = aroma_color_adjust(theme->colors.surface, 0.1f);
    style.active_color = aroma_color_adjust(theme->colors.surface, -0.1f);
    style.disabled_color = aroma_color_blend(theme->colors.surface, theme->colors.text_secondary, 0.38f);
    style.border_color = theme->colors.border;
    style.text_color = theme->colors.text_primary;
    style.background_color = theme->colors.background;

    style.padding = theme->spacing.padding;
    style.margin = theme->spacing.margin;
    style.border_radius = theme->spacing.border_radius;
    style.border_width = theme->spacing.border_width;

    style.shadow_offset_x = theme->spacing.shadow_offset;
    style.shadow_offset_y = theme->spacing.shadow_offset;
    style.shadow_blur = theme->enable_shadows ? 4 : 0;
    style.shadow_color = 0x000000;

    style.is_disabled = false;
    style.has_custom_colors = false;

    return style;
}

AromaStyle aroma_style_create_default(void) {
    AromaTheme default_theme = aroma_theme_get_global();
    return aroma_style_create_from_theme(&default_theme);
}

void aroma_style_apply_theme_colors(AromaStyle* style, const AromaTheme* theme, bool is_primary) {
    if (!style || !theme) return;

    if (is_primary) {
        style->idle_color = theme->colors.primary;
        style->hover_color = theme->colors.primary_light;
        style->active_color = theme->colors.primary_dark;
    } else {
        style->idle_color = theme->colors.secondary;
        style->hover_color = aroma_color_adjust(theme->colors.secondary, 0.1f);
        style->active_color = aroma_color_adjust(theme->colors.secondary, -0.1f);
    }

    style->text_color = theme->colors.text_primary;
    style->border_color = theme->colors.border;
    style->has_custom_colors = true;
}

AromaStyle aroma_style_create_primary(void) {
    AromaTheme theme = aroma_theme_get_global();
    AromaStyle style = aroma_style_create_from_theme(&theme);
    aroma_style_apply_theme_colors(&style, &theme, true);
    return style;
}

AromaStyle aroma_style_create_secondary(void) {
    AromaTheme theme = aroma_theme_get_global();
    AromaStyle style = aroma_style_create_from_theme(&theme);
    aroma_style_apply_theme_colors(&style, &theme, false);
    return style;
}

AromaStyle aroma_style_create_error(void) {
    AromaTheme theme = aroma_theme_get_global();
    AromaStyle style = aroma_style_create_from_theme(&theme);
    
    style.idle_color = theme.colors.error;
    style.hover_color = aroma_color_adjust(theme.colors.error, 0.2f);
    style.active_color = aroma_color_adjust(theme.colors.error, -0.2f);
    style.text_color = theme.colors.text_primary;
    style.has_custom_colors = true;
    
    return style;
}

AromaShadow aroma_shadow_create_soft(void) {
    AromaShadow shadow;
    shadow.blur_radius = 5;
    shadow.offset_x = 0;
    shadow.offset_y = 2;
    shadow.color = 0x000000;
    shadow.opacity = 0.20f;
    return shadow;
}

AromaShadow aroma_shadow_create_subtle(void) {
    AromaShadow shadow;
    shadow.blur_radius = 3;
    shadow.offset_x = 0;
    shadow.offset_y = 1;
    shadow.color = 0x000000;
    shadow.opacity = 0.12f;
    return shadow;
}

AromaShadow aroma_shadow_create_deep(void) {
    AromaShadow shadow;
    shadow.blur_radius = 10;
    shadow.offset_x = 0;
    shadow.offset_y = 4;
    shadow.color = 0x000000;
    shadow.opacity = 0.35f;
    return shadow;
}

AromaShadow aroma_shadow_create_dark_mode(void) {
    AromaShadow shadow;
    shadow.blur_radius = 8;
    shadow.offset_x = 0;
    shadow.offset_y = 4;
    shadow.color = 0x000000;
    shadow.opacity = 0.40f;
    return shadow;
}

AromaShadow aroma_shadow_create_custom(int blur_radius, int offset_x, int offset_y, 
                                        uint32_t color, float opacity) {
    AromaShadow shadow;
    shadow.blur_radius = blur_radius;
    shadow.offset_x = offset_x;
    shadow.offset_y = offset_y;
    shadow.color = color;
    shadow.opacity = opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
    return shadow;
}

AromaShadow aroma_shadow_get_theme_default(void) {
    if (!g_theme_initialized) {
        return aroma_shadow_create_soft();
    }

    if (g_global_theme.enable_shadows) {
        return aroma_shadow_create_soft();
    } else {
        AromaShadow none;
        memset(&none, 0, sizeof(AromaShadow));
        return none;
    }
}

void aroma_style_apply_shadow(AromaStyle* style, const AromaShadow* shadow) {
    if (!style || !shadow) return;

    style->shadow_blur = shadow->blur_radius;
    style->shadow_offset_x = shadow->offset_x;
    style->shadow_offset_y = shadow->offset_y;
    style->shadow_color = shadow->color;
}