
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
theme.spacing.border_radius = 8;

// Apply to a specific widget style
AromaStyle style = aroma_style_create_from_theme(&theme);
aroma_button_apply_style(btn, &style);
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
| `aroma_theme_create_material_blue_dark()` (and `_teal/green/orange/pink_dark`) | Material dark theme variants |

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
2. `aroma_ui_set_theme()` invalidates every window and requests a redraw,
   so the switch paints on the next frame with no manual tree walk
3. Widgets follow the live global theme on every draw; widgets that cache
   derived colors keep a `use_theme_colors` flag and refresh from the
   current theme unless you overrode them with an explicit setter
   (e.g. `aroma_button_set_colors()`, `aroma_listview_set_header_colors()`,
   `aroma_gauge_set_colors()`), which opts that widget out
4. Call `aroma_style_create_from_theme(&theme)` to map theme colors to a
   widget's state colors
5. The widget uses these colors during its `draw_cb`

Helpers for theme-aware code:

```c
uint64_t v0 = aroma_theme_get_version();
aroma_ui_set_theme(&black);
bool changed = (aroma_theme_get_version() != v0); /* true */

if (aroma_theme_is_dark(NULL)) { /* NULL selects the global theme */
    /* pick inverse-surface colors */
}
```

## Theming APIs

The theme system exposes these functions for runtime customization:

| Function | Purpose |
|---|---|
| `aroma_ui_set_theme(&theme)` | Install a global theme; invalidates all windows and redraws |
| `aroma_theme_get_version()` | Generation counter, bumped on every theme switch |
| `aroma_theme_is_dark(theme)` | True when the theme background is dark (`NULL` = global) |
| `aroma_theme_create_custom()` | Zero-initialized theme to fill in from scratch |
| `aroma_style_create_from_theme(&theme)` | Map theme colors to a widget style (returns `AromaStyle`) |
| `aroma_color_blend(c1, c2, factor)` | Linearly interpolate between two colors |
| `aroma_color_adjust(color, factor)` | Brighten or darken a color |
| `aroma_shadow_create_soft/subtle/deep()` | Generate shadow presets |
| `aroma_style_apply_shadow(&style, &shadow)` | Bind shadow to a widget style |

Widgets follow the live global theme on every draw. Override per-widget appearance with an explicit setter (e.g. `aroma_button_set_colors()`); the override persists across theme switches for that widget.

## Live demo

Toggle the docs light/dark switch and watch this preview follow it. The
dropdown at the top lists every built-in theme - picking one re-themes the
whole preview instantly:

```incense-demo theming
```

## What's Next

- Explore [Widget Library](../widget-library/Layout-and-Navigation-Widgets.md) to see theming in action.
- Learn [Rendering](Rendering-Pipeline-and-DrawList.md) for draw optimization.
- Check [Animation](Animation-Engine.md) for property transitions.
