#ifndef AROMA_DROPDOWN_H
#define AROMA_DROPDOWN_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"
#include "aroma_logger.h"
#ifdef __cplusplus
extern "C" {
#endif
#define AROMA_DROPDOWN_MAX_OPTIONS 32
#define AROMA_DROPDOWN_OPTION_MAX 128
#define AROMA_DROPDOWN_DEFAULT_MAX_ROWS 6

typedef struct  AromaDropdown
{
    AromaRect rect;
    char** options;
    int option_count;
    int selected_index;
    int hover_index;
    bool is_expanded;
    bool is_hovered;
    /* Scrollable list: at most max_visible_rows are shown at once
       (<= 0 means show all). scroll_offset is the first visible row. */
    int scroll_offset;
    int max_visible_rows;
    /* Touch drag-scroll state. */
    int touch_id;
    int touch_start_y;
    int touch_last_y;
    int touch_accum_dy;
    bool touch_moved;
    void (*on_selection_changed)(int index, const char* option, void* user_data);
    void* user_data;
    AromaFont* font;
    uint32_t text_color;
    uint32_t list_bg_color;
    uint32_t hover_bg_color;
    uint32_t selected_bg_color;
    uint32_t border_color;
    bool use_theme_colors;
    float corner_radius;
    struct {
        struct AromaNode* node;
        void (*handler)(struct AromaDropdown*, const char*, int, void*);
        void* user_data;
    } bridge;
} AromaDropdown;

typedef struct  AromaWindow AromaWindow;

AromaNode* aroma_dropdown_create(AromaNode* parent, int x, int y, int width, int height);

void aroma_dropdown_add_option(AromaNode* dropdown_node, const char* option);

void aroma_dropdown_set_on_change(AromaNode* dropdown_node,
    void (*on_change)(int index, const char* option, void* user_data),
    void* user_data);

void aroma_dropdown_setup_events(AromaNode* dropdown_node, void (*on_redraw_callback)(void*), void* user_data);

void aroma_dropdown_draw(AromaNode* dropdown_node, size_t window_id);
void aroma_dropdown_render_overlays(size_t window_id);
bool aroma_dropdown_overlay_hit_test(int x, int y, AromaNode** out_node);
void aroma_dropdown_set_font(AromaNode* dropdown_node, AromaFont* font);
void aroma_dropdown_set_text_color(AromaNode* dropdown_node, uint32_t text_color);

/* Cap the expanded list height in rows (default
 * AROMA_DROPDOWN_DEFAULT_MAX_ROWS). Excess rows scroll with the mouse
 * wheel or touch drag. Pass <= 0 to show all options at once. */
void aroma_dropdown_set_max_visible_rows(AromaNode* dropdown_node, int rows);

void aroma_dropdown_destroy(AromaNode* dropdown_node);

void aroma_ui_request_redraw(void* user_data);

#ifdef __cplusplus
}
#endif
#endif
