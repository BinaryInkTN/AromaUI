#ifndef AROMA_SNACKBAR_H
#define AROMA_SNACKBAR_H

#include "aroma_node.h"
#include "aroma_font.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Transient bottom toast. Colours, corner radius and animation timing all
 * follow the global theme unless explicitly overridden. Shows and dismisses
 * are animated (slide + fade); the auto-dismiss timer can be a no-op by
 * passing duration_ms <= 0 (falls back to 4000). */
AromaNode* aroma_snackbar_create(AromaNode* parent, const char* message, int duration_ms);
void aroma_snackbar_set_action(AromaNode* snackbar_node, const char* action_text, void (*callback)(void* user_data), void* user_data);
void aroma_snackbar_set_font(AromaNode* snackbar_node, AromaFont* font);
/* Explicit colours override the theme-derived ones. 0 keeps the theme colour
 * for that slot (background or message text). */
void aroma_snackbar_set_colors(AromaNode* snackbar_node, uint32_t bg_color, uint32_t text_color);
/* Radius in pixels; negative values restore the theme radius. */
void aroma_snackbar_set_corner_radius(AromaNode* snackbar_node, float radius);
void aroma_snackbar_show(AromaNode* snackbar_node);
void aroma_snackbar_dismiss(AromaNode* snackbar_node);
void aroma_snackbar_draw(AromaNode* snackbar_node, size_t window_id);
void aroma_snackbar_destroy(AromaNode* snackbar_node);
/* Binds tap handling. Safe to call more than once. */
bool aroma_snackbar_setup_events(AromaNode* snackbar_node, void (*on_redraw_callback)(void*), void* user_data);

#ifdef __cplusplus
}
#endif

#endif
