#include "core/aroma_drawlist.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <stdlib.h>
#include <string.h>

typedef struct AromaDrawCmd {
    AromaDrawCmdType type;
    bool is_drawn;
    union {
        struct {
            uint32_t color;
        } clear;
        struct {
            int x;
            int y;
            int width;
            int height;
            uint32_t color;
            bool is_rounded;
            float corner_radius;
        } fill_rect;
        struct {
            int x;
            int y;
            int width;
            int height;
            uint32_t color;
            int border_width;
            bool is_rounded;
            float corner_radius;
        } hollow_rect;
        struct {
            int cx;
            int cy;
            int radius;
            float start_angle;
            float end_angle;
            uint32_t color;
            int thickness;
        } arc;
        struct {
            AromaFont* font;
            char* text;
            int x;
            int y;
            uint32_t color;
            float scale;
        } text;
        struct {
            int x;
            int y;
            int width;
            int height;
            unsigned int texture_id;
        } image;
        struct {
            int x;
            int y;
            int width;
            int height;
        } scissor;
    } data;
} AromaDrawCmd;

struct AromaDrawList {
    AromaDrawCmd* commands;
    size_t count;
    size_t capacity;
};

static AromaDrawList* g_active_drawlist = NULL;

static void aroma_drawlist_reserve(AromaDrawList* list, size_t additional)
{
    if (!list) return;
    size_t required = list->count + additional;
    if (required <= list->capacity) return;

    size_t new_capacity = list->capacity == 0 ? 64 : list->capacity;
    if (new_capacity < 64)
        new_capacity *= 2;
    else
        new_capacity += new_capacity / 2;
    while (new_capacity < required)
        new_capacity += new_capacity / 2;

    if (new_capacity > SIZE_MAX / sizeof(AromaDrawCmd))
        return;

    AromaDrawCmd* next = realloc(list->commands, new_capacity * sizeof(AromaDrawCmd));
    if (!next)
        return;

    list->commands = next;
    list->capacity = new_capacity;
}

AromaDrawList* aroma_drawlist_create(void)
{
    AromaDrawList* list = calloc(1, sizeof(AromaDrawList));
    if (!list)
        return NULL;
    return list;
}

void aroma_drawlist_destroy(AromaDrawList* list)
{
    if (!list) return;
    aroma_drawlist_reset(list);
    free(list->commands);
    list->commands = NULL;
    list->capacity = 0;
    list->count = 0;
    free(list);
}

void aroma_drawlist_reset(AromaDrawList* list)
{
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        AromaDrawCmd* cmd = &list->commands[i];
        if (cmd->type == AROMA_DRAW_CMD_TEXT) {
            free(cmd->data.text.text);
            cmd->data.text.text = NULL;
        }
        cmd->is_drawn = false;
    }
    list->count = 0;
}

void aroma_drawlist_begin(AromaDrawList* list)
{
    g_active_drawlist = list;
}

void aroma_drawlist_end(void)
{
    g_active_drawlist = NULL;
}

bool aroma_drawlist_is_active(void)
{
    return g_active_drawlist != NULL;
}

AromaDrawList* aroma_drawlist_get_active(void)
{
    return g_active_drawlist;
}

void aroma_drawlist_cmd_clear(AromaDrawList* list, uint32_t color)
{
    #ifndef ESP32
    if (!list) return;
    aroma_drawlist_reserve(list, 1);
    if (list->count >= list->capacity) return;
    AromaDrawCmd* cmd = &list->commands[list->count++];
    cmd->type = AROMA_DRAW_CMD_CLEAR;
    cmd->data.clear.color = color;
    cmd->is_drawn = false;
    #endif
}

void aroma_drawlist_cmd_fill_rect(AromaDrawList* list, int x, int y, int width, int height,
                                  uint32_t color, bool is_rounded, float corner_radius)
{
    if (!list) return;
    aroma_drawlist_reserve(list, 1);
    if (list->count >= list->capacity) return;
    AromaDrawCmd* cmd = &list->commands[list->count++];
    cmd->type = AROMA_DRAW_CMD_FILL_RECT;
    cmd->data.fill_rect.x = x;
    cmd->data.fill_rect.y = y;
    cmd->data.fill_rect.width = width;
    cmd->data.fill_rect.height = height;
    cmd->data.fill_rect.color = color;
    cmd->data.fill_rect.is_rounded = is_rounded;
    cmd->data.fill_rect.corner_radius = corner_radius;
    cmd->is_drawn = false;
}

void aroma_drawlist_cmd_hollow_rect(AromaDrawList* list, int x, int y, int width, int height,
                                    uint32_t color, int border_width, bool is_rounded, float corner_radius)
{
    if (!list) return;
    aroma_drawlist_reserve(list, 1);
    if (list->count >= list->capacity) return;
    AromaDrawCmd* cmd = &list->commands[list->count++];
    cmd->type = AROMA_DRAW_CMD_HOLLOW_RECT;
    cmd->data.hollow_rect.x = x;
    cmd->data.hollow_rect.y = y;
    cmd->data.hollow_rect.width = width;
    cmd->data.hollow_rect.height = height;
    cmd->data.hollow_rect.color = color;
    cmd->data.hollow_rect.border_width = border_width;
    cmd->data.hollow_rect.is_rounded = is_rounded;
    cmd->data.hollow_rect.corner_radius = corner_radius;
    cmd->is_drawn = false;
}

void aroma_drawlist_cmd_arc(AromaDrawList* list, int cx, int cy, int radius,
                            float start_angle, float end_angle, uint32_t color, int thickness)
{
    if (!list) return;
    aroma_drawlist_reserve(list, 1);
    if (list->count >= list->capacity) return;
    AromaDrawCmd* cmd = &list->commands[list->count++];
    cmd->type = AROMA_DRAW_CMD_ARC;
    cmd->data.arc.cx = cx;
    cmd->data.arc.cy = cy;
    cmd->data.arc.radius = radius;
    cmd->data.arc.start_angle = start_angle;
    cmd->data.arc.end_angle = end_angle;
    cmd->data.arc.color = color;
    cmd->data.arc.thickness = thickness;
    cmd->is_drawn = false;
}

void aroma_drawlist_cmd_text(AromaDrawList* list, AromaFont* font, const char* text,
                             int x, int y, uint32_t color, float scale)
{
    if (!list || !text) return;

    char* text_copy = strdup(text);
    if (!text_copy) {
        return;
    }

    aroma_drawlist_reserve(list, 1);
    if (list->count >= list->capacity) {
        free(text_copy);
        return;
    }

    AromaDrawCmd* cmd = &list->commands[list->count++];
    cmd->type = AROMA_DRAW_CMD_TEXT;
    cmd->data.text.font = font;
    cmd->data.text.text = text_copy;
    cmd->data.text.x = x;
    cmd->data.text.y = y;
    cmd->data.text.color = color;
    cmd->data.text.scale = scale;
    cmd->is_drawn = false;
}

void aroma_drawlist_cmd_image(AromaDrawList* list, int x, int y, int width, int height, unsigned int texture_id)
{
    if (!list) return;
    aroma_drawlist_reserve(list, 1);
    if (list->count >= list->capacity) return;
    AromaDrawCmd* cmd = &list->commands[list->count++];
    cmd->type = AROMA_DRAW_CMD_IMAGE;
    cmd->data.image.x = x;
    cmd->data.image.y = y;
    cmd->data.image.width = width;
    cmd->data.image.height = height;
    cmd->data.image.texture_id = texture_id;
    cmd->is_drawn = false;
}

void aroma_drawlist_cmd_scissor_push(AromaDrawList* list, int x, int y, int width, int height)
{
    if (!list) return;
    aroma_drawlist_reserve(list, 1);
    if (list->count >= list->capacity) return;
    AromaDrawCmd* cmd = &list->commands[list->count++];
    cmd->type = AROMA_DRAW_CMD_SCISSOR_PUSH;
    cmd->data.scissor.x = x;
    cmd->data.scissor.y = y;
    cmd->data.scissor.width = width;
    cmd->data.scissor.height = height;
    cmd->is_drawn = false;
}

void aroma_drawlist_cmd_scissor_pop(AromaDrawList* list)
{
    if (!list) return;
    aroma_drawlist_reserve(list, 1);
    if (list->count >= list->capacity) return;
    AromaDrawCmd* cmd = &list->commands[list->count++];
    cmd->type = AROMA_DRAW_CMD_SCISSOR_POP;
    cmd->is_drawn = false;
}

void aroma_drawlist_flush(AromaDrawList* list, size_t window_id)
{
    if (!list || list->count == 0) return;

    AromaDrawList* previous = g_active_drawlist;
    g_active_drawlist = NULL;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) {
        g_active_drawlist = previous;
        return;
    }

    for (size_t i = 0; i < list->count; i++) {
        const AromaDrawCmd* cmd = &list->commands[i];

        switch (cmd->type) {
            case AROMA_DRAW_CMD_CLEAR:
                if (gfx->clear)
                    gfx->clear(window_id, cmd->data.clear.color);
                break;
            case AROMA_DRAW_CMD_FILL_RECT:
                if (gfx->fill_rectangle)
                    gfx->fill_rectangle(window_id,
                                        cmd->data.fill_rect.x,
                                        cmd->data.fill_rect.y,
                                        cmd->data.fill_rect.width,
                                        cmd->data.fill_rect.height,
                                        cmd->data.fill_rect.color,
                                        cmd->data.fill_rect.is_rounded,
                                        cmd->data.fill_rect.corner_radius);
                break;
            case AROMA_DRAW_CMD_HOLLOW_RECT:
                if (gfx->draw_hollow_rectangle)
                    gfx->draw_hollow_rectangle(window_id,
                                               cmd->data.hollow_rect.x,
                                               cmd->data.hollow_rect.y,
                                               cmd->data.hollow_rect.width,
                                               cmd->data.hollow_rect.height,
                                               cmd->data.hollow_rect.color,
                                               cmd->data.hollow_rect.border_width,
                                               cmd->data.hollow_rect.is_rounded,
                                               cmd->data.hollow_rect.corner_radius);
                break;
            case AROMA_DRAW_CMD_ARC:
                if (gfx->draw_arc)
                    gfx->draw_arc(window_id,
                                  cmd->data.arc.cx,
                                  cmd->data.arc.cy,
                                  cmd->data.arc.radius,
                                  cmd->data.arc.start_angle,
                                  cmd->data.arc.end_angle,
                                  cmd->data.arc.color,
                                  cmd->data.arc.thickness);
                break;
            case AROMA_DRAW_CMD_TEXT:
                if (gfx->render_text)
                    gfx->render_text(window_id,
                                     cmd->data.text.font,
                                     cmd->data.text.text ? cmd->data.text.text : "",
                                     cmd->data.text.x,
                                     cmd->data.text.y,
                                     cmd->data.text.color,
                                     cmd->data.text.scale);
                break;
            case AROMA_DRAW_CMD_IMAGE:
                if (gfx->draw_image)
                    gfx->draw_image(window_id,
                                    cmd->data.image.x,
                                    cmd->data.image.y,
                                    cmd->data.image.width,
                                    cmd->data.image.height,
                                    cmd->data.image.texture_id);
                break;
            case AROMA_DRAW_CMD_SCISSOR_PUSH:
                if (gfx->graphics_set_clip)
                    gfx->graphics_set_clip(cmd->data.scissor.x,
                                           cmd->data.scissor.y,
                                           cmd->data.scissor.width,
                                           cmd->data.scissor.height);
                break;
            case AROMA_DRAW_CMD_SCISSOR_POP:
                if (gfx->graphics_clear_clip)
                    gfx->graphics_clear_clip();
                break;
        }
    }

    aroma_drawlist_reset(list);
    g_active_drawlist = previous;
}

static inline bool rect_intersects(
    int ax, int ay, int aw, int ah,
    int bx, int by, int bw, int bh)
{
    if (aw <= 0 || ah <= 0 || bw <= 0 || bh <= 0)
        return false;
    return !(ax + aw <= bx ||
             bx + bw <= ax ||
             ay + ah <= by ||
             by + bh <= ay);
}

void aroma_drawlist_smart_flush(AromaDrawList* list,
                                size_t window_id,
                                int x, int y, int width, int height)
{
    if (!list || list->count == 0) return;
    if (width <= 0 || height <= 0) return;

    AromaDrawList* previous = g_active_drawlist;
    g_active_drawlist = NULL;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) {
        g_active_drawlist = previous;
        return;
    }

    for (size_t i = 0; i < list->count; i++) {
        AromaDrawCmd* cmd = &list->commands[i];

        switch (cmd->type) {
            case AROMA_DRAW_CMD_FILL_RECT:
                if (gfx->fill_rectangle &&
                    rect_intersects(cmd->data.fill_rect.x,
                                    cmd->data.fill_rect.y,
                                    cmd->data.fill_rect.width,
                                    cmd->data.fill_rect.height,
                                    x, y, width, height))
                    gfx->fill_rectangle(window_id,
                                        cmd->data.fill_rect.x,
                                        cmd->data.fill_rect.y,
                                        cmd->data.fill_rect.width,
                                        cmd->data.fill_rect.height,
                                        cmd->data.fill_rect.color,
                                        cmd->data.fill_rect.is_rounded,
                                        cmd->data.fill_rect.corner_radius);
                break;

            case AROMA_DRAW_CMD_HOLLOW_RECT:
                if (gfx->draw_hollow_rectangle &&
                    rect_intersects(cmd->data.hollow_rect.x,
                                    cmd->data.hollow_rect.y,
                                    cmd->data.hollow_rect.width,
                                    cmd->data.hollow_rect.height,
                                    x, y, width, height))
                    gfx->draw_hollow_rectangle(window_id,
                                               cmd->data.hollow_rect.x,
                                               cmd->data.hollow_rect.y,
                                               cmd->data.hollow_rect.width,
                                               cmd->data.hollow_rect.height,
                                               cmd->data.hollow_rect.color,
                                               cmd->data.hollow_rect.border_width,
                                               cmd->data.hollow_rect.is_rounded,
                                               cmd->data.hollow_rect.corner_radius);
                break;

            case AROMA_DRAW_CMD_ARC:
                if (gfx->draw_arc &&
                    rect_intersects(cmd->data.arc.cx - cmd->data.arc.radius,
                                    cmd->data.arc.cy - cmd->data.arc.radius,
                                    cmd->data.arc.radius * 2,
                                    cmd->data.arc.radius * 2,
                                    x, y, width, height))
                    gfx->draw_arc(window_id,
                                  cmd->data.arc.cx,
                                  cmd->data.arc.cy,
                                  cmd->data.arc.radius,
                                  cmd->data.arc.start_angle,
                                  cmd->data.arc.end_angle,
                                  cmd->data.arc.color,
                                  cmd->data.arc.thickness);
                break;

            case AROMA_DRAW_CMD_TEXT:
                if (gfx->render_text)
                    gfx->render_text(window_id,
                                     cmd->data.text.font,
                                     cmd->data.text.text ? cmd->data.text.text : "",
                                     cmd->data.text.x,
                                     cmd->data.text.y,
                                     cmd->data.text.color,
                                     cmd->data.text.scale);
                break;

            case AROMA_DRAW_CMD_IMAGE:
                if (gfx->draw_image &&
                    rect_intersects(cmd->data.image.x,
                                    cmd->data.image.y,
                                    cmd->data.image.width,
                                    cmd->data.image.height,
                                    x, y, width, height))
                    gfx->draw_image(window_id,
                                    cmd->data.image.x,
                                    cmd->data.image.y,
                                    cmd->data.image.width,
                                    cmd->data.image.height,
                                    cmd->data.image.texture_id);
                break;

            case AROMA_DRAW_CMD_SCISSOR_PUSH:
                if (gfx->graphics_set_clip)
                    gfx->graphics_set_clip(cmd->data.scissor.x,
                                           cmd->data.scissor.y,
                                           cmd->data.scissor.width,
                                           cmd->data.scissor.height);
                break;

            case AROMA_DRAW_CMD_SCISSOR_POP:
                if (gfx->graphics_clear_clip)
                    gfx->graphics_clear_clip();
                break;

            default:
                break;
        }
    }

    g_active_drawlist = previous;
}