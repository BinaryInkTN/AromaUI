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
#include "aroma_dp.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef __EMSCRIPTEN__
#include <pthread.h>
#include "aroma_http.h"
#endif
#ifdef __ANDROID__
#include "aroma_android.h"
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#endif

#define AROMA_IMAGE_PATH_MAX 1024

#ifdef __ANDROID__
static inline int image_dp(int dp) { return aroma_android_dp_to_px(dp); }
static inline float image_dp_f(float dp) { return aroma_android_dp_to_px_f(dp); }
#else
static inline int image_dp(int dp) { return dp; }
static inline float image_dp_f(float dp) { return dp; }
#endif

typedef struct   AromaImage {
    AromaRect rect;
    unsigned int texture_id;
    char image_path[AROMA_IMAGE_PATH_MAX];
    bool owns_texture;
    float corner_radius;
    AromaImageScaleMode scale_mode;

    bool (*on_click)(AromaNode*, void*);
    bool (*on_hover)(AromaNode*, void*);
    void *user_data;
    int active_pointer_id;
    bool events_registered;
    bool remote_fetching;
    bool remote_fetch_done;
    char remote_cache[1024];
} AromaImage;

static unsigned int __image_load_texture(const char *image_path);
static void __image_destroy_texture(AromaImage *image);

#ifndef __EMSCRIPTEN__
typedef struct {
    AromaNode *node;
    char url[1024];
    char cache_path[1024];
} ImageFetchJob;

static void *__image_fetch_thread(void *arg)
{
    ImageFetchJob *job = (ImageFetchJob *)arg;
    if (job) {
        bool ok = aroma_http_fetch_to_file(job->url, job->cache_path);
        LOG_INFO("AROMA_TEST img_fetch=%d url=%.48s", ok ? 1 : 0, job->url);
        if (ok && job->node && job->node->node_widget_ptr) {
            AromaImage *image = (AromaImage *)job->node->node_widget_ptr;
            strncpy(image->remote_cache, job->cache_path, sizeof(image->remote_cache) - 1);
            image->remote_fetch_done = true;
            aroma_node_invalidate(job->node);
        }
        if (job->node) {
            AromaImage *image = (AromaImage *)job->node->node_widget_ptr;
            if (image) {
                image->remote_fetching = false;
            }
        }
        free(job);
    }
    return NULL;
}

static void __image_fetch_remote_native(AromaNode *node, const char *url)
{
    if (!node || !node->node_widget_ptr || !url || !url[0])
        return;
    AromaImage *image = (AromaImage *)node->node_widget_ptr;
    if (image->remote_fetching || image->texture_id != 0)
        return;
    char cache[1024];
    aroma_http_cache_path_for_url(url, cache, sizeof(cache));
    if (!cache[0])
        return;
    FILE *probe = fopen(cache, "rb");
    if (probe) {
        fclose(probe);
        unsigned int tex = __image_load_texture(cache);
        if (tex != 0) {
            __image_destroy_texture(image);
            image->texture_id = tex;
            image->owns_texture = true;
            aroma_node_invalidate(node);
            return;
        }
    }
    ImageFetchJob *job = (ImageFetchJob *)calloc(1, sizeof(ImageFetchJob));
    if (!job)
        return;
    job->node = node;
    strncpy(job->url, url, sizeof(job->url) - 1);
    strncpy(job->cache_path, cache, sizeof(job->cache_path) - 1);
    image->remote_fetching = true;
    image->remote_fetch_done = false;
    pthread_t tid;
    if (pthread_create(&tid, NULL, __image_fetch_thread, job) == 0) {
        pthread_detach(tid);
    } else {
        image->remote_fetching = false;
        free(job);
    }
}
#endif

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

static bool __image_is_remote_url(const char* path)
{
    return path && (strncmp(path, "http://", 7) == 0 ||
                    strncmp(path, "https://", 8) == 0);
}

#ifdef __EMSCRIPTEN__
typedef struct {
    AromaNode* node;
} ImageFetchReq;

static void __image_fetch_success(emscripten_fetch_t* fetch)
{
    ImageFetchReq* req = (ImageFetchReq*)fetch->userData;
    if (req && req->node && req->node->node_widget_ptr &&
        fetch->data && fetch->numBytes > 0) {
        AromaImage* image = (AromaImage*)req->node->node_widget_ptr;
        AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
        if (gfx && gfx->load_image_from_memory) {
            unsigned int tex = gfx->load_image_from_memory(
                (unsigned char*)fetch->data, (size_t)fetch->numBytes);
            if (tex != 0) {
                __image_destroy_texture(image);
                image->texture_id = tex;
                image->owns_texture = true;
                aroma_node_invalidate(req->node);
            } else {
                LOG_WARNING("Remote image decode failed");
            }
        }
    }
    if (req)
        free(req);
    emscripten_fetch_close(fetch);
}

static void __image_fetch_error(emscripten_fetch_t* fetch)
{
    ImageFetchReq* req = (ImageFetchReq*)fetch->userData;
    LOG_WARNING("Remote image fetch failed (code %d)", (int)fetch->status);
    if (req)
        free(req);
    emscripten_fetch_close(fetch);
}

static void __image_fetch_remote(AromaNode* node, const char* url)
{
    if (!node || !url)
        return;
    ImageFetchReq* req = (ImageFetchReq*)calloc(1, sizeof(ImageFetchReq));
    if (!req)
        return;
    req->node = node;
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = __image_fetch_success;
    attr.onerror = __image_fetch_error;
    attr.timeoutMSecs = 15000;
    attr.userData = req;
    emscripten_fetch(&attr, url);
}
#endif

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

#ifndef __EMSCRIPTEN__
    if (image->texture_id == 0 && image->remote_fetch_done && image->remote_cache[0]) {
        unsigned int tex = __image_load_texture(image->remote_cache);
        image->remote_fetch_done = false;
        if (tex != 0) {
            __image_destroy_texture(image);
            image->texture_id = tex;
            image->owns_texture = true;
            LOG_INFO("AROMA_TEST img_ready node=%llu", (unsigned long long)image_node->node_id);
        }
    }
#endif
    if (image->texture_id == 0) {
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

    int dx = image->rect.x, dy = image->rect.y;
    int dw = image->rect.width, dh = image->rect.height;
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    if (image->scale_mode != AROMA_IMAGE_SCALE_FILL &&
        gfx->get_image_size) {
        int tw = 0, th = 0;
        if (gfx->get_image_size(image->texture_id, &tw, &th) &&
            tw > 0 && th > 0) {
            if (image->scale_mode == AROMA_IMAGE_SCALE_FIT) {
                /* Whole image, aspect kept, centered in the rect. */
                float s = (float)dw / (float)tw;
                float s2 = (float)dh / (float)th;
                if (s2 < s) s = s2;
                int fw = (int)(tw * s), fh = (int)(th * s);
                if (fw < 1) fw = 1;
                if (fh < 1) fh = 1;
                dx += (dw - fw) / 2;
                dy += (dh - fh) / 2;
                dw = fw;
                dh = fh;
            } else {
                /* Cover: fill the rect, center-crop the source. */
                float s = (float)dw / (float)tw;
                float s2 = (float)dh / (float)th;
                if (s2 > s) s = s2;
                float sw = (float)dw / s, sh = (float)dh / s;
                float sx = ((float)tw - sw) / 2.0f;
                float sy = ((float)th - sh) / 2.0f;
                u0 = sx / (float)tw;
                v0 = sy / (float)th;
                u1 = (sx + sw) / (float)tw;
                v1 = (sy + sh) / (float)th;
            }
        }
    }

    if (gfx->draw_image_uv &&
        (u0 != 0.0f || v0 != 0.0f || u1 != 1.0f || v1 != 1.0f)) {
        gfx->draw_image_uv(window_id, dx, dy, dw, dh,
                           image->texture_id, image->corner_radius,
                           u0, v0, u1, v1);
    } else {
        gfx->draw_image(window_id,
                        dx, dy, dw, dh,
                        image->texture_id, image->corner_radius);
    }
    
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
#ifdef __EMSCRIPTEN__
    /* Remote URLs cannot be opened with fopen in the browser. Fetch them
       asynchronously and swap in the texture when the bytes arrive. */
    if (image_path && __image_is_remote_url(image_path) && image->texture_id == 0) {
        __image_fetch_remote(node, image->image_path);
    }
#endif
#ifndef __EMSCRIPTEN__
    if (image_path && __image_is_remote_url(image_path) && image->texture_id == 0) {
        __image_fetch_remote_native(node, image->image_path);
    }
#endif
    
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
#ifdef __EMSCRIPTEN__
        if (__image_is_remote_url(image_path) && image->texture_id == 0) {
            __image_fetch_remote(image_node, image->image_path);
        }
#endif
#ifndef __EMSCRIPTEN__
        if (__image_is_remote_url(image_path) && image->texture_id == 0) {
            image->remote_fetching = false;
            image->remote_fetch_done = false;
            image->remote_cache[0] = '\0';
            __image_fetch_remote_native(image_node, image->image_path);
        }
#endif
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
    /* Keep an existing radius inside the new bounds. */
    float max_r = (float)((width < height ? width : height) / 2);
    if (image->corner_radius > max_r)
        image->corner_radius = max_r;
    
    aroma_node_invalidate(image_node);
    
    LOG_INFO("Set image size to %dx%d", width, height);
}

static float __image_clamp_radius(AromaImage* image, float radius)
{
    if (!image)
        return 0.0f;
    if (radius < 0.0f)
        return 0.0f;
    int min_side = image->rect.width < image->rect.height
        ? image->rect.width : image->rect.height;
    float max_r = (float)min_side / 2.0f;
    if (max_r < 0.0f)
        max_r = 0.0f;
    return radius > max_r ? max_r : radius;
}

void aroma_image_set_corner_radius(AromaNode* image_node, float radius)
{
    if (!image_node || !image_node->node_widget_ptr) {
        LOG_ERROR("Invalid image node");
        return;
    }
    AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
    image->corner_radius = __image_clamp_radius(image, radius);
    aroma_node_invalidate(image_node);
    LOG_INFO("Set image corner radius to %.1f", image->corner_radius);
}

float aroma_image_get_corner_radius(AromaNode* image_node)
{
    if (!image_node || !image_node->node_widget_ptr) {
        LOG_ERROR("Invalid image node");
        return 0.0f;
    }
    AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
    return image->corner_radius;
}

void aroma_image_set_scale_mode(AromaNode* image_node, AromaImageScaleMode mode)
{
    if (!image_node || !image_node->node_widget_ptr) {
        LOG_ERROR("Invalid image node");
        return;
    }
    if (mode < AROMA_IMAGE_SCALE_FILL || mode > AROMA_IMAGE_SCALE_COVER) {
        LOG_WARNING("Invalid image scale mode %d, keeping current", (int)mode);
        return;
    }
    AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
    image->scale_mode = mode;
    aroma_node_invalidate(image_node);
    LOG_INFO("Set image scale mode to %d", (int)mode);
}

AromaImageScaleMode aroma_image_get_scale_mode(AromaNode* image_node)
{
    if (!image_node || !image_node->node_widget_ptr) {
        LOG_ERROR("Invalid image node");
        return AROMA_IMAGE_SCALE_FILL;
    }
    AromaImage* image = (AromaImage*)image_node->node_widget_ptr;
    return image->scale_mode;
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