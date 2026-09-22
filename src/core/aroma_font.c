#include "core/aroma_font.h"
#include "core/aroma_logger.h"
#include "aroma_native_utils.h"
#include <stdint.h>

#ifdef __ANDROID__
#include "aroma_android.h"
#endif

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

/* Single-threaded target: no locking needed. */
void aroma_font_lock(void) {}
void aroma_font_unlock(void) {}

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
#include <pthread.h>

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
    int size_px;
    int line_height;
    int ascender;
    int descender;
    int glyph_count;
    GlyphMetrics glyph_cache[GLYPH_CACHE_SIZE];
    FT_Face face;

};

static FT_Library ft_library = NULL;

/* Process-wide FreeType face lock. FT_Face is stateful (glyph slot,
 * charmaps) and not thread-safe: the UI thread draws/measures while
 * worker threads update labels. Every FT_Face touch in this file and in
 * the graphics text renderers goes through aroma_font_lock(). */
static pthread_mutex_t s_ft_mutex = PTHREAD_MUTEX_INITIALIZER;

void aroma_font_lock(void)
{
    pthread_mutex_lock(&s_ft_mutex);
}

void aroma_font_unlock(void)
{
    pthread_mutex_unlock(&s_ft_mutex);
}

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

    #ifdef __ANDROID__
    size_px = aroma_android_sp_to_px(size_px);
    #endif
        AromaFont* font = malloc(sizeof(AromaFont));
    if (!font) return NULL;

    char resolved_font_path[1024];
    const char* load_path = aroma_resolve_asset_path(font_path, resolved_font_path, sizeof(resolved_font_path));

    FT_Error error = FT_New_Face(ft_library, load_path, 0, &font->face);
    if (error) {
        LOG_ERROR("FT_New_Face failed for %s: %d", load_path, error);
        free(font);
        return NULL;
    }

    aroma_font_lock();
    FT_Set_Pixel_Sizes(font->face, 0, size_px);

    font->size_px = size_px;
    font->line_height = font->face->size->metrics.height >> 6;
    font->ascender = font->face->size->metrics.ascender >> 6;
    font->descender = font->face->size->metrics.descender >> 6;
    aroma_font_unlock();
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

    #ifdef __ANDROID__
    size_px = aroma_android_sp_to_px(size_px);
    #endif

    LOG_INFO("aroma_font_create_from_memory: data=%p data_len=%u size_px=%d", (void*)data, data_len, size_px);

    AromaFont* font = malloc(sizeof(AromaFont));
    if (!font) return NULL;

    FT_Error error = FT_New_Memory_Face(
        ft_library, data, data_len, 0, &font->face
    );
    if (error) {
        LOG_ERROR("FT_New_Memory_Face (memory) failed: %d", error);
        free(font);
        return NULL;
    }

    LOG_INFO("FT_New_Memory_Face succeeded: face=%p", (void*)font->face);

    aroma_font_lock();
    FT_Set_Pixel_Sizes(font->face, 0, size_px);

    font->size_px = size_px;
    font->line_height = font->face->size->metrics.height >> 6;
    font->ascender = font->face->size->metrics.ascender >> 6;
    font->descender = font->face->size->metrics.descender >> 6;
    aroma_font_unlock();
    font->glyph_count = 0;

    return font;
}

int aroma_font_get_px_size(AromaFont* font) {
    if (!font || ((uintptr_t)font & (sizeof(int) - 1)) != 0)
        return 0;
    return font->size_px;
}

void aroma_font_destroy(AromaFont* font) {
    if (!font) return;
    aroma_font_lock();
    if (font->face) FT_Done_Face(font->face);
    aroma_font_unlock();
    free(font);
}

/* Decode one UTF-8 codepoint, advancing *pp past it. Returns 0 on
 * malformed/empty input (caller skips it, matching render-side behavior
 * which drops undecodable bytes via __utf8_next). */
static uint32_t aroma_font_utf8_next(const char **pp)
{
    const unsigned char *p = (const unsigned char *)*pp;
    uint32_t cp;
    if (*p == '\0')
        return 0;
    if (*p < 0x80)
    {
        cp = *p++;
    }
    else if ((*p & 0xE0) == 0xC0)
    {
        cp = (*p++ & 0x1F) << 6;
        cp |= (*p++ & 0x3F);
    }
    else if ((*p & 0xF0) == 0xE0)
    {
        cp = (*p++ & 0x0F) << 12;
        cp |= (*p++ & 0x3F) << 6;
        cp |= (*p++ & 0x3F);
    }
    else if ((*p & 0xF8) == 0xF0)
    {
        cp = (*p++ & 0x07) << 18;
        cp |= (*p++ & 0x3F) << 12;
        cp |= (*p++ & 0x3F) << 6;
        cp |= (*p++ & 0x3F);
    }
    else
    {
        /* Malformed lead byte: skip it so we always make progress. */
        p++;
        *pp = (const char *)p;
        return 0;
    }
    *pp = (const char *)p;
    return cp;
}

int aroma_font_get_line_width(AromaFont* font, const char* text) {
    if (!font || !text || !font->face) return 0;

    aroma_font_lock();
    int width = 0;
    FT_GlyphSlot slot = font->face->glyph;

    /* Measure per Unicode codepoint, not per byte: a 3-byte icon glyph
     * (e.g. U+E5C3) previously summed three .notdef advances (~3x too
     * wide), which pushed every centered icon off-center and forced
     * callers to carry compensating shifts. Pure-ASCII text decodes to
     * the same single-byte codepoints as before, so its width is
     * unchanged. */
    const char *p = text;
    while (*p != '\0') {
        uint32_t cp = aroma_font_utf8_next(&p);
        if (cp == 0)
            continue;
        if (FT_Load_Char(font->face, cp, FT_LOAD_DEFAULT) == 0) {
            width += slot->advance.x >> 6;
        }
    }
    aroma_font_unlock();

    return width;
}

int aroma_font_get_line_height(AromaFont* font) {
    if (!font || ((uintptr_t)font & (sizeof(int) - 1)) != 0)
        return 0;
    return font->line_height;
}

int aroma_font_get_ascender(AromaFont* font) {
    if (!font || ((uintptr_t)font & (sizeof(int) - 1)) != 0)
        return 0;
    return font->ascender;
}

int aroma_font_get_descender(AromaFont* font) {
    if (!font || ((uintptr_t)font & (sizeof(int) - 1)) != 0)
        return 0;
    return font->descender;
}

void* aroma_font_get_face(AromaFont* font) {
    if (!font || ((uintptr_t)font & (sizeof(int) - 1)) != 0)
        return NULL;
    return font->face;
}

#endif /* ESP32 */