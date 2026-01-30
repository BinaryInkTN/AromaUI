#ifndef AROMA_STYLE_H
#define AROMA_STYLE_H

#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AromaColorPalette {
    uint32_t primary;
    uint32_t primary_dark;
    uint32_t primary_light;
    uint32_t secondary;
    uint32_t background;
    uint32_t surface;
    uint32_t text_primary;
    uint32_t text_secondary;
    uint32_t border;
    uint32_t error;
} AromaColorPalette;

typedef struct AromaSpacing {
    int padding;
    int margin;
    int border_radius;
    int border_width;
    int shadow_offset;
} AromaSpacing;

typedef struct AromaTypography {
    int font_size;
    int line_height;
    int letter_spacing;
    uint32_t font_color;
    const char* font_name;
} AromaTypography;

typedef struct AromaTheme {
    AromaColorPalette colors;
    AromaSpacing spacing;
    AromaTypography typography;

    int transition_duration_ms;
    bool enable_shadows;
} AromaTheme;

typedef enum AromaMaterialThemePreset {
    AROMA_THEME_MATERIAL_PURPLE,
    AROMA_THEME_MATERIAL_BLUE,
    AROMA_THEME_MATERIAL_TEAL,
    AROMA_THEME_MATERIAL_GREEN,
    AROMA_THEME_MATERIAL_ORANGE,
    AROMA_THEME_MATERIAL_PINK
} AromaMaterialThemePreset;

typedef struct AromaStyle {

    uint32_t idle_color;
    uint32_t hover_color;
    uint32_t active_color;
    uint32_t disabled_color;
    uint32_t border_color;
    uint32_t text_color;
    uint32_t background_color;

    int padding;
    int margin;
    int border_radius;
    int border_width;

    int shadow_blur;
    int shadow_offset_x;
    int shadow_offset_y;
    uint32_t shadow_color;

    bool is_disabled;
    bool has_custom_colors;
} AromaStyle;

/* Light theme functions */
AromaTheme aroma_theme_create_default(void);
AromaTheme aroma_theme_create_high_contrast(void);
AromaTheme aroma_theme_create_custom(void);

/* Material theme presets (light) */
AromaTheme aroma_theme_create_material_preset(AromaMaterialThemePreset preset);
AromaTheme aroma_theme_create_material_blue(void);
AromaTheme aroma_theme_create_material_teal(void);
AromaTheme aroma_theme_create_material_green(void);
AromaTheme aroma_theme_create_material_orange(void);
AromaTheme aroma_theme_create_material_pink(void);

/* Dark theme functions */
AromaTheme aroma_theme_create_dark(void);
AromaTheme aroma_theme_create_material_black(void);
AromaTheme aroma_theme_create_high_contrast_dark(void);
AromaTheme aroma_theme_create_material_preset_dark(AromaMaterialThemePreset preset);

/* Dark material theme presets */
AromaTheme aroma_theme_create_material_blue_dark(void);
AromaTheme aroma_theme_create_material_teal_dark(void);
AromaTheme aroma_theme_create_material_green_dark(void);
AromaTheme aroma_theme_create_material_orange_dark(void);
AromaTheme aroma_theme_create_material_pink_dark(void);

/* Style functions */
AromaStyle aroma_style_create_from_theme(const AromaTheme* theme);
AromaStyle aroma_style_create_default(void);
AromaStyle aroma_style_create_primary(void);
AromaStyle aroma_style_create_secondary(void);
AromaStyle aroma_style_create_error(void);

/* Theme management */
void aroma_theme_set_global(const AromaTheme* theme);
AromaTheme aroma_theme_get_global(void);
void aroma_style_apply_theme_colors(AromaStyle* style, const AromaTheme* theme, bool is_primary);

/* Color functions */
uint32_t aroma_color_adjust(uint32_t color, float factor);
uint32_t aroma_color_blend(uint32_t color1, uint32_t color2, float blend);
uint32_t aroma_color_rgb(uint8_t r, uint8_t g, uint8_t b);
uint32_t aroma_color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void aroma_color_extract_rgb(uint32_t color, uint8_t* r, uint8_t* g, uint8_t* b);

/* Shadow functions */
typedef struct AromaShadow {
    int blur_radius;
    int offset_x;
    int offset_y;
    uint32_t color;
    float opacity;
} AromaShadow;

AromaShadow aroma_shadow_create_soft(void);
AromaShadow aroma_shadow_create_subtle(void);
AromaShadow aroma_shadow_create_deep(void);
AromaShadow aroma_shadow_create_dark_mode(void);
AromaShadow aroma_shadow_create_custom(int blur_radius, int offset_x, int offset_y,
                                        uint32_t color, float opacity);
AromaShadow aroma_shadow_get_theme_default(void);
void aroma_style_apply_shadow(AromaStyle* style, const AromaShadow* shadow);
#ifdef __cplusplus
}
#endif
#endif
