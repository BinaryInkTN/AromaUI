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

#include "core/aroma_drawlist.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <stdlib.h>
#include <string.h>

typedef struct AromaDrawCmd {
    AromaDrawCmdType type;
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
        } text;
        struct {
            int x;
            int y;
            int width;
            int height;
            unsigned int texture_id;
        } image;
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

    size_t new_capacity = list->capacity == 0 ? 64 : list->capacity * 2;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    AromaDrawCmd* next = realloc(list->commands, new_capacity * sizeof(AromaDrawCmd));
    if (!next) {
        return;
    }
    list->commands = next;
    list->capacity = new_capacity;
}

AromaDrawList* aroma_drawlist_create(void)
{
    AromaDrawList* list = calloc(1, sizeof(AromaDrawList));
    return list;
}

void aroma_drawlist_destroy(AromaDrawList* list)
{
    if (!list) return;
    aroma_drawlist_reset(list);
    free(list->commands);
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
    if (!list) return;
    aroma_drawlist_reserve(list, 1);
    AromaDrawCmd* cmd = &list->commands[list->count++];
    cmd->type = AROMA_DRAW_CMD_CLEAR;
    cmd->data.clear.color = color;
}

void aroma_drawlist_cmd_fill_rect(AromaDrawList* list, int x, int y, int width, int height,
                                  uint32_t color, bool is_rounded, float corner_radius)
{
    if (!list) return;
    aroma_drawlist_reserve(list, 1);
    AromaDrawCmd* cmd = &list->commands[list->count++];
    cmd->type = AROMA_DRAW_CMD_FILL_RECT;
    cmd->data.fill_rect.x = x;
    cmd->data.fill_rect.y = y;
    cmd->data.fill_rect.width = width;
    cmd->data.fill_rect.height = height;
    cmd->data.fill_rect.color = color;
    cmd->data.fill_rect.is_rounded = is_rounded;
    cmd->data.fill_rect.corner_radius = corner_radius;
}

void aroma_drawlist_cmd_hollow_rect(AromaDrawList* list, int x, int y, int width, int height,
                                    uint32_t color, int border_width, bool is_rounded, float corner_radius)
{
    if (!list) return;
    aroma_drawlist_reserve(list, 1);
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
}

void aroma_drawlist_cmd_arc(AromaDrawList* list, int cx, int cy, int radius,
                            float start_angle, float end_angle, uint32_t color, int thickness)
{
    if (!list) return;
    aroma_drawlist_reserve(list, 1);
    AromaDrawCmd* cmd = &list->commands[list->count++];
    cmd->type = AROMA_DRAW_CMD_ARC;
    cmd->data.arc.cx = cx;
    cmd->data.arc.cy = cy;
    cmd->data.arc.radius = radius;
    cmd->data.arc.start_angle = start_angle;
    cmd->data.arc.end_angle = end_angle;
    cmd->data.arc.color = color;
    cmd->data.arc.thickness = thickness;
}

void aroma_drawlist_cmd_text(AromaDrawList* list, AromaFont* font, const char* text,
                             int x, int y, uint32_t color)
{
    if (!list || !text) return;
    aroma_drawlist_reserve(list, 1);
    AromaDrawCmd* cmd = &list->commands[list->count++];
    cmd->type = AROMA_DRAW_CMD_TEXT;
    cmd->data.text.font = font;
    cmd->data.text.text = strdup(text);
    cmd->data.text.x = x;
    cmd->data.text.y = y;
    cmd->data.text.color = color;
}


void aroma_drawlist_cmd_image(AromaDrawList* list, int x, int y, int width, int height, unsigned int texture_id)
{
    if (!list) return;
    aroma_drawlist_reserve(list, 1);
    AromaDrawCmd* cmd = &list->commands[list->count++];
    cmd->type = AROMA_DRAW_CMD_IMAGE;
    cmd->data.image.x = x;
    cmd->data.image.y = y;
    cmd->data.image.width = width;
    cmd->data.image.height = height;
    cmd->data.image.texture_id = texture_id;
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
                if (gfx->clear) {
                    gfx->clear(window_id, cmd->data.clear.color);
                }
                break;
            case AROMA_DRAW_CMD_FILL_RECT:
                if (gfx->fill_rectangle) {
                    gfx->fill_rectangle(window_id,
                                        cmd->data.fill_rect.x,
                                        cmd->data.fill_rect.y,
                                        cmd->data.fill_rect.width,
                                        cmd->data.fill_rect.height,
                                        cmd->data.fill_rect.color,
                                        cmd->data.fill_rect.is_rounded,
                                        cmd->data.fill_rect.corner_radius);
                }
                break;
            case AROMA_DRAW_CMD_HOLLOW_RECT:
                if (gfx->draw_hollow_rectangle) {
                    gfx->draw_hollow_rectangle(window_id,
                                               cmd->data.hollow_rect.x,
                                               cmd->data.hollow_rect.y,
                                               cmd->data.hollow_rect.width,
                                               cmd->data.hollow_rect.height,
                                               cmd->data.hollow_rect.color,
                                               cmd->data.hollow_rect.border_width,
                                               cmd->data.hollow_rect.is_rounded,
                                               cmd->data.hollow_rect.corner_radius);
                }
                break;
            case AROMA_DRAW_CMD_ARC:
                if (gfx->draw_arc) {
                    gfx->draw_arc(window_id,
                                  cmd->data.arc.cx,
                                  cmd->data.arc.cy,
                                  cmd->data.arc.radius,
                                  cmd->data.arc.start_angle,
                                  cmd->data.arc.end_angle,
                                  cmd->data.arc.color,
                                  cmd->data.arc.thickness);
                }
                break;
            case AROMA_DRAW_CMD_TEXT:
                if (gfx->render_text) {
                    gfx->render_text(window_id,
                                     cmd->data.text.font,
                                     cmd->data.text.text ? cmd->data.text.text : "",
                                     cmd->data.text.x,
                                     cmd->data.text.y,
                                     cmd->data.text.color);
                }
                break;
            case AROMA_DRAW_CMD_IMAGE:
                if (gfx->draw_image) {
                    gfx->draw_image(window_id,
                                    cmd->data.image.x,
                                    cmd->data.image.y,
                                    cmd->data.image.width,   
                                    cmd->data.image.height,
                                    cmd->data.image.texture_id);
                    }
                    break;
        }
    }

    aroma_drawlist_reset(list);
    g_active_drawlist = previous;
}
