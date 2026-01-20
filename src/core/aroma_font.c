
#include "core/aroma_font.h"
#include "core/aroma_logger.h"
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
    if (ft_library != NULL) {
        return true;
    }

    FT_Error error = FT_Init_FreeType(&ft_library);
    if (error) {
        LOG_ERROR("Failed to initialize FreeType: %d\n", error);
        return false;
    }

    return true;
}

AromaFont* aroma_font_create(const char* font_path, int size_px) {
    if (!init_freetype()) {
        return NULL;
    }

    if (!font_path || size_px <= 0) {
        LOG_ERROR("Invalid font parameters: path=%s, size=%d\n", font_path, size_px);
        return NULL;
    }

    AromaFont* font = malloc(sizeof(AromaFont));
    if (!font) {
        LOG_ERROR("Failed to allocate font structure\n");
        return NULL;
    }

    FT_Error error = FT_New_Face(ft_library, font_path, 0, &font->face);
    if (error) {
        LOG_ERROR("Failed to load font: %s (error: %d)\n", font_path, error);
        free(font);
        return NULL;
    }

    FT_Set_Pixel_Sizes(font->face, 0, size_px);

    font->size_px = size_px;
    font->line_height = font->face->size->metrics.height >> 6;
    font->ascender = font->face->size->metrics.ascender >> 6;
    font->descender = font->face->size->metrics.descender >> 6;
    font->glyph_count = 0;

    LOG_INFO("Font loaded: %s at %dpx (line_height=%d, ascender=%d, descender=%d)\n",
             font_path, size_px, font->line_height, font->ascender, font->descender);

    return font;
}

AromaFont* aroma_font_create_from_memory(const unsigned char* data, unsigned int data_len, int size_px) {
    if (!init_freetype()) {
        return NULL;
    }
    if (!data || data_len == 0 || size_px <= 0) {
        LOG_ERROR("Invalid font memory parameters\n");
        return NULL;
    }
    AromaFont* font = malloc(sizeof(AromaFont));
    if (!font) {
        LOG_ERROR("Failed to allocate font structure\n");
        return NULL;
    }
    FT_Error error = FT_New_Memory_Face(ft_library, data, data_len, 0, &font->face);
    if (error) {
        LOG_ERROR("Failed to load font from memory (error: %d)\n", error);
        free(font);
        return NULL;
    }
    FT_Set_Pixel_Sizes(font->face, 0, size_px);
    font->size_px = size_px;
    font->line_height = font->face->size->metrics.height >> 6;
    font->ascender = font->face->size->metrics.ascender >> 6;
    font->descender = font->face->size->metrics.descender >> 6;
    font->glyph_count = 0;
    LOG_INFO("Font loaded from memory at %dpx (line_height=%d, ascender=%d, descender=%d)\n",
             size_px, font->line_height, font->ascender, font->descender);
    return font;
}
void aroma_font_destroy(AromaFont* font) {
    if (!font) {
        return;
    }

    if (font->face) {
        FT_Done_Face(font->face);
    }

    free(font);
}

int aroma_font_get_line_height(AromaFont* font) {
    if (!font) return 0;
    return font->line_height;
}

int aroma_font_get_ascender(AromaFont* font) {
    if (!font) return 0;
    return font->ascender;
}

int aroma_font_get_descender(AromaFont* font) {
    if (!font) return 0;
    return font->descender;
}

void* aroma_font_get_face(AromaFont* font) {
    if (!font) return NULL;
    return font->face;
}
