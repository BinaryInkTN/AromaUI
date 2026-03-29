<img src="ui/font_system.png"/>

## Overview

The **Aroma Font Module** provides functionality for loading, managing, and querying font data within AromaUI. It supports loading fonts from both file systems and memory buffers, and exposes essential typography metrics such as line height, ascender, descender, and text width.

This module is designed to be lightweight, flexible, and backend agnostic, making it suitable for desktop, mobile, and embedded environments.

## Header

```c
#include "aroma_font.h"
```

## Data Types

### `AromaFont`

```c
typedef struct AromaFont AromaFont;
```

An opaque structure representing a loaded font instance. The internal implementation is hidden and may vary depending on the backend (for example FreeType).

## API Reference

### Font Creation

#### `aroma_font_create`

```c
AromaFont* aroma_font_create(const char* font_path, int size_px);
```

Loads a font from a file on disk.

**Parameters:**

* `font_path`: Path to the font file (for example `.ttf`, `.otf`)
* `size_px`: Desired font size in pixels

**Returns:**

* Pointer to `AromaFont` on success
* `NULL` on failure

#### `aroma_font_create_from_memory`

```c
AromaFont* aroma_font_create_from_memory(const unsigned char* data, unsigned int data_len, int size_px);
```

Loads a font from a memory buffer.

**Parameters:**

* `data`: Pointer to raw font data
* `data_len`: Size of the data buffer in bytes
* `size_px`: Desired font size in pixels

**Returns:**

* Pointer to `AromaFont` on success
* `NULL` on failure

> AromaUI includes embedded font data for the Ubuntu font family, which can be loaded using `aroma_font_create_from_memory` with the provided `aroma_ubuntu_ttf` and `aroma_ubuntu_ttf_len` variables.

### Font Destruction

#### `aroma_font_destroy`

```c
void aroma_font_destroy(AromaFont* font);
```

Frees all resources associated with a font.

**Parameters:**

* `font`: Font instance to destroy

### Font Metrics

#### `aroma_font_get_line_height`

```c
int aroma_font_get_line_height(AromaFont* font);
```

Returns the total height of a line of text.

#### `aroma_font_get_ascender`

```c
int aroma_font_get_ascender(AromaFont* font);
```

Returns the ascender height (distance above the baseline).

#### `aroma_font_get_descender`

```c
int aroma_font_get_descender(AromaFont* font);
```

Returns the descender height (distance below the baseline).

#### `aroma_font_get_px_size`

```c
int aroma_font_get_px_size(AromaFont* font);
```

Returns the configured pixel size of the font.

### Text Measurement

#### `aroma_font_get_line_width`

```c
int aroma_font_get_line_width(AromaFont* font, const char* text);
```

Calculates the width of a string when rendered with the font.

**Parameters:**

* `font`: Font instance
* `text`: Null terminated string

**Returns:**

* Width in pixels

### Low Level Access

#### `aroma_font_get_face`

```c
void* aroma_font_get_face(AromaFont* font);
```

Provides access to the underlying font face object (for example `FT_Face` in FreeType).

**Use Cases:**

* Advanced glyph rendering
* Custom text shaping
* Integration with external rendering systems

**Returns:**

* Pointer to backend specific object
* `NULL` if unavailable

## Usage Example

```c
#include "aroma_font.h"
#include <stdio.h>

int main() {
    AromaFont* font = aroma_font_create("assets/Roboto-Regular.ttf", 16);
    if (!font) {
        printf("Failed to load font\n");
        return 1;
    }

    int width = aroma_font_get_line_width(font, "Hello, AromaUI!");
    int height = aroma_font_get_line_height(font);

    printf("Text width: %d px\n", width);
    printf("Line height: %d px\n", height);

    aroma_font_destroy(font);
    return 0;
}
```

## Notes

* Always call `aroma_font_destroy` to prevent memory leaks.
* Font size is specified in pixels, not points.
* Behavior of `aroma_font_get_face` depends on the backend implementation.
* Text measurement assumes simple layout unless the backend provides advanced shaping.

