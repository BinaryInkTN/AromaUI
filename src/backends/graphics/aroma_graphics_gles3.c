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

#include "aroma_graphics_interface.h"
#include "utils/helpers_gles3.h"
#include "utils/aroma_gles3_text.h"
#include "aroma_abi.h"
#include "core/aroma_logger.h"
#include "core/aroma_font.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>

typedef struct
{
    GLuint textureID;
    int width, height;
    int bearingX, bearingY;
    int advance;
} Glyph;

typedef struct
{
    GLuint text_programs[256];
    GLuint shape_program;
    GLuint text_vbo;
    GLuint shape_vbo;
    GLuint text_vaos[256];
    GLuint shape_vaos[256];
    mat4x4 projection;
    GLuint text_fragment_shader;
    GLuint text_vertex_shader;

    unsigned int selected_color;
    bool is_running;
    size_t num_windows;

    GLES3TextRenderer text_renderers[256];

    Glyph glyph_cache[128];

} AromaGLES3Context;

static AromaGLES3Context ctx = {0};

int setup_shared_window_resources(void) 
{  
     glGenBuffers(1, &ctx.text_vbo);

    ctx.text_vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(ctx.text_vertex_shader, 1, &text_vertex_shader_source, NULL);
    glCompileShader(ctx.text_vertex_shader);
    if(!check_shader_compile(ctx.text_vertex_shader))
    {
        LOG_CRITICAL("Failed to compile text vertex shader");
        return 0;   
    }

    ctx.text_fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(ctx.text_fragment_shader, 1, &text_fragment_shader_source, NULL);
    glCompileShader(ctx.text_fragment_shader);
    if(!check_shader_compile(ctx.text_fragment_shader))
    {
        LOG_CRITICAL("Failed to compile text fragment shader");
        return 0;   
    }

    glGenBuffers(1, &ctx.shape_vbo);

    GLuint shape_vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(shape_vertex_shader, 1, &rectangle_vertex_shader, NULL);
    glCompileShader(shape_vertex_shader);

    if(!check_shader_compile(shape_vertex_shader))
    {
        LOG_CRITICAL("Failed to compile shape vertex shader");
        return 0;   
    }

    GLuint shape_fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(shape_fragment_shader, 1, &rectangle_fragment_shader, NULL);
    glCompileShader(shape_fragment_shader);

    if(!check_shader_compile(shape_fragment_shader))
    {
        LOG_CRITICAL("Failed to compile shape fragment shader");
        return 0;   
    }

    ctx.shape_program = glCreateProgram();
    glAttachShader(ctx.shape_program, shape_vertex_shader);
    glAttachShader(ctx.shape_program, shape_fragment_shader);
    glLinkProgram(ctx.shape_program);

    if(!check_shader_link(ctx.shape_program))
    {
        LOG_CRITICAL("Failed to link shape shader program");
        return 0;   
    }

    glDeleteShader(shape_vertex_shader);
    glDeleteShader(shape_fragment_shader);
    return 1;
}

int setup_separate_window_resources(size_t window_id) 
{  
   ctx.text_programs[window_id] = glCreateProgram();
    glAttachShader(ctx.text_programs[window_id], ctx.text_vertex_shader);
    glAttachShader(ctx.text_programs[window_id], ctx.text_fragment_shader);
    glLinkProgram(ctx.text_programs[window_id]);
    check_shader_link(ctx.text_programs[window_id]);

    if (!gles3_text_renderer_init(&ctx.text_renderers[window_id])) {
        LOG_ERROR("Failed to initialize text renderer for window %zu\n", window_id);
        return 0;
    }

    GLuint text_vao;
    glGenVertexArrays(1, &text_vao);

    glBindVertexArray(text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, ctx.text_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    ctx.text_vaos[window_id] = text_vao;
    glBindVertexArray(0);

    GLuint shape_vao;
    glGenVertexArrays(1, &shape_vao);
    glBindVertexArray(shape_vao);
    glBindBuffer(GL_ARRAY_BUFFER, ctx.shape_vbo);

    GLint position_attrib = glGetAttribLocation(ctx.shape_program, "pos");
    glEnableVertexAttribArray(position_attrib);
    glVertexAttribPointer(position_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, pos));

    GLint col_attrib = glGetAttribLocation(ctx.shape_program, "col");
    glEnableVertexAttribArray(col_attrib);
    glVertexAttribPointer(col_attrib, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, col));
    ctx.shape_vaos[window_id] = shape_vao;
    glBindVertexArray(0);
    return 1;
}

void draw_rectangle(size_t window_id, int x, int y, int width, int height)
{

}

void fill_rectangle(size_t window_id, int x, int y, int width, int height, uint32_t color, bool isRounded, float cornerRadius) 
{  
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (!platform || !platform->make_context_current || !platform->get_window_size) {
        LOG_ERROR("Platform interface missing required functions for rectangle draw");
        return;
    }

    platform->make_context_current(window_id);

    int window_width = 0;
    int window_height = 0;
    platform->get_window_size(window_id, &window_width, &window_height);
    if (window_width <= 0 || window_height <= 0) {
        LOG_WARNING("Skipping rectangle draw due to invalid window size (%d x %d)", window_width, window_height);
        return;
    }

    glViewport(0, 0, window_width, window_height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    mat4x4 projection;
    mat4x4_ortho(projection, 0.0f, (float)window_width, (float)window_height, 0.0f, -1.0f, 1.0f);

    vec3 color_rgb;
    convert_hex_to_rgb(&color_rgb, color);

    Vertex vertices[6];
    vec2 texCoords[6] = {
    {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    for (int i = 0; i < 6; i++)
    {
    vertices[i].col[0] = color_rgb[0];
    vertices[i].col[1] = color_rgb[1];
    vertices[i].col[2] = color_rgb[2];
    vertices[i].texCoord[0] = texCoords[i][0];
    vertices[i].texCoord[1] = texCoords[i][1];
    }

    float x0 = (float)x;
    float y0 = (float)y;
    float x1 = x0 + (float)width;
    float y1 = y0 + (float)height;

    vertices[0].pos[0] = x0;
    vertices[0].pos[1] = y0;
    vertices[1].pos[0] = x1;
    vertices[1].pos[1] = y0;
    vertices[2].pos[0] = x0;
    vertices[2].pos[1] = y1;

    vertices[3].pos[0] = x1;
    vertices[3].pos[1] = y0;
    vertices[4].pos[0] = x1;
    vertices[4].pos[1] = y1;
    vertices[5].pos[0] = x0;
    vertices[5].pos[1] = y1;

    glBindBuffer(GL_ARRAY_BUFFER, ctx.shape_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glUseProgram(ctx.shape_program);
    glUniformMatrix4fv(glGetUniformLocation(ctx.shape_program, "projection"), 1, GL_FALSE, (const GLfloat*)projection);
    glUniform1i(glGetUniformLocation(ctx.shape_program, "useTexture"), 0);
    glUniform2f(glGetUniformLocation(ctx.shape_program, "size"), width, height);
    glUniform1f(glGetUniformLocation(ctx.shape_program, "radius"), cornerRadius);
    glUniform1f(glGetUniformLocation(ctx.shape_program, "borderWidth"), 1.0f);
    glUniform1i(glGetUniformLocation(ctx.shape_program, "isRounded"), isRounded ? 1 : 0);
    glUniform1i(glGetUniformLocation(ctx.shape_program, "isHollow"), 0);
    glUniform1i(glGetUniformLocation(ctx.shape_program, "shapeType"), 0);

    glBindVertexArray(ctx.shape_vaos[window_id]);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, col));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, texCoord));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

static void shutdown(void) 
{  
    for (int i = 0; i < 256; i++) {
        gles3_text_renderer_cleanup(&ctx.text_renderers[i]);
        if (ctx.text_programs[i]) {
            glDeleteProgram(ctx.text_programs[i]);
        }
    }

    glDeleteProgram(ctx.shape_program);
    glDeleteShader(ctx.text_vertex_shader);
    glDeleteShader(ctx.text_fragment_shader);
    glDeleteBuffers(1, &ctx.shape_vbo);
    glDeleteBuffers(1, &ctx.text_vbo);
}

static void clear(size_t window_id, uint32_t color)
{
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (!platform || !platform->make_context_current || !platform->get_window_size) {
        LOG_ERROR("Platform interface missing required functions for clear");
        return;
    }

    platform->make_context_current(window_id);

    int window_width = 0;
    int window_height = 0;
    platform->get_window_size(window_id, &window_width, &window_height);
    if (window_width > 0 && window_height > 0) {
        glViewport(0, 0, window_width, window_height);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    vec3 color_rgb;
    convert_hex_to_rgb(&color_rgb, color);
    glClearColor(color_rgb[0], color_rgb[1], color_rgb[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void render_text(size_t window_id, AromaFont* font, const char* text, int x, int y, uint32_t color)
{
    if (!font || !text || window_id >= 256) {
        return;
    }

    GLES3TextRenderer* renderer = &ctx.text_renderers[window_id];

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->make_context_current) {
        platform->make_context_current(window_id);
    }

    gles3_text_render_text(renderer, ctx.text_programs[window_id], text, 
                          (float)x, (float)y, 1.0f, color, window_id);
}

static float measure_text(size_t window_id, AromaFont* font, const char* text)
{
    if (!font || !text || window_id >= 256) {
        return 0.0f;
    }

    GLES3TextRenderer* renderer = &ctx.text_renderers[window_id];
    if (renderer->glyph_count == 0) {
        return 0.0f;
    }

    return gles3_text_measure_text(renderer, text, 1.0f);
}

static void draw_hollow_rectangle(size_t window_id, int x, int y, int width, int height, 
                                  uint32_t color, int border_width, bool isRounded, float cornerRadius)
{
    if (border_width <= 0) return;

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (!platform || !platform->make_context_current || !platform->get_window_size) {
        return;
    }

    platform->make_context_current(window_id);

    int window_width = 0;
    int window_height = 0;
    platform->get_window_size(window_id, &window_width, &window_height);
    if (window_width <= 0 || window_height <= 0) {
        return;
    }

    glViewport(0, 0, window_width, window_height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    mat4x4 projection;
    mat4x4_ortho(projection, 0.0f, (float)window_width, (float)window_height, 0.0f, -1.0f, 1.0f);

    vec3 color_rgb;
    convert_hex_to_rgb(&color_rgb, color);

    Vertex vertices[6];
    vec2 texCoords[6] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, 
        {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}
    };

    for (int i = 0; i < 6; i++) {
        vertices[i].col[0] = color_rgb[0];
        vertices[i].col[1] = color_rgb[1];
        vertices[i].col[2] = color_rgb[2];
        vertices[i].texCoord[0] = texCoords[i][0];
        vertices[i].texCoord[1] = texCoords[i][1];
    }

    float x0 = (float)x;
    float y0 = (float)y;
    float x1 = x0 + (float)width;
    float y1 = y0 + (float)height;

    vertices[0].pos[0] = x0; vertices[0].pos[1] = y0;
    vertices[1].pos[0] = x1; vertices[1].pos[1] = y0;
    vertices[2].pos[0] = x0; vertices[2].pos[1] = y1;
    vertices[3].pos[0] = x1; vertices[3].pos[1] = y0;
    vertices[4].pos[0] = x1; vertices[4].pos[1] = y1;
    vertices[5].pos[0] = x0; vertices[5].pos[1] = y1;

    glBindBuffer(GL_ARRAY_BUFFER, ctx.shape_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glUseProgram(ctx.shape_program);
    glUniformMatrix4fv(glGetUniformLocation(ctx.shape_program, "projection"), 1, GL_FALSE, (const GLfloat*)projection);
    glUniform1i(glGetUniformLocation(ctx.shape_program, "useTexture"), 0);
    glUniform2f(glGetUniformLocation(ctx.shape_program, "size"), (float)width, (float)height);
    glUniform1f(glGetUniformLocation(ctx.shape_program, "radius"), cornerRadius);
    glUniform1f(glGetUniformLocation(ctx.shape_program, "borderWidth"), (float)border_width);
    glUniform1i(glGetUniformLocation(ctx.shape_program, "isRounded"), isRounded ? 1 : 0);
    glUniform1i(glGetUniformLocation(ctx.shape_program, "isHollow"), 1);
    glUniform1i(glGetUniformLocation(ctx.shape_program, "shapeType"), 0);

    glBindVertexArray(ctx.shape_vaos[window_id]);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, col));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));

    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

static void draw_arc(size_t window_id, int cx, int cy, int radius, float start_angle, float end_angle, 
                     uint32_t color, int thickness)
{

}

void aroma_gles3_load_font_for_window(size_t window_id, AromaFont* font)
{
    if (!font || window_id >= 256) {
        return;
    }

    GLES3TextRenderer* renderer = &ctx.text_renderers[window_id];

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->make_context_current) {
        platform->make_context_current(window_id);
    }

    FT_Face face = (FT_Face)aroma_font_get_face(font);

    if (face) {
        gles3_text_renderer_load_font(renderer, face);
        LOG_INFO("Loaded font glyphs for window %zu\n", window_id);
    }
}

AromaGraphicsInterface aroma_graphics_gles3 = {
    .setup_shared_window_resources = setup_shared_window_resources,
    .setup_separate_window_resources = setup_separate_window_resources,
    .clear = clear,
    .draw_rectangle = draw_rectangle,
    .fill_rectangle = fill_rectangle,
    .draw_hollow_rectangle = draw_hollow_rectangle,
    .draw_arc = draw_arc,
    .render_text = render_text,
    .measure_text = measure_text,
    .shutdown = shutdown
};