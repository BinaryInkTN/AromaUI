/**
 * Button Widget Example
 * 
 * This example demonstrates:
 * - Creating buttons with AromaUI
 * - Setting up click and hover callbacks
 * - Customizing button colors
 * - Handling mouse events for buttons
 */

#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "aroma_slab_alloc.h"
#include "aroma_node.h"
#include "widgets/aroma_window.h"
#include "widgets/aroma_button.h"
#include <stdio.h>
#include <unistd.h>

static int button_clicks = 0;

/**
 * Click callback for button 1
 */
bool on_button1_click(AromaNode* button_node, void* user_data)
{
    button_clicks++;
    printf("Button 1 clicked! Total clicks: %d\n", button_clicks);
    return true;
}

/**
 * Hover callback for button 1
 */
bool on_button1_hover(AromaNode* button_node, void* user_data)
{
    printf("Button 1 hovered!\n");
    return true;
}

/**
 * Click callback for button 2
 */
bool on_button2_click(AromaNode* button_node, void* user_data)
{
    printf("Button 2 clicked! This button resets counter.\n");
    button_clicks = 0;
    return true;
}

void window_update_callback(size_t window_id, void *data)
{
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    
    if (!gfx || !platform) return;

    // Clear screen with white background
    gfx->clear(window_id, 0xFFFFFF);
    
    // Get button nodes from user_data
    AromaNode** buttons = (AromaNode**)data;
    if (buttons)
    {
        // Draw buttons
        aroma_button_draw(buttons[0], window_id);
        aroma_button_draw(buttons[1], window_id);
    }

    platform->swap_buffers(window_id);
}

int main()
{
    printf("Initializing Aroma UI Framework with Buttons...\n");
    aroma_memory_system_init();
    __node_system_init();
    
    aroma_backend_abi.get_platform_interface()->initialize();
    
    printf("Creating main window...\n");
    AromaNode* main_window = aroma_window_create(
        "Aroma Button Example",
        100,    // x position
        100,    // y position
        600,    // width
        400     // height
    );
    
    if (!main_window)
    {
        printf("Failed to create window\n");
        return 1;
    }

    printf("Creating buttons...\n");
    
    // Create button 1
    AromaNode* button1 = aroma_button_create(main_window, "Click Me", 100, 100, 200, 50);
    if (!button1)
    {
        printf("Failed to create button1\n");
        return 1;
    }
    
    // Set button1 colors (orange/red theme)
    aroma_button_set_colors(button1, 0xFF6B35, 0xFF8C42, 0xD84315, 0xFFFFFF);
    aroma_button_set_on_click(button1, on_button1_click, NULL);
    aroma_button_set_on_hover(button1, on_button1_hover, NULL);

    // Create button 2
    AromaNode* button2 = aroma_button_create(main_window, "Reset", 100, 200, 200, 50);
    if (!button2)
    {
        printf("Failed to create button2\n");
        return 1;
    }
    
    // Set button2 colors (green theme)
    aroma_button_set_colors(button2, 0x06A77D, 0x00D084, 0x004D40, 0xFFFFFF);
    aroma_button_set_on_click(button2, on_button2_click, NULL);

    // Store button references for rendering
    AromaNode* buttons[2] = { button1, button2 };
    
    // Set the window update callback with buttons data
    aroma_backend_abi.get_platform_interface()->set_window_update_callback(
        window_update_callback, 
        (void*)buttons
    );

    printf("Running event loop...\n");
    printf("Click the buttons to test event handling\n");

    // Main event loop
    while (aroma_backend_abi.get_platform_interface()->run_event_loop())
    {
        usleep(16000);  // ~60 FPS
    }

    // Cleanup
    printf("Shutting down application...\n");
    aroma_button_destroy(button1);
    aroma_button_destroy(button2);
    __node_system_destroy();
    
    printf("Application closed successfully.\n");
    return 0;
}
