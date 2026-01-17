#include "widgets/aroma_window.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include <stdlib.h>
#include <string.h>

AromaNode* aroma_window_create(const char* title, int x, int y, int width, int height)
{
    AromaWindow* node = (AromaWindow*)aroma_widget_alloc(sizeof(AromaWindow));
    if (!node)
    {
        return NULL;
    }
    
    AromaNode* scene_node = (AromaNode*) __create_node(NODE_TYPE_ROOT, NULL, node);
    if(!scene_node)
    {
        __slab_pool_free(&global_memory_system.node_pool, node);
        return NULL;
    }
    
    AromaPlatformInterface* platform_interface = aroma_backend_abi.get_platform_interface();
    node->window_id = platform_interface->create_window(title, x, y, width, height);

    return scene_node;
}