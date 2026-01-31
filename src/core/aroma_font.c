#include "core/aroma_font.h"
#include "core/aroma_logger.h"

#ifdef ESP32
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "aroma_font.h"
#include "core/aroma_logger.h"

#define AROMA_FONT_GLCD      0
#define AROMA_FONT_FREEFONT  1

#define GLCD_LINE_HEIGHT  10
#define GLCD_ASCENDER     7
#define GLCD_DESCENDER    1

#define FREESANS12_SIZE_PX      12
#define FREESANS12_LINE_HEIGHT  18
#define FREESANS12_ASCENDER     14
#define FREESANS12_DESCENDER    4

struct AromaFont {
    int size_px;
    int line_height;
    int ascender;
    int descender;
    uint8_t font_type;
};

AromaFont* aroma_font_create(const char* font_path, int size_px)
{
    AromaFont* font = (AromaFont*)malloc(sizeof(AromaFont));
    if (!font) return NULL;

    if (!font_path || strstr(font_path, "FreeSans12pt7b")) {
        font->font_type   = AROMA_FONT_FREEFONT;
        font->size_px     = FREESANS12_SIZE_PX;
        font->line_height = FREESANS12_LINE_HEIGHT;
        font->ascender    = FREESANS12_ASCENDER;
        font->descender   = FREESANS12_DESCENDER;
        LOG_INFO("font: FreeSans12pt7b");
        return font;
    }

    if (strstr(font_path, "GLCD")) {
        font->font_type   = AROMA_FONT_GLCD;
        font->size_px     = 12;
        font->line_height = GLCD_LINE_HEIGHT;
        font->ascender    = GLCD_ASCENDER;
        font->descender   = GLCD_DESCENDER;
        LOG_INFO("font: GLCD");
        return font;
    }

    font->font_type   = AROMA_FONT_FREEFONT;
    font->size_px     = FREESANS12_SIZE_PX;
    font->line_height = FREESANS12_LINE_HEIGHT;
    font->ascender    = FREESANS12_ASCENDER;
    font->descender   = FREESANS12_DESCENDER;
    LOG_INFO("font: fallback FreeSans12pt7b");
    return font;
}

AromaFont* aroma_font_create_from_memory(
    const unsigned char* data,
    unsigned int data_len,
    int size_px
) {
    (void)data;
    (void)data_len;
    (void)size_px;
    return aroma_font_create(NULL, 12);
}

void aroma_font_destroy(AromaFont* font)
{
    if (font) free(font);
}

int aroma_font_get_line_height(AromaFont* font)
{
    return font ? font->line_height : FREESANS12_LINE_HEIGHT;
}

int aroma_font_get_ascender(AromaFont* font)
{
    return font ? font->ascender : FREESANS12_ASCENDER;
}

int aroma_font_get_descender(AromaFont* font)
{
    return font ? font->descender : FREESANS12_DESCENDER;
}

void* aroma_font_get_face(AromaFont* font)
{
    (void)font;
    return NULL;
}

#else

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