#include "widgets/aroma_card.h"
#include "widgets/aroma_container.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#include <stdlib.h>
#ifdef __ANDROID__
#include "aroma_android.h"
#endif


typedef struct   AromaCard
{
    AromaRect rect;
    AromaCardType type;
    uint32_t bg_color;
    uint32_t border_color;
    float border_radius;
    uint32_t shadow_color;
    bool use_theme_colors;
    void (*click_callback)(void *user_data);
    void *user_data;
} AromaCard;

#define CARD_REGISTRY_MAX 128
static struct
{
    uint64_t id;
    AromaCard *card;
} s_card_registry[CARD_REGISTRY_MAX];
static int s_card_count = 0;

static void card_registry_add(uint64_t node_id, AromaCard *card)
{
    if (s_card_count < CARD_REGISTRY_MAX)
    {
        s_card_registry[s_card_count].id = node_id;
        s_card_registry[s_card_count].card = card;
        s_card_count++;
    }
}

static AromaCard *card_registry_get(uint64_t node_id)
{
    for (int i = 0; i < s_card_count; i++)
    {
        if (s_card_registry[i].id == node_id)
            return s_card_registry[i].card;
    }
    return NULL;
}

static void card_registry_remove(uint64_t node_id)
{
    for (int i = 0; i < s_card_count; i++)
    {
        if (s_card_registry[i].id == node_id)
        {
            s_card_registry[i] = s_card_registry[--s_card_count];
            return;
        }
    }
}

void aroma_card_draw(AromaNode *card_node, size_t window_id)
{
    if (!card_node)
        return;
    if (aroma_node_is_hidden(card_node))
        return;

    AromaCard *card = card_registry_get(card_node->node_id);
    if (!card)
        return;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;

    AromaRect *r = (AromaRect *)card_node->node_widget_ptr;
    if (r)
    {
        card->rect = *r;
    }

    if (card->use_theme_colors)
    {
        AromaTheme theme = aroma_theme_get_global();
        if (card->type == CARD_TYPE_FILLED) {
            card->bg_color = aroma_color_blend(theme.colors.surface, theme.colors.primary, 0.2f);
        } else if (card->type == CARD_TYPE_GLASS) {
            uint8_t r, g, b;
            aroma_color_extract_rgb(theme.colors.surface, &r, &g, &b);
            card->bg_color = aroma_color_rgba(r, g, b, 180); // ~70% opacity, implies glass blur layer behind
        } else {
            card->bg_color = theme.colors.surface;
        }
        card->border_color = theme.colors.border;
        
        if (card->type == CARD_TYPE_GLASS) {
            uint8_t r, g, b;
            aroma_color_extract_rgb(theme.colors.border, &r, &g, &b);
            card->border_color = aroma_color_rgba(255, 255, 255, 60); // Glossy thin edge for glass effect
        }
    }

    if (card->type == CARD_TYPE_ELEVATED && card->shadow_color != 0)
    {
        gfx->fill_rectangle(window_id,
                            card->rect.x + 1, card->rect.y + 2,
                            card->rect.width, card->rect.height,
                            card->shadow_color, true, card->border_radius);
    }

    gfx->fill_rectangle(window_id,
                        card->rect.x, card->rect.y,
                        card->rect.width, card->rect.height,
                        card->bg_color, true, card->border_radius);

    if (card->type == CARD_TYPE_OUTLINED || card->type == CARD_TYPE_GLASS)
    {
        gfx->draw_hollow_rectangle(window_id,
                                   card->rect.x, card->rect.y,
                                   card->rect.width, card->rect.height,
                                   card->border_color, 1, true, card->border_radius);
    }
}

AromaNode *aroma_card_create(AromaNode *parent, int x, int y, int width, int height, AromaCardType type)
{
    if (!parent)
        return NULL;

    AromaNode *node = aroma_container_create(parent, x, y, width, height);
    if (!node)
        return NULL;
        
    aroma_node_set_layout_mode(node, AROMA_LAYOUT_MODE_NONE);
    aroma_node_set_flex_direction(node, 0);
    aroma_node_set_justify_content(node, 0);
    aroma_node_set_align_items(node, 0);

#ifdef __ANDROID__
x = aroma_android_dp_to_px(x);
y = aroma_android_dp_to_px(y);
width = aroma_android_dp_to_px(width);
height = aroma_android_dp_to_px(height);
#endif
    
AromaCard *card = (AromaCard *)calloc(1, sizeof(AromaCard));
    if (!card)
        return node;

    card->rect.x = x;
    card->rect.y = y;
    card->rect.width = width;
    card->rect.height = height;
    card->type = type;

    AromaTheme theme = aroma_theme_get_global();
    if (type == CARD_TYPE_FILLED) {
        card->bg_color = aroma_color_blend(theme.colors.surface, theme.colors.primary_light, 0.08f);
    } else if (type == CARD_TYPE_GLASS) {
        uint8_t r, g, b;
        aroma_color_extract_rgb(theme.colors.surface, &r, &g, &b);
        card->bg_color = aroma_color_rgba(r, g, b, 180);
    } else {
        card->bg_color = theme.colors.surface;
    }
    
    card->border_color = theme.colors.border;
    if (type == CARD_TYPE_GLASS) {
        card->border_color = aroma_color_rgba(255, 255, 255, 60); // Glossy thin edge
    }
    card->border_radius = 12.0f;
    card->shadow_color = 0x40000000;
    card->use_theme_colors = true;
    card->click_callback = NULL;
    card->user_data = NULL;

    card_registry_add(node->node_id, card);

    aroma_node_set_draw_cb(node, aroma_card_draw);

    return node;
}

void aroma_card_set_colors(AromaNode *card_node, uint32_t bg_color, uint32_t border_color)
{
    if (!card_node)
        return;
    AromaCard *card = card_registry_get(card_node->node_id);
    if (!card)
        return;
    card->bg_color = bg_color;
    card->border_color = border_color;
    card->use_theme_colors = false;
    aroma_node_invalidate(card_node);
}

void aroma_card_set_click_callback(AromaNode *card_node, void (*callback)(void *user_data), void *user_data)
{
    if (!card_node)
        return;
    AromaCard *card = card_registry_get(card_node->node_id);
    if (!card)
        return;
    card->click_callback = callback;
    card->user_data = user_data;
}
bool aroma_card_is_card(AromaNode *node)
{
    if (!node)
        return false;
    return card_registry_get(node->node_id) != NULL;
}
void aroma_card_destroy(AromaNode *card_node)
{
    if (!card_node)
        return;
    AromaCard *card = card_registry_get(card_node->node_id);
    if (card)
    {
        card_registry_remove(card_node->node_id);
        free(card);
    }
}
