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
#include "core/aroma_event.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "core/aroma_logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef __ANDROID__
#include "aroma_android.h"
#endif

static bool window_resize_handler(AromaEvent* event, void* user_data) {
    AromaNode* window_node = (AromaNode*)user_data;
    if (event->event_type == EVENT_TYPE_WINDOW_RESIZE) {
        int w = event->data.resize.width;
        int h = event->data.resize.height;        
        LOG_INFO("Handling window resize event for node %lu: new size %dx%d", window_node->node_id, w, h);
        aroma_node_update_layout(window_node, 0, 0, w, h);
        return true;
    }
    return false;
}

 AromaNode* aroma_window_create(const char* title, int x, int y, int width, int height)
 {
     printf("[WIN] Creating window: title=%s %dx%d\n", title ? title : "(null)", width, height);
     AromaWindow* node = (AromaWindow*)aroma_widget_alloc(sizeof(AromaWindow));
     if (!node)
     {
         printf("[WIN] widget_alloc failed\n");
         return NULL;
     }
#ifdef __ANDROID__
    x = aroma_android_dp_to_px(x);
    y = aroma_android_dp_to_px(y);
    width = aroma_android_dp_to_px(width);
    height = aroma_android_dp_to_px(height);
#endif
    
      AromaNode* scene_node = (AromaNode*) __create_node(NODE_TYPE_ROOT, NULL, node);
      if(!scene_node)
      {
          printf("[WIN] __create_node failed\n");
#ifdef ESP32
          __slab_pool_free(&global_memory_system.node_pool, node);
#else
          free(node);
#endif
          return NULL;
      }
    
    AromaPlatformInterface* platform_interface = aroma_backend_abi.get_platform_interface();
    node->window_id = platform_interface->create_window(title, x, y, width, height);
    printf("[WIN] platform->create_window returned id=%hu\n", node->window_id);
    node->rect.x = x;
    node->rect.y = y;
    
    // Attempt to sync with actual platform window size immediately
    if (platform_interface->get_window_size) {
        int pw = 0, ph = 0;
        platform_interface->get_window_size(node->window_id, &pw, &ph);
        if (pw > 0 && ph > 0) {
            width = pw;
            height = ph;
        }
    }
    
    node->rect.width = width;
    node->rect.height = height;

    
    aroma_event_subscribe(scene_node->node_id, EVENT_TYPE_WINDOW_RESIZE, window_resize_handler, scene_node, 0);

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

void aroma_window_destroy(AromaNode* window_node) {
    if (!window_node || window_node->node_type != NODE_TYPE_ROOT) {
        return;
    }
    
    // Unsubscribe from window-level events
    aroma_event_unsubscribe(window_node->node_id, EVENT_TYPE_WINDOW_RESIZE, window_resize_handler);

    // Completely destroy the node tree recursively. 
    // __destroy_node_tree will trigger all child destroy_callbacks.
    extern void __destroy_node_tree(AromaNode*);
    __destroy_node_tree(window_node);
}
