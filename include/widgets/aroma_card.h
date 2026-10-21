#ifndef AROMA_CARD_H
#define AROMA_CARD_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    CARD_TYPE_ELEVATED,
    CARD_TYPE_FILLED,
    CARD_TYPE_OUTLINED,
    CARD_TYPE_GLASS
} AromaCardType;

typedef struct  AromaCard AromaCard;
bool aroma_card_is_card(AromaNode *node);

AromaNode* aroma_card_create(AromaNode* parent, int x, int y, int width, int height, AromaCardType type);


void aroma_card_set_colors(AromaNode* card_node, uint32_t bg_color, uint32_t border_color);


void aroma_card_set_backdrop_blur(AromaNode* card_node, float radius_px);


/* Sets the card's corner radius in dp. Negative leaves the default. */
void aroma_card_set_corner_radius(AromaNode* card_node, float radius_px);


float aroma_card_get_backdrop_blur(AromaNode* card_node);


bool aroma_graphics_supports_backdrop_blur(void);


void aroma_card_set_click_callback(AromaNode* card_node, void (*callback)(void* user_data), void* user_data);


/* Makes a card interactive: it lightens on hover and darkens while pressed.
   `hover_lift`/`press_lift` are relative adjustments (0.06 = +6%, -0.10 =
   -10%). Call once per card; repeated calls just retune the factors. */
void aroma_card_set_states(AromaNode* card_node, float hover_lift, float press_lift);


void aroma_card_draw(AromaNode* card_node, size_t window_id);


void aroma_card_destroy(AromaNode* card_node);
#ifdef __cplusplus
}
#endif
#endif
