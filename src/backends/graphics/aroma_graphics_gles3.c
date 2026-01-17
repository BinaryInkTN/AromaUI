#include "aroma_graphics_interface.h"
#include "utils/helpers_gles3.h"
#include "utils/aroma_gles3_text.h"
#include "aroma_abi.h"
#include "aroma_logger.h"
#include "aroma_font.h"
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
    // Stub implementation
}

void fill_rectangle(size_t window_id, int x, int y, int width, int height, uint32_t color, bool isRounded, float cornerRadius) 
{  
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->make_context_current) {
        platform->make_context_current(window_id);
    }

    float ndc_x, ndc_y;
    float ndc_width, ndc_height;
    vec3 color_rgb;

    convert_coords_to_ndc(window_id, &ndc_x, &ndc_y, x, y);
    convert_dimension_to_ndc(window_id, &ndc_width, &ndc_height, width, height);
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

    vertices[0].pos[0] = ndc_x;
    vertices[0].pos[1] = ndc_y;
    vertices[1].pos[0] = ndc_x + ndc_width;
    vertices[1].pos[1] = ndc_y;
    vertices[2].pos[0] = ndc_x;
    vertices[2].pos[1] = ndc_y + ndc_height;

    vertices[3].pos[0] = ndc_x + ndc_width;
    vertices[3].pos[1] = ndc_y;
    vertices[4].pos[0] = ndc_x + ndc_width;
    vertices[4].pos[1] = ndc_y + ndc_height;
    vertices[5].pos[0] = ndc_x;
    vertices[5].pos[1] = ndc_y + ndc_height;

    glBindBuffer(GL_ARRAY_BUFFER, ctx.shape_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glUseProgram(ctx.shape_program);
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
    if (platform && platform->make_context_current) {
        platform->make_context_current(window_id);
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

static void draw_hollow_rectangle(size_t window_id, int x, int y, int width, int height, 
                                  uint32_t color, int border_width, bool isRounded, float cornerRadius)
{
    if (border_width <= 0) return;
    
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->make_context_current) {
        platform->make_context_current(window_id);
    }

    // Draw border as filled rectangles: top, bottom, left, right
    fill_rectangle(window_id, x, y, width, border_width, color, false, 0.0f);  // top
    fill_rectangle(window_id, x, y + height - border_width, width, border_width, color, false, 0.0f);  // bottom
    fill_rectangle(window_id, x, y + border_width, border_width, height - 2*border_width, color, false, 0.0f);  // left
    fill_rectangle(window_id, x + width - border_width, y + border_width, border_width, height - 2*border_width, color, false, 0.0f);  // right
}

static void draw_arc(size_t window_id, int cx, int cy, int radius, float start_angle, float end_angle, 
                     uint32_t color, int thickness)
{
    // Stub implementation
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
    .shutdown = shutdown
};