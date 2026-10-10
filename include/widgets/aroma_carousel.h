#ifndef AROMA_CAROUSEL_H
#define AROMA_CAROUSEL_H

#include "aroma_common.h"
#include "aroma_font.h"
#include "aroma_node.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct AromaCarousel AromaCarousel;

typedef void (*AromaCarouselPageCb)(AromaNode *node, int index,
                                    void *user_data);

AromaNode *aroma_carousel_create(AromaNode *parent, int x, int y, int width,
                                 int height);

AromaNode *aroma_carousel_add_page(AromaNode *car_node);
int aroma_carousel_get_count(AromaNode *car_node);
int aroma_carousel_get_page(AromaNode *car_node);
void aroma_carousel_set_page(AromaNode *car_node, int index);
void aroma_carousel_next(AromaNode *car_node);
void aroma_carousel_prev(AromaNode *car_node);
void aroma_carousel_set_on_change(AromaNode *car_node, AromaCarouselPageCb cb,
                                  void *user_data);
void aroma_carousel_set_font(AromaNode *car_node, AromaFont *font);
bool aroma_carousel_setup_events(AromaNode *car_node,
                                 void (*on_redraw)(void *), void *user_data);
void aroma_carousel_draw(AromaNode *car_node, size_t window_id);
void aroma_carousel_destroy(AromaNode *car_node);

#ifdef __cplusplus
}
#endif
#endif
