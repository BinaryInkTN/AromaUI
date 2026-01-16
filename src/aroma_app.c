#include "backends/aroma_backend_interface.h"
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

int main()
{
    printf("Initializing Aroma UI Framework...\n");
    aroma_memory_system_init();
    __node_system_init();
    
    aroma_platform_glps.initialize();

    printf("Creating main window...\n");
    AromaNode* main_window = aroma_window_create(
        "Aroma Scene Graph Example", 
        100,     // x position
        100,     // y position
        800,     // width
        600      // height
    );
    

    printf("Window created successfully (Scene node ID: %llu)\n", main_window->node_id);
    
    // Debug: Print the scene graph structure
    // This shows the root window node and any child nodes in a tree format
    __print_node_tree(main_window);
    
    printf("Running event loop...\n");

    // Main event loop
    // The scene graph manages all UI nodes within the window
    // This runs until the window is closed or shutdown is requested
    while (aroma_platform_glps.run_event_loop())
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
