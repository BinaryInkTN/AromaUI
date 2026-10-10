




















#ifndef AROMA_IMAGE_H
#define AROMA_IMAGE_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"
#ifdef __cplusplus
extern "C" {
#endif







typedef enum AromaImageScaleMode
{
    AROMA_IMAGE_SCALE_FILL = 0,
    AROMA_IMAGE_SCALE_FIT = 1,
    AROMA_IMAGE_SCALE_COVER = 2
} AromaImageScaleMode;












AromaNode* aroma_image_create(AromaNode* parent, const char* image_path,
                              int x, int y, int width, int height);













AromaNode* aroma_image_create_from_memory(AromaNode* parent, unsigned char* data,
                                          size_t data_size, int x, int y,
                                          int width, int height);













AromaNode* aroma_image_create_from_texture(AromaNode* parent, unsigned int texture_id,
                                           int x, int y, int width, int height,
                                           bool take_ownership);







void aroma_image_set_source(AromaNode* image_node, const char* image_path);












void aroma_image_apply_fetch_result(AromaNode* image_node, bool ok,
                                    const char* cache_path);


#define AROMA_IMAGE_FETCH_COMPLETE 1001








void aroma_image_set_size(AromaNode* image_node, int width, int height);












void aroma_image_set_corner_radius(AromaNode* image_node, float radius);







float aroma_image_get_corner_radius(AromaNode* image_node);












void aroma_image_set_scale_mode(AromaNode* image_node, AromaImageScaleMode mode);







AromaImageScaleMode aroma_image_get_scale_mode(AromaNode* image_node);








void aroma_image_set_position(AromaNode* image_node, int x, int y);








void aroma_image_get_size(AromaNode* image_node, int* width, int* height);








void aroma_image_get_position(AromaNode* image_node, int* x, int* y);







unsigned int aroma_image_get_texture_id(AromaNode* image_node);







const char* aroma_image_get_source(AromaNode* image_node);







void aroma_image_draw(AromaNode* image_node, size_t window_id);






void aroma_image_destroy(AromaNode* image_node);
void aroma_image_set_on_click(AromaNode* image_node, bool (*on_click)(AromaNode*, void*), void* user_data);
void aroma_image_set_on_hover(AromaNode* image_node, bool (*on_hover)(AromaNode*, void*), void* user_data);
#ifdef __cplusplus
}
#endif
#endif
