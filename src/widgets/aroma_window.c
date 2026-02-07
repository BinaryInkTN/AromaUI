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
    node->rect.x = x;
    node->rect.y = y;
    node->rect.width = width;
    node->rect.height = height;

    return scene_node;
}

void aroma_window_get_size(AromaNode* window_node, int* width, int* height) {
    if (!window_node || window_node->node_type != NODE_TYPE_ROOT || !window_node->node_widget_ptr) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    AromaWindow* win = (AromaWindow*)window_node->node_widget_ptr;
    AromaPlatformInterface* platform_interface = aroma_backend_abi.get_platform_interface();
    platform_interface->get_window_size(win->window_id, width, height);
}

void aroma_window_set_fullscreen(AromaNode* window_node, bool enable) {
    if (!window_node || window_node->node_type != NODE_TYPE_ROOT || !window_node->node_widget_ptr) {
        return;
    }
    AromaWindow* win = (AromaWindow*)window_node->node_widget_ptr;
    AromaPlatformInterface* platform_interface = aroma_backend_abi.get_platform_interface();
    if (platform_interface->set_fullscreen) {
        platform_interface->set_fullscreen(win->window_id, enable);
    }
}