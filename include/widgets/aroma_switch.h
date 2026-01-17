#ifndef AROMA_SWITCH_H
#define AROMA_SWITCH_H

#include "aroma_common.h"
#include "aroma_node.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct AromaSwitch
{
    AromaRect rect;
    bool state;  // true = ON, false = OFF
    uint32_t color_on;
    uint32_t color_off;
    bool (*on_change)(AromaNode* node, void* user_data);
    void* user_data;
} AromaSwitch;

/**
 * Create a switch/toggle widget
 * @param parent Parent node
 * @param x X position
 * @param y Y position
 * @param width Width of switch
 * @param height Height of switch
 * @param initial_state Initial on/off state
 * @return Node pointer to the switch, or NULL on failure
 */
AromaNode* aroma_switch_create(AromaNode* parent, int x, int y, int width, int height, bool initial_state);

/**
 * Set switch state
 * @param node Switch node
 * @param state True for ON, false for OFF
 */
void aroma_switch_set_state(AromaNode* node, bool state);

/**
 * Get switch state
 * @param node Switch node
 * @return True if ON, false if OFF
 */
bool aroma_switch_get_state(AromaNode* node);

/**
 * Set callback for state change
 * @param node Switch node
 * @param callback Function to call when state changes
 * @param user_data User data to pass to callback
 */
void aroma_switch_set_on_change(AromaNode* node, bool (*callback)(AromaNode*, void*), void* user_data);

/**
 * Draw switch
 * @param node Switch node
 * @param window_id Window to draw to
 */
void aroma_switch_draw(AromaNode* node, size_t window_id);

/**
 * Destroy switch widget
 * @param node Switch node to destroy
 */
void aroma_switch_destroy(AromaNode* node);

/**
 * Setup switch with automatic event handling
 * This convenience function sets up all necessary event subscriptions
 * for the switch to work properly (mouse click to toggle)
 * 
 * @param switch_node Switch node to setup
 * @param on_redraw_callback Called when switch needs redraw
 * @param user_data User-defined data passed to on_redraw_callback
 * @return true if setup was successful
 */
bool aroma_switch_setup_events(AromaNode* switch_node, void (*on_redraw_callback)(void*), void* user_data);

#endif
