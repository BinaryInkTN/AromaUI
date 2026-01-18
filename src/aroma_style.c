#include "aroma_style.h"
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

    theme.colors.primary = 0x0078D7;        
    theme.colors.primary_dark = 0x005499;   
    theme.colors.primary_light = 0x4CC2FF;  
    theme.colors.secondary = 0xFF9C00;      
    theme.colors.background = 0xF0F0F0;     
    theme.colors.surface = 0xFFFFFF;        
    theme.colors.text_primary = 0x000000;   
    theme.colors.text_secondary = 0x6D6D6D; 
    theme.colors.border = 0x707070;         
    theme.colors.error = 0xE81123;          

    theme.spacing.padding = 8;              
    theme.spacing.margin = 6;               
    theme.spacing.border_radius = 3;        
    theme.spacing.border_width = 1;         
    theme.spacing.shadow_offset = 2;        

    theme.typography.font_size = 12;        
    theme.typography.line_height = 16;      
    theme.typography.letter_spacing = 0;
    theme.typography.font_color = 0x000000;
    theme.typography.font_name = "system";

    theme.transition_duration_ms = 150;     
    theme.enable_shadows = true;            

    return theme;
}

AromaTheme aroma_theme_create_dark(void) {
    AromaTheme theme = aroma_theme_create_default();

    theme.colors.primary = 0x3399FF;        
    theme.colors.background = 0x2D2D30;     
    theme.colors.surface = 0x2C2C2E;        
    theme.colors.text_primary = 0xFFFFFF;   
    theme.colors.text_secondary = 0xA1A1A6; 
    theme.colors.border = 0x3C3C3F;         

    return theme;
}

AromaTheme aroma_theme_create_high_contrast(void) {
    AromaTheme theme;
    memset(&theme, 0, sizeof(AromaTheme));

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
    theme.typography.font_color = 0x000000;

    theme.transition_duration_ms = 100;
    theme.enable_shadows = false;            

    return theme;
}

AromaTheme aroma_theme_create_custom(void) {
    AromaTheme theme;
    memset(&theme, 0, sizeof(AromaTheme));
    return theme;
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
    style.hover_color = theme->colors.primary_light;
    style.active_color = theme->colors.primary_dark;
    style.disabled_color = theme->colors.text_secondary;
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

