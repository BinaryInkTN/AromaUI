/**
 * Simple Aroma GUI Application Example
 * 
 * This example demonstrates how to use the Aroma UI framework in your own application.
 * It shows the basic setup and usage of all widgets with the public API.
 */

#include "aroma_framework.h"
#include <stdio.h>
#include <unistd.h>

// Application state
static int button_clicks = 0;
static char textbox_content[256] = "";
static int slider_value = 50;
static bool switch_state = false;

/**
 * Global redraw callback - called whenever a widget needs redraw
 */
static void on_widget_changed(void* user_data)
{
    printf("[APP] Widget changed, redrawing...\n");
    // In a real app, you would mark your scene for redraw here
}

/**
 * Button 1 click callback
 */
static bool on_button1_click(AromaNode* button_node, void* user_data)
{
    button_clicks++;
    printf("[APP] Button 1 clicked! Total clicks: %d\n", button_clicks);
    on_widget_changed(NULL);
    return true;
}

/**
 * Button 2 click callback
 */
static bool on_button2_click(AromaNode* button_node, void* user_data)
{
    printf("[APP] Button 2 (Reset) clicked! Counter was: %d\n", button_clicks);
    button_clicks = 0;
    on_widget_changed(NULL);
    return true;
}

/**
 * Switch state changed callback
 */
static bool on_switch_change(AromaNode* switch_node, void* user_data)
{
    switch_state = aroma_switch_get_state(switch_node);
    printf("[APP] Switch changed to: %s\n", switch_state ? "ON" : "OFF");
    on_widget_changed(NULL);
    return true;
}

/**
 * Slider value changed callback
 */
static bool on_slider_change(AromaNode* slider_node, void* user_data)
{
    slider_value = aroma_slider_get_value(slider_node);
    printf("[APP] Slider changed to: %d\n", slider_value);
    on_widget_changed(NULL);
    return true;
}

/**
 * Textbox text changed callback
 */
static bool on_textbox_text_changed(AromaNode* textbox_node, const char* text, void* user_data)
{
    snprintf(textbox_content, sizeof(textbox_content), "%s", text);
    printf("[APP] Textbox text changed to: %s\n", textbox_content);
    on_widget_changed(NULL);
    return true;
}

int main()
{
    printf("=== Aroma UI Framework Example ===\n\n");
    
    // Initialize the framework
    printf("1. Initializing framework...\n");
    aroma_memory_system_init();
    __node_system_init();
    aroma_event_system_init();
    aroma_backend_abi.get_platform_interface()->initialize();
    printf("   ✓ Framework initialized\n\n");
    
    // Create main window
    printf("2. Creating main window...\n");
    AromaNode* main_window = aroma_window_create(
        "Aroma GUI Example", 
        100, 100, 800, 600
    );
    aroma_event_set_root(main_window);
    printf("   ✓ Window created (ID: %" PRIu64 ")\n\n", (uint64_t)main_window->node_id);
    
    // Load font
    printf("3. Loading font...\n");
    AromaFont* font = aroma_font_create("/usr/share/fonts/truetype/ubuntu/Ubuntu-M.ttf", 18);
    if (font) {
        aroma_gles3_load_font_for_window(0, font);
        printf("   ✓ Font loaded\n\n");
    }
    
    // Create buttons
    printf("4. Creating buttons...\n");
    AromaNode* button1 = aroma_button_create(main_window, "Click Me!", 100, 100, 150, 50);
    if (button1) {
        aroma_button_set_colors(button1, 0x0078D4, 0x005A9C, 0x004070, 0xFFFFFF);
        aroma_button_set_on_click(button1, on_button1_click, NULL);
        aroma_button_setup_events(button1, on_widget_changed, NULL);
        printf("   ✓ Button 1 created\n");
    }
    
    AromaNode* button2 = aroma_button_create(main_window, "Reset", 300, 100, 150, 50);
    if (button2) {
        aroma_button_set_colors(button2, 0xFF6B35, 0xD85829, 0xA8320C, 0xFFFFFF);
        aroma_button_set_on_click(button2, on_button2_click, NULL);
        aroma_button_setup_events(button2, on_widget_changed, NULL);
        printf("   ✓ Button 2 created\n");
    }
    printf("\n");
    
    // Create switch
    printf("5. Creating switch widget...\n");
    AromaNode* toggle_switch = aroma_switch_create(main_window, 200, 160, 50, 30, false);
    if (toggle_switch) {
        aroma_switch_set_on_change(toggle_switch, on_switch_change, NULL);
        aroma_switch_setup_events(toggle_switch, on_widget_changed, NULL);
        printf("   ✓ Switch created\n\n");
    }
    
    // Create slider
    printf("6. Creating slider widget...\n");
    AromaNode* slider = aroma_slider_create(main_window, 50, 250, 300, 30, 0, 100, 50);
    if (slider) {
        aroma_slider_set_on_change(slider, on_slider_change, NULL);
        aroma_slider_setup_events(slider, on_widget_changed, NULL);
        printf("   ✓ Slider created\n\n");
    }
    
    // Create textbox
    printf("7. Creating textbox widget...\n");
    AromaNode* textbox = aroma_textbox_create(main_window, 50, 350, 300, 40);
    if (textbox) {
        aroma_textbox_set_placeholder(textbox, "Enter text here...");
        aroma_textbox_set_font(textbox, font);
        aroma_textbox_setup_events(textbox, on_widget_changed, on_textbox_text_changed, NULL);
        printf("   ✓ Textbox created\n\n");
    }
    
    printf("=== Setup Complete ===\n");
    printf("The GUI framework is ready to use. Widgets will call callbacks when interacted.\n");
    printf("In a real application, you would implement your rendering loop and integrate\n");
    printf("the widget rendering with your graphics system.\n\n");
    
    // Cleanup
    printf("8. Cleaning up...\n");
    if (button1) aroma_button_destroy(button1);
    if (button2) aroma_button_destroy(button2);
    if (toggle_switch) aroma_switch_destroy(toggle_switch);
    if (slider) aroma_slider_destroy(slider);
    if (textbox) aroma_textbox_destroy(textbox);
    if (font) aroma_font_destroy(font);
    aroma_event_system_shutdown();
    __node_system_destroy();
    
    printf("   ✓ Cleanup complete\n");
    printf("Application finished successfully.\n");
    
    return 0;
}
