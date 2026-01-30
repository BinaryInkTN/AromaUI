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

#include "widgets/aroma_label.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>

#define AROMA_LABEL_TEXT_MAX 256

typedef struct AromaLabel {
    AromaRect rect;
    char text[AROMA_LABEL_TEXT_MAX];
    AromaLabelStyle style;
    uint32_t color;
    AromaFont* font;
    float text_scale;
} AromaLabel;

static uint32_t __label_default_color(void)
{
    AromaTheme theme = aroma_theme_get_global();
    return theme.colors.text_primary;
}

AromaNode* aroma_label_create(AromaNode* parent, const char* text, int x, int y, AromaLabelStyle style)
{
    if (!parent || !text) {
        LOG_ERROR("Invalid label parameters");
        return NULL;
    }

    AromaLabel* label = (AromaLabel*)aroma_widget_alloc(sizeof(AromaLabel));
    if (!label) return NULL;

    memset(label, 0, sizeof(AromaLabel));
    label->rect.x = x;
    label->rect.y = y;
    label->rect.width = 0;
    label->rect.height = 0;
    label->style = style;
    label->color = __label_default_color();
    label->font = NULL;
    strncpy(label->text, text, AROMA_LABEL_TEXT_MAX - 1);
    
    static const float LABEL_SCALES[] = {
        [LABEL_STYLE_LABEL_LARGE]     = 1.0f,
        [LABEL_STYLE_LABEL_MEDIUM]    = 0.9f,
        [LABEL_STYLE_LABEL_SMALL]     = 0.8f
    };

    if (style >= 0 && style < (int)(sizeof(LABEL_SCALES) / sizeof(float))) {
        label->text_scale = LABEL_SCALES[style];
    } else {
        label->text_scale = 1.0f;
    }

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, label);
    if (!node) {
        aroma_widget_free(label);
        return NULL;
    }


    aroma_node_set_draw_cb(node, aroma_label_draw);
    
    #ifdef ESP32
    aroma_node_invalidate(node); 
    #endif
    
    return node;
}

void aroma_label_set_text(AromaNode* label_node, const char* text)
{
    if (!label_node || !label_node->node_widget_ptr || !text) return;
    AromaLabel* label = (AromaLabel*)label_node->node_widget_ptr;
    strncpy(label->text, text, AROMA_LABEL_TEXT_MAX - 1);
    aroma_node_invalidate(label_node);
}

void aroma_label_set_color(AromaNode* label_node, uint32_t color)
{
    if (!label_node || !label_node->node_widget_ptr) return;
    AromaLabel* label = (AromaLabel*)label_node->node_widget_ptr;
    label->color = color;
    aroma_node_invalidate(label_node);
}

void aroma_label_set_font(AromaNode* label_node, AromaFont* font)
{
    if (!label_node || !label_node->node_widget_ptr) return;
    AromaLabel* label = (AromaLabel*)label_node->node_widget_ptr;
    label->font = font;
    aroma_node_invalidate(label_node);
}

void aroma_label_set_style(AromaNode* label_node, AromaLabelStyle style)
{
    if (!label_node || !label_node->node_widget_ptr) return;
    AromaLabel* label = (AromaLabel*)label_node->node_widget_ptr;
    label->style = style;
    aroma_node_invalidate(label_node);
}

void aroma_label_draw(AromaNode* label_node, size_t window_id)
{
    if (!label_node || !label_node->node_widget_ptr) return;
    AromaLabel* label = (AromaLabel*)label_node->node_widget_ptr;
    if (aroma_node_is_hidden(label_node)) return;
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    #ifndef ESP32
    if (!gfx || !gfx->render_text || !label->font) return;
    #endif
    //        aroma_node_invalidate(label_node);
    if (!gfx || !gfx->render_text) return;    
    gfx->render_text(window_id, label->font, label->text, label->rect.x, label->rect.y, label->color, label->text_scale);
}

void aroma_label_destroy(AromaNode* label_node)
{
    if (!label_node) return;
    if (label_node->node_widget_ptr) {
        aroma_widget_free(label_node->node_widget_ptr);
        label_node->node_widget_ptr = NULL;
    }
    __destroy_node(label_node);
}