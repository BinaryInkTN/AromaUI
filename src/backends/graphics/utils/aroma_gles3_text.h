#ifndef ESP32

#ifndef AROMA_GLES3_TEXT_H
#define AROMA_GLES3_TEXT_H

#include <GLES3/gl3.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_GLYPHS 2048
#define ATLAS_SIZE 2048
#define ATLAS_PADDING 2
#define ATLAS_MAX_SIZE_BYTES (16 * 1024 * 1024)
#define MAX_ATLAS_COUNT 4

typedef struct {
    uint32_t codepoint;
    float tex_u0, tex_v0;
    float tex_u1, tex_v1;
    int width;
    int height;
    int bearing_x;
    int bearing_y;
    int advance;
    int atlas_index;
} GLES3Glyph;

typedef struct {
    GLuint texture_id;
    int current_x;
    int current_y;
    int row_height;
    size_t memory_used;
} GlyphAtlas;

typedef struct {
    GLES3Glyph glyphs[MAX_GLYPHS];
    int glyph_count;
    GlyphAtlas atlases[MAX_ATLAS_COUNT];
    int atlas_count;
    int atlas_width;
    int atlas_height;
    int font_height;
    FT_Face face;
    GLuint vao;
    GLuint vbo;
    int ascender;
    int descender;
    int line_height;
    bool has_kerning;
} GLES3TextRenderer;

int gles3_text_renderer_init(GLES3TextRenderer* renderer);
void gles3_text_renderer_load_font(GLES3TextRenderer* renderer, FT_Face face);
void gles3_text_render_text(GLES3TextRenderer* renderer, GLuint program,
                            const char* text, float x, float y, float scale,
                            uint32_t color, size_t window_id);
float gles3_text_measure_text(const GLES3TextRenderer* renderer, const char* text, float scale);
void gles3_text_renderer_cleanup(GLES3TextRenderer* renderer);
int gles3_text_get_atlas_count(const GLES3TextRenderer* renderer);
GLuint gles3_text_get_atlas_texture(const GLES3TextRenderer* renderer, int index);

#endif
#endif