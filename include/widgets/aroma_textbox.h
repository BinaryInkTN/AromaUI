#ifndef AROMA_TEXTBOX_H
#define AROMA_TEXTBOX_H

#include "aroma_common.h"
#include "aroma_node.h"
#include <stdbool.h>
#include <stddef.h>

#define AROMA_TEXTBOX_MAX_LENGTH 256
#define AROMA_TEXTBOX_CURSOR_BLINK_RATE 500  // milliseconds

typedef struct AromaTextbox
{
    AromaRect rect;
    char text[AROMA_TEXTBOX_MAX_LENGTH];
    size_t text_length;
    size_t cursor_pos;           // Current cursor position in text
    bool is_focused;
    bool show_cursor;            // For blinking cursor animation
    uint64_t cursor_blink_time;  // Timestamp for cursor blinking
    uint32_t bg_color;
    uint32_t text_color;
    uint32_t border_color;
    uint32_t cursor_color;
    uint32_t placeholder_color;
    char placeholder[AROMA_TEXTBOX_MAX_LENGTH];
    void* font;                  // Font for text rendering (optional)
    bool (*on_text_changed)(AromaNode* node, const char* text, void* user_data);
    bool (*on_focus_changed)(AromaNode* node, bool focused, void* user_data);
    void* user_data;
} AromaTextbox;

/**
 * Create a textbox widget
 * @param parent Parent node to attach the textbox to (required)
 * @param x X position
 * @param y Y position
 * @param width Textbox width
 * @param height Textbox height
 * @return AromaNode pointer to the textbox node
 */
AromaNode* aroma_textbox_create(AromaNode* parent, int x, int y, int width, int height);

/**
 * Set placeholder text (displayed when textbox is empty)
 * @param node Textbox node
 * @param placeholder Placeholder text
 */
void aroma_textbox_set_placeholder(AromaNode* node, const char* placeholder);

/**
 * Set textbox text
 * @param node Textbox node
 * @param text New text to display
 */
void aroma_textbox_set_text(AromaNode* node, const char* text);

/**
 * Get textbox text
 * @param node Textbox node
 * @return Current text in textbox
 */
const char* aroma_textbox_get_text(AromaNode* node);

/**
 * Set focus state
 * @param node Textbox node
 * @param focused Whether textbox is focused
 */
void aroma_textbox_set_focused(AromaNode* node, bool focused);

/**
 * Check if textbox is focused
 * @param node Textbox node
 * @return True if focused
 */
bool aroma_textbox_is_focused(AromaNode* node);

/**
 * Handle mouse click on textbox
 * @param node Textbox node
 * @param mouse_x Mouse X coordinate
 * @param mouse_y Mouse Y coordinate
 */
void aroma_textbox_on_click(AromaNode* node, int mouse_x, int mouse_y);

/**
 * Handle character input
 * @param node Textbox node
 * @param character Character to insert
 */
void aroma_textbox_on_char(AromaNode* node, char character);

/**
 * Handle backspace key
 * @param node Textbox node
 */
void aroma_textbox_on_backspace(AromaNode* node);

/**
 * Set text changed callback
 * @param node Textbox node
 * @param callback Callback function
 * @param user_data User-defined data passed to callback
 */
void aroma_textbox_set_on_text_changed(AromaNode* node, 
                                      bool (*callback)(AromaNode*, const char*, void*), 
                                      void* user_data);

/**
 * Set focus changed callback
 * @param node Textbox node
 * @param callback Callback function
 * @param user_data User-defined data passed to callback
 */
void aroma_textbox_set_on_focus_changed(AromaNode* node, 
                                       bool (*callback)(AromaNode*, bool, void*), 
                                       void* user_data);

/**
 * Draw the textbox
 * @param node Textbox node
 * @param window_id Window to draw to
 */
void aroma_textbox_draw(AromaNode* node, size_t window_id);

/**
 * Set font for text rendering
 * @param node Textbox node
 * @param font Font to use for rendering text
 */
void aroma_textbox_set_font(AromaNode* node, void* font);

/**
 * Destroy textbox and clean up resources
 * @param node Textbox node
 */
void aroma_textbox_destroy(AromaNode* node);

/**
 * Setup textbox with automatic event handling
 * This convenience function sets up all necessary event subscriptions
 * for the textbox to work properly (mouse click, keyboard input, etc.)
 * 
 * @param textbox_node Textbox node to setup
 * @param on_redraw_callback Called when textbox needs redraw
 * @param on_text_changed_callback Called when text changes
 * @param user_data User-defined data passed to callbacks
 * @return true if setup was successful
 */
bool aroma_textbox_setup_events(AromaNode* textbox_node, 
                               void (*on_redraw_callback)(void*),
                               bool (*on_text_changed_callback)(AromaNode*, const char*, void*),
                               void* user_data);

#endif
