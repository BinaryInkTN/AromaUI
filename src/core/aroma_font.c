#include "core/aroma_font.h"
#include "core/aroma_logger.h"

#ifdef ESP32
struct AromaFont {
    int size_px;
    int line_height;
    int ascender;
    int descender;
    uint8_t font_type; // 0 = GLCD (default), 1 = Free Font
};

// TFT_eSPI GLCD (default) font metrics
// Default font is 6x8 pixels for normal size
#define GLCD_CHAR_WIDTH  6
#define GLCD_CHAR_HEIGHT 8
#define GLCD_LINE_HEIGHT (GLCD_CHAR_HEIGHT + 2) // 10px with spacing
#define GLCD_ASCENDER    7  // Most characters are 7px tall (except g, j, p, q, y)
#define GLCD_DESCENDER   1  // Descender for g, j, p, q, y

AromaFont* aroma_font_create(const char* font_path, int size_px) {
    (void)font_path;

    if (size_px <= 0) return NULL;

    AromaFont* font = (AromaFont*)malloc(sizeof(AromaFont));
    if (!font) return NULL;

    font->size_px = size_px;
    font->font_type = 0; // GLCD default font
    
    // For GLCD font, size parameter is ignored - font is fixed size
    // But we can scale metrics if user requested different size
    float scale = size_px / 8.0f; // Base size is 8px
    
    font->line_height = (int)(GLCD_LINE_HEIGHT * scale);
    font->ascender = (int)(GLCD_ASCENDER * scale);
    font->descender = (int)(GLCD_DESCENDER * scale);
    
    // Ensure minimum values
    if (font->line_height < 10) font->line_height = 10;
    if (font->ascender < 7) font->ascender = 7;
    if (font->descender < 1) font->descender = 1;

    LOG_INFO("aroma_font_create: using TFT_eSPI GLCD font, size=%d, line_height=%d", 
             size_px, font->line_height);

    return font;
}

AromaFont* aroma_font_create_from_memory(
    const unsigned char* data,
    unsigned int data_len,
    int size_px
) {
    (void)data;
    (void)data_len;
    // For ESP32, memory fonts are not supported - fall back to default
    LOG_INFO("aroma_font_create_from_memory: falling back to default GLCD font");
    return aroma_font_create(NULL, size_px);
}

void aroma_font_destroy(AromaFont* font) {
    if (font) {
        free(font);
    }
}

int aroma_font_get_line_height(AromaFont* font) {
    return font ? font->line_height : GLCD_LINE_HEIGHT;
}

int aroma_font_get_ascender(AromaFont* font) {
    return font ? font->ascender : GLCD_ASCENDER;
}

int aroma_font_get_descender(AromaFont* font) {
    return font ? font->descender : GLCD_DESCENDER;
}

void* aroma_font_get_face(AromaFont* font) {
    (void)font;
    return NULL;
}

#else
/* ============================
   DESKTOP / FULL IMPLEMENTATION
   ============================ */

#include <ft2build.h>
#include FT_FREETYPE_H
#include <string.h>
#include <stdlib.h>

typedef struct {
    uint32_t codepoint;
    uint32_t texture_id;
    int width;
    int height;
    int bearing_x;
    int bearing_y;
    int advance;
} GlyphMetrics;

#define GLYPH_CACHE_SIZE 128

struct AromaFont {
    FT_Face face;
    int size_px;
    int line_height;
    int ascender;
    int descender;
    GlyphMetrics glyph_cache[GLYPH_CACHE_SIZE];
    int glyph_count;
};

static FT_Library ft_library = NULL;

static bool init_freetype(void) {
    if (ft_library) return true;

    FT_Error error = FT_Init_FreeType(&ft_library);
    if (error) {
        LOG_ERROR("Failed to initialize FreeType: %d\n", error);
        return false;
    }
    return true;
}

AromaFont* aroma_font_create(const char* font_path, int size_px) {
    if (!init_freetype() || !font_path || size_px <= 0)
        return NULL;

    AromaFont* font = malloc(sizeof(AromaFont));
    if (!font) return NULL;

    FT_Error error = FT_New_Face(ft_library, font_path, 0, &font->face);
    if (error) {
        free(font);
        return NULL;
    }

    FT_Set_Pixel_Sizes(font->face, 0, size_px);

    font->size_px = size_px;
    font->line_height = font->face->size->metrics.height >> 6;
    font->ascender = font->face->size->metrics.ascender >> 6;
    font->descender = font->face->size->metrics.descender >> 6;
    font->glyph_count = 0;

    return font;
}

AromaFont* aroma_font_create_from_memory(
    const unsigned char* data,
    unsigned int data_len,
    int size_px
) {
    if (!init_freetype() || !data || size_px <= 0)
        return NULL;

    AromaFont* font = malloc(sizeof(AromaFont));
    if (!font) return NULL;

    FT_Error error = FT_New_Memory_Face(
        ft_library, data, data_len, 0, &font->face
    );
    if (error) {
        free(font);
        return NULL;
    }

    FT_Set_Pixel_Sizes(font->face, 0, size_px);

    font->size_px = size_px;
    font->line_height = font->face->size->metrics.height >> 6;
    font->ascender = font->face->size->metrics.ascender >> 6;
    font->descender = font->face->size->metrics.descender >> 6;
    font->glyph_count = 0;

    return font;
}

void aroma_font_destroy(AromaFont* font) {
    if (!font) return;
    if (font->face) FT_Done_Face(font->face);
    free(font);
}

int aroma_font_get_line_height(AromaFont* font) {
    return font ? font->line_height : 0;
}

int aroma_font_get_ascender(AromaFont* font) {
    return font ? font->ascender : 0;
}

int aroma_font_get_descender(AromaFont* font) {
    return font ? font->descender : 0;
}

void* aroma_font_get_face(AromaFont* font) {
    return font ? font->face : NULL;
}

#endif /* ESP32 */