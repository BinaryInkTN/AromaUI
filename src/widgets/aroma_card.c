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

/* Card metadata — stored in the container's node_widget_ptr is the
 * AromaContainer, so we keep this as a small side-struct pointed to
 * from the container via a fixed slot (child_nodes won't collide
 * because we look it up by the draw callback's user_data pattern).
 * We stash a pointer in the node's draw_cb user context by using a
 * simple global lookup keyed on node_id. */

typedef struct AromaCard {
    AromaRect rect;          /* kept in sync with container rect */
    AromaCardType type;
    uint32_t bg_color;
    uint32_t border_color;
    float border_radius;
    uint32_t shadow_color;
    bool use_theme_colors;
    void (*click_callback)(void* user_data);
    void* user_data;
} AromaCard;

/* ── Simple card registry (node_id → AromaCard*) ──────────────────── */
#define CARD_REGISTRY_MAX 128
static struct { uint64_t id; AromaCard *card; } s_card_registry[CARD_REGISTRY_MAX];
static int s_card_count = 0;

static void card_registry_add(uint64_t node_id, AromaCard *card) {
    if (s_card_count < CARD_REGISTRY_MAX) {
        s_card_registry[s_card_count].id = node_id;
        s_card_registry[s_card_count].card = card;
        s_card_count++;
    }
}

static AromaCard* card_registry_get(uint64_t node_id) {
    for (int i = 0; i < s_card_count; i++) {
        if (s_card_registry[i].id == node_id)
            return s_card_registry[i].card;
    }
    return NULL;
}

static void card_registry_remove(uint64_t node_id) {
    for (int i = 0; i < s_card_count; i++) {
        if (s_card_registry[i].id == node_id) {
            s_card_registry[i] = s_card_registry[--s_card_count];
            return;
        }
    }
}

/* ── Draw callback — renders card background then container children ─ */
void aroma_card_draw(AromaNode* card_node, size_t window_id) {
    if (!card_node) return;
    if (aroma_node_is_hidden(card_node)) return;

    AromaCard* card = card_registry_get(card_node->node_id);
    if (!card) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    /* Sync rect from container layout */
    AromaRect *r = (AromaRect *)card_node->node_widget_ptr;
    if (r) {
        card->rect = *r;
    }

    if (card->use_theme_colors) {
        AromaTheme theme = aroma_theme_get_global();
        card->bg_color = card->type == CARD_TYPE_FILLED
            ? aroma_color_blend(theme.colors.surface, theme.colors.primary_light, 0.08f)
            : theme.colors.surface;
        card->border_color = theme.colors.border;
    }

    /* Shadow for elevated cards */
    if (card->type == CARD_TYPE_ELEVATED && card->shadow_color != 0) {
        gfx->fill_rectangle(window_id,
                            card->rect.x + 1, card->rect.y + 2,
                            card->rect.width, card->rect.height,
                            card->shadow_color, true, card->border_radius);
    }

    /* Background fill */
    gfx->fill_rectangle(window_id,
                        card->rect.x, card->rect.y,
                        card->rect.width, card->rect.height,
                        card->bg_color, true, card->border_radius);

    /* Border for outlined cards */
    if (card->type == CARD_TYPE_OUTLINED) {
        gfx->draw_hollow_rectangle(window_id,
                                   card->rect.x, card->rect.y,
                                   card->rect.width, card->rect.height,
                                   card->border_color, 1, true, card->border_radius);
    }

    /* Children are drawn by the normal tree traversal (collect_draw_tasks)
     * since this is a NODE_TYPE_CONTAINER and not scrollable. */
}

/* ── Public API ─────────────────────────────────────────────────────── */

AromaNode* aroma_card_create(AromaNode* parent, int x, int y, int width, int height, AromaCardType type) {
    if (!parent) return NULL;

    /* Create a real container so children can be added */
    AromaNode* node = aroma_container_create(parent, x, y, width, height);
    if (!node) return NULL;

    /* Allocate card metadata */
    AromaCard* card = (AromaCard*)calloc(1, sizeof(AromaCard));
    if (!card) return node; /* degrade gracefully — still a usable container */

    card->rect.x = x;
    card->rect.y = y;
    card->rect.width = width;
    card->rect.height = height;
    card->type = type;

    AromaTheme theme = aroma_theme_get_global();
    card->bg_color = type == CARD_TYPE_FILLED
        ? aroma_color_blend(theme.colors.surface, theme.colors.primary_light, 0.08f)
        : theme.colors.surface;
    card->border_color = theme.colors.border;
    card->border_radius = 12.0f;
    card->shadow_color = 0x40000000;
    card->use_theme_colors = true;
    card->click_callback = NULL;
    card->user_data = NULL;

    card_registry_add(node->node_id, card);

    /* Override the container's draw callback with our card draw */
    aroma_node_set_draw_cb(node, aroma_card_draw);

    return node;
}

void aroma_card_set_colors(AromaNode* card_node, uint32_t bg_color, uint32_t border_color) {
    if (!card_node) return;
    AromaCard* card = card_registry_get(card_node->node_id);
    if (!card) return;
    card->bg_color = bg_color;
    card->border_color = border_color;
    card->use_theme_colors = false;
    aroma_node_invalidate(card_node);
}

void aroma_card_set_click_callback(AromaNode* card_node, void (*callback)(void* user_data), void* user_data) {
    if (!card_node) return;
    AromaCard* card = card_registry_get(card_node->node_id);
    if (!card) return;
    card->click_callback = callback;
    card->user_data = user_data;
}

void aroma_card_destroy(AromaNode* card_node) {
    if (!card_node) return;
    AromaCard* card = card_registry_get(card_node->node_id);
    if (card) {
        card_registry_remove(card_node->node_id);
        free(card);
    }
    /* The container and its children are cleaned up by the node tree */
}
