/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "widgets/aroma_window.h"
#include "core/aroma_node.h"
#include "core/aroma_slab_alloc.h"
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