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

#include "widgets/aroma_image.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "backends/aroma_abi.h"
#include "widgets/aroma_container.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "core/aroma_common.h"
#include <string.h>
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define AROMA_IMAGE_PATH_MAX 1024

typedef struct   AromaImage {
    AromaRect rect;
    unsigned int texture_id;
    char image_path[AROMA_IMAGE_PATH_MAX];
    bool owns_texture;

    bool (*on_click)(AromaNode*, void*);
    bool (*on_hover)(AromaNode*, void*);
    void *user_data;
    int active_pointer_id;
    bool events_registered;
} AromaImage;

static void __image_destroy_texture(AromaImage* image)
{
    if (image->texture_id != 0 && image->owns_texture) {
        AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
        if (gfx && gfx->unload_image) {
            gfx->unload_image(image->texture_id);
        }
        image->texture_id = 0;
    }
}

static unsigned int __image_load_texture(const char* image_path)
{
    if (!image_path || strlen(image_path) == 0) {
        LOG_WARNING("Empty image path provided");
        return 0;
    }
    
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->load_image) {
        LOG_ERROR("Graphics interface not available or missing load_image function");
        return 0;
    }
    
    return gfx->load_image(image_path);
}

static bool aroma_image_point_in_bounds(AromaImage* image, int x, int y)
{
    if (!image) return false;
    return (x >= image->rect.x && x <= (image->rect.x + image->rect.width) &&
            y >= image->rect.y && y <= (image->rect.y + image->rect.height));
}

static bool __image_default_event_handler(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;
    AromaImage* image = (AromaImage*)event->target_node->node_widget_ptr;
    if (!image) return false;

    (void)user_data;

    int x = 0, y = 0;
    bool is_release = false;
    bool handle_click = false;
    bool handle_hover = false;
   
    int adjusted_x = event->event_type == EVENT_TYPE_TOUCH_DOWN || event->event_type == EVENT_TYPE_TOUCH_UP || event->event_type == EVENT_TYPE_TOUCH_MOVE
                     ? event->data.touch.x : event->data.mouse.x;
    int adjusted_y = event->event_type == EVENT_TYPE_TOUCH_DOWN || event->event_type == EVENT_TYPE_TOUCH_UP || event->event_type == EVENT_TYPE_TOUCH_MOVE
                     ? event->data.touch.y : event->data.mouse.y;

    AromaNode *cur = event->target_node->parent_node;
    while (cur) {
    if (aroma_container_is_scrollable(cur)) {
        int scroll_x, scroll_y;
        aroma_container_get_scroll(cur, &scroll_x, &scroll_y);
        adjusted_x += scroll_x;
        adjusted_y += scroll_y;
    }
    cur = cur->parent_node;
    }


    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_CLICK:
            if (image->active_pointer_id != -1) return false;
            x = event->data.mouse.x; y = event->data.mouse.y;
            return aroma_image_point_in_bounds(image, adjusted_x, adjusted_y);

        case EVENT_TYPE_MOUSE_RELEASE:
            if (image->active_pointer_id != -1) return false;
            x = event->data.mouse.x; y = event->data.mouse.y;
            is_release = true;
            handle_click = true;
            break;

        case EVENT_TYPE_MOUSE_MOVE:
        case EVENT_TYPE_MOUSE_ENTER:
            if (image->active_pointer_id != -1) return false;
            x = event->data.mouse.x; y = event->data.mouse.y;
            handle_hover = true;
            break;

        case EVENT_TYPE_MOUSE_EXIT:
            return false;

        case EVENT_TYPE_TOUCH_DOWN:
            if (image->active_pointer_id != -1) return false;
            x = event->data.touch.x; y = event->data.touch.y;
            if (!aroma_image_point_in_bounds(image, adjusted_x, adjusted_y)) return false;
            image->active_pointer_id = event->data.touch.id;
            return true;

        case EVENT_TYPE_TOUCH_UP:
            if (image->active_pointer_id != event->data.touch.id) return false;
            image->active_pointer_id = -1;
            x = event->data.touch.x; y = event->data.touch.y;
            is_release = true;
            handle_click = true;
            break;

        case EVENT_TYPE_TOUCH_MOVE:
            if (image->active_pointer_id != event->data.touch.id) return false;
            return true;

        default:
            return false;
    }

    bool in_bounds = aroma_image_point_in_bounds(image, x, y);

    if (handle_hover && in_bounds && image->on_hover)
    {
        LOG_INFO("Image hovered (node_id=%llu)", (unsigned long long)event->target_node->node_id);
        image->on_hover(event->target_node, image->user_data);
    }

    if (handle_click && is_release && in_bounds && image->on_click)
    {
        LOG_INFO("Image clicked (node_id=%llu)", (unsigned long long)event->target_node->node_id);
        image->on_click(event->target_node, image->user_data);
    }

    return in_bounds;
}

static void __image_ensure_events_registered(AromaNode* image_node, AromaImage* image)
{
    if (image->events_registered) return;

    aroma_event_subscribe(image_node->node_id, EVENT_TYPE_MOUSE_CLICK, __image_default_event_handler, NULL, 90);
    aroma_event_subscribe(image_node->node_id, EVENT_TYPE_MOUSE_RELEASE, __image_default_event_handler, NULL, 90);
    aroma_event_subscribe(image_node->node_id, EVENT_TYPE_MOUSE_MOVE, __image_default_event_handler, NULL, 80);
    aroma_event_subscribe(image_node->node_id, EVENT_TYPE_MOUSE_ENTER, __image_default_event_handler, NULL, 80);
    aroma_event_subscribe(image_node->node_id, EVENT_TYPE_MOUSE_EXIT, __image_default_event_handler, NULL, 80);
    aroma_event_subscribe(image_node->node_id, EVENT_TYPE_TOUCH_DOWN, __image_default_event_handler, NULL, 90);
    aroma_event_subscribe(image_node->node_id, EVENT_TYPE_TOUCH_UP, __image_default_event_handler, NULL, 90);
    aroma_event_subscribe(image_node->node_id, EVENT_TYPE_TOUCH_MOVE, __image_default_event_handler, NULL, 80);

    image->events_registered = true;
}

void aroma_image_draw(AromaNode* image_node, size_t window_id)
{
    if (!image_node || !image_node->node_widget_ptr) {
        LOG_ERROR("aroma_image_draw: Invalid image node for drawing (node=%p)", (void*)image_node);
        return;
    }
    
    if (aroma_node_is_hidden(image_node)) {
        return;
    }
    
    AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
    if (!image) {
        LOG_ERROR("aroma_image_draw: node->node_widget_ptr is NULL");
        return;
    }

    if (image->texture_id == 0) {
        LOG_INFO("aroma_image_draw: Skipping image draw - no texture loaded (node_id=%llu)", (unsigned long long)image_node->node_id);
        return;
    }
    
    if (image->rect.width <= 0 || image->rect.height <= 0) {
        LOG_INFO("Skipping image draw - invalid size: %dx%d", 
                  image->rect.width, image->rect.height);
        return;
    }
    
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->draw_image) {
        LOG_ERROR("aroma_image_draw: Graphics interface not available or missing draw_image function");
        return;
    }
    
    if (image->rect.width <= 0 || image->rect.height <= 0) {
        LOG_WARNING("aroma_image_draw: invalid rect size (%d x %d) for node_id=%llu", image->rect.width, image->rect.height, (unsigned long long)image_node->node_id);
        return;
    }

    gfx->draw_image(window_id,
                    image->rect.x, image->rect.y, 
                    image->rect.width, image->rect.height, image->texture_id );
    
    LOG_INFO("aroma_image_draw: Drew image node_id=%llu at (%d, %d) size %dx%d, texture ID: %u", 
              (unsigned long long)image_node->node_id,
              image->rect.x, image->rect.y, 
              image->rect.width, image->rect.height, 
              image->texture_id);
}
AromaNode* aroma_image_create(AromaNode* parent, const char* image_path, int x, int y, int width, int height)
{
    if (!parent) {
        LOG_ERROR("Invalid parent node for image widget");
        return NULL;
    }


#ifdef __ANDROID__
x = aroma_android_dp_to_px(x);
y = aroma_android_dp_to_px(y);
width = aroma_android_dp_to_px(width);
height = aroma_android_dp_to_px(height);
#endif


    AromaImage* image = (AromaImage*)aroma_widget_alloc(sizeof(AromaImage));
    if (!image) {
        LOG_ERROR("Failed to allocate memory for image widget");
        return NULL;
    }

    memset(image, 0, sizeof(AromaImage));
    image->rect.x = x;
    image->rect.y = y;
    image->rect.width = width;
    image->rect.height = height;
    image->texture_id = 0;
    image->owns_texture = true;
    image->active_pointer_id = -1;
    
    if (image_path) {
        strncpy(image->image_path, image_path, AROMA_IMAGE_PATH_MAX - 1);
        image->image_path[AROMA_IMAGE_PATH_MAX - 1] = '\0';
        image->texture_id = __image_load_texture(image_path);
        if (image->texture_id == 0) {
            LOG_WARNING("Failed to load image: %s", image_path);
        }
    }

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, image);
    if (!node) {
        __image_destroy_texture(image);
        aroma_widget_free(image);
        LOG_ERROR("Failed to create node for image widget");
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_image_draw);
    
    LOG_INFO("Created image widget at (%d, %d) size %dx%d, texture ID: %u", 
              x, y, width, height, image->texture_id);
    
    #ifdef ESP32
    aroma_node_invalidate(node);
    #endif

    return node;
}

AromaNode* aroma_image_create_from_memory(AromaNode* parent, unsigned char* data, size_t data_size, 
                                          int x, int y, int width, int height)
{
    if (!parent || !data || data_size == 0) {
        LOG_ERROR("Invalid parameters for memory image widget");
        return NULL;
    }

    AromaImage* image = (AromaImage*)aroma_widget_alloc(sizeof(AromaImage));
    if (!image) {
        LOG_ERROR("Failed to allocate memory for image widget");
        return NULL;
    }

    memset(image, 0, sizeof(AromaImage));
    image->rect.x = x;
    image->rect.y = y;
    image->rect.width = width;
    image->rect.height = height;
    image->owns_texture = true;
    image->image_path[0] = '\0'; 
    image->active_pointer_id = -1;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (gfx && gfx->load_image_from_memory) {
        image->texture_id = gfx->load_image_from_memory(data, data_size);
    }
    
    if (image->texture_id == 0) {
        LOG_ERROR("Failed to load image from memory");
        aroma_widget_free(image);
        return NULL;
    }

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, image);
    if (!node) {
        __image_destroy_texture(image);
        aroma_widget_free(image);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_image_draw);
       #ifdef ESP32
    aroma_node_invalidate(node);
    #endif
    
    LOG_INFO("Created memory image widget at (%d, %d) size %dx%d, texture ID: %u", 
              x, y, width, height, image->texture_id);
    
    return node;
}

AromaNode* aroma_image_create_from_texture(AromaNode* parent, unsigned int texture_id, 
                                           int x, int y, int width, int height, bool take_ownership)
{
    if (!parent || texture_id == 0) {
        LOG_ERROR("Invalid parameters for texture image widget");
        return NULL;
    }

    AromaImage* image = (AromaImage*)aroma_widget_alloc(sizeof(AromaImage));
    if (!image) {
        LOG_ERROR("Failed to allocate memory for image widget");
        return NULL;
    }

    memset(image, 0, sizeof(AromaImage));
    image->rect.x = x;
    image->rect.y = y;
    image->rect.width = width;
    image->rect.height = height;
    image->texture_id = texture_id;
    image->owns_texture = take_ownership;
    image->image_path[0] = '\0'; 
    image->active_pointer_id = -1;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, image);
    if (!node) {
        if (take_ownership) {
            __image_destroy_texture(image);
        }
        aroma_widget_free(image);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_image_draw);
    
    LOG_INFO("Created texture image widget at (%d, %d) size %dx%d, texture ID: %u", 
              x, y, width, height, texture_id);
    
    return node;
}

void aroma_image_set_source(AromaNode* image_node, const char* image_path)
{
    if (!image_node || !image_node->node_widget_ptr) {
        LOG_ERROR("Invalid image node");
        return;
    }
    
    AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
    
    __image_destroy_texture(image);
    
    // Load new texture
    if (image_path) {
        strncpy(image->image_path, image_path, AROMA_IMAGE_PATH_MAX - 1);
        image->texture_id = __image_load_texture(image_path);
        image->owns_texture = true;
    } else {
        image->image_path[0] = '\0';
        image->texture_id = 0;
        image->owns_texture = false;
    }
    
    aroma_node_invalidate(image_node);
    
    LOG_INFO("Set image source to: %s, texture ID: %u", image_path ? image_path : "(null)", image->texture_id);
}

void aroma_image_set_size(AromaNode* image_node, int width, int height)
{
    if (!image_node || !image_node->node_widget_ptr) {
        LOG_ERROR("Invalid image node");
        return;
    }
    
    if (width <= 0 || height <= 0) {
        LOG_WARNING("Invalid image size: %dx%d", width, height);
        return;
    }
    
    AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
    image->rect.width = width;
    image->rect.height = height;
    
    aroma_node_invalidate(image_node);
    
    LOG_INFO("Set image size to %dx%d", width, height);
}

void aroma_image_set_position(AromaNode* image_node, int x, int y)
{
    if (!image_node || !image_node->node_widget_ptr) {
        LOG_ERROR("Invalid image node");
        return;
    }
    
    AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
    image->rect.x = x;
    image->rect.y = y;
    
    aroma_node_invalidate(image_node);
    
    LOG_INFO("Set image position to (%d, %d)", x, y);
}

void aroma_image_get_size(AromaNode* image_node, int* width, int* height)
{
    if (!image_node || !image_node->node_widget_ptr || !width || !height) {
        LOG_ERROR("Invalid parameters for get_size");
        return;
    }
    
    AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
    *width = image->rect.width;
    *height = image->rect.height;
}

void aroma_image_get_position(AromaNode* image_node, int* x, int* y)
{
    if (!image_node || !image_node->node_widget_ptr || !x || !y) {
        LOG_ERROR("Invalid parameters for get_position");
        return;
    }
    
    AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
    *x = image->rect.x;
    *y = image->rect.y;
}

unsigned int aroma_image_get_texture_id(AromaNode* image_node)
{
    if (!image_node || !image_node->node_widget_ptr) {
        LOG_ERROR("Invalid image node");
        return 0;
    }
    
    AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
    return image->texture_id;
}

const char* aroma_image_get_source(AromaNode* image_node)
{
    if (!image_node || !image_node->node_widget_ptr) {
        LOG_ERROR("Invalid image node");
        return NULL;
    }
    
    AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
    return image->image_path;
}

void aroma_image_set_on_click(AromaNode* image_node, bool (*on_click)(AromaNode*, void*), void* user_data)
{
    if (!image_node || !image_node->node_widget_ptr) {
        LOG_ERROR("Invalid image node");
        return;
    }

    AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
    image->on_click = on_click;
    image->user_data = user_data;
    __image_ensure_events_registered(image_node, image);

    LOG_INFO("Image click callback registered (node_id=%llu)", (unsigned long long)image_node->node_id);
}

void aroma_image_set_on_hover(AromaNode* image_node, bool (*on_hover)(AromaNode*, void*), void* user_data)
{
    if (!image_node || !image_node->node_widget_ptr) {
        LOG_ERROR("Invalid image node");
        return;
    }

    AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
    image->on_hover = on_hover;
    image->user_data = user_data;
    __image_ensure_events_registered(image_node, image);

    LOG_INFO("Image hover callback registered (node_id=%llu)", (unsigned long long)image_node->node_id);
}

void aroma_image_destroy(AromaNode* image_node)
{
    if (!image_node) {
        LOG_ERROR("Invalid image node for destruction");
        return;
    }
    
    if (image_node->node_widget_ptr) {
        AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
        
        __image_destroy_texture(image);
        
        aroma_widget_free(image);
        image_node->node_widget_ptr = NULL;
        
        LOG_INFO("Destroyed image widget");
    }
    
    __destroy_node(image_node);
}