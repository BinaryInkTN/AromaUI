/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef ESP32
#include "aroma_gles3_text.h"
#include "helpers_gles3.h"
#include "core/aroma_logger.h"
#include "aroma_abi.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

int gles3_text_renderer_init(GLES3TextRenderer* renderer) {
    if (!renderer) {
        return 0;
    }

    memset(renderer, 0, sizeof(GLES3TextRenderer));

    glGenVertexArrays(1, &renderer->vao);
    glGenBuffers(1, &renderer->vbo);

    glBindVertexArray(renderer->vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return 1;
}

void gles3_text_renderer_load_font(GLES3TextRenderer* renderer, FT_Face face) {
    if (!renderer || !face) {
        return;
    }

    renderer->face = face; // Store face
    renderer->font_height = face->size->metrics.height >> 6;
    renderer->glyph_count = 0;

    // Preload ASCII 32-126 for performance
    for (uint32_t c = 32; c < 127; c++) {
        FT_Error error = FT_Load_Char(face, c, FT_LOAD_RENDER);
        if (error) continue;

        FT_GlyphSlot g = face->glyph;
        if (!g) continue;

        if (renderer->glyph_count >= MAX_GLYPHS) break;

        GLES3Glyph glyph = {
            .codepoint = c,
            .texture_id = 0,
            .width = g->bitmap.width,
            .height = g->bitmap.rows,
            .bearing_x = g->bitmap_left,
            .bearing_y = g->bitmap_top,
            .advance = (int)(g->advance.x >> 6)
        };

        if (g->bitmap.width > 0 && g->bitmap.rows > 0 && g->bitmap.buffer) {
            GLuint texture;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, g->bitmap.width, g->bitmap.rows, 0,
                         GL_RED, GL_UNSIGNED_BYTE, g->bitmap.buffer);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);
            glyph.texture_id = texture;
        }

        renderer->glyphs[renderer->glyph_count++] = glyph;
    }

    LOG_INFO("Loaded initial glyphs: %d\n", renderer->glyph_count);
}

// Helper: Get or load glyph
static GLES3Glyph* __get_glyph(GLES3TextRenderer* renderer, uint32_t codepoint) {
    // Search cache
    for (int i = 0; i < renderer->glyph_count; i++) {
        if (renderer->glyphs[i].codepoint == codepoint) {
            return &renderer->glyphs[i];
        }
    }

    // Not found, load it
    if (renderer->glyph_count >= MAX_GLYPHS) {
        // Cache full - simple strategy: do not load (or implement eviction)
        // For icons, this might be an issue if we have > 512 distinct chars
        return NULL;
    }

    if (!renderer->face) return NULL;

    FT_Error error = FT_Load_Char(renderer->face, codepoint, FT_LOAD_RENDER);
    if (error) return NULL;

    FT_GlyphSlot g = renderer->face->glyph;
    if (!g) return NULL;

    GLES3Glyph glyph = {
        .codepoint = codepoint,
        .texture_id = 0,
        .width = g->bitmap.width,
        .height = g->bitmap.rows,
        .bearing_x = g->bitmap_left,
        .bearing_y = g->bitmap_top,
        .advance = (int)(g->advance.x >> 6)
    };

    if (g->bitmap.width > 0 && g->bitmap.rows > 0 && g->bitmap.buffer) {
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, g->bitmap.width, g->bitmap.rows, 0,
                     GL_RED, GL_UNSIGNED_BYTE, g->bitmap.buffer);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        glyph.texture_id = texture;
    }

    renderer->glyphs[renderer->glyph_count] = glyph;
    return &renderer->glyphs[renderer->glyph_count++];
}

static uint32_t __utf8_next(const char** p) {
    const unsigned char* s = (const unsigned char*)*p;
    uint32_t c = *s;
    if (c == 0) return 0;

    int len = 0;
    if (c < 0x80) len = 1;
    else if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    else len = 1; // Invalid

    if (len == 1) {
        *p += 1;
        return c;
    }
    
    // Simple decoding
    uint32_t v = 0;
    if (len == 2) v = c & 0x1F;
    else if (len == 3) v = c & 0x0F;
    else if (len == 4) v = c & 0x07;

    for (int i = 1; i < len; i++) {
        v = (v << 6) | (s[i] & 0x3F);
    }
    *p += len;
    return v;
}


void gles3_text_render_text(GLES3TextRenderer* renderer, GLuint program,
                            const char* text, float x, float y, float scale,
                            uint32_t color, size_t window_id) {
    if (!renderer || !text || !program) {
        return;
    }

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->make_context_current) {
        platform->make_context_current(window_id);
    }

    int window_width, window_height;
    if (!platform || !platform->get_window_size) {
        LOG_ERROR("Platform interface missing for window size\n");
        return;
    }
    platform->get_window_size(window_id, &window_width, &window_height);

    mat4x4 projection;
    mat4x4_ortho(projection, 0.0f, (float)window_width, (float)window_height, 0.0f, -1.0f, 1.0f);

    vec3 text_color;
    convert_hex_to_rgb(&text_color, color);

    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "projection"), 1, GL_FALSE, (float*)projection);
    glUniform3f(glGetUniformLocation(program, "textColor"),
                text_color[0], text_color[1], text_color[2]);

    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(program, "text"), 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glBindVertexArray(renderer->vao);

    float current_x = x;
    int glyphs_rendered = 0;
    
    const char* p = text;
    while (*p != '\0') {
        uint32_t codepoint = __utf8_next(&p);
        if (codepoint == 0) break;

        GLES3Glyph* g = __get_glyph(renderer, codepoint);
        if (!g) continue;

        if (g->texture_id == 0) {
            current_x += g->advance * scale;
            continue;
        }

        if (g->width == 0 || g->height == 0) {
            current_x += g->advance * scale;
            continue;
        }

        float x_pos = current_x + g->bearing_x * scale;
        float y_pos = y + (renderer->font_height - g->bearing_y) * scale;


        float w = (float)g->width * scale;
        float h = (float)g->height * scale;

        float vertices[6][4] = {
            { x_pos,     y_pos,       0.0f, 1.0f },
            { x_pos,     y_pos + h,   0.0f, 0.0f },
            { x_pos + w, y_pos + h,   1.0f, 0.0f },
            { x_pos,     y_pos,       0.0f, 1.0f },
            { x_pos + w, y_pos + h,   1.0f, 0.0f },
            { x_pos + w, y_pos,       1.0f, 1.0f }
        };

        glBindTexture(GL_TEXTURE_2D, g->texture_id);
        glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glyphs_rendered++;

        current_x += (float)g->advance * scale;
    }

    glDisable(GL_BLEND);
    glBindVertexArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    (void)glyphs_rendered;
}

// Helper declaration
static uint32_t __utf8_next(const char** p);
static GLES3Glyph* __get_glyph(GLES3TextRenderer* renderer, uint32_t codepoint);

float gles3_text_measure_text(const GLES3TextRenderer* renderer, const char* text, float scale) {
    if (!renderer || !text || scale <= 0.0f) {
        return 0.0f;
    }
    
    // We cannot load glyphs in measure_text because it is const GLES3TextRenderer*
    // However, for layout purposes, we really should. 
    // But for now, let's just assume simple measurement or cast away const if really needed.
    // Casting away const is unsafe if called from thread, but this is UI thread single threaded mostly.
    
    GLES3TextRenderer* mutable_renderer = (GLES3TextRenderer*)renderer;

    float width = 0.0f;
    const char* p = text;
    while (*p != '\0') {
        uint32_t codepoint = __utf8_next(&p);
        if (codepoint == 0) break;
        
        GLES3Glyph* g = __get_glyph(mutable_renderer, codepoint);
        if (g) {
            width += (float)g->advance * scale;
        }
    }

    return width;
}

void gles3_text_renderer_cleanup(GLES3TextRenderer* renderer) {
    if (!renderer) {
        return;
    }

    for (int i = 0; i < renderer->glyph_count; i++) {
        if (renderer->glyphs[i].texture_id) {
            glDeleteTextures(1, &renderer->glyphs[i].texture_id);
        }
    }

    if (renderer->vao) {
        glDeleteVertexArrays(1, &renderer->vao);
    }

    if (renderer->vbo) {
        glDeleteBuffers(1, &renderer->vbo);
    }

    memset(renderer, 0, sizeof(GLES3TextRenderer));
}

#endif
