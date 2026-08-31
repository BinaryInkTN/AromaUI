#ifndef ESP32
#include "aroma_gles3_text.h"
#include "helpers_gles3.h"
#include "core/aroma_logger.h"
#include "aroma_abi.h"
#include <string.h>
#include <stdlib.h>

#define MAX_BATCH_GLYPHS 256
#define MAX_BATCH_VERTICES (MAX_BATCH_GLYPHS * 6)

static GLuint __text_vao = 0;
static GLuint __text_vbo = 0;
static int __text_ref_count = 0;
static int __max_texture_size = 0;

static int __get_max_texture_size(void)
{
    if (__max_texture_size == 0)
    {
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &__max_texture_size);
        if (__max_texture_size <= 0)
            __max_texture_size = 1024;
    }
    return __max_texture_size;
}

static int __create_atlas(GLES3TextRenderer* renderer)
{
    if (renderer->atlas_count >= MAX_ATLAS_COUNT)
    {
        LOG_ERROR("__create_atlas: Maximum atlas count reached");
        return -1;
    }

    int max_size = __get_max_texture_size();
    int atlas_size = renderer->atlas_width;
    
    if (atlas_size > max_size)
        atlas_size = max_size;

    GlyphAtlas* atlas = &renderer->atlases[renderer->atlas_count];
    
    glGenTextures(1, &atlas->texture_id);
    if (atlas->texture_id == 0)
    {
        LOG_ERROR("__create_atlas: Failed to create atlas texture");
        return -1;
    }
    
    glBindTexture(GL_TEXTURE_2D, atlas->texture_id);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, atlas_size, atlas_size, 
                 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    
    GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        LOG_ERROR("__create_atlas: Failed to allocate atlas texture (0x%X)", error);
        glDeleteTextures(1, &atlas->texture_id);
        atlas->texture_id = 0;
        glBindTexture(GL_TEXTURE_2D, 0);
        return -1;
    }
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    atlas->current_x = ATLAS_PADDING;
    atlas->current_y = ATLAS_PADDING;
    atlas->row_height = 0;
    atlas->memory_used = 0;
    
    int idx = renderer->atlas_count;
    renderer->atlas_count++;
    
    return idx;
}

int gles3_text_renderer_init(GLES3TextRenderer* renderer) {
    if (!renderer) {
        return 0;
    }

    memset(renderer, 0, sizeof(GLES3TextRenderer));
    
    int max_size = __get_max_texture_size();
    renderer->atlas_width = ATLAS_SIZE;
    renderer->atlas_height = ATLAS_SIZE;
    
    if (renderer->atlas_width > max_size)
        renderer->atlas_width = max_size;
    if (renderer->atlas_height > max_size)
        renderer->atlas_height = max_size;
    
    if (__create_atlas(renderer) < 0)
    {
        LOG_ERROR("gles3_text_renderer_init: Failed to create initial atlas");
        return 0;
    }
    
    if (__text_ref_count == 0) {
        glGenVertexArrays(1, &__text_vao);
        glGenBuffers(1, &__text_vbo);
        
        if (__text_vao == 0 || __text_vbo == 0) {
            LOG_ERROR("gles3_text_renderer_init: Failed to create shared text VAO/VBO");
            if (__text_vao) glDeleteVertexArrays(1, &__text_vao);
            if (__text_vbo) glDeleteBuffers(1, &__text_vbo);
            __text_vao = 0;
            __text_vbo = 0;
            gles3_text_renderer_cleanup(renderer);
            return 0;
        }
        
        glBindVertexArray(__text_vao);
        glBindBuffer(GL_ARRAY_BUFFER, __text_vbo);
        glBufferData(GL_ARRAY_BUFFER, MAX_BATCH_VERTICES * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
    __text_ref_count++;
    
    renderer->vao = __text_vao;
    renderer->vbo = __text_vbo;

    return 1;
}

static int __pack_glyph_to_atlas(GLES3TextRenderer* renderer, 
                                  const unsigned char* bitmap_data,
                                  int width, int height,
                                  float* u0, float* v0, float* u1, float* v1,
                                  int* out_atlas_index) {
    if (!renderer || !bitmap_data || width <= 0 || height <= 0) {
        return 0;
    }

    if (width > renderer->atlas_width || height > renderer->atlas_height) {
        LOG_ERROR("__pack_glyph_to_atlas: Glyph too large for atlas (%dx%d)", width, height);
        return 0;
    }

    int padded_width = width + ATLAS_PADDING * 2;
    int padded_height = height + ATLAS_PADDING * 2;
    
    size_t padded_size = (size_t)padded_width * (size_t)padded_height;

    int atlas_idx = 0;
    GlyphAtlas* atlas = NULL;
    
    for (atlas_idx = 0; atlas_idx < renderer->atlas_count; atlas_idx++)
    {
        atlas = &renderer->atlases[atlas_idx];
        
        if (atlas->memory_used + padded_size > ATLAS_MAX_SIZE_BYTES)
            continue;
        
        if (atlas->current_x + padded_width > renderer->atlas_width) {
            atlas->current_x = ATLAS_PADDING;
            atlas->current_y += atlas->row_height + ATLAS_PADDING * 2;
            atlas->row_height = 0;
        }
        
        if (atlas->current_y + padded_height <= renderer->atlas_height)
            break;
        
        atlas = NULL;
    }
    
    if (!atlas)
    {
        atlas_idx = __create_atlas(renderer);
        if (atlas_idx < 0)
        {
            LOG_ERROR("__pack_glyph_to_atlas: Cannot create new atlas, all atlases full");
            return 0;
        }
        atlas = &renderer->atlases[atlas_idx];
    }

    *u0 = (float)(atlas->current_x) / renderer->atlas_width;
    *v0 = (float)(atlas->current_y) / renderer->atlas_height;
    *u1 = (float)(atlas->current_x + width) / renderer->atlas_width;
    *v1 = (float)(atlas->current_y + height) / renderer->atlas_height;

    glBindTexture(GL_TEXTURE_2D, atlas->texture_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    unsigned char* padded_bitmap = (unsigned char*)calloc(padded_width * padded_height, 1);
    if (!padded_bitmap) {
        LOG_ERROR("__pack_glyph_to_atlas: Failed to allocate padded buffer");
        glBindTexture(GL_TEXTURE_2D, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        return 0;
    }
    
    for (int y = 0; y < height; y++) {
        memcpy(padded_bitmap + (y + ATLAS_PADDING) * padded_width + ATLAS_PADDING,
               bitmap_data + y * width, width);
    }
    
    glTexSubImage2D(GL_TEXTURE_2D, 0, 
                    atlas->current_x - ATLAS_PADDING, 
                    atlas->current_y - ATLAS_PADDING,
                    padded_width, padded_height,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE, padded_bitmap);
    
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        LOG_ERROR("__pack_glyph_to_atlas: glTexSubImage2D failed (0x%X)", error);
        free(padded_bitmap);
        glBindTexture(GL_TEXTURE_2D, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        return 0;
    }
    
    free(padded_bitmap);
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);

    atlas->current_x += padded_width;
    if (height > atlas->row_height) {
        atlas->row_height = height;
    }
    
    atlas->memory_used += padded_size;
    *out_atlas_index = atlas_idx;

    return 1;
}

void gles3_text_renderer_load_font(GLES3TextRenderer* renderer, FT_Face face) {
    if (!renderer || !face) {
        LOG_ERROR("gles3_text_renderer_load_font: Invalid renderer or face");
        return;
    }

    if (!face->size) {
        LOG_ERROR("gles3_text_renderer_load_font: No size set on FT_Face");
        return;
    }

    FT_Error error = FT_Load_Char(face, 'M', FT_LOAD_RENDER);
    if (error) {
        LOG_ERROR("gles3_text_renderer_load_font: Failed to load test glyph");
        return;
    }

    renderer->face = face;
    renderer->ascender = face->size->metrics.ascender >> 6;
    renderer->descender = face->size->metrics.descender >> 6;
    renderer->line_height = face->size->metrics.height >> 6;
    renderer->font_height = renderer->ascender;
    renderer->glyph_count = 0;
    renderer->has_kerning = FT_HAS_KERNING(face);
    
    for (int i = 0; i < renderer->atlas_count; i++)
    {
        GlyphAtlas* atlas = &renderer->atlases[i];
        glBindTexture(GL_TEXTURE_2D, atlas->texture_id);
        /* Resetting an atlas means "give this texture fresh, undefined/zeroed
         * storage again" — that's glTexImage2D's job, and it's the only one
         * of the two calls that accepts NULL for the pixels argument.
         * glTexSubImage2D always requires a real source buffer; passing NULL
         * here previously triggered "INVALID_VALUE: texSubImage2D: no pixels"
         * on every font load and left every atlas's prior contents in place
         * (the call was rejected before touching the texture), so glyphs
         * packed into a "reset" atlas were silently drawn over stale data
         * from the last time this atlas slot was used. */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, renderer->atlas_width, renderer->atlas_height,
                     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
        atlas->current_x = ATLAS_PADDING;
        atlas->current_y = ATLAS_PADDING;
        atlas->row_height = 0;
        atlas->memory_used = 0;
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    for (uint32_t c = 32; c < 127 && renderer->glyph_count < MAX_GLYPHS; c++) {
        FT_Error load_error = FT_Load_Char(face, c, FT_LOAD_RENDER);
        if (load_error) continue;

        FT_GlyphSlot g = face->glyph;
        if (!g) continue;

        if (g->bitmap.width == 0 || g->bitmap.rows == 0) {
            GLES3Glyph glyph = {
                .codepoint = c,
                .tex_u0 = 0.0f, .tex_v0 = 0.0f,
                .tex_u1 = 0.0f, .tex_v1 = 0.0f,
                .width = 0,
                .height = 0,
                .bearing_x = g->bitmap_left,
                .bearing_y = g->bitmap_top,
                .advance = (int)(g->advance.x >> 6),
                .atlas_index = 0
            };
            renderer->glyphs[renderer->glyph_count++] = glyph;
            continue;
        }

        GLES3Glyph glyph = {
            .codepoint = c,
            .width = (int)g->bitmap.width,
            .height = (int)g->bitmap.rows,
            .bearing_x = g->bitmap_left,
            .bearing_y = g->bitmap_top,
            .advance = (int)(g->advance.x >> 6),
            .atlas_index = 0
        };

        if (!__pack_glyph_to_atlas(renderer, 
                                   g->bitmap.buffer,
                                   glyph.width, glyph.height,
                                   &glyph.tex_u0, &glyph.tex_v0,
                                   &glyph.tex_u1, &glyph.tex_v1,
                                   &glyph.atlas_index)) {
            continue;
        }

        renderer->glyphs[renderer->glyph_count++] = glyph;
    }

    LOG_INFO("Loaded %d glyphs into font atlas", renderer->glyph_count);
}

static GLES3Glyph* __get_glyph(GLES3TextRenderer* renderer, uint32_t codepoint) {
    if (!renderer) return NULL;

    for (int i = 0; i < renderer->glyph_count; i++) {
        if (renderer->glyphs[i].codepoint == codepoint) {
            return &renderer->glyphs[i];
        }
    }

    if (renderer->glyph_count >= MAX_GLYPHS) {
        return NULL;
    }

    if (!renderer->face) {
        return NULL;
    }

    FT_Error error = FT_Load_Char(renderer->face, codepoint, FT_LOAD_RENDER);
    if (error) {
        return NULL;
    }

    FT_GlyphSlot g = renderer->face->glyph;
    if (!g) {
        return NULL;
    }

    GLES3Glyph glyph = {
        .codepoint = codepoint,
        .width = (int)g->bitmap.width,
        .height = (int)g->bitmap.rows,
        .bearing_x = g->bitmap_left,
        .bearing_y = g->bitmap_top,
        .advance = (int)(g->advance.x >> 6),
        .atlas_index = 0
    };

    if (g->bitmap.width > 0 && g->bitmap.rows > 0) {
        if (!__pack_glyph_to_atlas(renderer, 
                                   g->bitmap.buffer,
                                   glyph.width, glyph.height,
                                   &glyph.tex_u0, &glyph.tex_v0,
                                   &glyph.tex_u1, &glyph.tex_v1,
                                   &glyph.atlas_index)) {
            glyph.width = 0;
            glyph.height = 0;
            glyph.tex_u0 = glyph.tex_v0 = glyph.tex_u1 = glyph.tex_v1 = 0.0f;
        }
    } else {
        glyph.tex_u0 = glyph.tex_v0 = glyph.tex_u1 = glyph.tex_v1 = 0.0f;
    }

    renderer->glyphs[renderer->glyph_count] = glyph;
    GLES3Glyph* result = &renderer->glyphs[renderer->glyph_count];
    renderer->glyph_count++;
    return result;
}

static int __get_kerning(GLES3TextRenderer* renderer, uint32_t left, uint32_t right)
{
    if (!renderer || !renderer->face || !renderer->has_kerning)
        return 0;
    
    FT_Vector kerning;
    FT_UInt left_index = FT_Get_Char_Index(renderer->face, left);
    FT_UInt right_index = FT_Get_Char_Index(renderer->face, right);
    
    if (left_index == 0 || right_index == 0)
        return 0;
    
    FT_Error error = FT_Get_Kerning(renderer->face, left_index, right_index, 
                                     FT_KERNING_DEFAULT, &kerning);
    if (error)
        return 0;
    
    return kerning.x >> 6;
}

static uint32_t __utf8_next(const char** p) {
    if (!p || !*p) return 0;
    
    const unsigned char* s = (const unsigned char*)*p;
    uint32_t c = *s;
    if (c == 0) return 0;

    int len = 0;
    if (c < 0x80) len = 1;
    else if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    else { *p += 1; return 0xFFFD; }

    if (len == 1) {
        *p += 1;
        return c;
    }

    for (int i = 1; i < len; i++) {
        if (s[i] == 0 || (s[i] & 0xC0) != 0x80) {
            *p += 1;
            return 0xFFFD;
        }
    }

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
    if (!renderer || !text || !program || scale <= 0.0f) {
        return;
    }

    if (renderer->vao == 0 || renderer->vbo == 0) {
        LOG_ERROR("gles3_text_render_text: Invalid VAO or VBO");
        return;
    }

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (!platform) {
        LOG_ERROR("gles3_text_render_text: Platform interface not available");
        return;
    }

    if (platform->make_context_current) {
        platform->make_context_current(window_id);
    }

    int window_width, window_height;
    if (!platform->get_window_size) {
        LOG_ERROR("Platform interface missing for window size\n");
        return;
    }
    platform->get_window_size(window_id, &window_width, &window_height);
    
    if (window_width <= 0 || window_height <= 0) {
        LOG_ERROR("gles3_text_render_text: Invalid window dimensions");
        return;
    }

    GLboolean blend_enabled = glIsEnabled(GL_BLEND);
    GLint blend_src, blend_dst;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst);
    GLint unpack_alignment;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
    GLint active_texture;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    GLint bound_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_texture);
    GLint current_program;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    GLint array_buffer;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
    GLint vertex_array;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertex_array);

    mat4x4 projection;
    mat4x4_ortho(projection, 0.0f, (float)window_width, (float)window_height, 0.0f, -1.0f, 1.0f);

    vec3 text_color;
    convert_hex_to_rgb(&text_color, color);

    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "projection"), 1, GL_FALSE, (float*)projection);
    glUniform3f(glGetUniformLocation(program, "textColor"),
                text_color[0], text_color[1], text_color[2]);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glBindVertexArray(renderer->vao);

    float* batch_vertices = (float*)malloc(MAX_BATCH_VERTICES * 4 * sizeof(float));
    if (!batch_vertices) {
        LOG_ERROR("gles3_text_render_text: Failed to allocate batch buffer");
        glBindVertexArray(0);
        return;
    }
    
    int batch_count = 0;
    float current_x = x;
    int current_atlas = -1;
    const char* p = text;
    uint32_t prev_codepoint = 0;

    while (*p != '\0') {
        uint32_t codepoint = __utf8_next(&p);
        if (codepoint == 0) break;

        if (prev_codepoint != 0)
        {
            int kerning = __get_kerning(renderer, prev_codepoint, codepoint);
            current_x += kerning * scale;
        }
        prev_codepoint = codepoint;

        GLES3Glyph* g = __get_glyph(renderer, codepoint);
        if (!g) continue;

        if (g->width == 0 || g->height == 0) {
            current_x += g->advance * scale;
            continue;
        }

        if (g->atlas_index != current_atlas || batch_count >= MAX_BATCH_GLYPHS)
        {
            if (batch_count > 0)
            {
                glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
                glBufferSubData(GL_ARRAY_BUFFER, 0, batch_count * 6 * 4 * sizeof(float), batch_vertices);
                glDrawArrays(GL_TRIANGLES, 0, batch_count * 6);
                batch_count = 0;
            }
            
            if (g->atlas_index != current_atlas)
            {
                current_atlas = g->atlas_index;
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, renderer->atlases[current_atlas].texture_id);
                glUniform1i(glGetUniformLocation(program, "text"), 0);
            }
        }

        float x_pos = current_x + g->bearing_x * scale;
        float y_pos = y + (renderer->font_height - g->bearing_y) * scale;

        float w = (float)g->width * scale;
        float h = (float)g->height * scale;

        int base = batch_count * 6 * 4;
        
        batch_vertices[base + 0] = x_pos;
        batch_vertices[base + 1] = y_pos;
        batch_vertices[base + 2] = g->tex_u0;
        batch_vertices[base + 3] = g->tex_v0;
        
        batch_vertices[base + 4] = x_pos;
        batch_vertices[base + 5] = y_pos + h;
        batch_vertices[base + 6] = g->tex_u0;
        batch_vertices[base + 7] = g->tex_v1;
        
        batch_vertices[base + 8] = x_pos + w;
        batch_vertices[base + 9] = y_pos + h;
        batch_vertices[base + 10] = g->tex_u1;
        batch_vertices[base + 11] = g->tex_v1;
        
        batch_vertices[base + 12] = x_pos;
        batch_vertices[base + 13] = y_pos;
        batch_vertices[base + 14] = g->tex_u0;
        batch_vertices[base + 15] = g->tex_v0;
        
        batch_vertices[base + 16] = x_pos + w;
        batch_vertices[base + 17] = y_pos + h;
        batch_vertices[base + 18] = g->tex_u1;
        batch_vertices[base + 19] = g->tex_v1;
        
        batch_vertices[base + 20] = x_pos + w;
        batch_vertices[base + 21] = y_pos;
        batch_vertices[base + 22] = g->tex_u1;
        batch_vertices[base + 23] = g->tex_v0;
        
        batch_count++;
        current_x += (float)g->advance * scale;
    }

    if (batch_count > 0)
    {
        glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, batch_count * 6 * 4 * sizeof(float), batch_vertices);
        glDrawArrays(GL_TRIANGLES, 0, batch_count * 6);
    }

    free(batch_vertices);

    if (!blend_enabled) glDisable(GL_BLEND);
    glBlendFunc(blend_src, blend_dst);
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
    glActiveTexture(active_texture);
    glBindTexture(GL_TEXTURE_2D, bound_texture);
    glUseProgram(current_program);
    glBindBuffer(GL_ARRAY_BUFFER, array_buffer);
    glBindVertexArray(vertex_array);
}

float gles3_text_measure_text(const GLES3TextRenderer* renderer, const char* text, float scale) {
    if (!renderer || !text || scale <= 0.0f) {
        return 0.0f;
    }

    GLES3TextRenderer* mutable_renderer = (GLES3TextRenderer*)renderer;

    float width = 0.0f;
    uint32_t prev_codepoint = 0;
    const char* p = text;
    while (*p != '\0') {
        uint32_t codepoint = __utf8_next(&p);
        if (codepoint == 0) break;

        if (prev_codepoint != 0)
        {
            int kerning = __get_kerning(mutable_renderer, prev_codepoint, codepoint);
            width += kerning * scale;
        }
        prev_codepoint = codepoint;

        GLES3Glyph* g = __get_glyph(mutable_renderer, codepoint);
        if (g) {
            width += (float)g->advance * scale;
        }
    }

    return width;
}

int gles3_text_get_atlas_count(const GLES3TextRenderer* renderer)
{
    if (!renderer) return 0;
    return renderer->atlas_count;
}

GLuint gles3_text_get_atlas_texture(const GLES3TextRenderer* renderer, int index)
{
    if (!renderer || index < 0 || index >= renderer->atlas_count) return 0;
    return renderer->atlases[index].texture_id;
}

void gles3_text_renderer_cleanup(GLES3TextRenderer* renderer) {
    if (!renderer) {
        return;
    }

    for (int i = 0; i < renderer->atlas_count; i++)
    {
        if (renderer->atlases[i].texture_id)
        {
            glDeleteTextures(1, &renderer->atlases[i].texture_id);
            renderer->atlases[i].texture_id = 0;
        }
    }

    renderer->glyph_count = 0;
    renderer->face = NULL;
    renderer->vao = 0;
    renderer->vbo = 0;
    renderer->atlas_count = 0;
    
    __text_ref_count--;
    if (__text_ref_count == 0) {
        if (__text_vao) {
            glDeleteVertexArrays(1, &__text_vao);
            __text_vao = 0;
        }
        if (__text_vbo) {
            glDeleteBuffers(1, &__text_vbo);
            __text_vbo = 0;
        }
    }
}

#endif