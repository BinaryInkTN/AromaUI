

#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "core/aroma_common.h"
#include "aroma_ui.h"
#include "aroma_font.h"
#include "aroma_ubuntu_font.h"
#include "aroma_timer.h"
#include "aroma_timer.h"

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
    uint64_t access_seq;
    char filepath[256];
} MapTile;

#define MAX_MARKERS 32
typedef struct {
    double lat;
    double lon;
    uint32_t color;
} MapMarker;

struct AromaMapExtra {
    MapTile tiles[MAX_TILES_MEM];
    double center_px_x;
    double center_px_y;
    int zoom;
    AromaNode* node_ptr;
    uint64_t root_id;
    uint64_t access_counter;
    MapMarker markers[MAX_MARKERS];
    int marker_count;
    AromaFont* font;

    AromaTimer* anim_timer;
    double velocity_x;
    double velocity_y;
    double display_zoom;
    double display_px_x;
    double display_px_y;
};


typedef struct {
    int z, x, y;
    bool is_dark;
    char filepath[256];
    uint64_t node_id;
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
                snprintf(url, sizeof(url), "https:
            } else {
                snprintf(url, sizeof(url), "https:
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
                        
                        AromaEvent *ev = aroma_event_create_custom(req.node_id, 999, NULL, NULL);
                        if (ev) aroma_event_queue(ev);
                    } else {
                        unlink(tmp_path);
                        
                        AromaEvent *ev = aroma_event_create_custom(req.node_id, 998, NULL, NULL);
                        if (ev) aroma_event_queue(ev);
                    }
                }
                curl_easy_cleanup(curl);
            }
        }
    }
    return NULL;
}

static bool request_tile_download(int z, int x, int y, bool is_dark, const char* filepath, uint64_t node_id) {
    bool queued = false;
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
            fetch_queue[queue_tail].node_id = node_id;
            strncpy(fetch_queue[queue_tail].filepath, filepath, 255);
            queue_tail = next_tail;
            pthread_cond_signal(&queue_cond);
        }
        queued = true;
    }
    pthread_mutex_unlock(&queue_mutex);
    return queued;
}




static void __map_anim_tick(void* user_data) {
    if (!user_data) return;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)user_data;
    if (!extra->node_ptr || !extra->node_ptr->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)extra->node_ptr->node_widget_ptr;
    
    bool changed = false;

    if (fabs(extra->display_zoom - extra->zoom) > 0.001) {
        extra->display_zoom += (extra->zoom - extra->display_zoom) * 0.15;
        changed = true;
    } else {
        extra->display_zoom = extra->zoom;
    }

    if (!map->is_dragging) {
        if (fabs(extra->velocity_x) > 0.1 || fabs(extra->velocity_y) > 0.1) {
            extra->center_px_x += extra->velocity_x;
            extra->center_px_y += extra->velocity_y;
            extra->velocity_x *= 0.90;
            extra->velocity_y *= 0.90;
            changed = true;
        } else {
            extra->velocity_x = 0;
            extra->velocity_y = 0;
        }
    } else {
        extra->velocity_x = 0;
        extra->velocity_y = 0;
    }

    double diff_x = extra->center_px_x - extra->display_px_x;
    double diff_y = extra->center_px_y - extra->display_px_y;
    if (fabs(diff_x) > 0.1 || fabs(diff_y) > 0.1) {
        extra->display_px_x += diff_x * 0.4;
        extra->display_px_y += diff_y * 0.4;
        changed = true;
    } else {
        extra->display_px_x = extra->center_px_x;
        extra->display_px_y = extra->center_px_y;
    }

    if (changed) {
        aroma_node_invalidate(extra->node_ptr);
    }
}

static void unload_old_zoom_tiles(struct AromaMapExtra* extra) {
    
    
    
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
            extra->display_px_x *= 2.0;

            extra->center_px_y *= 2.0;
            extra->display_px_y *= 2.0;

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
                extra->display_px_x -= dx;
                extra->display_px_y -= dy;
                
                extra->velocity_x = -dx * 0.5;
                extra->velocity_y = -dy * 0.5;
                
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

        case EVENT_TYPE_CUSTOM:
            if (event->data.custom.custom_type == 999) {
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


    int z = (int)round(extra->display_zoom);
    if (z < 0) z = 0; if (z > 18) z = 18;
    double scale = pow(2.0, extra->display_zoom - z);
    
    double center_x = extra->display_px_x * pow(2.0, extra->display_zoom - extra->zoom);
    double center_y = extra->display_px_y * pow(2.0, extra->display_zoom - extra->zoom);

    double max_px = (double)((1 << z) * TILE_SIZE);
    
    double view_tl_x = center_x - map->rect.width / 2.0;
    double view_tl_y = center_y - map->rect.height / 2.0;
    
    double current_tile_size = TILE_SIZE * scale;
    int tx_start = (int)floor(view_tl_x / current_tile_size) - 1;
    int ty_start = (int)floor(view_tl_y / current_tile_size) - 1;
    int tx_end = (int)floor((view_tl_x + map->rect.width) / current_tile_size) + 1;
    int ty_end = (int)floor((view_tl_y + map->rect.height) / current_tile_size) + 1;

    for (int y = ty_start; y <= ty_end; y++) {
        for (int x = tx_start; x <= tx_end; x++) {
            
            if (y < 0 || y >= (1<<z)) continue;
            int wrapped_x = (x % (1<<z) + (1<<z)) % (1<<z); 

            char filepath[256];
            snprintf(filepath, sizeof(filepath), "%s/osm_%s_%d_%d_%d.png", TILE_CACHE_DIR, theme_is_dark ? "dark" : "light", z, wrapped_x, y);

            MapTile* found_tile = NULL;
            int oldest_idx = -1;
            uint64_t oldest_seq = UINT64_MAX;

            for (int i=0; i<MAX_TILES_MEM; i++) {
                if (extra->tiles[i].valid) {
                    if (extra->tiles[i].z == z && extra->tiles[i].x == wrapped_x && extra->tiles[i].y == y) {
                        found_tile = &extra->tiles[i];
                        break;
                    }
                    if (extra->tiles[i].access_seq < oldest_seq) {
                        oldest_seq = extra->tiles[i].access_seq;
                        oldest_idx = i;
                    }
                } else {
                    oldest_idx = i;
                    oldest_seq = 0;
                }
            }

            if (found_tile) {
                found_tile->access_seq = ++extra->access_counter;
            } else if (oldest_idx != -1) {
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
                found_tile->access_seq = ++extra->access_counter;
                found_tile->texture_id = 0;
            }

            if (found_tile && !found_tile->is_ready) {
                if (access(filepath, F_OK) != -1) {
                    if (found_tile->is_loading) {
                        found_tile->is_loading = false;
                    }
                    if (gfx && gfx->load_image) {
                        found_tile->texture_id = gfx->load_image(filepath);
                        found_tile->is_ready = true;
                    }
                } else if (!found_tile->is_loading) {
                    found_tile->is_loading = true;
                    if (!request_tile_download(z, wrapped_x, y, theme_is_dark, filepath, node->node_id)) {
                        found_tile->is_loading = false;
                    }
                }
            }

            int draw_x = map->rect.x + (int)(x * current_tile_size - view_tl_x);
            int draw_y = map->rect.y + (int)(y * current_tile_size - view_tl_y);
            int draw_size = (int)(current_tile_size) + 1;

            if (found_tile && found_tile->is_ready && found_tile->texture_id != 0 && gfx && gfx->draw_image) {
                gfx->draw_image(window_id, draw_x, draw_y, draw_size, draw_size, found_tile->texture_id);

            } else {
                bool drawn_fallback = false;
                if (z > 0 && gfx && gfx->draw_image) {
                    int pz = z - 1;
                    int px = wrapped_x / 2;
                    int py = y / 2;
                    MapTile* fallback = NULL;
                    for (int i=0; i<MAX_TILES_MEM; i++) {
                        if (extra->tiles[i].valid && extra->tiles[i].z == pz && extra->tiles[i].x == px && extra->tiles[i].y == py && extra->tiles[i].is_ready) {
                            fallback = &extra->tiles[i];
                            break;
                        }
                    }
                    if (fallback) {
                        int parent_x = (x >= 0) ? (x / 2) : ((x - 1) / 2);
                        int parent_y = y / 2;
                        int p_draw_x = map->rect.x + (int)(parent_x * 2.0 * TILE_SIZE - view_tl_x);
                        int p_draw_y = map->rect.y + (int)(parent_y * 2.0 * TILE_SIZE - view_tl_y);

                        int cx = draw_x < map->rect.x ? map->rect.x : draw_x;
                        int cy = draw_y < map->rect.y ? map->rect.y : draw_y;
                        int cw = draw_x + TILE_SIZE > map->rect.x + map->rect.width ? map->rect.x + map->rect.width - cx : draw_x + TILE_SIZE - cx;
                        int ch = draw_y + TILE_SIZE > map->rect.y + map->rect.height ? map->rect.y + map->rect.height - cy : draw_y + TILE_SIZE - cy;
                        
                        if (cw > 0 && ch > 0) {
                            gfx->graphics_set_clip(cx, cy, cw, ch);
                            gfx->draw_image(window_id, p_draw_x, p_draw_y, TILE_SIZE * 2, TILE_SIZE * 2, fallback->texture_id);
                            gfx->graphics_set_clip(map->rect.x, map->rect.y, map->rect.width, map->rect.height);
                            drawn_fallback = true;
                        }
                    }
                }
                
                if (!drawn_fallback && draw_x < map->rect.x + map->rect.width && draw_x + TILE_SIZE > map->rect.x &&
                    draw_y < map->rect.y + map->rect.height && draw_y + TILE_SIZE > map->rect.y) {
                    gfx->draw_hollow_rectangle(window_id, draw_x, draw_y, TILE_SIZE, TILE_SIZE, grid_color, 1, false, 0.0f);
                }
            }
        }
    }
    
    for (int i = 0; i < extra->marker_count; i++) {
        double lat = extra->markers[i].lat;
        double lon = extra->markers[i].lon;
        
        double lat_rad = lat * M_PI / 180.0;
        double px_x = (lon + 180.0) / 360.0 * (1 << z) * TILE_SIZE;
        double px_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * (1 << z) * TILE_SIZE;
        
        int draw_x = map->rect.x + (int)(px_x - view_tl_x);
        int draw_y = map->rect.y + (int)(px_y - view_tl_y);
        
        if (draw_x >= map->rect.x && draw_x <= map->rect.x + map->rect.width &&
            draw_y >= map->rect.y && draw_y <= map->rect.y + map->rect.height) {
            
            if (gfx && gfx->fill_rectangle) {
                
                gfx->fill_rectangle(window_id, draw_x - 8, draw_y - 8, 16, 16, extra->markers[i].color, true, 8.0f);
                
                gfx->fill_rectangle(window_id, draw_x - 6, draw_y - 6, 12, 12, 0xFFFFFFFF, true, 6.0f);
                
                gfx->fill_rectangle(window_id, draw_x - 4, draw_y - 4, 8, 8, extra->markers[i].color, true, 4.0f);
            }
        }
    }

    if (theme_is_dark) {
        
    }

    if (map->show_osm_attribution && extra->font && gfx && gfx->render_text) {
        const char* text = "Powered by OpenStreetMap";
        int text_w = aroma_font_get_line_width(extra->font, text);
        int text_h = aroma_font_get_line_height(extra->font);
        
        int padding = 4;
        int bg_w = text_w + padding * 2;
        int bg_h = text_h + padding * 2;
        int bg_x = map->rect.x + map->rect.width - bg_w - 4;
        int bg_y = map->rect.y + map->rect.height - bg_h - 4;
        
        uint32_t bg_attr_color = theme_is_dark ? 0xAA000000 : 0xAAFFFFFF;
        if (gfx->fill_rectangle) {
            gfx->fill_rectangle(window_id, bg_x, bg_y, bg_w, bg_h, bg_attr_color, true, 4.0f);
        }
        
        uint32_t text_color = theme_is_dark ? 0xFFFFFFFF : 0xFF000000;
        gfx->render_text(window_id, extra->font, text, bg_x + padding, bg_y + padding, text_color, 1.0f);
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
        if (extra->anim_timer) {
            aroma_timer_cancel(extra->anim_timer);
        }
        AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
        for (int i=0; i<MAX_TILES_MEM; i++) {
            if (extra->tiles[i].valid && extra->tiles[i].is_ready && gfx && gfx->unload_image) {
                gfx->unload_image(extra->tiles[i].texture_id);
            }
        }
        if (extra->font) aroma_font_destroy(extra->font);
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
            extra->display_px_x *= 2.0;

            extra->center_px_y *= 2.0;
            extra->display_px_y *= 2.0;

            unload_old_zoom_tiles(extra);
        }

            if (extra->node_ptr) aroma_node_invalidate(extra->node_ptr);
            return true;
        }
        else if (event->data.key.key_code == 'x' || event->data.key.key_code == 'X' || event->data.key.key_code == '-') {
            if (extra->zoom > 2) {
                extra->zoom--;
                extra->center_px_x /= 2.0;
                extra->display_px_x /= 2.0;

                extra->center_px_y /= 2.0;
                extra->display_px_y /= 2.0;

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
            extra->display_px_x *= 2.0;

            extra->center_px_y *= 2.0;
            extra->display_px_y *= 2.0;

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
                extra->display_px_x /= 2.0;

        extra->center_px_y /= 2.0;
                extra->display_px_y /= 2.0;

        unload_old_zoom_tiles(extra);
    }
    aroma_node_invalidate(node);
}

void aroma_map_set_center(AromaNode* node, double lat, double lon) {
    if (!node || !node->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (!extra) return;

    double lat_rad = lat * M_PI / 180.0;
    double px_x = (lon + 180.0) / 360.0 * (1 << extra->zoom) * TILE_SIZE;
    double px_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * (1 << extra->zoom) * TILE_SIZE;

    extra->center_px_x = px_x;
    extra->center_px_y = px_y;
    map->center_lat = lat;
    map->center_lon = lon;
    aroma_node_invalidate(node);
}

void aroma_map_pan_to(AromaNode* node, double lat, double lon) {
    aroma_map_set_center(node, lat, lon);
}

void aroma_map_set_zoom(AromaNode* node, int zoom) {
    if (!node || !node->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (!extra) return;

    if (zoom < 2) zoom = 2;
    if (zoom > 18) zoom = 18;
    
    if (extra->zoom == zoom) return;

    extra->zoom = zoom;
    map->zoom = zoom;
    
    double lat_rad = map->center_lat * M_PI / 180.0;
    double px_x = (map->center_lon + 180.0) / 360.0 * (1 << zoom) * TILE_SIZE;
    double px_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * (1 << zoom) * TILE_SIZE;

    extra->center_px_x = px_x;
    extra->center_px_y = px_y;

    unload_old_zoom_tiles(extra);
    aroma_node_invalidate(node);
}

void aroma_map_set_show_attribution(AromaNode* node, bool show) {
    if (!node || !node->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    if (map->show_osm_attribution != show) {
        map->show_osm_attribution = show;
        aroma_node_invalidate(node);
    }
}

void aroma_map_add_marker(AromaNode* node, double lat, double lon, uint32_t color) {
    if (!node || !node->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (!extra) return;

    if (extra->marker_count < MAX_MARKERS) {
        extra->markers[extra->marker_count].lat = lat;
        extra->markers[extra->marker_count].lon = lon;
        extra->markers[extra->marker_count].color = color;
        extra->marker_count++;
        aroma_node_invalidate(node);
    }
}

void aroma_map_clear_markers(AromaNode* node) {
    if (!node || !node->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (!extra) return;

    extra->marker_count = 0;
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
    map->zoom = 6;
    map->center_lat = 0.0;
    map->center_lon = 0.0;
    map->show_osm_attribution = false;

    struct AromaMapExtra* extra = aroma_widget_alloc(sizeof(struct AromaMapExtra));
    memset(extra, 0, sizeof(struct AromaMapExtra));
    
    extra->zoom = map->zoom;
    
    extra->center_px_x = 8192.0; 
    extra->center_px_y = 8192.0;
    extra->display_px_x = 8192.0;
    extra->display_px_y = 8192.0;
    extra->display_zoom = map->zoom;
    extra->velocity_x = 0;
    extra->velocity_y = 0;
    extra->anim_timer = aroma_timer_create(16, true, __map_anim_tick, extra);

    extra->font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 12);

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
    aroma_event_subscribe(node->node_id, EVENT_TYPE_CUSTOM, __map_event_handler, extra, 90);

    return node;
}
