/**
 * @file aroma_container.h
 * @brief Container widget with optional scrollable mode and scissor clipping.
 */
#ifndef AROMA_CONTAINER_H
#define AROMA_CONTAINER_H

#include "aroma_common.h"
#include "aroma_node.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AromaContainer AromaContainer;

/** @brief Scroll direction flags. */
typedef enum {
    AROMA_SCROLL_VERTICAL   = 0x01, /**< Allow vertical scrolling. */
    AROMA_SCROLL_HORIZONTAL = 0x02, /**< Allow horizontal scrolling. */
    AROMA_SCROLL_BOTH       = 0x03  /**< Allow both directions. */
} AromaScrollDirection;

/* ── creation / destruction ──────────────────────────────────────────── */

AromaNode* aroma_container_create(AromaNode* parent, int x, int y, int width, int height);
void aroma_container_destroy(AromaNode* container_node);

/* ── geometry ────────────────────────────────────────────────────────── */

void aroma_container_set_rect(AromaNode* container_node, int x, int y, int width, int height);
AromaRect aroma_container_get_rect(AromaNode* container_node);

/* ── background ──────────────────────────────────────────────────────── */

void aroma_container_set_debug_bg(AromaNode* container_node, uint32_t color);

/* ── scroll mode ─────────────────────────────────────────────────────── */

/**
 * @brief Enable or disable scrollable mode on a container.
 *
 * When enabled the container clips its children to the viewport rect
 * (using GPU scissor) and allows touch / mouse scrolling.
 *
 * @param node      Container node.
 * @param scrollable true to enable, false to disable.
 */
void aroma_container_set_scrollable(AromaNode* node, bool scrollable);

/**
 * @brief Set the total content size (determines the scroll range).
 *
 * The maximum scroll offset equals content_size - viewport_size.
 * Pass 0 for a dimension to keep it equal to the viewport (no scroll in that axis).
 *
 * @note Setting an explicit content size disables auto content measurement.
 *       If you want the container to auto-measure from its children (the
 *       default), do NOT call this function.
 */
void aroma_container_set_content_size(AromaNode* node, int content_width, int content_height);

/**
 * @brief Set allowed scroll directions (default: AROMA_SCROLL_VERTICAL).
 */
void aroma_container_set_scroll_direction(AromaNode* node, AromaScrollDirection direction);

/**
 * @brief Get the current scroll offset.
 */
void aroma_container_get_scroll(AromaNode* node, int* scroll_x, int* scroll_y);

/**
 * @brief Set the scroll offset programmatically.
 */
void aroma_container_set_scroll(AromaNode* node, int scroll_x, int scroll_y);

/**
 * @brief Scroll by a relative delta.
 */
void aroma_container_scroll_by(AromaNode* node, int dx, int dy);

/**
 * @brief Set the scroll speed multiplier (default 1.0).
 */
void aroma_container_set_scroll_speed(AromaNode* node, float speed);

/**
 * @brief Show or hide scrollbar indicators (default: true when scrollable).
 */
void aroma_container_show_scrollbar(AromaNode* node, bool show);

/**
 * @brief Set scrollbar indicator color.
 */
void aroma_container_set_scrollbar_color(AromaNode* node, uint32_t color);

/* ── drawing ─────────────────────────────────────────────────────────── */

void aroma_container_draw(AromaNode* container_node, size_t window_id);

/* ── query helpers (used by layout engine) ────────────────────────────── */

/**
 * @brief Check if a container node is in scrollable mode.
 * @return true when scrolling is enabled, false otherwise or if not a container.
 */
bool aroma_container_is_scrollable(AromaNode* node);

/**
 * @brief Get the content dimensions of a scrollable container.
 *
 * For the layout engine: children should be laid out within the content
 * area, not just the visible viewport.
 */
void aroma_container_get_content_size(AromaNode* node, int* out_w, int* out_h);

/**
 * @brief Measure children and auto-update content size.
 *
 * Called by the layout engine after laying out children.  Only effective
 * when auto_content_size is true (no explicit set_content_size call).
 */
void aroma_container_update_auto_content_size(AromaNode* node);

/* ── accessor ────────────────────────────────────────────────────────── */

static inline AromaContainer* aroma_container_get(AromaNode* node) {
    return AROMA_NODE_AS(node, AromaContainer);
}

#ifdef __cplusplus
}
#endif
#endif
