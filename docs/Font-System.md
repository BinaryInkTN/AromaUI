
AromaUI's font system loads, manages, and renders typography across Linux, Android, Web, and embedded targets.

## Quick Start

```c
// Load from bundled Ubuntu font
AromaFont *font = aroma_font_create_from_memory(
    aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 24);

// Or from disk
AromaFont *font = aroma_font_create("/path/to/font.ttf", 24);

// Measure text
int w = aroma_font_get_text_width(font, "Hello", 12);
int h = aroma_font_get_line_height(font);
```

## Architecture

- **Core** (`aroma_font.c`) - platform-agnostic font loading and metrics
- **FreeType** - vector rasterization on Linux/Android/Web
- **GLES3 backend** - GPU glyph cache with LRU eviction
- **ESP32/TFT** - bypasses FreeType, uses built-in bitmap fonts

## Key APIs

| Function | Purpose |
|---|---|
| `aroma_font_create(path, size)` | Load font from filesystem |
| `aroma_font_create_from_memory(data, len, size)` | Load from memory buffer |
| `aroma_font_destroy(font)` | Release font resources |
| `aroma_font_get_text_width(font, text, max_width)` | Measure text width |
| `aroma_font_get_line_height(font)` | Get recommended line spacing |
| `aroma_font_get_ascender(font)` | Distance from baseline to top |
| `aroma_font_get_descender(font)` | Distance from baseline to bottom |

## Glyph Caching (GLES3)

The GLES3 backend maintains a per-window glyph cache. Characters 32–127 (ASCII) are pre-loaded on font initialization. Extended characters are loaded on demand and cached as GL textures.

- Cache limit: 16 fonts per window (LRU eviction)
- Texture format: `GL_RED` (single channel) for memory efficiency

## DP/SP Scaling

AromaUI supports density-independent pixels (DP) for layouts and scale-independent pixels (SP) for fonts. On Android, SP values are converted to physical pixels based on the device's display metrics.

```c
#define FONT_TITLE_SP 36
int px = sp(FONT_TITLE_SP);
AromaFont *font = aroma_font_create_from_memory(ttf, len, px);
```

## Platform Specifics

- **ESP32**: Uses fixed bitmap fonts (`FreeSans12pt7b`, `GLCD`). No FreeType dependency.
- **Web/WASM**: Uses Emscripten's file system for font loading, or memory buffers for portability.
- **Android**: Bridges to system font services via JNI when needed.

## What's Next

- Explore [Theming](Theming-and-Styling.md) for typography configuration.
- Learn [Rendering](Rendering-Pipeline-and-DrawList.md) for text draw optimization.
- Check [Testing & Quality](Testing-and-Quality.md) for font rendering tests.
