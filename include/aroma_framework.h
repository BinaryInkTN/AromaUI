/**
 * @file aroma_framework.h
 * @brief Aroma UI Framework - Public API
 * 
 * This is the main header file for using the Aroma UI framework in your own applications.
 * Include this file to access all public APIs for creating and managing GUI widgets.
 * 
 * @section getting_started Getting Started
 * 
 * 1. Include this header in your application
 * 2. Initialize the framework with aroma_memory_system_init(), __node_system_init(), 
 *    aroma_event_system_init(), and platform initialization
 * 3. Create a main window with aroma_window_create()
 * 4. Create widgets (buttons, sliders, switches, textboxes)
 * 5. Call aroma_*_setup_events() on each widget to enable automatic event handling
 * 6. Integrate widget rendering with your graphics system
 * 7. Render widgets in your frame callback using aroma_*_draw() functions
 * 
 * @section example Quick Example
 * 
 * @code
 * // Initialize
 * aroma_memory_system_init();
 * __node_system_init();
 * aroma_event_system_init();
 * aroma_backend_abi.get_platform_interface()->initialize();
 * 
 * // Create window
 * AromaNode* window = aroma_window_create("My App", 100, 100, 800, 600);
 * aroma_event_set_root(window);
 * 
 * // Create button
 * AromaNode* button = aroma_button_create(window, "Click Me", 100, 100, 150, 50);
 * aroma_button_set_on_click(button, my_click_callback, NULL);
 * aroma_button_setup_events(button, on_redraw, NULL);
 * 
 * // In your render loop
 * void render_frame() {
 *     aroma_event_process_queue();
 *     aroma_button_draw(button, window_id);
 * }
 * @endcode
 */

#ifndef AROMA_FRAMEWORK_H
#define AROMA_FRAMEWORK_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_font.h"
#include "aroma_logger.h"
#include "aroma_slab_alloc.h"
#include "widgets/aroma_window.h"
#include "widgets/aroma_button.h"
#include "widgets/aroma_switch.h"
#include "widgets/aroma_slider.h"
#include "widgets/aroma_textbox.h"

/**
 * @defgroup Framework Core Framework Functions
 * @{
 */

/**
 * Initialize the memory system for the Aroma framework.
 * Must be called before any other Aroma function.
 */
void aroma_memory_system_init(void);

/**
 * Initialize the node system.
 * Must be called after memory system initialization.
 */
void __node_system_init(void);

/**
 * Shutdown the node system.
 * Must be called during application cleanup.
 */
void __node_system_destroy(void);

/**
 * Initialize the event system.
 * Must be called before creating any widgets.
 * @return true on success
 */
bool aroma_event_system_init(void);

/**
 * Shutdown the event system.
 * Must be called during application cleanup.
 */
void aroma_event_system_shutdown(void);

/**
 * Set the root node for event dispatching.
 * Must be called with your main window after creating it.
 * 
 * @param root The root window node
 */
void aroma_event_set_root(AromaNode* root);

/**
 * Process all queued events.
 * Should be called once per frame before rendering.
 */
void aroma_event_process_queue(void);

/**
 * Subscribe to events for a node.
 * 
 * @param node_id Target node ID
 * @param event_type Type of event to listen for
 * @param handler Callback function
 * @param user_data User-defined data passed to handler
 * @param priority Event handler priority (higher = called first)
 * @return true on success
 */
bool aroma_event_subscribe(uint64_t node_id, AromaEventType event_type,
                          bool (*handler)(AromaEvent*, void*),
                          void* user_data, uint32_t priority);

/** @} */

/**
 * @defgroup Buttons Button Widget API
 * @{
 */

/**
 * Create a button widget
 * 
 * @param parent Parent window/container node
 * @param label Button text
 * @param x X position in pixels
 * @param y Y position in pixels
 * @param width Button width in pixels
 * @param height Button height in pixels
 * @return Node pointer to the button
 */
AromaNode* aroma_button_create(AromaNode* parent, const char* label,
                               int x, int y, int width, int height);

/**
 * Set button colors
 * 
 * @param button Button node
 * @param idle_color Color when idle (ARGB format)
 * @param hover_color Color when hovering (ARGB format)
 * @param pressed_color Color when pressed (ARGB format)
 * @param text_color Text color (ARGB format)
 */
void aroma_button_set_colors(AromaNode* button, uint32_t idle_color,
                             uint32_t hover_color, uint32_t pressed_color,
                             uint32_t text_color);

/**
 * Set button click callback
 * 
 * @param button Button node
 * @param callback Function called when button is clicked
 * @param user_data User data passed to callback
 */
void aroma_button_set_on_click(AromaNode* button,
                              bool (*callback)(AromaNode*, void*),
                              void* user_data);

/**
 * Set button hover callback
 * 
 * @param button Button node
 * @param callback Function called when button is hovered
 * @param user_data User data passed to callback
 */
void aroma_button_set_on_hover(AromaNode* button,
                              bool (*callback)(AromaNode*, void*),
                              void* user_data);

/**
 * Setup button automatic event handling
 * Automatically subscribes to all necessary mouse events.
 * 
 * @param button Button node
 * @param on_redraw Callback when button needs redraw
 * @param user_data User data
 * @return true if setup succeeded
 */
bool aroma_button_setup_events(AromaNode* button,
                              void (*on_redraw)(void*),
                              void* user_data);

/**
 * Draw button to screen
 * 
 * @param button Button node
 * @param window_id Window ID to draw to
 */
void aroma_button_draw(AromaNode* button, size_t window_id);

/**
 * Destroy button widget
 * 
 * @param button Button node to destroy
 */
void aroma_button_destroy(AromaNode* button);

/** @} */

/**
 * @defgroup Sliders Slider Widget API
 * @{
 */

/**
 * Create a slider widget
 * 
 * @param parent Parent window/container node
 * @param x X position in pixels
 * @param y Y position in pixels
 * @param width Slider width in pixels
 * @param height Slider height in pixels
 * @param min_value Minimum value
 * @param max_value Maximum value
 * @param initial_value Starting value
 * @return Node pointer to the slider
 */
AromaNode* aroma_slider_create(AromaNode* parent, int x, int y, int width, int height,
                               int min_value, int max_value, int initial_value);

/**
 * Set slider value
 * 
 * @param slider Slider node
 * @param value New value (clamped to min/max range)
 */
void aroma_slider_set_value(AromaNode* slider, int value);

/**
 * Get current slider value
 * 
 * @param slider Slider node
 * @return Current value
 */
int aroma_slider_get_value(AromaNode* slider);

/**
 * Set slider value changed callback
 * 
 * @param slider Slider node
 * @param callback Function called when value changes
 * @param user_data User data passed to callback
 */
void aroma_slider_set_on_change(AromaNode* slider,
                               bool (*callback)(AromaNode*, void*),
                               void* user_data);

/**
 * Setup slider automatic event handling
 * Automatically subscribes to mouse click/drag events.
 * 
 * @param slider Slider node
 * @param on_redraw Callback when slider needs redraw
 * @param user_data User data
 * @return true if setup succeeded
 */
bool aroma_slider_setup_events(AromaNode* slider,
                              void (*on_redraw)(void*),
                              void* user_data);

/**
 * Draw slider to screen
 * 
 * @param slider Slider node
 * @param window_id Window ID to draw to
 */
void aroma_slider_draw(AromaNode* slider, size_t window_id);

/**
 * Destroy slider widget
 * 
 * @param slider Slider node to destroy
 */
void aroma_slider_destroy(AromaNode* slider);

/** @} */

/**
 * @defgroup Switches Switch/Toggle Widget API
 * @{
 */

/**
 * Create a switch (toggle) widget
 * 
 * @param parent Parent window/container node
 * @param x X position in pixels
 * @param y Y position in pixels
 * @param width Switch width in pixels
 * @param height Switch height in pixels
 * @param initial_state Initial on/off state
 * @return Node pointer to the switch
 */
AromaNode* aroma_switch_create(AromaNode* parent, int x, int y, int width, int height,
                               bool initial_state);

/**
 * Set switch state
 * 
 * @param switch_node Switch node
 * @param state true for ON, false for OFF
 */
void aroma_switch_set_state(AromaNode* switch_node, bool state);

/**
 * Get switch state
 * 
 * @param switch_node Switch node
 * @return true if ON, false if OFF
 */
bool aroma_switch_get_state(AromaNode* switch_node);

/**
 * Set switch state changed callback
 * 
 * @param switch_node Switch node
 * @param callback Function called when state changes
 * @param user_data User data passed to callback
 */
void aroma_switch_set_on_change(AromaNode* switch_node,
                               bool (*callback)(AromaNode*, void*),
                               void* user_data);

/**
 * Setup switch automatic event handling
 * Automatically subscribes to mouse click events.
 * 
 * @param switch_node Switch node
 * @param on_redraw Callback when switch needs redraw
 * @param user_data User data
 * @return true if setup succeeded
 */
bool aroma_switch_setup_events(AromaNode* switch_node,
                              void (*on_redraw)(void*),
                              void* user_data);

/**
 * Draw switch to screen
 * 
 * @param switch_node Switch node
 * @param window_id Window ID to draw to
 */
void aroma_switch_draw(AromaNode* switch_node, size_t window_id);

/**
 * Destroy switch widget
 * 
 * @param switch_node Switch node to destroy
 */
void aroma_switch_destroy(AromaNode* switch_node);

/** @} */

/**
 * @defgroup Textboxes Textbox Widget API
 * @{
 */

/**
 * Create a textbox widget
 * 
 * @param parent Parent window/container node
 * @param x X position in pixels
 * @param y Y position in pixels
 * @param width Textbox width in pixels
 * @param height Textbox height in pixels
 * @return Node pointer to the textbox
 */
AromaNode* aroma_textbox_create(AromaNode* parent, int x, int y, int width, int height);

/**
 * Set textbox placeholder text
 * Displayed when textbox is empty and unfocused.
 * 
 * @param textbox Textbox node
 * @param placeholder Placeholder text
 */
void aroma_textbox_set_placeholder(AromaNode* textbox, const char* placeholder);

/**
 * Set textbox text
 * 
 * @param textbox Textbox node
 * @param text New text content
 */
void aroma_textbox_set_text(AromaNode* textbox, const char* text);

/**
 * Get textbox text
 * 
 * @param textbox Textbox node
 * @return Current text content
 */
const char* aroma_textbox_get_text(AromaNode* textbox);

/**
 * Set textbox font for text rendering
 * 
 * @param textbox Textbox node
 * @param font Font created with aroma_font_create()
 */
void aroma_textbox_set_font(AromaNode* textbox, void* font);

/**
 * Set focus state
 * 
 * @param textbox Textbox node
 * @param focused true to focus, false to unfocus
 */
void aroma_textbox_set_focused(AromaNode* textbox, bool focused);

/**
 * Check if textbox is focused
 * 
 * @param textbox Textbox node
 * @return true if focused
 */
bool aroma_textbox_is_focused(AromaNode* textbox);

/**
 * Set text changed callback
 * 
 * @param textbox Textbox node
 * @param callback Function called when text changes
 * @param user_data User data passed to callback
 */
void aroma_textbox_set_on_text_changed(AromaNode* textbox,
                                      bool (*callback)(AromaNode*, const char*, void*),
                                      void* user_data);

/**
 * Setup textbox automatic event handling
 * Automatically subscribes to mouse and keyboard events.
 * 
 * @param textbox Textbox node
 * @param on_redraw Callback when textbox needs redraw
 * @param on_text_changed Callback when text changes (optional)
 * @param user_data User data
 * @return true if setup succeeded
 */
bool aroma_textbox_setup_events(AromaNode* textbox,
                               void (*on_redraw)(void*),
                               bool (*on_text_changed)(AromaNode*, const char*, void*),
                               void* user_data);

/**
 * Draw textbox to screen
 * 
 * @param textbox Textbox node
 * @param window_id Window ID to draw to
 */
void aroma_textbox_draw(AromaNode* textbox, size_t window_id);

/**
 * Destroy textbox widget
 * 
 * @param textbox Textbox node to destroy
 */
void aroma_textbox_destroy(AromaNode* textbox);

/** @} */

/**
 * @defgroup Windows Window API
 * @{
 */

/**
 * Create main application window
 * 
 * @param title Window title
 * @param x X position in pixels
 * @param y Y position in pixels
 * @param width Window width in pixels
 * @param height Window height in pixels
 * @return Node pointer to the window (root of widget tree)
 */
AromaNode* aroma_window_create(const char* title, int x, int y, int width, int height);

/**
 * Destroy window widget
 * 
 * @param window Window node to destroy
 */
void aroma_window_destroy(AromaNode* window);

/** @} */

/**
 * @defgroup Graphics Graphics API
 * @{
 */

/**
 * Clear screen with a color
 * 
 * @param window_id Window ID
 * @param color Color in 0xRRGGBB format
 */
void aroma_graphics_clear(size_t window_id, uint32_t color);

/**
 * Fill a rectangle
 * 
 * @param window_id Window ID
 * @param x X position in pixels
 * @param y Y position in pixels
 * @param width Rectangle width in pixels
 * @param height Rectangle height in pixels
 * @param color Color in 0xRRGGBB format
 * @param filled Whether to fill or just outline
 * @param rotation Rotation angle in degrees
 */
void aroma_graphics_fill_rectangle(size_t window_id, int x, int y, int width, int height, 
                                   uint32_t color, bool filled, float rotation);

/**
 * Render text to screen
 * 
 * @param window_id Window ID
 * @param font Font to use
 * @param text Text string to render
 * @param x X position in pixels
 * @param y Y position in pixels
 * @param color Color in 0xRRGGBB format
 */
void aroma_graphics_render_text(size_t window_id, AromaFont* font, const char* text,
                                int x, int y, uint32_t color);

/**
 * Load font glyphs for a specific window
 * 
 * @param window_id Window ID
 * @param font Font to load glyphs for
 */
void aroma_graphics_load_font_for_window(size_t window_id, AromaFont* font);

/**
 * Swap display buffers (present rendered frame)
 * 
 * @param window_id Window ID
 */
void aroma_graphics_swap_buffers(size_t window_id);

/** @} */

/**
 * @defgroup Platform Platform API
 * @{
 */

/**
 * Initialize the platform/window system
 */
void aroma_platform_initialize(void);

/**
 * Request a window update/redraw
 * 
 * @param window_id Window ID to update
 */
void aroma_platform_request_window_update(size_t window_id);

/**
 * Set the window update callback
 * 
 * @param callback Function to call on window update
 * @param user_data Optional user data passed to callback
 */
void aroma_platform_set_window_update_callback(void (*callback)(size_t window_id, void* data), void* user_data);

/**
 * Run the main event loop iteration
 * 
 * @return true if the application should continue, false if it should exit
 */
bool aroma_platform_run_event_loop(void);

/**
 * Process all queued events
 */
void aroma_event_process_queue(void);

/** @} */

#endif // AROMA_FRAMEWORK_H
