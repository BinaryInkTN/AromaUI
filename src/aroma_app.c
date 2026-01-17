#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "aroma_slab_alloc.h"
#include "aroma_node.h"
#include "widgets/aroma_window.h"
#include <unistd.h>
#include <stdio.h>


/**
 * Aroma UI Example Application
 * 
 * This example demonstrates:
 * - Window creation with the Aroma framework
 * - Scene graph initialization and management
 * - Main event loop handling
 * - Proper resource cleanup
 */

void window_update_callback(size_t window_id, void *data)
{
    printf("Window %zu update callback triggered.\n", window_id);
    
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) {
        printf("Graphics interface not available");
        return;
    }
    
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if(!platform)
    {
        printf("Platform interface not available");
        return;
    }

    gfx->clear(window_id, 0xFFFFFF); // Clear to white background
    gfx->fill_rectangle(
        window_id,
        50, 50, 200, 200,
        0xFF0000,
        false,
        0.0f
    );
    platform->swap_buffers(window_id);

}


int main()
{
    printf("Initializing Aroma UI Framework...\n");
    aroma_memory_system_init();
    __node_system_init();
    
    aroma_backend_abi.get_platform_interface()->initialize();
    
    printf("Creating main window...\n");
    AromaNode* main_window = aroma_window_create(
        "Aroma Scene Graph Example", 
        100,     // x position
        100,     // y position
        800,     // width
        600      // height
    );
    

    printf("Window created successfully (Scene node ID: %llu)\n", main_window->node_id);
    
    aroma_backend_abi.get_platform_interface()->set_window_update_callback(
        window_update_callback, NULL); // No custom update callback for now

    // Debug: Print the scene graph structure
    // This shows the root window node and any child nodes in a tree format
    __print_node_tree(main_window);
    
    printf("Running event loop...\n");

    // Main event loop
    // The scene graph manages all UI nodes within the window
    // This runs until the window is closed or shutdown is requested

    while (aroma_backend_abi.get_platform_interface()->run_event_loop())
    {
        // Frame timing: 16ms = ~60 FPS
        usleep(16000);
    }

    // Cleanup
    printf("Shutting down application...\n");
    __node_system_destroy();
    
    printf("Application closed successfully.\n");
    return 0;
}
