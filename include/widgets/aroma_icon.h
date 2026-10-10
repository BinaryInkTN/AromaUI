#ifndef AROMA_ICON_H
#define AROMA_ICON_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct  AromaIcon AromaIcon;









AromaNode* aroma_icon_create(AromaNode* parent, int x, int y, int size);







void aroma_icon_set_text(AromaNode* icon_node, const char* icon_text, AromaFont* font);






void aroma_icon_set_image(AromaNode* icon_node, const char* image_path);






void aroma_icon_set_texture(AromaNode* icon_node, unsigned int texture_id);







void aroma_icon_set_color(AromaNode* icon_node, uint32_t color);





void aroma_icon_destroy(AromaNode* icon_node);

#ifdef __cplusplus
}
#endif

#endif
