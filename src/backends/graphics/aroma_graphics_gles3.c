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
#define NANOSVG_ALL_COLOR_KEYWORDS	
#define NANOSVG_IMPLEMENTATION		
#include "utils/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "utils/nanosvgrast.h"
#define STB_IMAGE_IMPLEMENTATION
#include "utils/stb_image.h"

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


static int is_stb_supported_image_format(const char *path)
{
    if (!path)
        return 0;

    const char *ext = strrchr(path, '.');
    if (!ext)
        return 0;

    ext++;

    if (strcasecmp(ext, "png") == 0 ||
        strcasecmp(ext, "jpg") == 0 ||
        strcasecmp(ext, "jpeg") == 0)
    {
        return 1;
    }

    return 0;
}

void unload_image(unsigned int texture_id)
{
    glDeleteTextures(1, &texture_id);
}

unsigned int load_image(const char* image_path)
{
    if (!image_path) {
        LOG_ERROR("Null image path provided");
        return 0;
    }
    
    LOG_INFO("Attempting to load image: %s", image_path);
    
    FILE *file = fopen(image_path, "rb");
    if (!file) {
        LOG_ERROR("Image file not found or inaccessible: %s", image_path);
        return 0;
    }
    fclose(file);


    unsigned int texture = 0;
    glGenTextures(1, &texture);
    
    if (texture == 0) {
        GLenum error = glGetError();
        LOG_ERROR("glGenTextures failed! Could not generate texture ID. OpenGL error: 0x%X", error);
        return 0;
    }
    
    LOG_INFO("Generated OpenGL texture ID: %u for %s", texture, image_path);

    
    glBindTexture(GL_TEXTURE_2D, texture);
    if (glGetError() != GL_NO_ERROR) {
        LOG_ERROR("Failed to bind texture %u", texture);
        glDeleteTextures(1, &texture);
        return 0;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    unsigned char *data = NULL;
    int img_width = 0, img_height = 0, nrChannels = 0;
    int success = 0;

    const char *ext = strrchr(image_path, '.');
    
    GLenum glError = glGetError();
    if (glError != GL_NO_ERROR) {
        LOG_ERROR("OpenGL error after binding texture %u: 0x%X", texture, glError);
        glDeleteTextures(1, &texture);
        return 0;
    }

    if (ext && (strcasecmp(ext, ".svg") == 0)) {
        LOG_INFO("Loading SVG file: %s", image_path);
        NSVGimage *image = NULL;
        NSVGrasterizer *rast = NULL;
        
        image = nsvgParseFromFile(image_path, "px", 96.0f);
        if (!image) {
            LOG_ERROR("NanoSVG failed to parse SVG file: %s", image_path);
            glDeleteTextures(1, &texture);
            return 0;
        }
        
        rast = nsvgCreateRasterizer();
        if (!rast) {
            LOG_ERROR("Failed to create NanoSVG rasterizer for: %s", image_path);
            nsvgDelete(image);
            glDeleteTextures(1, &texture);
            return 0;
        }
        
        img_width = (int)image->width;
        img_height = (int)image->height;
        nrChannels = 4;
        
        LOG_INFO("SVG dimensions: %dx%d", img_width, img_height);
        
        size_t data_size = img_width * img_height * 4;
        data = (unsigned char*)malloc(data_size);
        if (!data) {
            LOG_ERROR("Failed to allocate memory for SVG rasterization: %s (needed %zu bytes)", 
                     image_path, data_size);
            nsvgDeleteRasterizer(rast);
            nsvgDelete(image);
            glDeleteTextures(1, &texture);
            return 0;
        }
        
        memset(data, 0, data_size);
        nsvgRasterize(rast, image, 0, 0, 1, data, img_width, img_height, img_width * 4);
        success = 1;
        
        nsvgDeleteRasterizer(rast);
        nsvgDelete(image);
        LOG_INFO("Successfully rasterized SVG: %s", image_path);
    }
    else if (is_stb_supported_image_format(image_path)) {
        LOG_INFO("Loading raster image: %s", image_path);
        stbi_set_flip_vertically_on_load(1);
        data = stbi_load(image_path, &img_width, &img_height, &nrChannels, 0);
        if (data) {
            success = 1;
            LOG_INFO("STB loaded image: %s (%dx%d, %d channels)",
                     image_path, img_width, img_height, nrChannels);
        } else {
            LOG_ERROR("STB failed to load image: %s", image_path);
            const char* reason = stbi_failure_reason();
            if (reason) {
                LOG_ERROR("STB failure reason: %s", reason);
            }
        }
    }
    else {
        LOG_ERROR("Unsupported image format: %s", image_path);
        glDeleteTextures(1, &texture);
        return 0;
    }

    if (!success || !data) {
        LOG_ERROR("Failed to load image data: %s", image_path);
        glDeleteTextures(1, &texture);
        
        if (ext && strcasecmp(ext, ".svg") == 0) {
            free(data);
        } else {
            stbi_image_free(data);
        }
        return 0;
    }

    if (img_width <= 0 || img_height <= 0) {
        LOG_ERROR("Invalid image dimensions: %s (%dx%d)", image_path, img_width, img_height);
        glDeleteTextures(1, &texture);
        
        if (ext && strcasecmp(ext, ".svg") == 0) {
            free(data);
        } else {
            stbi_image_free(data);
        }
        return 0;
    }

    if (!data) {
        LOG_ERROR("Image data is NULL after loading: %s", image_path);
        glDeleteTextures(1, &texture);
        return 0;
    }

    size_t total_pixels = img_width * img_height;
    size_t total_bytes = total_pixels * nrChannels;
    unsigned int zero_count = 0;
    unsigned int max_value = 0;
    
    for (size_t i = 0; i < total_bytes && i < 100; i++) {
        if (data[i] == 0) zero_count++;
        if (data[i] > max_value) max_value = data[i];
    }
    
    LOG_INFO("Image data: %zu bytes, first 100 bytes - zeros: %u, max value: %u", 
             total_bytes, zero_count, max_value);

    GLenum format;
    switch (nrChannels) {
        case 1: format = GL_RED; break;
        case 2: format = GL_RG; break;
        case 3: format = GL_RGB; break;
        case 4: format = GL_RGBA; break;
        default:
            LOG_ERROR("Unsupported number of channels: %d for %s", nrChannels, image_path);
            glDeleteTextures(1, &texture);
            if (ext && strcasecmp(ext, ".svg") == 0) {
                free(data);
            } else {
                stbi_image_free(data);
            }
            return 0;
    }

    LOG_INFO("Uploading texture data to GPU (format: 0x%X, %dx%d)", 
             format, img_width, img_height);
    
    glTexImage2D(GL_TEXTURE_2D, 0, format, img_width, img_height, 0, 
                 format, GL_UNSIGNED_BYTE, data);
    
    glError = glGetError();
    if (glError != GL_NO_ERROR) {
        LOG_ERROR("OpenGL error during glTexImage2D for texture %u: 0x%X", texture, glError);
        glDeleteTextures(1, &texture);
        
        if (ext && strcasecmp(ext, ".svg") == 0) {
            free(data);
        } else {
            stbi_image_free(data);
        }
        return 0;
    }
    
    glGenerateMipmap(GL_TEXTURE_2D);
    
    glError = glGetError();
    if (glError != GL_NO_ERROR) {
        LOG_WARNING("OpenGL warning during glGenerateMipmap for texture %u: 0x%X", texture, glError);
    }

    if (ext && strcasecmp(ext, ".svg") == 0) {
        free(data);
    } else {
        stbi_image_free(data);
    }

    if (!glIsTexture(texture)) {
        LOG_ERROR("Texture validation failed! ID %u is not a valid texture after loading", texture);
        glDeleteTextures(1, &texture);
        return 0;
    }

    LOG_INFO("Texture %u successfully created: %s (%dx%d)", 
             texture, image_path, img_width, img_height);

    return texture;
}

unsigned int load_image_from_memory(unsigned char* data, size_t binary_length)
{
    if (!data || binary_length == 0) {
        LOG_ERROR("Invalid data or length for memory image loading");
        return 0;
    }
    
    unsigned int texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_set_flip_vertically_on_load(1);
    int width, height, channels;
    unsigned char *img_data = stbi_load_from_memory(data, (int)binary_length, 
                                                    &width, &height, &channels, 0);
    if (!img_data) {
        LOG_ERROR("Failed to load image from memory");
        glDeleteTextures(1, &texture);
        return 0;
    }

    GLenum format;
    switch (channels) {
        case 1: format = GL_RED; break;
        case 2: format = GL_RG; break;
        case 3: format = GL_RGB; break;
        case 4: format = GL_RGBA; break;
        default:
            LOG_ERROR("Unsupported number of channels in memory image: %d", channels);
            stbi_image_free(img_data);
            glDeleteTextures(1, &texture);
            return 0;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, 
                 format, GL_UNSIGNED_BYTE, img_data);
    glGenerateMipmap(GL_TEXTURE_2D);
    
    stbi_image_free(img_data);
    
    LOG_INFO("Successfully loaded texture from memory (ID: %u, %dx%d, %d channels)", 
             texture, width, height, channels);
    return texture;
}

void draw_image(size_t window_id, int x, int y, int width, int height, unsigned int texture_id)
{
    LOG_INFO("draw_image called: window=%zu, pos=(%d,%d), size=(%dx%d), texture=%u", 
             window_id, x, y, width, height, texture_id);
    
    if (texture_id == 0) {
        LOG_ERROR("Cannot draw texture ID 0 (OpenGL reserved)");
        return;
    }

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (!platform || !platform->make_context_current || !platform->get_window_size) {
        LOG_ERROR("Platform interface missing required functions");
        return;
    }

    platform->make_context_current(window_id);
    
    if (!glIsTexture(texture_id)) {
        LOG_ERROR("Texture ID %u is not a valid OpenGL texture", texture_id);
        
        // Check OpenGL error state
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            LOG_ERROR("OpenGL error before drawing: 0x%X", error);
        }
        return;
    }

    int window_width = 0;
    int window_height = 0;
    platform->get_window_size(window_id, &window_width, &window_height);
    
    if (window_width <= 0 || window_height <= 0) {
        LOG_WARNING("Invalid window size: %dx%d", window_width, window_height);
        return;
    }

    glViewport(0, 0, window_width, window_height);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    mat4x4 projection;
    mat4x4_ortho(projection, 0.0f, (float)window_width, (float)window_height, 0.0f, -1.0f, 1.0f);

    Vertex vertices[6];
    
    vec2 texCoords[6] = {
        {0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f},
        {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}
    };

    float x0 = (float)x;
    float y0 = (float)y;
    float x1 = x0 + (float)width;
    float y1 = y0 + (float)height;
    for (int i = 0; i < 6; i++) {
        vertices[i].col[0] = 1.0f;  
        vertices[i].col[1] = 1.0f;
        vertices[i].col[2] = 1.0f;
        vertices[i].texCoord[0] = texCoords[i][0];
        vertices[i].texCoord[1] = texCoords[i][1];
    }

    vertices[0].pos[0] = x0; vertices[0].pos[1] = y0; 
    vertices[1].pos[0] = x1; vertices[1].pos[1] = y0; 
    vertices[2].pos[0] = x0; vertices[2].pos[1] = y1; 
    
    vertices[3].pos[0] = x1; vertices[3].pos[1] = y0;  
    vertices[4].pos[0] = x1; vertices[4].pos[1] = y1;  
    vertices[5].pos[0] = x0; vertices[5].pos[1] = y1;  

    glBindBuffer(GL_ARRAY_BUFFER, ctx.shape_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glUseProgram(ctx.shape_program);
    
    glUniformMatrix4fv(glGetUniformLocation(ctx.shape_program, "projection"), 
                      1, GL_FALSE, (const GLfloat*)projection);
    glUniform1i(glGetUniformLocation(ctx.shape_program, "useTexture"), 1);
    glUniform2f(glGetUniformLocation(ctx.shape_program, "size"), 0.0f, 0.0f);
    glUniform1f(glGetUniformLocation(ctx.shape_program, "radius"), 0.0f);
    glUniform1f(glGetUniformLocation(ctx.shape_program, "borderWidth"), 0.0f);
    glUniform1i(glGetUniformLocation(ctx.shape_program, "isRounded"), 0);
    glUniform1i(glGetUniformLocation(ctx.shape_program, "isHollow"), 0);
    glUniform1i(glGetUniformLocation(ctx.shape_program, "shapeType"), 0);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glUniform1i(glGetUniformLocation(ctx.shape_program, "tex"), 0);

    glBindVertexArray(ctx.shape_vaos[window_id]);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 
                         (void*)offsetof(Vertex, pos));
    
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), 
                         (void*)offsetof(Vertex, col));
    
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), 
                         (void*)offsetof(Vertex, texCoord));
    
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        LOG_ERROR("OpenGL error during image draw: 0x%X", error);
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    
    LOG_INFO("Image drawn successfully: texture %u", texture_id);
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
    .unload_image = unload_image,
    .load_image = load_image,
    .load_image_from_memory = load_image_from_memory,
    .draw_image = draw_image,
    .shutdown = shutdown
};