#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "core/aroma_common.h"
#include "aroma_ui.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
static bool __map_event_handler_global(AromaEvent* event, void* user_data);
static bool __map_event_handler_global(AromaEvent* event, void* user_data);


static bool __map_event_handler_global(AromaEvent* event, void* user_data);

#include <stdlib.h>
#include <curl/curl.h>

#define TILE_CACHE_DIR "/tmp/aroma_tiles"
#define MAX_TILES_MEM 128
#define TILE_SIZE 256

typedef struct {
    int z, x, y;
    bool is_dark;
    unsigned int texture_id;
    bool is_loading;
    bool is_ready;
    bool valid;
    time_t last_used;
    char filepath[256];
} MapTile;

struct AromaMapExtra {
    MapTile tiles[MAX_TILES_MEM];
    double center_px_x;
    double center_px_y;
    int zoom;
    AromaNode* node_ptr;
    uint64_t root_id;
};


typedef struct {
    int z, x, y;
    bool is_dark;
    char filepath[256];
} TileRequest;

#define MAX_QUEUE 256
static TileRequest fetch_queue[MAX_QUEUE];
static int queue_head = 0;
static int queue_tail = 0;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

static pthread_t worker_thread;
static bool worker_running = false;
static bool curl_initialized = false;

static size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

static void* tile_fetch_worker(void* arg) {
    while (worker_running) {
        TileRequest req;
        bool has_req = false;
        
        pthread_mutex_lock(&queue_mutex);
        while (queue_head == queue_tail && worker_running) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            pthread_cond_timedwait(&queue_cond, &queue_mutex, &ts);
        }
        if (!worker_running) {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        if (queue_head != queue_tail) {
            req = fetch_queue[queue_head];
            queue_head = (queue_head + 1) % MAX_QUEUE;
            has_req = true;
        }
        pthread_mutex_unlock(&queue_mutex);
        
        if (has_req) {
            
            if (access(req.filepath, F_OK) != -1) {
                continue;
            }
            
            char url[512];
            if (req.is_dark) {
                snprintf(url, sizeof(url), "https://a.basemaps.cartocdn.com/dark_all/%d/%d/%d.png", req.z, req.x, req.y);
            } else {
                snprintf(url, sizeof(url), "https://tile.openstreetmap.org/%d/%d/%d.png", req.z, req.x, req.y);
            }
            
            char tmp_path[512];
            snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", req.filepath);
            
            CURL *curl = curl_easy_init();
            if (curl) {
                FILE *fp = fopen(tmp_path, "wb");
                if (fp) {
                    curl_easy_setopt(curl, CURLOPT_URL, url);
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
                    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AromaUI/1.0");
                    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
                    
                    CURLcode res = curl_easy_perform(curl);
                    fclose(fp);
                    
                    if (res == CURLE_OK) {
                        rename(tmp_path, req.filepath);
                        // Wake up event loop to redraw map immediately
                        aroma_ui_request_redraw(NULL);
                    } else {
                        unlink(tmp_path);
                    }
                }
                curl_easy_cleanup(curl);
            }
        }
    }
    return NULL;
}

static void request_tile_download(int z, int x, int y, bool is_dark, const char* filepath) {
    pthread_mutex_lock(&queue_mutex);
    int next_tail = (queue_tail + 1) % MAX_QUEUE;
    if (next_tail != queue_head) {
        
        bool exists = false;
        for (int i = queue_head; i != queue_tail; i = (i + 1) % MAX_QUEUE) {
            if (fetch_queue[i].z == z && fetch_queue[i].x == x && fetch_queue[i].y == y && fetch_queue[i].is_dark == is_dark) {
                exists = true; break;
            }
        }
        if (!exists) {
            fetch_queue[queue_tail].z = z;
            fetch_queue[queue_tail].x = x;
            fetch_queue[queue_tail].y = y;
            fetch_queue[queue_tail].is_dark = is_dark;
            strncpy(fetch_queue[queue_tail].filepath, filepath, 255);
            queue_tail = next_tail;
            pthread_cond_signal(&queue_cond);
        }
    }
    pthread_mutex_unlock(&queue_mutex);
}


static void unload_old_zoom_tiles(struct AromaMapExtra* extra) {
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->unload_image) return;
    for (int i=0; i<MAX_TILES_MEM; i++) {
        if (extra->tiles[i].valid && extra->tiles[i].z != extra->zoom) {
            if (extra->tiles[i].is_ready && extra->tiles[i].texture_id != 0) gfx->unload_image(extra->tiles[i].texture_id);
            extra->tiles[i].valid = false;
            extra->tiles[i].is_ready = false;
            extra->tiles[i].is_loading = false;
            extra->tiles[i].texture_id = 0;
        }
    }
}
static bool __map_event_handler(AromaEvent* event, void* user_data) {
    if (!event || !event->target_node || !event->target_node->node_widget_ptr) return false;
    AromaMap* map = (AromaMap*)event->target_node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)user_data;
    if (!extra) return false;

    LOG_INFO("MAP EVENT %d (Drag: %d, x: %d y: %d rx: %d ry: %d rw: %d rh: %d)", 
        event->event_type, map->is_dragging, event->data.mouse.x, event->data.mouse.y, map->rect.x, map->rect.y, map->rect.width, map->rect.height);
    
    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_DOUBLE_CLICK:
            if (event->data.mouse.x >= map->rect.x && event->data.mouse.x <= map->rect.x + map->rect.width &&
                event->data.mouse.y >= map->rect.y && event->data.mouse.y <= map->rect.y + map->rect.height) {
                
        if (extra->zoom < 18) {
            extra->zoom++;
            extra->center_px_x *= 2.0;
            extra->center_px_y *= 2.0;
            unload_old_zoom_tiles(extra);
        }

                aroma_node_invalidate(event->target_node);
                return true;
            }
            break;

        case EVENT_TYPE_MOUSE_CLICK:
            if (event->data.mouse.x >= map->rect.x && event->data.mouse.x <= map->rect.x + map->rect.width &&
                event->data.mouse.y >= map->rect.y && event->data.mouse.y <= map->rect.y + map->rect.height) {
                map->is_dragging = true;
                map->last_mouse_x = event->data.mouse.x;
                map->last_mouse_y = event->data.mouse.y;
                aroma_node_invalidate(event->target_node);
                return true;
            }
            break;
            
        case EVENT_TYPE_MOUSE_MOVE:
            if (map->is_dragging) {
                if (event->data.mouse.x < map->rect.x || event->data.mouse.x > map->rect.x + map->rect.width ||
                    event->data.mouse.y < map->rect.y || event->data.mouse.y > map->rect.y + map->rect.height) {
                    map->is_dragging = false;
                    aroma_node_invalidate(event->target_node);
                    return true;
                }
                
                int dx = event->data.mouse.x - map->last_mouse_x;
                int dy = event->data.mouse.y - map->last_mouse_y;
                
                
                extra->center_px_x -= dx;
                extra->center_px_y -= dy;
                
                map->last_mouse_x = event->data.mouse.x;
                map->last_mouse_y = event->data.mouse.y;
                
                aroma_node_invalidate(event->target_node);
                return true;
            }
            break;
            
        case EVENT_TYPE_MOUSE_EXIT:
        case EVENT_TYPE_MOUSE_RELEASE:
            if (map->is_dragging) {
                map->is_dragging = false;
                aroma_node_invalidate(event->target_node);
                return true;
            }
            break;

        default:
            break;
    }
    return false;
}

static void __map_draw(AromaNode* node, size_t window_id) {
    if (!node || !node->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (!extra) return;


    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    
    AromaTheme theme = aroma_ui_get_theme();
    uint32_t bg_color = theme.colors.surface;
    uint32_t grid_color = theme.colors.border;
    
    
    uint8_t r = (bg_color >> 16) & 0xFF;
    uint8_t g = (bg_color >> 8) & 0xFF;
    uint8_t b = bg_color & 0xFF;
    bool theme_is_dark = ((r * 299 + g * 587 + b * 114) / 1000) < 128;

    gfx->fill_rectangle(window_id, map->rect.x, map->rect.y, map->rect.width, map->rect.height, bg_color, false, 0.0f);

    gfx->graphics_set_clip(map->rect.x, map->rect.y, map->rect.width, map->rect.height);

    int z = extra->zoom;
    double center_x = extra->center_px_x;
    double center_y = extra->center_px_y;

    
    double max_px = (double)((1 << z) * TILE_SIZE);
    
    
    
    
    double view_tl_x = center_x - map->rect.width / 2.0;
    double view_tl_y = center_y - map->rect.height / 2.0;
    
    int tx_start = (int)floor(view_tl_x / TILE_SIZE);
    int ty_start = (int)floor(view_tl_y / TILE_SIZE);
    int tx_end = (int)floor((view_tl_x + map->rect.width) / TILE_SIZE) + 1;
    int ty_end = (int)floor((view_tl_y + map->rect.height) / TILE_SIZE) + 1;
    


    time_t now = time(NULL);

    for (int y = ty_start; y <= ty_end; y++) {
        for (int x = tx_start; x <= tx_end; x++) {
            
            if (y < 0 || y >= (1<<z)) continue;
            int wrapped_x = (x % (1<<z) + (1<<z)) % (1<<z); 

            
            char filepath[256];
            snprintf(filepath, sizeof(filepath), "%s/osm_%s_%d_%d_%d.png", TILE_CACHE_DIR, theme_is_dark ? "dark" : "light", z, wrapped_x, y);

            MapTile* found_tile = NULL;
            int oldest_idx = -1;
            time_t oldest_time = now + 1;

            for (int i=0; i<MAX_TILES_MEM; i++) {
                if (extra->tiles[i].valid) {
                    if (extra->tiles[i].z == z && extra->tiles[i].x == wrapped_x && extra->tiles[i].y == y) {
                        found_tile = &extra->tiles[i];
                        extra->tiles[i].last_used = now;
                        break;
                    }
                    if (extra->tiles[i].last_used < oldest_time) {
                        oldest_time = extra->tiles[i].last_used;
                        oldest_idx = i;
                    }
                } else {
                    oldest_idx = i;
                    oldest_time = 0;
                }
            }

            if (!found_tile && oldest_idx != -1) {
                
                found_tile = &extra->tiles[oldest_idx];
                if (found_tile->valid && found_tile->is_ready && found_tile->texture_id != 0 && gfx && gfx->unload_image) {
                    gfx->unload_image(found_tile->texture_id);
                }
                found_tile->valid = true;
                found_tile->z = z;
                found_tile->x = wrapped_x;
                found_tile->y = y;
                found_tile->is_loading = false;
                found_tile->is_ready = false;
                found_tile->last_used = now;
                found_tile->texture_id = 0;
            }

            if (found_tile && !found_tile->is_ready) {
                if (access(filepath, F_OK) != -1) {
                    
                    if (found_tile->is_loading) {
                        found_tile->is_loading = false;
                    }
                    if (gfx && gfx->load_image) {
                        found_tile->texture_id = gfx->load_image(filepath);
                        if (found_tile->texture_id != 0) {
                            found_tile->is_ready = true;
                        }
                    }
                } else if (!found_tile->is_loading) {
                    
                    found_tile->is_loading = true;
                    request_tile_download(z, wrapped_x, y, theme_is_dark, filepath);
                }
            }

            int draw_x = map->rect.x + (int)(x * TILE_SIZE - view_tl_x);
            int draw_y = map->rect.y + (int)(y * TILE_SIZE - view_tl_y);

            if (found_tile && found_tile->is_ready && gfx && gfx->draw_image) {
                gfx->draw_image(window_id, draw_x, draw_y, TILE_SIZE, TILE_SIZE, found_tile->texture_id);
            } else {
                
                gfx->draw_hollow_rectangle(window_id, draw_x, draw_y, TILE_SIZE, TILE_SIZE, grid_color, 1, false, 0.0f);
            }
        }
    }
    
    if (theme_is_dark) {
        // No need for software overlay, using dark tiles
    }

    gfx->graphics_clear_clip();
}

void aroma_map_destroy(AromaNode* node) {
    if (!node || !node->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    
    if (extra && extra->root_id) {
        aroma_event_unsubscribe(extra->root_id, EVENT_TYPE_KEY_PRESS, __map_event_handler_global);
    }
    if (extra) {
        AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
        for (int i=0; i<MAX_TILES_MEM; i++) {
            if (extra->tiles[i].valid && extra->tiles[i].is_ready && gfx && gfx->unload_image) {
                gfx->unload_image(extra->tiles[i].texture_id);
            }
        }
        aroma_widget_free(extra);
        map->extra = NULL;
    }
    
    aroma_widget_free(node->node_widget_ptr);
    node->node_widget_ptr = NULL;
    __destroy_node(node);
}

static bool __map_event_handler_global(AromaEvent* event, void* user_data) {
    if (!event || !user_data) return false;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)user_data;

    if (event->event_type == EVENT_TYPE_KEY_PRESS) {
        
        if (event->data.key.key_code == 'z' || event->data.key.key_code == 'Z' || event->data.key.key_code == '=') {
            
        if (extra->zoom < 18) {
            extra->zoom++;
            extra->center_px_x *= 2.0;
            extra->center_px_y *= 2.0;
            unload_old_zoom_tiles(extra);
        }

            if (extra->node_ptr) aroma_node_invalidate(extra->node_ptr);
            return true;
        }
        else if (event->data.key.key_code == 'x' || event->data.key.key_code == 'X' || event->data.key.key_code == '-') {
            if (extra->zoom > 2) {
                extra->zoom--;
                extra->center_px_x /= 2.0;
                extra->center_px_y /= 2.0;
                unload_old_zoom_tiles(extra);
            }
            if (extra->node_ptr) aroma_node_invalidate(extra->node_ptr);
            return true;
        }
    }
    return false;
}

void aroma_map_zoom_in(AromaNode* node) {
    if (!node || !node->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (!extra) return;

    
        if (extra->zoom < 18) {
            extra->zoom++;
            extra->center_px_x *= 2.0;
            extra->center_px_y *= 2.0;
            unload_old_zoom_tiles(extra);
        }

    aroma_node_invalidate(node);
}

void aroma_map_zoom_out(AromaNode* node) {
    if (!node || !node->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (!extra) return;

    if (extra->zoom > 2) {
        extra->zoom--;
        extra->center_px_x /= 2.0;
        extra->center_px_y /= 2.0;
        unload_old_zoom_tiles(extra);
    }
    aroma_node_invalidate(node);
}

AromaNode* aroma_map_create(AromaNode* parent, int x, int y, int width, int height) {
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        curl_initialized = true;
        mkdir(TILE_CACHE_DIR, 0777);
        worker_running = true;
        pthread_create(&worker_thread, NULL, tile_fetch_worker, NULL);
        pthread_detach(worker_thread);
    }

    AromaMap* map = (AromaMap*)aroma_widget_alloc(sizeof(AromaMap));
    if (!map) return NULL;
    
    memset(map, 0, sizeof(AromaMap));
    map->rect.x = x;
    map->rect.y = y;
    map->rect.width = width;
    map->rect.height = height;
    map->zoom = 2; 

    struct AromaMapExtra* extra = aroma_widget_alloc(sizeof(struct AromaMapExtra));
    memset(extra, 0, sizeof(struct AromaMapExtra));
    
    extra->zoom = 6; 
    
    extra->center_px_x = 8623.0;
    extra->center_px_y = 6545.0;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, map);
    if (node) extra->node_ptr = node;

    
    AromaNode* root = aroma_event_get_root();
    extra->root_id = root ? root->node_id : 0;
    
    if (extra->root_id) {
        aroma_event_subscribe(extra->root_id, EVENT_TYPE_KEY_PRESS, __map_event_handler_global, extra, 90);
    }

    if (!node) {
        aroma_widget_free(extra);
        aroma_widget_free(map);
        return NULL;
    }
    
    node->draw_cb = __map_draw;
    node->destroy_cb = aroma_map_destroy;
    map->extra = extra;
    
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __map_event_handler, extra, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_DOUBLE_CLICK, __map_event_handler, extra, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, __map_event_handler, extra, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_MOVE, __map_event_handler, extra, 80);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_EXIT, __map_event_handler, extra, 80);

    return node;
}
