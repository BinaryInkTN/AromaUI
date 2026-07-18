#ifndef __ANDROID__

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

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <ctype.h>
#include <stdlib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#endif
#ifndef __EMSCRIPTEN__
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

static bool __map_event_handler_global(AromaEvent* event, void* user_data);

#ifndef __EMSCRIPTEN__
#include <curl/curl.h>
#endif

#define TILE_CACHE_DIR "/tmp/aroma_tiles"
#define MAX_TILES_MEM 128
#define TILE_SIZE 256

typedef struct {
    uint64_t access_seq;
    int z, x, y;
    unsigned int texture_id;
    bool is_dark;
    bool is_loading;
    bool is_ready;
    bool valid;
    char filepath[256];
} MapTile;

#define MAX_MARKERS 32
typedef struct {
    double lat;
    double lon;
    uint32_t color;
    const char* icon_code;
    const char* popup_text;
} MapMarker;

typedef struct {
    char query[256];
    void (*callback)(GeocodeResult* results, int count, void* user_data);
    void* user_data;
    uint64_t node_id;
} GeocodeRequest;

#ifdef __EMSCRIPTEN__
typedef struct {
    char query[256];
    void (*callback)(GeocodeResult* results, int count, void* user_data);
    void* user_data;
} EmscriptenGeocodeRequest;
#endif

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
    int active_popup_idx;
    AromaFont* font;
    AromaTimer* anim_timer;
    double velocity_x;
    double velocity_y;
    double display_zoom;
    double display_px_x;
    double display_px_y;
    double* route_lats;
    double* route_lons;
    int route_point_count;
    uint32_t route_color;
    bool route_active;
    bool route_loading;
    pthread_mutex_t route_mutex;
    pthread_mutex_t geocode_mutex;
    bool geocode_loading;
    bool animations_enabled;
};

typedef struct {
    uint64_t node_id;
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

#ifdef __EMSCRIPTEN__
#define MAP_ROUTE_MUTEX_INIT(extra) ((void)0)
#define MAP_ROUTE_MUTEX_DESTROY(extra) ((void)0)
#define MAP_ROUTE_MUTEX_LOCK(extra) ((void)0)
#define MAP_ROUTE_MUTEX_UNLOCK(extra) ((void)0)
#define MAP_GEOCODE_MUTEX_INIT(extra) ((void)0)
#define MAP_GEOCODE_MUTEX_DESTROY(extra) ((void)0)
#define MAP_GEOCODE_MUTEX_LOCK(extra) ((void)0)
#define MAP_GEOCODE_MUTEX_UNLOCK(extra) ((void)0)
#else
#define MAP_ROUTE_MUTEX_INIT(extra) pthread_mutex_init(&(extra)->route_mutex, NULL)
#define MAP_ROUTE_MUTEX_DESTROY(extra) pthread_mutex_destroy(&(extra)->route_mutex)
#define MAP_ROUTE_MUTEX_LOCK(extra) pthread_mutex_lock(&(extra)->route_mutex)
#define MAP_ROUTE_MUTEX_UNLOCK(extra) pthread_mutex_unlock(&(extra)->route_mutex)
#define MAP_GEOCODE_MUTEX_INIT(extra) pthread_mutex_init(&(extra)->geocode_mutex, NULL)
#define MAP_GEOCODE_MUTEX_DESTROY(extra) pthread_mutex_destroy(&(extra)->geocode_mutex)
#define MAP_GEOCODE_MUTEX_LOCK(extra) pthread_mutex_lock(&(extra)->geocode_mutex)
#define MAP_GEOCODE_MUTEX_UNLOCK(extra) pthread_mutex_unlock(&(extra)->geocode_mutex)
#endif

#define MAX_WORKER_THREADS 16
static pthread_t worker_threads[MAX_WORKER_THREADS];
static int num_active_workers = 2;
static bool worker_running = false;
#ifndef __EMSCRIPTEN__
static bool curl_initialized = false;
#endif

#ifndef __EMSCRIPTEN__
static size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(ptr == NULL) return 0;
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}
#endif

static void parse_photon_json(const char* json, GeocodeResult* results, int* count) {
    *count = 0;
    if (!json || !json[0]) return;
    const char* features_start = strstr(json, "\"features\"");
    if (!features_start) return;
    const char* ptr = features_start;
    while (*count < MAX_GEOCODE_RESULTS) {
        const char* type = strstr(ptr, "\"type\":\"Feature\"");
        if (!type) break;
        const char* coords = strstr(type, "\"coordinates\"");
        if (!coords) { ptr = type + 15; continue; }
        coords = strchr(coords, '[');
        if (!coords) { ptr = type + 15; continue; }
        coords++;
        char* endptr;
        results[*count].lon = strtod(coords, &endptr);
        coords = endptr;
        coords = strchr(coords, ',');
        if (!coords) { ptr = type + 15; continue; }
        coords++;
        results[*count].lat = strtod(coords, &endptr);
        const char* properties = strstr(type, "\"properties\"");
        if (!properties) { ptr = type + 15; continue; }
        const char* name_key = strstr(properties, "\"name\"");
        if (!name_key) { ptr = type + 15; continue; }
        name_key = strchr(name_key, ':');
        if (!name_key) { ptr = type + 15; continue; }
        name_key++;
        while (*name_key == ' ' || *name_key == '"') name_key++;
        const char* name_end = name_key;
        while (*name_end && *name_end != '"') {
            if ((*name_end & 0x80) == 0) { name_end++; }
            else if ((*name_end & 0xE0) == 0xC0) { name_end += 2; }
            else if ((*name_end & 0xF0) == 0xE0) { name_end += 3; }
            else if ((*name_end & 0xF8) == 0xF0) { name_end += 4; }
            else { name_end++; }
        }
        size_t len = name_end - name_key;
        if (len > 255) len = 255;
        memcpy(results[*count].display_name, name_key, len);
        results[*count].display_name[len] = '\0';
        const char* city = strstr(properties, "\"city\"");
        if (city) {
            city = strchr(city, ':');
            if (city) {
                city++;
                while (*city == ' ' || *city == '"') city++;
                const char* city_end = city;
                while (*city_end && *city_end != '"') {
                    if ((*city_end & 0x80) == 0) city_end++;
                    else if ((*city_end & 0xE0) == 0xC0) city_end += 2;
                    else if ((*city_end & 0xF0) == 0xE0) city_end += 3;
                    else city_end++;
                }
                size_t clen = city_end - city;
                if (clen > 63) clen = 63;
                memcpy(results[*count].category, city, clen);
                results[*count].category[clen] = '\0';
            }
        }
        if (results[*count].category[0] == '\0') {
            const char* country = strstr(properties, "\"country\"");
            if (country) {
                country = strchr(country, ':');
                if (country) {
                    country++;
                    while (*country == ' ' || *country == '"') country++;
                    const char* country_end = country;
                    while (*country_end && *country_end != '"') {
                        if ((*country_end & 0x80) == 0) country_end++;
                        else if ((*country_end & 0xE0) == 0xC0) country_end += 2;
                        else if ((*country_end & 0xF0) == 0xE0) country_end += 3;
                        else country_end++;
                    }
                    size_t colen = country_end - country;
                    if (colen > 63) colen = 63;
                    memcpy(results[*count].category, country, colen);
                    results[*count].category[colen] = '\0';
                }
            }
        }
        if (results[*count].category[0] == '\0') { strcpy(results[*count].category, "Location"); }
        strcpy(results[*count].type, "place");
        ptr = type + 15;
        (*count)++;
    }
}

static void decode_polyline(const char* encoded, double** lats, double** lons, int* count) {
    int cap = 100;
    *lats = malloc(cap * sizeof(double));
    *lons = malloc(cap * sizeof(double));
    *count = 0;
    int index = 0, len = strlen(encoded), lat = 0, lon = 0;
    while (index < len) {
        int b, shift = 0, result = 0;
        do {
            if (index >= len) break;
            b = encoded[index++];
            if (b == '\\' && encoded[index] == '\\') index++;
            b -= 63;
            result |= (b & 0x1f) << shift;
            shift += 5;
        } while (b >= 0x20 && index < len);
        int dlat = ((result & 1) ? ~(result >> 1) : (result >> 1));
        lat += dlat;
        shift = 0; result = 0;
        do {
            if (index >= len) break;
            b = encoded[index++];
            if (b == '\\' && encoded[index] == '\\') index++;
            b -= 63;
            result |= (b & 0x1f) << shift;
            shift += 5;
        } while (b >= 0x20 && index < len);
        int dlon = ((result & 1) ? ~(result >> 1) : (result >> 1));
        lon += dlon;
        if (*count >= cap) {
            cap *= 2;
            *lats = realloc(*lats, cap * sizeof(double));
            *lons = realloc(*lons, cap * sizeof(double));
        }
        (*lats)[*count] = lat / 1e5;
        (*lons)[*count] = lon / 1e5;
        (*count)++;
    }
}

static bool __map_apply_route_response(struct AromaMapExtra* extra, AromaNode* node, const char* response) {
    if (!extra || !response) return false;
    char* geom_start = strstr(response, "\"geometry\":\"");
    if (!geom_start) return false;
    geom_start += 12;
    char* geom_end = strchr(geom_start, '"');
    if (!geom_end) return false;
    *geom_end = '\0';
    double *rlats = NULL, *rlons = NULL;
    int rcount = 0;
    decode_polyline(geom_start, &rlats, &rlons, &rcount);
    for (int n = 0; n < rcount; n++) {
        double lat_rad = rlats[n] * M_PI / 180.0;
        rlats[n] = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0;
        rlons[n] = (rlons[n] + 180.0) / 360.0;
    }
    MAP_ROUTE_MUTEX_LOCK(extra);
    if (extra->route_lats) free(extra->route_lats);
    if (extra->route_lons) free(extra->route_lons);
    extra->route_lats = rlats;
    extra->route_lons = rlons;
    extra->route_point_count = rcount;
    extra->route_active = true;
    extra->route_loading = false;
    MAP_ROUTE_MUTEX_UNLOCK(extra);
    if (node) {
        AromaEvent *ev = aroma_event_create_custom(node->node_id, 999, NULL, NULL);
        if (ev) aroma_event_queue(ev);
    }
    return true;
}

#ifndef __EMSCRIPTEN__
typedef struct {
    double start_lat, start_lon, end_lat, end_lon;
    AromaNode* node;
    struct AromaMapExtra* extra;
} RouteRequest;

static void* route_fetch_worker(void* arg) {
    RouteRequest* req = (RouteRequest*)arg;
    struct AromaMapExtra* extra = req->extra;
    char url[512];
    snprintf(url, sizeof(url), "https://routing.openstreetmap.de/routed-car/route/v1/driving/%f,%f;%f,%f?overview=full&geometries=polyline",
             req->start_lon, req->start_lat, req->end_lon, req->end_lat);
    CURL *curl = curl_easy_init();
    if (curl) {
        struct MemoryStruct chunk;
        chunk.memory = malloc(1);
        chunk.size = 0;
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "AromaUI/1.0");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK && chunk.size > 0) {
            __map_apply_route_response(extra, req->node, chunk.memory);
        } else {
            MAP_ROUTE_MUTEX_LOCK(extra);
            extra->route_loading = false;
            extra->route_active = false;
            MAP_ROUTE_MUTEX_UNLOCK(extra);
        }
        free(chunk.memory);
        curl_easy_cleanup(curl);
    }
    free(req);
    return NULL;
}

static void* geocode_fetch_worker(void* arg) {
    GeocodeRequest* req = (GeocodeRequest*)arg;
    CURL* curl = curl_easy_init();
    if (!curl) {
        if (req->callback) req->callback(NULL, 0, req->user_data);
        free(req);
        return NULL;
    }
    char* encoded_query = curl_easy_escape(curl, req->query, 0);
    char url[1024];
    snprintf(url, sizeof(url), "https://photon.komoot.io/api/?q=%s&limit=%d&lang=en", encoded_query, MAX_GEOCODE_RESULTS);
    curl_free(encoded_query);
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AromaInfotainment/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    GeocodeResult results[MAX_GEOCODE_RESULTS];
    int count = 0;
    if (res == CURLE_OK && http_code == 200 && chunk.memory && chunk.size > 0) {
        parse_photon_json(chunk.memory, results, &count);
    }
    if (req->callback) req->callback(results, count, req->user_data);
    free(chunk.memory);
    free(req);
    return NULL;
}
#endif

#ifdef __EMSCRIPTEN__
typedef struct {
    int z;
    int x;
    int y;
    bool is_dark;
    struct AromaMapExtra* extra;
} EmscriptenTileRequest;

typedef struct {
    double start_lat;
    double start_lon;
    double end_lat;
    double end_lon;
    AromaNode* node;
    struct AromaMapExtra* extra;
} EmscriptenRouteRequest;

static void __map_request_visible_invalidate(struct AromaMapExtra* extra) {
    if (!extra || !extra->node_ptr) return;
    AromaNode* curr = extra->node_ptr;
    bool is_visible = true;
    while (curr) {
        if (curr->is_hidden) { is_visible = false; break; }
        curr = curr->parent_node;
    }
    if (is_visible) aroma_node_invalidate(extra->node_ptr);
}

static void __map_tile_fetch_success(emscripten_fetch_t* fetch) {
    EmscriptenTileRequest* req = fetch ? (EmscriptenTileRequest*)fetch->userData : NULL;
    if (req && req->extra) {
        struct AromaMapExtra* extra = req->extra;
        AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
        for (int i = 0; i < MAX_TILES_MEM; i++) {
            if (extra->tiles[i].valid && extra->tiles[i].z == req->z && extra->tiles[i].x == req->x && extra->tiles[i].y == req->y && extra->tiles[i].is_dark == req->is_dark) {
                extra->tiles[i].is_loading = false;
                if (gfx && gfx->load_image_from_memory) {
                    extra->tiles[i].texture_id = gfx->load_image_from_memory((unsigned char*)fetch->data, (unsigned long)fetch->numBytes);
                    extra->tiles[i].is_ready = extra->tiles[i].texture_id != 0;
                }
                break;
            }
        }
        __map_request_visible_invalidate(extra);
    }
    free(req);
    emscripten_fetch_close(fetch);
}

static void __map_tile_fetch_error(emscripten_fetch_t* fetch) {
    EmscriptenTileRequest* req = fetch ? (EmscriptenTileRequest*)fetch->userData : NULL;
    if (req && req->extra) {
        struct AromaMapExtra* extra = req->extra;
        for (int i = 0; i < MAX_TILES_MEM; i++) {
            if (extra->tiles[i].valid && extra->tiles[i].z == req->z && extra->tiles[i].x == req->x && extra->tiles[i].y == req->y && extra->tiles[i].is_dark == req->is_dark) {
                extra->tiles[i].is_loading = false;
                extra->tiles[i].is_ready = false;
                break;
            }
        }
        __map_request_visible_invalidate(extra);
    }
    free(req);
    emscripten_fetch_close(fetch);
}

static void __map_route_fetch_success(emscripten_fetch_t* fetch) {
    EmscriptenRouteRequest* req = fetch ? (EmscriptenRouteRequest*)fetch->userData : NULL;
    if (req && req->extra) {
        char* response = malloc((size_t)fetch->numBytes + 1);
        if (response) {
            memcpy(response, fetch->data, (size_t)fetch->numBytes);
            response[fetch->numBytes] = '\0';
            if (!__map_apply_route_response(req->extra, req->node, response)) {
                MAP_ROUTE_MUTEX_LOCK(req->extra);
                req->extra->route_loading = false;
                req->extra->route_active = false;
                MAP_ROUTE_MUTEX_UNLOCK(req->extra);
            }
            free(response);
        }
    }
    free(req);
    emscripten_fetch_close(fetch);
}

static void __map_route_fetch_error(emscripten_fetch_t* fetch) {
    EmscriptenRouteRequest* req = fetch ? (EmscriptenRouteRequest*)fetch->userData : NULL;
    if (req && req->extra) {
        MAP_ROUTE_MUTEX_LOCK(req->extra);
        req->extra->route_loading = false;
        req->extra->route_active = false;
        MAP_ROUTE_MUTEX_UNLOCK(req->extra);
        __map_request_visible_invalidate(req->extra);
    }
    free(req);
    emscripten_fetch_close(fetch);
}

static void __map_geocode_fetch_success(emscripten_fetch_t* fetch) {
    EmscriptenGeocodeRequest* req = fetch ? (EmscriptenGeocodeRequest*)fetch->userData : NULL;
    if (req) {
        GeocodeResult results[MAX_GEOCODE_RESULTS];
        int count = 0;
        if (fetch->data && fetch->numBytes > 0) {
            char* json = malloc((size_t)fetch->numBytes + 1);
            if (json) {
                memcpy(json, fetch->data, (size_t)fetch->numBytes);
                json[fetch->numBytes] = '\0';
                parse_photon_json(json, results, &count);
                free(json);
            }
        }
        if (req->callback) req->callback(results, count, req->user_data);
    }
    free(req);
    emscripten_fetch_close(fetch);
}

static void __map_geocode_fetch_error(emscripten_fetch_t* fetch) {
    EmscriptenGeocodeRequest* req = fetch ? (EmscriptenGeocodeRequest*)fetch->userData : NULL;
    if (req && req->callback) req->callback(NULL, 0, req->user_data);
    free(req);
    emscripten_fetch_close(fetch);
}
#endif

#ifndef __EMSCRIPTEN__
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
        if (!worker_running) { pthread_mutex_unlock(&queue_mutex); break; }
        if (queue_head != queue_tail) {
            req = fetch_queue[queue_head];
            queue_head = (queue_head + 1) % MAX_QUEUE;
            has_req = true;
        }
        pthread_mutex_unlock(&queue_mutex);
        if (has_req) {
            if (access(req.filepath, F_OK) != -1) {
                AromaEvent *ev = aroma_event_create_custom(req.node_id, 999, NULL, NULL);
                if (ev) aroma_event_queue(ev);
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
                    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AromaUI/0.0.1");
                    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
                    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
                    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
                    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
                    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
                    CURLcode res = curl_easy_perform(curl);
                    fclose(fp);
                    if (res == CURLE_OK) {
                        rename(tmp_path, req.filepath);
                        TileRequest* event_req = malloc(sizeof(TileRequest));
                        if (event_req) *event_req = req;
                        AromaEvent *ev = aroma_event_create_custom(req.node_id, 999, event_req, free);
                        if (ev) aroma_event_queue(ev);
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
#endif

static bool request_tile_download(int z, int x, int y, bool is_dark, const char* filepath, uint64_t node_id, struct AromaMapExtra* extra) {
#ifdef __EMSCRIPTEN__
    (void)filepath;
    (void)node_id;
    if (!extra) return false;
    EmscriptenTileRequest* req = malloc(sizeof(EmscriptenTileRequest));
    if (!req) return false;
    req->z = z;
    req->x = x;
    req->y = y;
    req->is_dark = is_dark;
    req->extra = extra;
    char url[512];
    if (is_dark) {
        snprintf(url, sizeof(url), "https://a.basemaps.cartocdn.com/dark_all/%d/%d/%d.png", z, x, y);
    } else {
        snprintf(url, sizeof(url), "https://tile.openstreetmap.org/%d/%d/%d.png", z, x, y);
    }
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = __map_tile_fetch_success;
    attr.onerror = __map_tile_fetch_error;
    attr.timeoutMSecs = 30000;
    attr.userData = req;
    emscripten_fetch(&attr, url);
    return true;
#else
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
#endif
}

static void __map_anim_tick(void* user_data) {
    if (!user_data) return;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)user_data;
    if (!extra->node_ptr || !extra->node_ptr->node_widget_ptr) return;
    AromaNode* curr = extra->node_ptr;
    bool is_visible = true;
    while(curr) { if (curr->is_hidden) { is_visible = false; break; } curr = curr->parent_node; }
    if (!is_visible) return;
    AromaMap* map = (AromaMap*)extra->node_ptr->node_widget_ptr;
    bool changed = false;
    if (!extra->animations_enabled) {
        /* Animations disabled (e.g. embedded/kiosk contexts): snap straight to the
           target zoom/position instead of easing, and drop any residual velocity
           so a prior fling can't resume a glide later. */
        if (extra->display_zoom != extra->zoom) { extra->display_zoom = extra->zoom; changed = true; }
        if (extra->display_px_x != extra->center_px_x || extra->display_px_y != extra->center_px_y) {
            extra->display_px_x = extra->center_px_x;
            extra->display_px_y = extra->center_px_y;
            changed = true;
        }
        if (extra->velocity_x != 0 || extra->velocity_y != 0) { extra->velocity_x = 0; extra->velocity_y = 0; changed = true; }
        if (changed) aroma_node_invalidate(extra->node_ptr);
        return;
    }
    if (fabs(extra->display_zoom - extra->zoom) > 0.001) {
        extra->display_zoom += (extra->zoom - extra->display_zoom) * 0.15;
        changed = true;
    } else { extra->display_zoom = extra->zoom; }
    if (!map->is_dragging) {
        if (fabs(extra->velocity_x) > 0.1 || fabs(extra->velocity_y) > 0.1) {
            extra->center_px_x += extra->velocity_x;
            extra->center_px_y += extra->velocity_y;
            extra->velocity_x *= 0.94;
            extra->velocity_y *= 0.94;
            changed = true;
        } else { extra->velocity_x = 0; extra->velocity_y = 0; }
    } else { extra->velocity_x = 0; extra->velocity_y = 0; changed = true; }
    double diff_x = extra->center_px_x - extra->display_px_x;
    double diff_y = extra->center_px_y - extra->display_px_y;
    if (fabs(diff_x) > 0.1 || fabs(diff_y) > 0.1) {
        extra->display_px_x += diff_x * 0.4;
        extra->display_px_y += diff_y * 0.4;
        changed = true;
    } else { extra->display_px_x = extra->center_px_x; extra->display_px_y = extra->center_px_y; }
    if (changed) aroma_node_invalidate(extra->node_ptr);
}

static void unload_old_zoom_tiles(struct AromaMapExtra* extra) {
    if (!extra) return;
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    for (int i = 0; i < MAX_TILES_MEM; i++) {
        if (extra->tiles[i].valid) {
            if (extra->tiles[i].is_ready && extra->tiles[i].texture_id != 0) {
                if (gfx && gfx->unload_image) gfx->unload_image(extra->tiles[i].texture_id);
            }
            extra->tiles[i].valid = false;
            extra->tiles[i].is_ready = false;
            extra->tiles[i].is_loading = false;
            extra->tiles[i].texture_id = 0;
        }
    }
    extra->access_counter = 0;
}

static bool __map_event_handler(AromaEvent* event, void* user_data) {
    if (!event || !event->target_node || !event->target_node->node_widget_ptr) return false;
    AromaMap* map = (AromaMap*)event->target_node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)user_data;
    if (!extra) return false;
    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_DOUBLE_CLICK:
            if (event->data.mouse.x >= map->rect.x && event->data.mouse.x <= map->rect.x + map->rect.width &&
                event->data.mouse.y >= map->rect.y && event->data.mouse.y <= map->rect.y + map->rect.height) {
                aroma_map_zoom_in(event->target_node);
                return true;
            }
            break;
        case EVENT_TYPE_MOUSE_CLICK:
            if (event->data.mouse.x >= map->rect.x && event->data.mouse.x <= map->rect.x + map->rect.width &&
                event->data.mouse.y >= map->rect.y && event->data.mouse.y <= map->rect.y + map->rect.height) {
                int clicked_marker = -1;
                double center_x = extra->display_px_x * pow(2.0, extra->display_zoom - extra->zoom);
                double center_y = extra->display_px_y * pow(2.0, extra->display_zoom - extra->zoom);
                double view_tl_x = center_x - map->rect.width / 2.0;
                double view_tl_y = center_y - map->rect.height / 2.0;
                for (int i = 0; i < extra->marker_count; i++) {
                    if (!extra->markers[i].popup_text) continue;
                    double lat_rad = extra->markers[i].lat * M_PI / 180.0;
                    double px_x = (extra->markers[i].lon + 180.0) / 360.0 * pow(2.0, extra->display_zoom) * TILE_SIZE;
                    double px_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * pow(2.0, extra->display_zoom) * TILE_SIZE;
                    int draw_x = map->rect.x + (int)(px_x - view_tl_x);
                    int draw_y = map->rect.y + (int)(px_y - view_tl_y);
                    if (event->data.mouse.x >= draw_x - 12 && event->data.mouse.x <= draw_x + 12 &&
                        event->data.mouse.y >= draw_y - 12 && event->data.mouse.y <= draw_y + 12) {
                        clicked_marker = i;
                        break;
                    }
                }
                if (clicked_marker != -1) {
                    if (extra->active_popup_idx == clicked_marker) { extra->active_popup_idx = -1; }
                    else { extra->active_popup_idx = clicked_marker; }
                } else {
                    map->is_dragging = true;
                    map->last_mouse_x = event->data.mouse.x;
                    map->last_mouse_y = event->data.mouse.y;
                    extra->active_popup_idx = -1;
                }
                aroma_node_invalidate(event->target_node);
                return true;
            }
            break;
        case EVENT_TYPE_MOUSE_MOVE:
            if (map->is_dragging) {
                int dx = event->data.mouse.x - map->last_mouse_x;
                int dy = event->data.mouse.y - map->last_mouse_y;
                extra->center_px_x -= dx;
                extra->center_px_y -= dy;
                extra->display_px_x -= dx;
                extra->display_px_y -= dy;
                if (extra->animations_enabled) {
                    extra->velocity_x = -dx * 0.8;
                    extra->velocity_y = -dy * 0.8;
                } else {
                    extra->velocity_x = 0;
                    extra->velocity_y = 0;
                }
                map->last_mouse_x = event->data.mouse.x;
                map->last_mouse_y = event->data.mouse.y;
                aroma_node_invalidate(event->target_node);
                return true;
            }
            break;
        case EVENT_TYPE_MOUSE_RELEASE:
            if (map->is_dragging) { map->is_dragging = false; aroma_node_invalidate(event->target_node); return true; }
            break;
        case EVENT_TYPE_MOUSE_SCROLL:
            if (event->data.mouse.x >= map->rect.x && event->data.mouse.x <= map->rect.x + map->rect.width &&
                event->data.mouse.y >= map->rect.y && event->data.mouse.y <= map->rect.y + map->rect.height) {
                if (event->data.mouse.scroll_y > 0) { aroma_map_zoom_in(event->target_node); }
                else if (event->data.mouse.scroll_y < 0) { aroma_map_zoom_out(event->target_node); }
                return true;
            }
            break;
        case EVENT_TYPE_CUSTOM:
            if (event->data.custom.custom_type == 999 || event->data.custom.custom_type == 998) {
                TileRequest* req = (TileRequest*)event->data.custom.data;
                if (req) {
                    for (int i = 0; i < MAX_TILES_MEM; i++) {
                        if (extra->tiles[i].valid && extra->tiles[i].z == req->z && extra->tiles[i].x == req->x && extra->tiles[i].y == req->y && extra->tiles[i].is_dark == req->is_dark) {
                            extra->tiles[i].is_loading = false;
                        }
                    }
                }
                if (event->data.custom.custom_type == 999 && extra->node_ptr) {
                    AromaNode* curr = extra->node_ptr;
                    bool is_visible = true;
                    while(curr) { if (curr->is_hidden) { is_visible = false; break; } curr = curr->parent_node; }
                    if (is_visible) aroma_node_invalidate(event->target_node);
                }
                return true;
            }
            break;
        default: break;
    }
    return false;
}

static void _map_draw_line(AromaGraphicsInterface* gfx, size_t window_id, int x0, int y0, int x1, int y1, uint32_t color, int thickness, int cx, int cy, int cw, int ch) {
    if (!gfx || !gfx->fill_rectangle) return;
    int outcode0 = (((x0) < cx) ? 1 : ((x0) > cx + cw) ? 2 : 0) | (((y0) < cy) ? 4 : ((y0) > cy + ch) ? 8 : 0);
    int outcode1 = (((x1) < cx) ? 1 : ((x1) > cx + cw) ? 2 : 0) | (((y1) < cy) ? 4 : ((y1) > cy + ch) ? 8 : 0);
    while (1) {
        if (!(outcode0 | outcode1)) break;
        if (outcode0 & outcode1) return;
        int out = outcode0 ? outcode0 : outcode1;
        int x, y;
        if (out & 8) { x = x0 + (x1-x0)*(cy+ch-y0)/(y1-y0); y = cy+ch; }
        else if (out & 4) { x = x0 + (x1-x0)*(cy-y0)/(y1-y0); y = cy; }
        else if (out & 2) { y = y0 + (y1-y0)*(cx+cw-x0)/(x1-x0); x = cx+cw; }
        else { y = y0 + (y1-y0)*(cx-x0)/(x1-x0); x = cx; }
        if (out == outcode0) { x0 = x; y0 = y; outcode0 = (((x0) < cx) ? 1 : ((x0) > cx + cw) ? 2 : 0) | (((y0) < cy) ? 4 : ((y0) > cy + ch) ? 8 : 0); }
        else { x1 = x; y1 = y; outcode1 = (((x1) < cx) ? 1 : ((x1) > cx + cw) ? 2 : 0) | (((y1) < cy) ? 4 : ((y1) > cy + ch) ? 8 : 0); }
    }
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int d_max = adx > ady ? adx : ady;
    int step_size = (thickness * 7) / 10;
    if (step_size < 1) step_size = 1;
    int steps = d_max / step_size;
    if (steps == 0) steps = 1;
    for (int i = 0; i <= steps; i++) {
        int x = x0 + dx * i / steps;
        int y = y0 + dy * i / steps;
        gfx->fill_rectangle(window_id, x - thickness/2, y - thickness/2, thickness, thickness, color, true, thickness/2.0f);
    }
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
            for (int i = 0; i < MAX_TILES_MEM; i++) {
                if (extra->tiles[i].valid) {
                    if (extra->tiles[i].z == z && extra->tiles[i].x == wrapped_x && extra->tiles[i].y == y && extra->tiles[i].is_dark == theme_is_dark) {
                        found_tile = &extra->tiles[i]; break;
                    }
                    if (extra->tiles[i].access_seq < oldest_seq) { oldest_seq = extra->tiles[i].access_seq; oldest_idx = i; }
                } else { oldest_idx = i; oldest_seq = 0; }
            }
            if (found_tile) { found_tile->access_seq = ++extra->access_counter; }
            else if (oldest_idx != -1) {
                found_tile = &extra->tiles[oldest_idx];
                if (found_tile->valid && found_tile->is_ready && found_tile->texture_id != 0 && gfx && gfx->unload_image) { gfx->unload_image(found_tile->texture_id); }
                found_tile->valid = true;
                found_tile->is_dark = theme_is_dark;
                found_tile->z = z;
                found_tile->x = wrapped_x;
                found_tile->y = y;
                found_tile->is_loading = false;
                found_tile->is_ready = false;
                found_tile->access_seq = ++extra->access_counter;
                found_tile->texture_id = 0;
            }
            if (found_tile && !found_tile->is_ready && !found_tile->is_loading) {
                found_tile->is_loading = true;
#ifdef __EMSCRIPTEN__
                if (!request_tile_download(z, wrapped_x, y, theme_is_dark, filepath, node->node_id, extra)) {
                    found_tile->is_loading = false;
                }
#else
                if (access(filepath, F_OK) != -1) {
                    if (gfx && gfx->load_image) {
                        found_tile->texture_id = gfx->load_image(filepath);
                        if (found_tile->texture_id != 0) { found_tile->is_ready = true; }
                        else { unlink(filepath); found_tile->is_loading = false; }
                    }
                } else if (!request_tile_download(z, wrapped_x, y, theme_is_dark, filepath, node->node_id, extra)) {
                    found_tile->is_loading = false;
                }
#endif
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
                    for (int i = 0; i < MAX_TILES_MEM; i++) {
                        if (extra->tiles[i].valid && extra->tiles[i].z == pz && extra->tiles[i].x == px && extra->tiles[i].y == py && extra->tiles[i].is_dark == theme_is_dark && extra->tiles[i].is_ready) {
                            fallback = &extra->tiles[i]; break;
                        }
                    }
                    if (fallback) {
                        int p_draw_x = map->rect.x + (int)(px * 2.0 * TILE_SIZE - view_tl_x);
                        int p_draw_y = map->rect.y + (int)(py * 2.0 * TILE_SIZE - view_tl_y);
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
    MAP_ROUTE_MUTEX_LOCK(extra);
    if (extra->route_active && extra->route_point_count > 1 && gfx) {
        uint32_t inner_color = extra->route_color;
        uint32_t r2 = (inner_color >> 16) & 0xFF;
        uint32_t g2 = (inner_color >> 8) & 0xFF;
        uint32_t b2 = inner_color & 0xFF;
        uint32_t a = (inner_color >> 24) & 0xFF;
        uint32_t border_color = (a << 24) | ((r2/2) << 16) | ((g2/2) << 8) | (b2/2);
        double z_factor = pow(2.0, extra->display_zoom) * TILE_SIZE;
        for (int pass = 0; pass < 2; pass++) {
            int thickness = pass == 0 ? 12 : 6;
            uint32_t pass_color = pass == 0 ? border_color : inner_color;
            int last_drawn_x = 0, last_drawn_y = 0;
            bool has_last_drawn = false;
            for (int i = 0; i < extra->route_point_count; i++) {
                double px_x = extra->route_lons[i] * z_factor;
                double px_y = extra->route_lats[i] * z_factor;
                int draw_x = map->rect.x + (int)(px_x - view_tl_x);
                int draw_y = map->rect.y + (int)(px_y - view_tl_y);
                if (has_last_drawn) {
                    int adx = draw_x - last_drawn_x; if (adx < 0) adx = -adx;
                    int ady = draw_y - last_drawn_y; if (ady < 0) ady = -ady;
                    if (adx < thickness && ady < thickness && i < extra->route_point_count - 1) continue;
                    int expand = 20;
                    int min_x = last_drawn_x < draw_x ? last_drawn_x : draw_x;
                    int max_x = last_drawn_x > draw_x ? last_drawn_x : draw_x;
                    int min_y = last_drawn_y < draw_y ? last_drawn_y : draw_y;
                    int max_y = last_drawn_y > draw_y ? last_drawn_y : draw_y;
                    if (!(max_x < map->rect.x - expand || min_x > map->rect.x + map->rect.width + expand ||
                          max_y < map->rect.y - expand || min_y > map->rect.y + map->rect.height + expand)) {
                        _map_draw_line(gfx, window_id, last_drawn_x, last_drawn_y, draw_x, draw_y, pass_color, thickness, map->rect.x - expand, map->rect.y - expand, map->rect.width + expand * 2, map->rect.height + expand * 2);
                    }
                }
                last_drawn_x = draw_x; last_drawn_y = draw_y; has_last_drawn = true;
            }
        }
    }
    MAP_ROUTE_MUTEX_UNLOCK(extra);
    double z_factor_m = pow(2.0, extra->display_zoom) * TILE_SIZE;
    for (int i = 0; i < extra->marker_count; i++) {
        double lat = extra->markers[i].lat;
        double lon = extra->markers[i].lon;
        double lat_rad = lat * M_PI / 180.0;
        double px_x = (lon + 180.0) / 360.0 * z_factor_m;
        double px_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * z_factor_m;
        int draw_x = map->rect.x + (int)(px_x - view_tl_x);
        int draw_y = map->rect.y + (int)(px_y - view_tl_y);
        if (draw_x >= map->rect.x && draw_x <= map->rect.x + map->rect.width &&
            draw_y >= map->rect.y && draw_y <= map->rect.y + map->rect.height) {
            if (gfx && gfx->fill_rectangle) {
                gfx->fill_rectangle(window_id, draw_x - 8, draw_y - 8, 16, 16, extra->markers[i].color, true, 8.0f);
                gfx->fill_rectangle(window_id, draw_x - 6, draw_y - 6, 12, 12, 0xFFFFFFFF, true, 6.0f);
                gfx->fill_rectangle(window_id, draw_x - 4, draw_y - 4, 8, 8, extra->markers[i].color, true, 4.0f);
            }
            if (extra->active_popup_idx == i && extra->markers[i].popup_text && extra->font && gfx && gfx->render_text) {
                const char* text = extra->markers[i].popup_text;
                int text_w = aroma_font_get_line_width(extra->font, text);
                int text_h = aroma_font_get_line_height(extra->font);
                int padding = 8;
                int bg_w = text_w + padding * 2;
                int bg_h = text_h + padding * 2;
                int bg_x = draw_x - (bg_w / 2);
                int bg_y = draw_y - 8 - bg_h - 10;
                if (bg_x < map->rect.x) bg_x = map->rect.x;
                if (bg_x + bg_w > map->rect.x + map->rect.width) bg_x = map->rect.x + map->rect.width - bg_w;
                uint32_t popup_bg = theme_is_dark ? 0xEE2A2A2A : 0xEEFFFFFF;
                uint32_t text_color = theme_is_dark ? 0xFFFFFFFF : 0xFF000000;
                uint32_t outline_color = theme_is_dark ? 0x66FFFFFF : 0x44000000;
                if (gfx->fill_rectangle) {
                    gfx->fill_rectangle(window_id, bg_x, bg_y, bg_w, bg_h, popup_bg, true, 6.0f);
                    if (gfx->draw_hollow_rectangle) { gfx->draw_hollow_rectangle(window_id, bg_x, bg_y, bg_w, bg_h, outline_color, 1, true, 6.0f); }
                }
                gfx->render_text(window_id, extra->font, text, bg_x + padding, bg_y + padding, text_color, 1.0f);
            }
        }
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
        if (gfx->fill_rectangle) { gfx->fill_rectangle(window_id, bg_x, bg_y, bg_w, bg_h, bg_attr_color, true, 4.0f); }
        uint32_t text_color = theme_is_dark ? 0xFFFFFFFF : 0xFF000000;
        gfx->render_text(window_id, extra->font, text, bg_x + padding, bg_y + padding, text_color, 1.0f);
    }
    gfx->graphics_clear_clip();
}

void aroma_map_destroy(AromaNode* node) {
    if (!node || !node->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (extra && extra->root_id) { aroma_event_unsubscribe(extra->root_id, EVENT_TYPE_KEY_PRESS, __map_event_handler_global); }
    if (extra) {
        if (extra->anim_timer) { aroma_timer_cancel(extra->anim_timer); }
        AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
        for (int i = 0; i < MAX_TILES_MEM; i++) {
            if (extra->tiles[i].valid && extra->tiles[i].is_ready && gfx && gfx->unload_image) { gfx->unload_image(extra->tiles[i].texture_id); }
        }
        if (extra->font) aroma_font_destroy(extra->font);
        if (extra->route_lats) free(extra->route_lats);
        if (extra->route_lons) free(extra->route_lons);
        MAP_ROUTE_MUTEX_DESTROY(extra);
        MAP_GEOCODE_MUTEX_DESTROY(extra);
        free(extra);
        map->extra = NULL;
    }
    free(node->node_widget_ptr);
    node->node_widget_ptr = NULL;
    __destroy_node(node);
}

static bool __map_event_handler_global(AromaEvent* event, void* user_data) {
    if (!event || !user_data) return false;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)user_data;
    if (event->event_type == EVENT_TYPE_KEY_PRESS) {
        if (event->data.key.key_code == 'z' || event->data.key.key_code == 'Z' || event->data.key.key_code == '=') { aroma_map_zoom_in(extra->node_ptr); return true; }
        else if (event->data.key.key_code == 'x' || event->data.key.key_code == 'X' || event->data.key.key_code == '-') { aroma_map_zoom_out(extra->node_ptr); return true; }
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
        extra->center_px_x *= 2.0; extra->display_px_x *= 2.0;
        extra->center_px_y *= 2.0; extra->display_px_y *= 2.0;
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
        extra->center_px_x /= 2.0; extra->display_px_x /= 2.0;
        extra->center_px_y /= 2.0; extra->display_px_y /= 2.0;
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

/* Set the map's center immediately, with no pan animation, regardless of
   whether animations are otherwise enabled. Intended for setting the real
   starting location right after aroma_map_create() (which always starts at
   Null Island), or for any other case where a visible glide is unwanted. */
void aroma_map_set_center_instant(AromaNode* node, double lat, double lon) {
    if (!node || !node->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (!extra) return;
    double lat_rad = lat * M_PI / 180.0;
    double px_x = (lon + 180.0) / 360.0 * (1 << extra->zoom) * TILE_SIZE;
    double px_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * (1 << extra->zoom) * TILE_SIZE;
    extra->center_px_x = px_x;
    extra->center_px_y = px_y;
    extra->display_px_x = px_x;
    extra->display_px_y = px_y;
    extra->velocity_x = 0;
    extra->velocity_y = 0;
    map->center_lat = lat;
    map->center_lon = lon;
    aroma_node_invalidate(node);
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

/* Enable or disable easing animations for pan/zoom/momentum. When disabled,
   aroma_map_set_center(), pan_to(), set_zoom(), zoom_in(), zoom_out(), and
   drag-release momentum all take effect on the next frame with no glide.
   Useful for embedded/kiosk displays where the flying-map animation is
   distracting or wastes CPU on constrained hardware. Toggling this off snaps
   any animation currently in flight to its target immediately. */
void aroma_map_set_animations_enabled(AromaNode* node, bool enabled) {
    if (!node || !node->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (!extra) return;
    extra->animations_enabled = enabled;
    if (!enabled) {
        extra->display_zoom = extra->zoom;
        extra->display_px_x = extra->center_px_x;
        extra->display_px_y = extra->center_px_y;
        extra->velocity_x = 0;
        extra->velocity_y = 0;
        aroma_node_invalidate(node);
    }
}

bool aroma_map_get_animations_enabled(AromaNode* node) {
    if (!node || !node->node_widget_ptr) return true;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    return extra ? extra->animations_enabled : true;
}

void aroma_map_set_show_attribution(AromaNode* node, bool show) {
    if (!node || !node->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    if (map->show_osm_attribution != show) { map->show_osm_attribution = show; aroma_node_invalidate(node); }
}

void aroma_map_add_marker(AromaNode* node, double lat, double lon, uint32_t color) {
    if (!node || node->node_type != NODE_TYPE_WIDGET) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    if (!map || !map->extra) return;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (extra->marker_count < MAX_MARKERS) {
        extra->markers[extra->marker_count].lat = lat;
        extra->markers[extra->marker_count].lon = lon;
        extra->markers[extra->marker_count].color = color;
        extra->markers[extra->marker_count].icon_code = NULL;
        extra->markers[extra->marker_count].popup_text = NULL;
        extra->marker_count++;
        aroma_node_invalidate(node);
    }
}

void aroma_map_add_icon_marker(AromaNode* node, double lat, double lon, uint32_t color, const char* icon_code) {
    if (!node || node->node_type != NODE_TYPE_WIDGET) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    if (!map || !map->extra) return;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (extra->marker_count < MAX_MARKERS) {
        extra->markers[extra->marker_count].lat = lat;
        extra->markers[extra->marker_count].lon = lon;
        extra->markers[extra->marker_count].color = color;
        extra->markers[extra->marker_count].icon_code = icon_code;
        extra->markers[extra->marker_count].popup_text = NULL;
        extra->marker_count++;
        aroma_node_invalidate(node);
    }
}

void aroma_map_add_popup_marker(AromaNode* node, double lat, double lon, uint32_t color, const char* popup_text) {
    if (!node || node->node_type != NODE_TYPE_WIDGET) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    if (!map || !map->extra) return;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (extra->marker_count < MAX_MARKERS) {
        extra->markers[extra->marker_count].lat = lat;
        extra->markers[extra->marker_count].lon = lon;
        extra->markers[extra->marker_count].color = color;
        extra->markers[extra->marker_count].icon_code = NULL;
        extra->markers[extra->marker_count].popup_text = popup_text ? strdup(popup_text) : NULL;
        extra->marker_count++;
        aroma_node_invalidate(node);
    }
}

void aroma_map_clear_markers(AromaNode* node) {
    if (!node || !node->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (!extra) return;
    for (int i = 0; i < extra->marker_count; i++) { if (extra->markers[i].popup_text) free((void*)extra->markers[i].popup_text); }
    extra->marker_count = 0;
    extra->active_popup_idx = -1;
    aroma_node_invalidate(node);
}

void aroma_map_geocode_search(AromaNode* node, const char* query,
                               void (*callback)(GeocodeResult* results, int count, void* user_data),
                               void* user_data) {
    if (!node || !node->node_widget_ptr || !query || !callback) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (!extra) return;
    MAP_GEOCODE_MUTEX_LOCK(extra);
    if (extra->geocode_loading) { MAP_GEOCODE_MUTEX_UNLOCK(extra); return; }
    extra->geocode_loading = true;
    MAP_GEOCODE_MUTEX_UNLOCK(extra);
#ifdef __EMSCRIPTEN__
    EmscriptenGeocodeRequest* req = malloc(sizeof(EmscriptenGeocodeRequest));
    if (!req) { MAP_GEOCODE_MUTEX_LOCK(extra); extra->geocode_loading = false; MAP_GEOCODE_MUTEX_UNLOCK(extra); return; }
    strncpy(req->query, query, 255); req->query[255] = '\0';
    req->callback = callback;
    req->user_data = user_data;
    char url[1024];
    snprintf(url, sizeof(url), "https://photon.komoot.io/api/?q=%s&limit=%d&lang=en", query, MAX_GEOCODE_RESULTS);
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = __map_geocode_fetch_success;
    attr.onerror = __map_geocode_fetch_error;
    attr.timeoutMSecs = 10000;
    attr.userData = req;
    emscripten_fetch(&attr, url);
    MAP_GEOCODE_MUTEX_LOCK(extra); extra->geocode_loading = false; MAP_GEOCODE_MUTEX_UNLOCK(extra);
#else
    GeocodeRequest* req = malloc(sizeof(GeocodeRequest));
    if (!req) { MAP_GEOCODE_MUTEX_LOCK(extra); extra->geocode_loading = false; MAP_GEOCODE_MUTEX_UNLOCK(extra); return; }
    strncpy(req->query, query, 255); req->query[255] = '\0';
    req->callback = callback;
    req->user_data = user_data;
    req->node_id = node->node_id;
    pthread_t fetch_thread;
    pthread_create(&fetch_thread, NULL, geocode_fetch_worker, req);
    pthread_detach(fetch_thread);
    MAP_GEOCODE_MUTEX_LOCK(extra); extra->geocode_loading = false; MAP_GEOCODE_MUTEX_UNLOCK(extra);
#endif
}

AromaNode* aroma_map_create(AromaNode* parent, int x, int y, int width, int height) {
#ifndef __EMSCRIPTEN__
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        curl_initialized = true;
        mkdir(TILE_CACHE_DIR, 0777);
#if defined(_SC_NPROCESSORS_ONLN)
        int cores = sysconf(_SC_NPROCESSORS_ONLN);
        if (cores > 0) { num_active_workers = cores * 2; }
#endif
#if defined(ESP32)
        num_active_workers = 1;
#endif
        if (num_active_workers > MAX_WORKER_THREADS) num_active_workers = MAX_WORKER_THREADS;
        if (num_active_workers < 1) num_active_workers = 1;
        worker_running = true;
        for (int i = 0; i < num_active_workers; i++) {
            pthread_create(&worker_threads[i], NULL, tile_fetch_worker, NULL);
            pthread_detach(worker_threads[i]);
        }
    }
#endif
    AromaMap* map = (AromaMap*)calloc(1, sizeof(AromaMap));
    if (!map) return NULL;
    map->rect.x = x; map->rect.y = y; map->rect.width = width; map->rect.height = height;
    map->zoom = 6; map->center_lat = 0.0; map->center_lon = 0.0; map->show_osm_attribution = false;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)calloc(1, sizeof(struct AromaMapExtra));
    if (!extra) { free(map); return NULL; }
    extra->zoom = map->zoom;
    MAP_ROUTE_MUTEX_INIT(extra);
    MAP_GEOCODE_MUTEX_INIT(extra);
    extra->route_lats = NULL; extra->route_lons = NULL; extra->route_point_count = 0;
    extra->route_color = 0; extra->route_active = false; extra->route_loading = false;
    extra->geocode_loading = false;
    /* center_px and display_px start equal (both at Null Island for the default
       zoom), so there is no initial pan animation on load -- the map simply
       appears at rest here. Callers that want a different starting location
       should use aroma_map_set_center_instant() before the first draw, or
       aroma_map_set_center()/pan_to() if an animated pan to that location is
       actually desired. */
    extra->center_px_x = 8192.0; extra->center_px_y = 8192.0;
    extra->display_px_x = 8192.0; extra->display_px_y = 8192.0;
    extra->display_zoom = map->zoom;
    extra->velocity_x = 0; extra->velocity_y = 0;
    extra->animations_enabled = true;
    extra->anim_timer = aroma_timer_create(16, true, __map_anim_tick, extra);
    extra->font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 12);
    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, map);
    if (node) extra->node_ptr = node;
    AromaNode* root = aroma_event_get_root();
    extra->root_id = root ? root->node_id : 0;
    if (extra->root_id) { aroma_event_subscribe(extra->root_id, EVENT_TYPE_KEY_PRESS, __map_event_handler_global, extra, 90); }
    if (!node) {
        if (extra->font) aroma_font_destroy(extra->font);
        if (extra->anim_timer) aroma_timer_cancel(extra->anim_timer);
        MAP_ROUTE_MUTEX_DESTROY(extra); MAP_GEOCODE_MUTEX_DESTROY(extra);
        free(extra); free(map);
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
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_SCROLL, __map_event_handler, extra, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_CUSTOM, __map_event_handler, extra, 90);
    return node;
}

void aroma_map_set_route(AromaNode* node, double start_lat, double start_lon, double end_lat, double end_lon, uint32_t color) {
    if (!node || node->node_type != NODE_TYPE_WIDGET) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (!extra) return;
    MAP_ROUTE_MUTEX_LOCK(extra);
    extra->route_color = color;
    if (extra->route_loading) { MAP_ROUTE_MUTEX_UNLOCK(extra); return; }
    extra->route_loading = true;
    MAP_ROUTE_MUTEX_UNLOCK(extra);
#ifdef __EMSCRIPTEN__
    EmscriptenRouteRequest* req = malloc(sizeof(EmscriptenRouteRequest));
    if (!req) { MAP_ROUTE_MUTEX_LOCK(extra); extra->route_loading = false; MAP_ROUTE_MUTEX_UNLOCK(extra); return; }
    req->start_lat = start_lat; req->start_lon = start_lon;
    req->end_lat = end_lat; req->end_lon = end_lon;
    req->node = node; req->extra = extra;
    char url[512];
    snprintf(url, sizeof(url), "https://routing.openstreetmap.de/routed-car/route/v1/driving/%f,%f;%f,%f?overview=full&geometries=polyline",
             req->start_lon, req->start_lat, req->end_lon, req->end_lat);
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = __map_route_fetch_success;
    attr.onerror = __map_route_fetch_error;
    attr.timeoutMSecs = 30000;
    attr.userData = req;
    emscripten_fetch(&attr, url);
#else
    RouteRequest* req = malloc(sizeof(RouteRequest));
    req->start_lat = start_lat; req->start_lon = start_lon;
    req->end_lat = end_lat; req->end_lon = end_lon;
    req->node = node; req->extra = extra;
    pthread_t fetch_thread;
    pthread_create(&fetch_thread, NULL, route_fetch_worker, req);
    pthread_detach(fetch_thread);
#endif
}

void aroma_map_clear_route(AromaNode* node) {
    if (!node || node->node_type != NODE_TYPE_WIDGET) return;
    AromaMap* map = (AromaMap*)node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    if (!extra) return;
    MAP_ROUTE_MUTEX_LOCK(extra);
    extra->route_active = false;
    extra->route_point_count = 0;
    if (extra->route_lats) { free(extra->route_lats); extra->route_lats = NULL; }
    if (extra->route_lons) { free(extra->route_lons); extra->route_lons = NULL; }
    MAP_ROUTE_MUTEX_UNLOCK(extra);
    aroma_node_invalidate(node);
}

#endif