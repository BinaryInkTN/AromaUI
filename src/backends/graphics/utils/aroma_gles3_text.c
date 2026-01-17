#include "aroma_gles3_text.h"
#include "helpers_gles3.h"
#include "aroma_logger.h"
#include "aroma_abi.h"
#include <string.h>
#include <stdlib.h>

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
    
    renderer->font_height = face->size->metrics.height >> 6;
    
    // Load ASCII printable characters (32-126)
    for (unsigned char c = 32; c < 127; c++) {
        int glyph_index = c - 32;  // Store at 0-94 instead of 32-126
        if (glyph_index >= 95) break;  // Safety check (95 printable chars)
        
        FT_Error error = FT_Load_Char(face, c, FT_LOAD_RENDER);
        if (error) {
            LOG_ERROR("Failed to load glyph %c (0x%02x): %d\n", c, c, error);
            renderer->glyphs[glyph_index].texture_id = 0;
            continue;
        }
        
        FT_GlyphSlot g = face->glyph;
        if (!g) {
            LOG_WARNING("Glyph %c slot is NULL\n", c);
            renderer->glyphs[glyph_index].texture_id = 0;
            continue;
        }
        
        // Store glyph metrics (don't create texture yet to avoid GL context issues)
        GLES3Glyph glyph = {
            .texture_id = 0,  // Will be created on first render
            .width = g->bitmap.width,
            .height = g->bitmap.rows,
            .bearing_x = g->bitmap_left,
            .bearing_y = g->bitmap_top,
            .advance = (int)(g->advance.x >> 6)
        };
        
        // Create texture now with proper error handling
        if (g->bitmap.width > 0 && g->bitmap.rows > 0 && g->bitmap.buffer) {
            GLuint texture;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            
            // Set pixel alignment to 1-byte for proper bitmap reading
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, g->bitmap.width, g->bitmap.rows, 0,
                         GL_RED, GL_UNSIGNED_BYTE, g->bitmap.buffer);
            
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glBindTexture(GL_TEXTURE_2D, 0);
            
            glyph.texture_id = texture;
        }
        
        renderer->glyphs[glyph_index] = glyph;
    }
    
    renderer->glyph_count = 95;  // ASCII 32-126
    LOG_INFO("Loaded glyphs: %d\n", renderer->glyph_count);
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
    
    // Get window size for projection matrix
    int window_width, window_height;
    if (!platform || !platform->get_window_size) {
        LOG_ERROR("Platform interface missing for window size\n");
        return;
    }
    platform->get_window_size(window_id, &window_width, &window_height);
    
    // Create orthographic projection matrix (pixel coordinates)
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
    
    // Enable blending for text rendering with alpha
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    glBindVertexArray(renderer->vao);
    
    float current_x = x;
    int glyphs_rendered = 0;
    
    for (const char* c = text; *c != '\0'; c++) {
        unsigned char ch = (unsigned char)*c;
        
        // Only support ASCII 32-126
        if (ch < 32 || ch >= 127) {
            continue;
        }
        
        int glyph_index = ch - 32;
        GLES3Glyph* g = &renderer->glyphs[glyph_index];
        
        // Skip glyphs without textures or empty glyphs
        if (g->texture_id == 0) {
            // Still advance position even for missing glyphs
            current_x += g->advance * scale;
            continue;
        }
        
        if (g->width == 0 || g->height == 0) {
            current_x += g->advance * scale;
            continue;
        }
        
        // Calculate glyph position in screen coordinates
        // bearing_x: offset from cursor to left of glyph
        // bearing_y: offset from baseline to top of glyph (FreeType: positive = above baseline)
        // The baseline is at y, so:
        // - top of glyph = y - bearing_y
        // - bottom of glyph = y - bearing_y + height
        float x_pos = current_x + (float)g->bearing_x * scale;
        float y_pos = y - (float)g->bearing_y * scale;
        
        float w = (float)g->width * scale;
        float h = (float)g->height * scale;
        
        // Vertices in screen space with correct texture coordinates
        // Format: x, y, tex_u, tex_v
        float vertices[6][4] = {
            // First triangle (bottom-left, top-left, top-right)
            { x_pos,     y_pos,       0.0f, 1.0f },  // bottom-left
            { x_pos,     y_pos + h,   0.0f, 0.0f },  // top-left
            { x_pos + w, y_pos + h,   1.0f, 0.0f },  // top-right
            
            // Second triangle (bottom-left, top-right, bottom-right)
            { x_pos,     y_pos,       0.0f, 1.0f },  // bottom-left
            { x_pos + w, y_pos + h,   1.0f, 0.0f },  // top-right
            { x_pos + w, y_pos,       1.0f, 1.0f }   // bottom-right
        };
        
        glBindTexture(GL_TEXTURE_2D, g->texture_id);
        glBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glyphs_rendered++;
        
        current_x += (float)g->advance * scale;
    }
    
    // Restore GL state
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    if (glyphs_rendered > 0) {
        LOG_INFO("Rendered %d glyphs for text: %s\n", glyphs_rendered, text);
    }
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
