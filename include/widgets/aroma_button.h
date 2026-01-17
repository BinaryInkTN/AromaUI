#ifndef AROMA_BUTTON_H
#define AROMA_BUTTON_H

#include "aroma_common.h"
#include "aroma_node.h"
#include <stdbool.h>

#define AROMA_BUTTON_LABEL_MAX 64

typedef enum AromaButtonState {
    BUTTON_STATE_IDLE,
    BUTTON_STATE_HOVER,
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_RELEASED
} AromaButtonState;

typedef struct AromaButton
{
    AromaRect rect;
    char label[AROMA_BUTTON_LABEL_MAX];
    AromaButtonState state;
    uint32_t idle_color;
    uint32_t hover_color;
    uint32_t pressed_color;
    uint32_t text_color;
    bool (*on_click)(AromaNode* button_node, void* user_data);
    bool (*on_hover)(AromaNode* button_node, void* user_data);
    void* user_data;
} AromaButton;

/**
 * Create a button widget
 * @param parent Parent node to attach the button to (required)
 * @param label Button label text
 * @param x X position
 * @param y Y position
 * @param width Button width
 * @param height Button height
 * @return AromaNode pointer to the button node
 */
AromaNode* aroma_button_create(AromaNode* parent, const char* label, int x, int y, int width, int height);

/**
 * Set click callback
 * @param button_node Button node
 * @param on_click Callback function
 * @param user_data User-defined data passed to callback
 */
void aroma_button_set_on_click(AromaNode* button_node, bool (*on_click)(AromaNode*, void*), void* user_data);

/**
 * Set hover callback
 * @param button_node Button node
 * @param on_hover Callback function
 * @param user_data User-defined data passed to callback
 */
void aroma_button_set_on_hover(AromaNode* button_node, bool (*on_hover)(AromaNode*, void*), void* user_data);

/**
 * Set button colors
 * @param button_node Button node
 * @param idle_color Color when idle
 * @param hover_color Color when hovering
 * @param pressed_color Color when pressed
 * @param text_color Text color
 */
void aroma_button_set_colors(AromaNode* button_node, uint32_t idle_color, uint32_t hover_color, uint32_t pressed_color, uint32_t text_color);

/**
 * Handle mouse events for the button
 * @param button_node Button node
 * @param mouse_x Mouse X position
 * @param mouse_y Mouse Y position
 * @param is_clicked Whether mouse is clicked
 * @return true if event was handled
 */
bool aroma_button_handle_mouse_event(AromaNode* button_node, int mouse_x, int mouse_y, bool is_clicked);

/**
 * Render the button
 * @param button_node Button node
 * @param window_id Window ID for rendering
 */
void aroma_button_draw(AromaNode* button_node, size_t window_id);

/**
 * Destroy button and cleanup resources
 * @param button_node Button node to destroy
 */
void aroma_button_destroy(AromaNode* button_node);

/**
 * Setup button with automatic event handling
 * This convenience function sets up all necessary event subscriptions
 * for the button to work properly (mouse events, hover, click, etc.)
 * 
 * @param button_node Button node to setup
 * @param on_redraw_callback Called when button needs redraw
 * @param user_data User-defined data passed to on_redraw_callback
 * @return true if setup was successful
 */
bool aroma_button_setup_events(AromaNode* button_node, void (*on_redraw_callback)(void*), void* user_data);

#endif