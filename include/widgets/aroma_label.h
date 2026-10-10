#ifndef AROMA_LABEL_H
#define AROMA_LABEL_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"
#include "aroma_logger.h"
#include "widgets/aroma_window.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    LABEL_STYLE_LABEL_LARGE,
    LABEL_STYLE_LABEL_MEDIUM,
    LABEL_STYLE_LABEL_SMALL
} AromaLabelStyle;

typedef struct  AromaLabel AromaLabel;


AromaNode* aroma_label_create(AromaNode* parent, const char* text, int x, int y, AromaLabelStyle style);

static inline AromaNode* aroma_ui_create_label(AromaNode* parent, const char* text, int x, int y, AromaLabelStyle style) {
    if (!parent) {
        LOG_ERROR("Invalid label parent");
        return NULL;
    }
    return aroma_label_create(parent, text, x, y, style);
}


void aroma_label_set_text(AromaNode* label_node, const char* text);


void aroma_label_set_color(AromaNode* label_node, uint32_t color);


void aroma_label_set_font(AromaNode* label_node, AromaFont* font);


const char* aroma_label_get_text(AromaNode* label_node);


AromaFont* aroma_label_get_font(AromaNode* label_node);


float aroma_label_get_scale(AromaNode* label_node);


void aroma_label_set_scale(AromaNode* label_node, float scale);


void aroma_label_set_style(AromaNode* label_node, AromaLabelStyle style);


void aroma_label_draw(AromaNode* label_node, size_t window_id);


void aroma_label_destroy(AromaNode* label_node);
#ifdef __cplusplus
}
#endif
#endif
