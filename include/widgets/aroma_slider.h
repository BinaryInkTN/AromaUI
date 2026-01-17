#ifndef AROMA_SLIDER_H
#define AROMA_SLIDER_H

#include "aroma_common.h"
#include "aroma_node.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct AromaSlider
{
    AromaRect rect;
    int min_value, max_value;
    int current_value;
    uint32_t track_color;
    uint32_t thumb_color;
    uint32_t thumb_hover_color;
    bool is_dragging;
    bool (*on_change)(AromaNode*, void*);
    void* user_data;
} AromaSlider;

/**
 * Create a slider widget
 * @param parent Parent node
 * @param x X position
 * @param y Y position
 * @param width Width of slider
 * @param height Height of slider
 * @param min_value Minimum value
 * @param max_value Maximum value
 * @param initial_value Initial value
 * @return Node pointer to the slider, or NULL on failure
 */
AromaNode* aroma_slider_create(AromaNode* parent, int x, int y, int width, int height, 
                               int min_value, int max_value, int initial_value);

/**
 * Set slider value
 * @param node Slider node
 * @param value Value to set (will be clamped to min/max)
 */
void aroma_slider_set_value(AromaNode* node, int value);

/**
 * Get slider value
 * @param node Slider node
 * @return Current slider value
 */
int aroma_slider_get_value(AromaNode* node);

/**
 * Set callback for value change
 * @param node Slider node
 * @param callback Function to call when value changes
 * @param user_data User data to pass to callback
 */
void aroma_slider_set_on_change(AromaNode* node, bool (*callback)(AromaNode*, void*), void* user_data);

/**
 * Draw slider
 * @param node Slider node
 * @param window_id Window to draw to
 */
void aroma_slider_draw(AromaNode* node, size_t window_id);

/**
 * Handle mouse click on slider
 * @param node Slider node
 * @param mouse_x Mouse X position
 * @param mouse_y Mouse Y position
 */
void aroma_slider_on_click(AromaNode* node, int mouse_x, int mouse_y);

/**
 * Handle mouse move on slider (for dragging)
 * @param node Slider node
 * @param mouse_x Mouse X position
 * @param mouse_y Mouse Y position
 * @param is_pressed Whether mouse button is pressed
 */
void aroma_slider_on_mouse_move(AromaNode* node, int mouse_x, int mouse_y, bool is_pressed);

/**
 * Handle mouse release on slider
 * @param node Slider node
 */
void aroma_slider_on_mouse_release(AromaNode* node);

/**
 * Destroy slider widget
 * @param node Slider node to destroy
 */
void aroma_slider_destroy(AromaNode* node);

/**
 * Setup slider with automatic event handling
 * This convenience function sets up all necessary event subscriptions
 * for the slider to work properly (mouse click, move, release for dragging)
 * 
 * @param slider_node Slider node to setup
 * @param on_redraw_callback Called when slider needs redraw
 * @param user_data User-defined data passed to on_redraw_callback
 * @return true if setup was successful
 */
bool aroma_slider_setup_events(AromaNode* slider_node, void (*on_redraw_callback)(void*), void* user_data);

#endif
