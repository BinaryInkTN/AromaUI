
AromaUI's theming system provides centralized control over colors, typography, spacing, and shadows. It supports built-in presets (Material, Dark, High Contrast) and runtime theme switching.

## Quick Start

```c
// Use a built-in dark theme
AromaTheme theme = aroma_theme_create_dark();
aroma_ui_set_theme(&theme);

// Customize colors
theme.colors.background = 0xFF1A1A1A;
theme.colors.primary = 0xBB86FC;

// Adjust spacing
theme.spacing.padding = 16;
theme.spacing.border_radius = 8.0f;

// Apply to a specific widget style
AromaStyle style;
aroma_style_create_from_theme(&style, &theme, true);
aroma_button_set_style(btn, &style);
```

## Theme Structure

| Struct | Purpose | Key Fields |
|---|---|---|
| `AromaColorPalette` | Color scheme | `primary`, `background`, `surface`, `text_primary`, `border` |
| `AromaSpacing` | Layout metrics | `padding`, `margin`, `border_radius`, `border_width` |
| `AromaTypography` | Text appearance | `font_size`, `line_height`, `font_name`, `font_color` |
| `AromaTheme` | Top-level container | Combines Palette, Spacing, Typography |
| `AromaStyle` | Component styling | State colors (`idle`, `hover`, `active`), shadow |

## Built-in Presets

| Function | Description |
|---|---|
| `aroma_theme_create_default()` | Standard light theme |
| `aroma_theme_create_dark()` | Standard dark theme |
| `aroma_theme_create_material_preset(preset)` | Material light theme (6 color variants) |
| `aroma_theme_create_material_preset_dark(preset)` | Material dark theme |

Material presets: `PURPLE`, `BLUE`, `TEAL`, `GREEN`, `ORANGE`, `PINK`

## Color Utilities

```c
// Adjust brightness (factor > 0 lightens, < 0 darkens)
uint32_t lighter = aroma_color_adjust(color, 0.2f);

// Blend two colors
uint32_t blended = aroma_color_blend(color1, color2, 0.5f);

// Pack RGBA
uint32_t rgba = aroma_color_rgba(0xFF, 0x00, 0x00, 0xFF);
```

## Shadows

```c
AromaShadow shadow = aroma_shadow_create_soft();
aroma_style_apply_shadow(&style, &shadow);
```

Presets: `aroma_shadow_create_soft()`, `aroma_shadow_create_subtle()`, `aroma_shadow_create_deep()`

## How Themes Apply

1. Set a global theme with `aroma_ui_set_theme(&theme)`
2. Widgets read the global theme during creation
3. Call `aroma_style_create_from_theme()` to map theme colors to a widget's state colors
4. The widget uses these colors during its `draw_cb`

## Theming APIs

The theme system exposes these functions for runtime customization:

| Function | Purpose |
|---|---|
| `aroma_ui_set_theme(&theme)` | Install a global theme for all widgets |
| `aroma_theme_create_custom(&palette, &spacing, &typo)` | Build a theme from scratch |
| `aroma_style_create_from_theme(&style, &theme, is_primary)` | Map theme colors to a widget style |
| `aroma_color_blend(c1, c2, factor)` | Linearly interpolate between two colors |
| `aroma_color_adjust(color, factor)` | Brighten or darken a color |
| `aroma_shadow_create_soft/subtle/deep()` | Generate shadow presets |
| `aroma_style_apply_shadow(&style, &shadow)` | Bind shadow to a widget style |

Widgets read the global theme during creation. Override per-widget appearance by creating a custom `AromaStyle` and attaching it to the node.

## What's Next

- Explore [Widget Library](Layout-and-Navigation-Widgets.md) to see theming in action.
- Learn [Rendering](Rendering-Pipeline-and-DrawList.md) for draw optimization.
- Check [Animation](Animation-Engine.md) for property transitions.
