# Theming System

AromaUI supports customizable themes for consistent UI appearance. Themes can be light, dark, or custom color schemes.

## Features
- Light/dark mode
- Custom color palettes
- Dynamic theme switching

## Architecture

```
flowchart TD
    Theme[Theme Manager] --> Apply[Apply Theme]
    Apply --> Widgets[Update Widgets]
```

## Example Usage
```c
aroma_theme_set(DARK);
aroma_theme_set_custom(palette);
```

## API Reference
- aroma_theme_set(mode)
- aroma_theme_set_custom(palette)
