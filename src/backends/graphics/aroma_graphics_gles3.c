#ifndef ESP32
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

#define MAX_FONT_CACHE_PER_WINDOW 16
#define MAX_WINDOWS 256
#define INVALID_FONT_INDEX -1

/* ── Batch constants ───────────────────────────────────────────────────── */
#define MAX_BATCH_QUADS    256
#define VERTS_PER_QUAD     6
#define MAX_BATCH_VERTICES (MAX_BATCH_QUADS * VERTS_PER_QUAD)

/* ── Cached shader uniform locations (avoids glGetUniformLocation per draw) */
typedef struct {
    GLint projection;
    GLint useTexture;
    GLint size;
    GLint radius;
    GLint borderWidth;
    GLint isRounded;
    GLint isHollow;
    GLint shapeType;
    GLint tex;
} ShapeUniforms;

/* ── Per-frame cached window/viewport state ────────────────────────────── */
typedef struct {
    size_t window_id;
    int    width;
    int    height;
    mat4x4 projection;
    bool   valid;
} CachedFrameState;

/* ── Shape vertex batch (non-rounded filled rects) ─────────────────────── */
typedef struct {
    Vertex vertices[MAX_BATCH_VERTICES];
    int    count;        /* current vertex count */
    size_t window_id;
} ShapeBatch;

typedef struct
{
    GLuint textureID;
    int width, height;
    int bearingX, bearingY;
    int advance;
} Glyph;

typedef struct
{
    GLES3TextRenderer renderer;
    AromaFont* font;
    uint32_t last_used_frame;
    uint32_t font_id;
} CachedFontRenderer;

typedef struct
{
    GLuint text_program;
    GLuint text_vao;
    GLuint shape_vao;
    CachedFontRenderer font_cache[MAX_FONT_CACHE_PER_WINDOW];
    int font_cache_count;
} WindowResources;

typedef struct
{
    GLuint shape_program;
    GLuint text_vbo;
    GLuint shape_vbo;
    mat4x4 projection;
    GLuint text_fragment_shader;
    GLuint text_vertex_shader;
    bool is_running;
    size_t num_windows;
    WindowResources windows[MAX_WINDOWS];
    uint32_t current_frame;

    /* ── Optimisation state ─────────────────────────────────── */
    ShapeUniforms shape_uniforms;
    CachedFrameState frame_cache;
    ShapeBatch batch;
} AromaGLES3Context;

static AromaGLES3Context ctx = {0};

static int find_font_in_cache(size_t window_id, AromaFont* font)
{
    if (window_id >= MAX_WINDOWS) return INVALID_FONT_INDEX;
    
    WindowResources* win = &ctx.windows[window_id];
    for (int i = 0; i < win->font_cache_count; i++)
    {
        if (win->font_cache[i].font == font)
        {
            win->font_cache[i].last_used_frame = ctx.current_frame;
            return i;
        }
    }
    return INVALID_FONT_INDEX;
}

static int evict_least_recently_used_font(size_t window_id)
{
    if (window_id >= MAX_WINDOWS) return INVALID_FONT_INDEX;
    
    WindowResources* win = &ctx.windows[window_id];
    if (win->font_cache_count == 0) return INVALID_FONT_INDEX;
    
    int lru_idx = 0;
    uint32_t oldest_frame = win->font_cache[0].last_used_frame;
    
    for (int i = 1; i < win->font_cache_count; i++)
    {
        if (win->font_cache[i].last_used_frame < oldest_frame)
        {
            oldest_frame = win->font_cache[i].last_used_frame;
            lru_idx = i;
        }
    }
    
    gles3_text_renderer_cleanup(&win->font_cache[lru_idx].renderer);
    
    for (int i = lru_idx; i < win->font_cache_count - 1; i++)
    {
        win->font_cache[i] = win->font_cache[i + 1];
    }
    
    win->font_cache_count--;
    return lru_idx;
}

static GLES3TextRenderer* get_or_load_font_renderer(size_t window_id, AromaFont* font)
{
    if (window_id >= MAX_WINDOWS || !font) return NULL;
    
    int idx = find_font_in_cache(window_id, font);
    if (idx != INVALID_FONT_INDEX)
    {
        return &ctx.windows[window_id].font_cache[idx].renderer;
    }
    
    WindowResources* win = &ctx.windows[window_id];
    
    if (win->font_cache_count >= MAX_FONT_CACHE_PER_WINDOW)
    {
        evict_least_recently_used_font(window_id);
    }
    
    int new_idx = win->font_cache_count;
    CachedFontRenderer* cache = &win->font_cache[new_idx];
    
    if (!gles3_text_renderer_init(&cache->renderer))
    {
        LOG_ERROR("Failed to init text renderer for new font in window %zu", window_id);
        return NULL;
    }
    
    FT_Face face = (FT_Face)aroma_font_get_face(font);
    if (!face)
    {
        LOG_ERROR("Failed to get font face for window %zu", window_id);
        return NULL;
    }
    
    gles3_text_renderer_load_font(&cache->renderer, face);
   
    
    cache->font = font;
    cache->last_used_frame = ctx.current_frame;
    cache->font_id = (uint32_t)(uintptr_t)font;
    
    win->font_cache_count++;
    
    LOG_INFO("Loaded new font for window %zu (total: %d)", window_id, win->font_cache_count);
    
    return &cache->renderer;
}

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

    /* Cache uniform locations once — avoids glGetUniformLocation per draw */
    ctx.shape_uniforms.projection  = glGetUniformLocation(ctx.shape_program, "projection");
    ctx.shape_uniforms.useTexture  = glGetUniformLocation(ctx.shape_program, "useTexture");
    ctx.shape_uniforms.size        = glGetUniformLocation(ctx.shape_program, "size");
    ctx.shape_uniforms.radius      = glGetUniformLocation(ctx.shape_program, "radius");
    ctx.shape_uniforms.borderWidth = glGetUniformLocation(ctx.shape_program, "borderWidth");
    ctx.shape_uniforms.isRounded   = glGetUniformLocation(ctx.shape_program, "isRounded");
    ctx.shape_uniforms.isHollow    = glGetUniformLocation(ctx.shape_program, "isHollow");
    ctx.shape_uniforms.shapeType   = glGetUniformLocation(ctx.shape_program, "shapeType");
    ctx.shape_uniforms.tex         = glGetUniformLocation(ctx.shape_program, "tex");

    /* Init batch */
    ctx.batch.count = 0;
    ctx.frame_cache.valid = false;

    return 1;
}

int setup_separate_window_resources(size_t window_id)
{
    if (window_id >= MAX_WINDOWS) return 0;
    
    WindowResources* win = &ctx.windows[window_id];
    
    if (win->text_program)
    {
        glDeleteProgram(win->text_program);
        win->text_program = 0;
    }

    win->text_program = glCreateProgram();
    glAttachShader(win->text_program, ctx.text_vertex_shader);
    glAttachShader(win->text_program, ctx.text_fragment_shader);
    glLinkProgram(win->text_program);
    check_shader_link(win->text_program);

    glGenVertexArrays(1, &win->text_vao);
    glBindVertexArray(win->text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, ctx.text_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    glGenVertexArrays(1, &win->shape_vao);
    glBindVertexArray(win->shape_vao);
    glBindBuffer(GL_ARRAY_BUFFER, ctx.shape_vbo);

    GLint position_attrib = glGetAttribLocation(ctx.shape_program, "pos");
    glEnableVertexAttribArray(position_attrib);
    glVertexAttribPointer(position_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, pos));

    GLint col_attrib = glGetAttribLocation(ctx.shape_program, "col");
    glEnableVertexAttribArray(col_attrib);
    glVertexAttribPointer(col_attrib, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, col));
    
    glBindVertexArray(0);
    
    win->font_cache_count = 0;
    
    return 1;
}


/* ═══════════════════════════════════════════════════════════════════════
 *  Frame-state caching & batch helpers
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Ensure the GL context, viewport and projection are set for `window_id`.
 * Caches the result so repeated calls within the same frame are free.
 */
static bool ensure_frame_state(size_t window_id)
{
    if (ctx.frame_cache.valid && ctx.frame_cache.window_id == window_id)
        return true;

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (!platform) return false;

    if (platform->make_context_current)
        platform->make_context_current(window_id);

    int w = 0, h = 0;
    if (platform->get_window_size)
        platform->get_window_size(window_id, &w, &h);
    if (w <= 0 || h <= 0) return false;

    ctx.frame_cache.window_id = window_id;
    ctx.frame_cache.width  = w;
    ctx.frame_cache.height = h;
    mat4x4_ortho(ctx.frame_cache.projection,
                 0.0f, (float)w, (float)h, 0.0f, -1.0f, 1.0f);
    ctx.frame_cache.valid = true;

    glViewport(0, 0, w, h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

/**
 * Issue one batched draw call for all queued non-rounded filled rects.
 */
static void flush_shape_batch(void)
{
    if (ctx.batch.count == 0) return;

    if (!ensure_frame_state(ctx.batch.window_id)) {
        ctx.batch.count = 0;
        return;
    }

    size_t wid = ctx.batch.window_id;

    glBindBuffer(GL_ARRAY_BUFFER, ctx.shape_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(ctx.batch.count * sizeof(Vertex)),
                 ctx.batch.vertices, GL_DYNAMIC_DRAW);

    glUseProgram(ctx.shape_program);
    glUniformMatrix4fv(ctx.shape_uniforms.projection, 1, GL_FALSE,
                       (const GLfloat*)ctx.frame_cache.projection);
    glUniform1i(ctx.shape_uniforms.useTexture, 0);
    glUniform1i(ctx.shape_uniforms.isRounded,  0);
    glUniform1i(ctx.shape_uniforms.isHollow,   0);
    glUniform1i(ctx.shape_uniforms.shapeType,  0);
    glUniform2f(ctx.shape_uniforms.size, 2.0f, 2.0f);   /* not used — shader fast path */
    glUniform1f(ctx.shape_uniforms.radius, 0.0f);
    glUniform1f(ctx.shape_uniforms.borderWidth, 0.0f);

    if (wid < MAX_WINDOWS)
        glBindVertexArray(ctx.windows[wid].shape_vao);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, col));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, texCoord));

    glDrawArrays(GL_TRIANGLES, 0, ctx.batch.count);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    ctx.batch.count = 0;
}

/**
 * Queue a non-rounded filled rect into the batch buffer.
 * Automatically flushes when the buffer is full or the window changes.
 */
static void batch_add_rect(size_t window_id,
                           int x, int y, int w, int h,
                           uint32_t color)
{
    if (ctx.batch.count > 0 && ctx.batch.window_id != window_id)
        flush_shape_batch();
    if (ctx.batch.count + VERTS_PER_QUAD > MAX_BATCH_VERTICES)
        flush_shape_batch();

    ctx.batch.window_id = window_id;

    vec3 rgb;
    convert_hex_to_rgb(&rgb, color);

    float x0 = (float)x,           y0 = (float)y;
    float x1 = x0 + (float)w,      y1 = y0 + (float)h;

    Vertex* v = &ctx.batch.vertices[ctx.batch.count];

    /* Two triangles forming the quad */
    v[0].pos[0] = x0; v[0].pos[1] = y0;
    v[1].pos[0] = x1; v[1].pos[1] = y0;
    v[2].pos[0] = x0; v[2].pos[1] = y1;
    v[3].pos[0] = x1; v[3].pos[1] = y0;
    v[4].pos[0] = x1; v[4].pos[1] = y1;
    v[5].pos[0] = x0; v[5].pos[1] = y1;

    for (int i = 0; i < VERTS_PER_QUAD; i++) {
        v[i].col[0] = rgb[0];
        v[i].col[1] = rgb[1];
        v[i].col[2] = rgb[2];
        v[i].texCoord[0] = 0.0f;
        v[i].texCoord[1] = 0.0f;
    }

    ctx.batch.count += VERTS_PER_QUAD;
}


/* ═══════════════════════════════════════════════════════════════════════
 *  Drawing functions
 * ═══════════════════════════════════════════════════════════════════════ */

void draw_rectangle(size_t window_id, int x, int y, int width, int height)
{
}

void fill_rectangle(size_t window_id, int x, int y, int width, int height, uint32_t color, bool isRounded, float cornerRadius)
{
    /* Non-rounded filled rects go into the batch buffer */
    if (!isRounded) {
        batch_add_rect(window_id, x, y, width, height, color);
        return;
    }

    /* Rounded rects: flush the batch first, then draw individually */
    flush_shape_batch();

    if (!ensure_frame_state(window_id)) return;

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

    float x0 = (float)x,  y0 = (float)y;
    float x1 = x0 + (float)width, y1 = y0 + (float)height;

    vertices[0].pos[0] = x0; vertices[0].pos[1] = y0;
    vertices[1].pos[0] = x1; vertices[1].pos[1] = y0;
    vertices[2].pos[0] = x0; vertices[2].pos[1] = y1;
    vertices[3].pos[0] = x1; vertices[3].pos[1] = y0;
    vertices[4].pos[0] = x1; vertices[4].pos[1] = y1;
    vertices[5].pos[0] = x0; vertices[5].pos[1] = y1;

    glBindBuffer(GL_ARRAY_BUFFER, ctx.shape_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glUseProgram(ctx.shape_program);
    glUniformMatrix4fv(ctx.shape_uniforms.projection, 1, GL_FALSE,
                       (const GLfloat*)ctx.frame_cache.projection);
    glUniform1i(ctx.shape_uniforms.useTexture, 0);
    glUniform2f(ctx.shape_uniforms.size, (float)width, (float)height);
    glUniform1f(ctx.shape_uniforms.radius, cornerRadius);
    glUniform1f(ctx.shape_uniforms.borderWidth, 1.0f);
    glUniform1i(ctx.shape_uniforms.isRounded, 1);
    glUniform1i(ctx.shape_uniforms.isHollow, 0);
    glUniform1i(ctx.shape_uniforms.shapeType, 0);

    if (window_id < MAX_WINDOWS)
        glBindVertexArray(ctx.windows[window_id].shape_vao);

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
    for (int w = 0; w < MAX_WINDOWS; w++)
    {
        WindowResources* win = &ctx.windows[w];
        
        for (int i = 0; i < win->font_cache_count; i++)
        {
            gles3_text_renderer_cleanup(&win->font_cache[i].renderer);
        }
        
        if (win->text_program)
        {
            glDeleteProgram(win->text_program);
        }
        
        if (win->text_vao)
        {
            glDeleteVertexArrays(1, &win->text_vao);
        }
        
        if (win->shape_vao)
        {
            glDeleteVertexArrays(1, &win->shape_vao);
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
    /* Discard any queued rects — we are clearing the whole screen */
    ctx.batch.count = 0;

    /* Invalidate frame cache so it re-queries window size (handles resize) */
    ctx.frame_cache.valid = false;

    if (!ensure_frame_state(window_id)) return;

    vec3 color_rgb;
    convert_hex_to_rgb(&color_rgb, color);
    glClearColor(color_rgb[0], color_rgb[1], color_rgb[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void render_text(size_t window_id, AromaFont* font, const char* text, int x, int y, uint32_t color, float scale)
{
    if (!font || !text || window_id >= MAX_WINDOWS) {
        return;
    }

    /* Text uses a different shader — flush pending shape batch first */
    flush_shape_batch();

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->make_context_current) {
        platform->make_context_current(window_id);
    }

    GLES3TextRenderer* renderer = get_or_load_font_renderer(window_id, font);
    if (!renderer)
    {
        LOG_ERROR("Failed to get renderer for font in window %zu", window_id);
        return;
    }

    WindowResources* win = &ctx.windows[window_id];
    gles3_text_render_text(renderer, win->text_program, text,
                          (float)x, (float)y, scale, color, window_id);
    
    ctx.current_frame++;
}

static float measure_text(size_t window_id, AromaFont* font, const char* text, float scale)
{
    if (!font || !text || window_id >= MAX_WINDOWS) {
        return 0.0f;
    }

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->make_context_current) {
        platform->make_context_current(window_id);
    }

    GLES3TextRenderer* renderer = get_or_load_font_renderer(window_id, font);
    if (!renderer)
    {
        LOG_ERROR("Failed to get renderer for font in window %zu", window_id);
        return 0.0f;
    }

    return gles3_text_measure_text(renderer, text, scale);
}

static void draw_hollow_rectangle(size_t window_id, int x, int y, int width, int height,
                                  uint32_t color, int border_width, bool isRounded, float cornerRadius)
{
    if (border_width <= 0) return;

    /* Different uniform state — flush pending batch */
    flush_shape_batch();

    if (!ensure_frame_state(window_id)) return;

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

    float x0 = (float)x,  y0 = (float)y;
    float x1 = x0 + (float)width, y1 = y0 + (float)height;

    vertices[0].pos[0] = x0; vertices[0].pos[1] = y0;
    vertices[1].pos[0] = x1; vertices[1].pos[1] = y0;
    vertices[2].pos[0] = x0; vertices[2].pos[1] = y1;
    vertices[3].pos[0] = x1; vertices[3].pos[1] = y0;
    vertices[4].pos[0] = x1; vertices[4].pos[1] = y1;
    vertices[5].pos[0] = x0; vertices[5].pos[1] = y1;

    glBindBuffer(GL_ARRAY_BUFFER, ctx.shape_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    glUseProgram(ctx.shape_program);
    glUniformMatrix4fv(ctx.shape_uniforms.projection, 1, GL_FALSE,
                       (const GLfloat*)ctx.frame_cache.projection);
    glUniform1i(ctx.shape_uniforms.useTexture, 0);
    glUniform2f(ctx.shape_uniforms.size, (float)width, (float)height);
    glUniform1f(ctx.shape_uniforms.radius, cornerRadius);
    glUniform1f(ctx.shape_uniforms.borderWidth, (float)border_width);
    glUniform1i(ctx.shape_uniforms.isRounded, isRounded ? 1 : 0);
    glUniform1i(ctx.shape_uniforms.isHollow, 1);
    glUniform1i(ctx.shape_uniforms.shapeType, 0);

    if (window_id < MAX_WINDOWS)
        glBindVertexArray(ctx.windows[window_id].shape_vao);

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
    if (!font || window_id >= MAX_WINDOWS) {
        return;
    }

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->make_context_current) {
        platform->make_context_current(window_id);
    }

    get_or_load_font_renderer(window_id, font);
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

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, format, img_width, img_height, 0,
                 format, GL_UNSIGNED_BYTE, data);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

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
    
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->make_context_current) {
        platform->make_context_current(0);
    } else {
        LOG_WARNING("Platform interface missing make_context_current, proceeding without explicit context switch");
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
                                                    &width, &height, &channels, 4);
    if (!img_data) {
        LOG_ERROR("Failed to load image from memory");
        glDeleteTextures(1, &texture);
        return 0;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img_data);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    glGenerateMipmap(GL_TEXTURE_2D);

    stbi_image_free(img_data);

    LOG_INFO("Successfully loaded texture from memory (ID: %u, %dx%d, Forced RGBA)",
             texture, width, height);
    return texture;
}

void draw_image(size_t window_id, int x, int y, int width, int height, unsigned int texture_id)
{
    if (texture_id == 0) {
        LOG_ERROR("Cannot draw texture ID 0 (OpenGL reserved)");
        return;
    }

    /* Image uses a texture — flush pending shape batch */
    flush_shape_batch();

    if (!ensure_frame_state(window_id)) return;

    if (!glIsTexture(texture_id)) {
        LOG_ERROR("Texture ID %u is not a valid OpenGL texture", texture_id);
        return;
    }

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    Vertex vertices[6];

    vec2 texCoords[6] = {
        {0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f},
        {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}
    };

    float x0 = (float)x,  y0 = (float)y;
    float x1 = x0 + (float)width, y1 = y0 + (float)height;

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
    glUniformMatrix4fv(ctx.shape_uniforms.projection, 1, GL_FALSE,
                       (const GLfloat*)ctx.frame_cache.projection);
    glUniform1i(ctx.shape_uniforms.useTexture, GL_TRUE);
    glUniform2f(ctx.shape_uniforms.size, (float)width, (float)height);
    glUniform1f(ctx.shape_uniforms.radius, 0.0f);
    glUniform1f(ctx.shape_uniforms.borderWidth, 0.0f);
    glUniform1i(ctx.shape_uniforms.isRounded, GL_FALSE);
    glUniform1i(ctx.shape_uniforms.isHollow, GL_FALSE);
    glUniform1i(ctx.shape_uniforms.shapeType, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glUniform1i(ctx.shape_uniforms.tex, 0);

    if (window_id < MAX_WINDOWS)
        glBindVertexArray(ctx.windows[window_id].shape_vao);

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

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

static void gles3_set_clip(int x, int y, int w, int h)
{
    /* Flush pending batch before changing scissor state */
    flush_shape_batch();

    int window_height = ctx.frame_cache.height;
    if (window_height <= 0) {
        AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
        int window_width = 0;
        if (platform && platform->get_window_size) {
            platform->get_window_size(0, &window_width, &window_height);
        }
    }

    /* OpenGL scissor origin is bottom-left, so flip Y */
    int gl_y = window_height - y - h;
    if (gl_y < 0) gl_y = 0;

    glEnable(GL_SCISSOR_TEST);
    glScissor(x, gl_y, w, h);
}

static void gles3_clear_clip(void)
{
    /* Flush pending batch before changing scissor state */
    flush_shape_batch();
    glDisable(GL_SCISSOR_TEST);
}

static void gles3_flush(void)
{
    flush_shape_batch();
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
    .shutdown = shutdown,
    .graphics_set_clip = gles3_set_clip,
    .graphics_clear_clip = gles3_clear_clip,
    .graphics_flush = gles3_flush,
};
#endif