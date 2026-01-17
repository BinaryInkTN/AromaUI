#ifndef AROMA_GLES3_TEXT_H
#define AROMA_GLES3_TEXT_H

#include <GLES3/gl3.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdint.h>

typedef struct {
    uint32_t texture_id;
    int width;
    int height;
    int bearing_x;
    int bearing_y;
    int advance;
} GLES3Glyph;

typedef struct {
    GLES3Glyph glyphs[128];
    int glyph_count;
    GLuint vao;
    GLuint vbo;
    int font_height;
} GLES3TextRenderer;

int gles3_text_renderer_init(GLES3TextRenderer* renderer);
void gles3_text_renderer_load_font(GLES3TextRenderer* renderer, FT_Face face);
void gles3_text_render_text(GLES3TextRenderer* renderer, GLuint program, 
                            const char* text, float x, float y, float scale, 
                            uint32_t color, size_t window_id);
void gles3_text_renderer_cleanup(GLES3TextRenderer* renderer);

#endif
