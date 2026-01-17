/**
 * Event System Example
 * 
 * Demonstrates:
 * - Event creation and dispatch
 * - Event subscription and handling
 * - Event propagation through scene graph
 * - Custom event types
 * - Event queue processing
 */

#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "aroma_slab_alloc.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "widgets/aroma_window.h"
#include "widgets/aroma_button.h"
#include <stdio.h>
#include <unistd.h>

static int click_count = 0;

/**
 * Handler for button click events
 */
bool on_button_event(AromaEvent* event, void* user_data)
{
    const char* button_name = (const char*)user_data;
    
    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_CLICK:
            click_count++;
            printf("[EVENT] %s clicked! Total clicks: %d\n", button_name, click_count);
            printf("[EVENT] Mouse position: (%d, %d)\n", 
                   event->data.mouse.x, event->data.mouse.y);
            aroma_event_consume(event);
            return true;
            
        case EVENT_TYPE_MOUSE_ENTER:
            printf("[EVENT] Mouse entered %s\n", button_name);
            return true;
            
        case EVENT_TYPE_MOUSE_EXIT:
            printf("[EVENT] Mouse left %s\n", button_name);
            return true;
            
        default:
            return false;
    }
}

/**
 * Handler for window-level events
 */
bool on_window_event(AromaEvent* event, void* user_data)
{
    switch (event->event_type) {
        case EVENT_TYPE_KEY_PRESS:
            printf("[EVENT] Key pressed: code=%u\n", event->data.key.key_code);
            if (event->data.key.key_code == 27) {  // ESC
                printf("[EVENT] Escape pressed - exiting\n");
                return true;
            }
            return false;
            
        case EVENT_TYPE_MOUSE_MOVE:
            printf("[EVENT] Mouse moved to (%d, %d)\n", 
                   event->data.mouse.x, event->data.mouse.y);
            return true;
            
        default:
            return false;
    }
}

/**
 * Custom event handler
 */
bool on_custom_event(AromaEvent* event, void* user_data)
{
    if (event->event_type == EVENT_TYPE_CUSTOM) {
        printf("[CUSTOM EVENT] Type: %u\n", event->data.custom.custom_type);
        return true;
    }
    return false;
}

void window_update_callback(size_t window_id, void *data)
{
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    
    if (!gfx || !platform) return;

    // Clear screen
    gfx->clear(window_id, 0xF0F0F0);
    
    // Draw UI elements
    AromaNode** buttons = (AromaNode**)data;
    if (buttons) {
        aroma_button_draw(buttons[0], window_id);
        aroma_button_draw(buttons[1], window_id);
    }

    platform->swap_buffers(window_id);
}

int main()
{
    printf("=== Aroma UI Event System Example ===\n\n");
    
    // Initialize systems
    printf("Initializing systems...\n");
    aroma_memory_system_init();
    __node_system_init();
    aroma_event_system_init();
    
    aroma_backend_abi.get_platform_interface()->initialize();
    
    // Create main window
    printf("Creating window...\n");
    AromaNode* main_window = aroma_window_create(
        "Aroma Event System Example",
        100, 100, 800, 600
    );
    
    printf("Window created (node_id: %llu)\n\n", main_window->node_id);
    
    // Create buttons
    printf("Creating buttons...\n");
    AromaNode* button1 = aroma_button_create(main_window, "Button 1", 100, 100, 150, 50);
    AromaNode* button2 = aroma_button_create(main_window, "Button 2", 300, 100, 150, 50);
    
    if (button1) {
        aroma_button_set_colors(button1, 0x0078D4, 0x107C10, 0x004B50, 0xFFFFFF);
    }
    
    if (button2) {
        aroma_button_set_colors(button2, 0xFF6B35, 0xFF8C42, 0xD84315, 0xFFFFFF);
    }
    
    printf("Buttons created\n\n");
    
    // Subscribe to events
    printf("Subscribing to events...\n");
    
    // Button 1 events
    aroma_event_subscribe(
        button1->node_id,
        EVENT_TYPE_MOUSE_CLICK,
        on_button_event,
        (void*)"Button 1",
        100
    );
    
    aroma_event_subscribe(
        button1->node_id,
        EVENT_TYPE_MOUSE_ENTER,
        on_button_event,
        (void*)"Button 1",
        100
    );
    
    aroma_event_subscribe(
        button1->node_id,
        EVENT_TYPE_MOUSE_EXIT,
        on_button_event,
        (void*)"Button 1",
        100
    );
    
    // Button 2 events
    aroma_event_subscribe(
        button2->node_id,
        EVENT_TYPE_MOUSE_CLICK,
        on_button_event,
        (void*)"Button 2",
        100
    );
    
    // Window events
    aroma_event_subscribe(
        main_window->node_id,
        EVENT_TYPE_KEY_PRESS,
        on_window_event,
        NULL,
        50
    );
    
    aroma_event_subscribe(
        main_window->node_id,
        EVENT_TYPE_MOUSE_MOVE,
        on_window_event,
        NULL,
        50
    );
    
    // Custom event
    aroma_event_subscribe(
        main_window->node_id,
        EVENT_TYPE_CUSTOM,
        on_custom_event,
        NULL,
        100
    );
    
    printf("Event subscriptions complete\n\n");
    
    // Test custom event
    printf("Dispatching custom event...\n");
    AromaEvent* custom_evt = aroma_event_create_custom(
        main_window->node_id,
        42,  // Custom event type
        NULL,
        NULL
    );
    if (custom_evt) {
        aroma_event_dispatch(custom_evt);
        aroma_event_destroy(custom_evt);
    }
    printf("\n");
    
    // Set up rendering
    AromaNode* buttons[2] = { button1, button2 };
    aroma_backend_abi.get_platform_interface()->set_window_update_callback(
        window_update_callback,
        (void*)buttons
    );
    
    // Print scene graph
    __print_node_tree(main_window);
    
    printf("\n=== Event Loop Running ===\n");
    printf("Try interacting with buttons or press ESC to exit\n\n");
    
    // Test event queueing
    printf("Queuing test events...\n");
    for (int i = 0; i < 3; i++) {
        AromaEvent* evt = aroma_event_create_mouse(
            EVENT_TYPE_MOUSE_CLICK,
            button1->node_id,
            100 + i*10, 100,
            0
        );
        if (evt) {
            aroma_event_queue(evt);
        }
    }
    
    // Process queued events
    printf("Processing queued events...\n\n");
    aroma_event_process_queue();
    
    // Main event loop
    while (aroma_backend_abi.get_platform_interface()->run_event_loop()) {
        usleep(16000);  // ~60 FPS
    }
    
    // Cleanup
    printf("\n=== Shutting Down ===\n");
    if (button1) aroma_button_destroy(button1);
    if (button2) aroma_button_destroy(button2);
    aroma_event_system_shutdown();
    __node_system_destroy();
    
    printf("Application closed successfully\n");
    return 0;
}
