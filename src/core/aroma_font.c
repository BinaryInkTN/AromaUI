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
#include <math.h>
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
    int size_px;      /* nominal size requested by the caller (design px) */
    int raster_px;    /* actual FreeType pixel size (size_px * raster scale) */
    struct AromaFont *next; /* next in the global live-font registry */
    int line_height;
    int ascender;
    int descender;
    int glyph_count;
    GlyphMetrics glyph_cache[GLYPH_CACHE_SIZE];
    FT_Face face;

};

static FT_Library ft_library = NULL;

/* Global raster scale: device px per design px for every live font.
 *
 * The web sandbox lays the UI out in fixed design units (the markup
 * Window size) but renders into a backing store sized to the actual
 * displayed CSS box times devicePixelRatio. To keep glyphs crisp the
 * faces are re-rasterized at round(design_px * scale); the metric
 * getters below convert back to design units so layout math (which
 * works in design px) is unchanged.
 */
static float s_raster_scale = 1.0f;
static unsigned s_raster_generation = 1;
static AromaFont *s_live_fonts = NULL;

static pthread_mutex_t s_ft_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Recompute design-unit vertical metrics from the face's current size. */
static void font_refresh_metrics(AromaFont *font)
{
    float inv = (font->raster_px > 0)
        ? (float)font->size_px / (float)font->raster_px : 1.0f;
    FT_Size_Metrics m = font->face->size->metrics;
    font->line_height = (int)lroundf((m.height / 64.0f) * inv);
    font->ascender    = (int)lroundf((m.ascender / 64.0f) * inv);
    font->descender   = (int)lroundf((m.descender / 64.0f) * inv);
}

/* Common tail of the create paths; caller holds no lock. */
static void font_register(AromaFont *font, int size_px)
{
    int raster = (int)lroundf((float)size_px * s_raster_scale);
    if (raster < 1) raster = 1;

    aroma_font_lock();
    FT_Set_Pixel_Sizes(font->face, 0, raster);
    font->size_px = size_px;
    font->raster_px = raster;
    font_refresh_metrics(font);
    font->next = s_live_fonts;
    s_live_fonts = font;
    aroma_font_unlock();
    font->glyph_count = 0;
}

void aroma_font_set_raster_scale(float scale)
{
    if (scale <= 0.0f)
        return;
    if (fabsf(scale - s_raster_scale) < 0.0005f)
        return; /* nothing to do (also avoids a generation bump) */
    s_raster_scale = scale;

    bool any_changed = false;
    aroma_font_lock();
    for (AromaFont *f = s_live_fonts; f; f = f->next) {
        int raster = (int)lroundf((float)f->size_px * scale);
        if (raster < 1) raster = 1;
        if (raster == f->raster_px)
            continue;
        FT_Set_Pixel_Sizes(f->face, 0, raster);
        f->raster_px = raster;
        font_refresh_metrics(f);
        f->glyph_count = 0; /* per-glyph metric cache is at the old size */
        any_changed = true;
    }
    aroma_font_unlock();

    /* Bump the generation so cached glyph atlases get rebuilt. */
    if (any_changed)
        s_raster_generation++;
}

unsigned aroma_font_raster_generation(void)
{
    return s_raster_generation;
}

float aroma_font_raster_scale(void)
{
    return s_raster_scale;
}

float aroma_font_design_scale(AromaFont *font)
{
    if (!font || font->raster_px <= 0)
        return 1.0f;
    return (float)font->size_px / (float)font->raster_px;
}

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

    font_register(font, size_px);

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

    font_register(font, size_px);

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
    AromaFont **pp = &s_live_fonts;
    while (*pp) {
        if (*pp == font) {
            *pp = font->next;
            break;
        }
        pp = &(*pp)->next;
    }
    if (font->face) FT_Done_Face(font->face);
    aroma_font_unlock();
    free(font);
}


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
    float width = 0;
    FT_GlyphSlot slot = font->face->glyph;


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

    /* Advances are in raster px; convert back to design units. */
    if (font->raster_px > 0 && font->raster_px != font->size_px)
        width *= (float)font->size_px / (float)font->raster_px;

    return (int)lroundf(width);
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

#endif