A Baked-In set of themes is included in AromaUI, which can be used as-is or customized to fit the needs of your application. The available themes include:


| Theme Name       | Description                                      |
|------------------|--------------------------------------------------|
| Default Theme    | A balanced default theme suitable for most applications. |
| High Contrast Theme | A theme with high contrast colors for improved readability and accessibility. |
| Material Theme Presets | A set of themes based on the Material Design color palette, including presets for purple, blue, teal, green, orange, and pink. |
| Black OLED Theme      | A theme with darker colors for a more subdued and modern look, suitable for low-light environments. |
| Material Dark Theme Presets | A set of dark themes based on the Material Design color palette, including presets for purple, blue, teal, green, orange, and pink. |

### Customization
In addition to the built-in themes, developers can create their own custom themes by defining their own color palettes, spacing, typography, and other style properties.

- The `aroma_theme_create_custom` function allows developers to create a custom theme by providing their own values for the various theme properties. 

- Developers can also modify the global theme using the `aroma_theme_set_global` function, which will affect all components that use the global theme for styling.

- The `aroma_style_apply_theme_colors` function can be used to apply the colors from a theme to a specific style, allowing for more granular control over the appearance of individual components.


### Color Manipulation
The theming system also includes utility functions for manipulating colors, such as adjusting brightness, blending colors, and converting between different color formats. These functions can be useful for creating dynamic themes or for implementing features such as dark mode or user-customizable themes.


For example, the `aroma_color_adjust` function can be used to adjust the brightness of a color by a specified factor, while the `aroma_color_blend` function can be used to blend two colors together based on a specified blend factor. The `aroma_color_rgb` and `aroma_color_rgba` functions can be used to create colors from RGB or RGBA values, while the `aroma_color_extract_rgb` function can be used to extract the RGB components from a color value.

### Shadows
The theming system also includes support for shadows, which can be used to add depth and visual interest to UI components.
- The `AromaShadow` struct defines the properties of a shadow, including the blur radius, offset, color, and opacity.

- The `aroma_shadow_create_*` functions provide predefined shadow styles, while the `aroma_shadow_create_custom` function allows developers to create their own custom shadow styles. 

- The `aroma_style_apply_shadow` function can be used to apply a shadow to a specific style, allowing for more control over the appearance of individual components. Shadows can be particularly effective when used in combination with themes, as they can help to create a sense of hierarchy and focus within the UI.

For example, a component with a deeper shadow may be perceived as being more important or interactive than a component with a softer shadow. Additionally, shadows can be used to create a sense of depth and layering within the UI, which can enhance the overall visual appeal and user experience of the application.    

