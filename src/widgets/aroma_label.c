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

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, label);
    if (!node) {
        aroma_widget_free(label);
        return NULL;
    }

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
    if (!gfx || !gfx->render_text || !label->font) return;

    gfx->render_text(window_id, label->font, label->text, label->rect.x, label->rect.y, label->color);
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