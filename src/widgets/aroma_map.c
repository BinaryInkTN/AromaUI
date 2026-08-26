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
#include "aroma_material_font.h"
#include "aroma_timer.h"
#include "widgets/aroma_map.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <ctype.h>
#include <stdlib.h>
#include <float.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#endif
#ifndef __EMSCRIPTEN__
#include <sys/stat.h>
#include <unistd.h>
#include <curl/curl.h>
#include <sqlite3.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

static bool __map_event_handler_global(AromaEvent *event, void *user_data);
static inline void safe_str_copy_len(char *dst, size_t dst_size, const char *src, size_t src_len);
#define TILE_CACHE_DIR "/tmp/aroma_tiles"
#define MAX_TILES_MEM 256
#define TILE_SIZE 256
#define INITIAL_MARKER_CAPACITY 512
#define MAX_QUEUE 256
#define MAX_WORKER_THREADS 16
#define MAX_ROUTE_POINTS 50000
#define MAX_TURN_INSTRUCTIONS 500
#define POI_CHUNK_SIZE 10000
#define MAX_POI_CATEGORIES 32
#define MAX_POI_NAME_LEN 128

#define GRID_CELL_SIZE 0.01
#define MAX_SEARCH_RADIUS 100

#define CHUNK_SIZE 8
#define MAX_CHUNKS 16
#define CHUNK_LOAD_DISTANCE 1

typedef void (*POIDrawCallback)(AromaNode *node, size_t window_id, PointOfInterest *poi, int draw_x, int draw_y, void *user_data);
typedef bool (*POIHitTestCallback)(PointOfInterest *poi, int draw_x, int draw_y, int click_x, int click_y, void *user_data);

typedef struct
{
    uint64_t access_seq;
    int z, x, y;
    unsigned int texture_id;
    bool is_dark;
    bool is_loading;
    bool is_ready;
    bool valid;
    char filepath[256];
} MapTile;

typedef struct
{
    int chunk_x, chunk_y;
    int z;
    MapTile *tiles[CHUNK_SIZE * CHUNK_SIZE];
    int tile_count;
    uint64_t last_access;
    bool is_loaded;
    bool is_loading;
} MapChunk;

typedef struct
{
    double lat;
    double lon;
    uint32_t color;
    char *icon_code;
    AromaFont *icon_font;
    char *popup_text;
} MapMarker;

typedef struct
{
    char query[256];
    void (*callback)(GeocodeResult *results, int count, void *user_data);
    void *user_data;
    uint64_t node_id;
} GeocodeRequest;

#ifdef __EMSCRIPTEN__
typedef struct
{
    char query[256];
    void (*callback)(GeocodeResult *results, int count, void *user_data);
    void *user_data;
} EmscriptenGeocodeRequest;

typedef struct
{
    double start_lat, start_lon, end_lat, end_lon;
    AromaNode *node;
    struct AromaMapExtra *extra;
} EmscriptenRouteRequest;

typedef struct
{
    int z, x, y;
    bool is_dark;
    struct AromaMapExtra *extra;
} EmscriptenTileRequest;
#endif

typedef struct
{
    uint32_t grid_width;
    uint32_t grid_height;
    double min_lat, min_lon;
    int *grid_cells;
    uint32_t *node_next;
    bool initialized;
} OSRMGridIndex;

typedef struct
{
    PointOfInterest *pois;
    int poi_count;
    int poi_capacity;
    bool pois_loaded;
    bool pois_visible;
    bool pois_loading;
    POICategory visible_categories[POI_CATEGORY_COUNT];
    int visible_category_count;
    pthread_mutex_t poi_mutex;
    pthread_t poi_thread;
    sqlite3 *poi_db;
    char poi_db_path[256];
    AromaNode *loading_node;
    bool shutting_down;
    pthread_cond_t worker_done_cond;
    bool worker_active;
    int total_poi_count;
    double loaded_min_lat;
    double loaded_max_lat;
    double loaded_min_lon;
    double loaded_max_lon;
} POIManager;

struct AromaMapExtra
{
    MapTile tiles[MAX_TILES_MEM];
    MapChunk chunks[MAX_CHUNKS];
    int chunk_count;
    uint64_t chunk_access_counter;
    double center_px_x;
    double center_px_y;
    int zoom;
    int min_zoom;
    int max_zoom;
    bool use_mbtiles_zoom;
    AromaNode *node_ptr;
    uint64_t root_id;
    uint64_t access_counter;
    MapMarker *markers;
    int marker_count;
    int marker_capacity;
    int active_popup_idx;
    AromaFont *font;
    AromaFont *icon_font;
    AromaTimer *anim_timer;
    double velocity_x;
    double velocity_y;
    double display_zoom;
    double display_px_x;
    double display_px_y;
    double *route_lats;
    double *route_lons;
    int route_point_count;
    uint32_t route_color;
    bool route_active;
    bool route_loading;
    pthread_mutex_t route_mutex;
    pthread_mutex_t geocode_mutex;
    bool geocode_loading;
    bool animations_enabled;
    OSRMGraph osrm_graph;
    OSRMGridIndex osrm_grid;
    TurnInstruction *turn_instructions;
    int turn_count;
    GPSPosition gps_position;
    RouteProgress route_progress;
    pthread_mutex_t gps_mutex;
    POIManager poi_manager;
    PointOfInterest *dynamic_pois;
    int dynamic_pois_capacity;
    POIDrawCallback poi_draw_callback;
    void *poi_draw_user_data;
    POIHitTestCallback poi_hit_test_callback;
    void *poi_hit_test_user_data;
    pthread_t osrm_thread;
    bool osrm_loading;
    char osrm_filepath[256];
#ifndef __EMSCRIPTEN__
    char mbtiles_path[256];
    sqlite3 *mbtiles_db;
    sqlite3_stmt *mbtiles_stmt_tile;
    pthread_mutex_t mbtiles_mutex;
#endif
};

typedef struct
{
    uint64_t node_id;
    int z, x, y;
    bool is_dark;
    char filepath[256];
    void *extra;
#ifndef __EMSCRIPTEN__
    unsigned char *image_data;
    size_t image_size;
    int img_w;
    int img_h;
#endif
} TileRequest;

typedef struct
{
    double start_lat, start_lon, end_lat, end_lon;
    AromaNode *node;
    struct AromaMapExtra *extra;
} RouteRequest;

typedef struct
{
    AromaNode *node;
    char filepath[256];
} OSRMLoadRequest;

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

#define MAP_GPS_MUTEX_INIT(extra) pthread_mutex_init(&(extra)->gps_mutex, NULL)
#define MAP_GPS_MUTEX_DESTROY(extra) pthread_mutex_destroy(&(extra)->gps_mutex)
#define MAP_GPS_MUTEX_LOCK(extra) pthread_mutex_lock(&(extra)->gps_mutex)
#define MAP_GPS_MUTEX_UNLOCK(extra) pthread_mutex_unlock(&(extra)->gps_mutex)

static pthread_t worker_threads[MAX_WORKER_THREADS];
static int num_active_workers = 2;
static bool worker_running = false;
#ifndef __EMSCRIPTEN__
static bool curl_initialized = false;
#endif

#ifndef __EMSCRIPTEN__
static size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!ptr || !stream)
        return 0;
    return fwrite(ptr, size, nmemb, stream);
}

struct MemoryStruct
{
    char *memory;
    size_t size;
};

static size_t write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    if (!mem)
        return 0;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL)
        return 0;
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}
#endif

double aroma_map_get_zoom(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return 0;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return 0;
    return extra->display_zoom;
}

static double haversine_distance(double lat1, double lon1, double lat2, double lon2)
{
    const double R = 6371000.0;
    double phi1 = lat1 * M_PI / 180.0;
    double phi2 = lat2 * M_PI / 180.0;
    double dphi = (lat2 - lat1) * M_PI / 180.0;
    double dlambda = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dphi / 2) * sin(dphi / 2) + cos(phi1) * cos(phi2) * sin(dlambda / 2) * sin(dlambda / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return R * c;
}

static double calculate_bearing(double lat1, double lon1, double lat2, double lon2)
{
    double phi1 = lat1 * M_PI / 180.0;
    double phi2 = lat2 * M_PI / 180.0;
    double dlambda = (lon2 - lon1) * M_PI / 180.0;
    double y = sin(dlambda) * cos(phi2);
    double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dlambda);
    double bearing = atan2(y, x) * 180.0 / M_PI;
    return fmod(bearing + 360.0, 360.0);
}

static void pixel_to_latlon(double px_x, double px_y, int zoom, double *lat, double *lon)
{
    double z_factor = pow(2.0, zoom) * TILE_SIZE;
    *lon = (px_x / z_factor) * 360.0 - 180.0;
    double n = M_PI - 2.0 * M_PI * (px_y / z_factor);
    *lat = 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
}

static void latlon_to_pixel(double lat, double lon, int zoom, double *px_x, double *px_y)
{
    double z_factor = pow(2.0, zoom) * TILE_SIZE;
    double lat_rad = lat * M_PI / 180.0;
    *px_x = (lon + 180.0) / 360.0 * z_factor;
    *px_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * z_factor;
}

// OPTIMIZED: MinHeap uses float for priority (was double)
// Saves 4 bytes per heap entry
typedef struct
{
    uint32_t node;
    float priority;
} HeapEntry;

typedef struct
{
    HeapEntry *entries;
    uint32_t size;
    uint32_t capacity;
} MinHeap;

static void heap_init(MinHeap *heap, uint32_t capacity)
{
    heap->entries = malloc(capacity * sizeof(HeapEntry));
    heap->size = 0;
    heap->capacity = capacity;
}

static void heap_destroy(MinHeap *heap)
{
    if (heap->entries)
        free(heap->entries);
    heap->entries = NULL;
    heap->size = 0;
    heap->capacity = 0;
}

static void heap_push(MinHeap *heap, uint32_t node, float priority)
{
    if (heap->size >= heap->capacity)
    {
        heap->capacity *= 2;
        heap->entries = realloc(heap->entries, heap->capacity * sizeof(HeapEntry));
    }
    uint32_t i = heap->size++;
    heap->entries[i].node = node;
    heap->entries[i].priority = priority;
    while (i > 0)
    {
        uint32_t parent = (i - 1) / 2;
        if (heap->entries[parent].priority <= heap->entries[i].priority)
            break;
        HeapEntry temp = heap->entries[parent];
        heap->entries[parent] = heap->entries[i];
        heap->entries[i] = temp;
        i = parent;
    }
}

static bool heap_pop(MinHeap *heap, uint32_t *node, float *priority)
{
    if (heap->size == 0)
        return false;
    *node = heap->entries[0].node;
    *priority = heap->entries[0].priority;
    heap->size--;
    if (heap->size > 0)
    {
        heap->entries[0] = heap->entries[heap->size];
        uint32_t i = 0;
        while (1)
        {
            uint32_t left = 2 * i + 1;
            uint32_t right = 2 * i + 2;
            uint32_t smallest = i;
            if (left < heap->size && heap->entries[left].priority < heap->entries[smallest].priority)
                smallest = left;
            if (right < heap->size && heap->entries[right].priority < heap->entries[smallest].priority)
                smallest = right;
            if (smallest == i)
                break;
            HeapEntry temp = heap->entries[smallest];
            heap->entries[smallest] = heap->entries[i];
            heap->entries[i] = temp;
            i = smallest;
        }
    }
    return true;
}

static void init_osrm_grid(OSRMGraph *graph, OSRMGridIndex *grid)
{
    if (!graph || !grid || graph->node_count == 0)
        return;

    grid->min_lat = graph->min_lat;
    grid->min_lon = graph->min_lon;
    grid->grid_width = (uint32_t)((graph->max_lon - graph->min_lon) / GRID_CELL_SIZE) + 1;
    grid->grid_height = (uint32_t)((graph->max_lat - graph->min_lat) / GRID_CELL_SIZE) + 1;

    uint32_t total_cells = grid->grid_width * grid->grid_height;
    grid->grid_cells = malloc(total_cells * sizeof(int));
    grid->node_next = malloc(graph->node_count * sizeof(uint32_t));

    if (!grid->grid_cells || !grid->node_next)
        return;

    for (uint32_t i = 0; i < total_cells; i++)
    {
        grid->grid_cells[i] = -1;
    }

    for (uint32_t i = 0; i < graph->node_count; i++)
    {
        int cell_x = (int)((graph->nodes[i].lon - grid->min_lon) / GRID_CELL_SIZE);
        int cell_y = (int)((graph->nodes[i].lat - grid->min_lat) / GRID_CELL_SIZE);

        if (cell_x < 0)
            cell_x = 0;
        if (cell_y < 0)
            cell_y = 0;
        if (cell_x >= (int)grid->grid_width)
            cell_x = grid->grid_width - 1;
        if (cell_y >= (int)grid->grid_height)
            cell_y = grid->grid_height - 1;

        uint32_t cell_idx = cell_y * grid->grid_width + cell_x;
        grid->node_next[i] = grid->grid_cells[cell_idx];
        grid->grid_cells[cell_idx] = i;
    }

    grid->initialized = true;
}

static void free_osrm_grid(OSRMGridIndex *grid)
{
    if (grid->grid_cells)
    {
        free(grid->grid_cells);
        grid->grid_cells = NULL;
    }
    if (grid->node_next)
    {
        free(grid->node_next);
        grid->node_next = NULL;
    }
    grid->initialized = false;
}

static uint32_t find_nearest_osrm_node(OSRMGraph *graph, OSRMGridIndex *grid, double lat, double lon)
{
    if (!graph || !grid || graph->node_count == 0)
        return 0;

    uint32_t nearest = 0;
    double min_dist = DBL_MAX;
    bool found = false;

    if (!grid->initialized)
    {
        for (uint32_t i = 0; i < graph->node_count; i++)
        {
            uint32_t edge_count = graph->adjacency_offset[i + 1] - graph->adjacency_offset[i];
            if (edge_count == 0)
                continue;

            double dist = haversine_distance(lat, lon, graph->nodes[i].lat, graph->nodes[i].lon);
            if (dist < min_dist)
            {
                min_dist = dist;
                nearest = i;
                found = true;
            }
        }
        return found ? nearest : 0;
    }

    int center_cell_x = (int)((lon - grid->min_lon) / GRID_CELL_SIZE);
    int center_cell_y = (int)((lat - grid->min_lat) / GRID_CELL_SIZE);

    double meters_per_deg_lat = 111320.0;
    double meters_per_deg_lon = 111320.0 * cos(lat * M_PI / 180.0);
    if (meters_per_deg_lon < 1.0)
        meters_per_deg_lon = 1.0;
    double cell_size_m = GRID_CELL_SIZE * (meters_per_deg_lat < meters_per_deg_lon ? meters_per_deg_lat : meters_per_deg_lon);

    for (int radius = 0; radius <= MAX_SEARCH_RADIUS; radius++)
    {
        if (found && radius > 0)
        {
            double ring_min_possible = (double)(radius - 1) * cell_size_m;
            if (ring_min_possible > min_dist)
                break;
        }

        int start_x = center_cell_x - radius;
        int end_x = center_cell_x + radius;
        int start_y = center_cell_y - radius;
        int end_y = center_cell_y + radius;

        for (int cell_y = start_y; cell_y <= end_y; cell_y++)
        {
            for (int cell_x = start_x; cell_x <= end_x; cell_x++)
            {
                if (cell_x < 0 || cell_y < 0 ||
                    cell_x >= (int)grid->grid_width || cell_y >= (int)grid->grid_height)
                    continue;

                if (radius > 0 &&
                    cell_x > start_x && cell_x < end_x &&
                    cell_y > start_y && cell_y < end_y)
                    continue;

                uint32_t cell_idx = cell_y * grid->grid_width + cell_x;
                int node_idx = grid->grid_cells[cell_idx];

                while (node_idx >= 0)
                {
                    uint32_t edge_count = graph->adjacency_offset[node_idx + 1] - graph->adjacency_offset[node_idx];

                    if (edge_count > 0)
                    {
                        double dist = haversine_distance(lat, lon,
                                                         graph->nodes[node_idx].lat, graph->nodes[node_idx].lon);
                        if (dist < min_dist)
                        {
                            min_dist = dist;
                            nearest = (uint32_t)node_idx;
                            found = true;
                        }
                    }

                    node_idx = grid->node_next[node_idx];
                }
            }
        }
    }

    if (!found)
    {
        for (uint32_t i = 0; i < graph->node_count; i++)
        {
            uint32_t edge_count = graph->adjacency_offset[i + 1] - graph->adjacency_offset[i];
            if (edge_count == 0)
                continue;

            double dist = haversine_distance(lat, lon, graph->nodes[i].lat, graph->nodes[i].lon);
            if (dist < min_dist)
            {
                min_dist = dist;
                nearest = i;
                found = true;
            }
        }
    }

    return found ? nearest : 0;
}

static void osrm_dijkstra(OSRMGraph *graph, uint32_t start, uint32_t end, uint32_t **path, uint32_t *path_len)
{
    if (!graph || !path || !path_len || graph->node_count == 0)
        return;
    if (start >= graph->node_count || end >= graph->node_count)
        return;

    uint32_t start_edges = graph->adjacency_offset[start + 1] - graph->adjacency_offset[start];
    uint32_t end_edges = graph->adjacency_offset[end + 1] - graph->adjacency_offset[end];

    if (start_edges == 0 || end_edges == 0)
    {
        *path_len = 0;
        *path = NULL;
        return;
    }

    // OPTIMIZED: Removed arrival_edge array (saves 4 bytes per node)
    // OPTIMIZED: Using float instead of double for dist (saves 4 bytes per node)
    // Total peak memory savings: ~62MB at 7.8M nodes
    float *dist = malloc(graph->node_count * sizeof(float));
    uint32_t *prev = malloc(graph->node_count * sizeof(uint32_t));
    bool *visited = calloc(graph->node_count, sizeof(bool));

    if (!dist || !prev || !visited)
    {
        if (dist)
            free(dist);
        if (prev)
            free(prev);
        if (visited)
            free(visited);
        *path_len = 0;
        *path = NULL;
        return;
    }

    MinHeap heap;
    heap_init(&heap, 1024);

    for (uint32_t i = 0; i < graph->node_count; i++)
    {
        dist[i] = FLT_MAX;
        prev[i] = UINT32_MAX;
    }

    dist[start] = 0.0f;
    heap_push(&heap, start, 0.0f);

    while (heap.size > 0)
    {
        uint32_t current;
        float priority;
        heap_pop(&heap, &current, &priority);

        if (visited[current])
            continue;
        visited[current] = true;

        uint32_t edge_start = graph->adjacency_offset[current];
        uint32_t edge_end = graph->adjacency_offset[current + 1];

        for (uint32_t i = edge_start; i < edge_end; i++)
        {
            uint32_t edge_idx = graph->adjacency_list[i];
            uint32_t to = graph->edges[edge_idx].to_node;
            double weight = graph->edges[edge_idx].weight;

            if (to >= graph->node_count)
                continue;

            // OPTIMIZED: Use prev[] instead of arrival_edge[] for backtrack detection
            if (prev[current] != UINT32_MAX && prev[current] == to)
            {
                weight *= 10.0;
            }

            if (!visited[to] && dist[current] + weight < dist[to])
            {
                dist[to] = dist[current] + (float)weight;
                prev[to] = current;
                double h = haversine_distance(
                               graph->nodes[to].lat, graph->nodes[to].lon,
                               graph->nodes[end].lat, graph->nodes[end].lon) /
                           5.0;
                heap_push(&heap, to, dist[to] + (float)h);
            }
        }
    }

    *path_len = 0;
    *path = NULL;

    if (dist[end] == FLT_MAX)
    {
        free(dist);
        free(prev);
        free(visited);
        heap_destroy(&heap);
        return;
    }

    uint32_t current = end;
    uint32_t count = 0;
    while (current != UINT32_MAX && count < MAX_ROUTE_POINTS)
    {
        count++;
        current = prev[current];
    }

    if (count >= MAX_ROUTE_POINTS)
    {
        free(dist);
        free(prev);
        free(visited);
        heap_destroy(&heap);
        return;
    }

    *path = malloc(count * sizeof(uint32_t));
    if (!*path)
    {
        free(dist);
        free(prev);
        free(visited);
        heap_destroy(&heap);
        return;
    }

    current = end;
    for (uint32_t i = 0; i < count; i++)
    {
        (*path)[count - 1 - i] = current;
        current = prev[current];
    }
    *path_len = count;

    free(dist);
    free(prev);
    free(visited);
    heap_destroy(&heap);
}

static void generate_turn_instructions(struct AromaMapExtra *extra, uint32_t *path, uint32_t path_len)
{
    if (!extra || !path || path_len < 2)
        return;

    if (extra->turn_instructions)
    {
        free(extra->turn_instructions);
        extra->turn_instructions = NULL;
    }
    extra->turn_count = 0;

    extra->turn_instructions = malloc(MAX_TURN_INSTRUCTIONS * sizeof(TurnInstruction));
    if (!extra->turn_instructions)
        return;

    double total_distance = 0;

    for (uint32_t i = 0; i < path_len - 1; i++)
    {
        total_distance += haversine_distance(
            extra->osrm_graph.nodes[path[i]].lat, extra->osrm_graph.nodes[path[i]].lon,
            extra->osrm_graph.nodes[path[i + 1]].lat, extra->osrm_graph.nodes[path[i + 1]].lon);
    }

    double cumulative = 0;
    bool in_roundabout = false;

    for (uint32_t i = 0; i < path_len - 1; i++)
    {
        double seg_dist = haversine_distance(
            extra->osrm_graph.nodes[path[i]].lat, extra->osrm_graph.nodes[path[i]].lon,
            extra->osrm_graph.nodes[path[i + 1]].lat, extra->osrm_graph.nodes[path[i + 1]].lon);
        cumulative += seg_dist;

        bool current_edge_is_roundabout = false;
        for (uint32_t e = extra->osrm_graph.adjacency_offset[path[i]];
             e < extra->osrm_graph.adjacency_offset[path[i] + 1]; e++)
        {
            uint32_t edge_idx = extra->osrm_graph.adjacency_list[e];
            if (extra->osrm_graph.edges[edge_idx].to_node == path[i + 1])
            {
                current_edge_is_roundabout = extra->osrm_graph.edges[edge_idx].is_roundabout;
                break;
            }
        }

        if (current_edge_is_roundabout && !in_roundabout)
        {
            TurnInstruction *instr = &extra->turn_instructions[extra->turn_count];
            instr->type = MANEUVER_ROUNDABOUT;
            instr->lat = extra->osrm_graph.nodes[path[i]].lat;
            instr->lon = extra->osrm_graph.nodes[path[i]].lon;
            instr->distance_to_turn = total_distance - cumulative;
            instr->distance_remaining = total_distance - cumulative;
            instr->time_remaining = (total_distance - cumulative) / 13.89;
            instr->speed_limit = 30;
            instr->roundabout_exit = 1;
            snprintf(instr->road_name, sizeof(instr->road_name), "Roundabout");
            extra->turn_count++;
            in_roundabout = true;
        }
        else if (!current_edge_is_roundabout && in_roundabout)
        {
            in_roundabout = false;
        }

        if (!in_roundabout && !current_edge_is_roundabout && i < path_len - 2)
        {
            double bearing1 = calculate_bearing(
                extra->osrm_graph.nodes[path[i]].lat, extra->osrm_graph.nodes[path[i]].lon,
                extra->osrm_graph.nodes[path[i + 1]].lat, extra->osrm_graph.nodes[path[i + 1]].lon);
            double bearing2 = calculate_bearing(
                extra->osrm_graph.nodes[path[i + 1]].lat, extra->osrm_graph.nodes[path[i + 1]].lon,
                extra->osrm_graph.nodes[path[i + 2]].lat, extra->osrm_graph.nodes[path[i + 2]].lon);

            double diff = bearing2 - bearing1;
            while (diff > 180)
                diff -= 360;
            while (diff < -180)
                diff += 360;

            if (fabs(diff) > 40.0 && extra->turn_count < MAX_TURN_INSTRUCTIONS - 1)
            {
                TurnInstruction *instr = &extra->turn_instructions[extra->turn_count];

                if (diff > 40.0 && diff <= 160.0)
                    instr->type = MANEUVER_TURN_RIGHT;
                else if (diff < -40.0 && diff >= -160.0)
                    instr->type = MANEUVER_TURN_LEFT;
                else
                    instr->type = MANEUVER_UTURN;

                instr->lat = extra->osrm_graph.nodes[path[i + 1]].lat;
                instr->lon = extra->osrm_graph.nodes[path[i + 1]].lon;
                instr->distance_to_turn = total_distance - cumulative;
                instr->distance_remaining = total_distance - cumulative;
                instr->time_remaining = (total_distance - cumulative) / 13.89;
                instr->speed_limit = 50;
                instr->roundabout_exit = 0;
                instr->road_name[0] = '\0';
                extra->turn_count++;
            }
        }
    }

    if (extra->turn_count < MAX_TURN_INSTRUCTIONS)
    {
        TurnInstruction *instr = &extra->turn_instructions[extra->turn_count];
        instr->type = MANEUVER_ARRIVE;
        instr->lat = extra->osrm_graph.nodes[path[path_len - 1]].lat;
        instr->lon = extra->osrm_graph.nodes[path[path_len - 1]].lon;
        instr->distance_to_turn = 0;
        instr->distance_remaining = 0;
        instr->time_remaining = 0;
        instr->speed_limit = 50;
        instr->roundabout_exit = 0;
        snprintf(instr->road_name, sizeof(instr->road_name), "Destination");
        extra->turn_count++;
    }

    extra->route_progress.total_distance_km = total_distance / 1000.0;
    extra->route_progress.total_time_seconds = total_distance / 13.89;
    extra->route_progress.distance_to_destination = total_distance;
    extra->route_progress.time_to_destination = total_distance / 13.89;
}

static void update_route_progress(struct AromaMapExtra *extra)
{
    if (!extra || !extra->route_active || extra->turn_count == 0)
        return;

    int current_idx = extra->route_progress.next_turn_index;
    if (current_idx >= extra->turn_count)
        current_idx = 0;

    while (current_idx < extra->turn_count - 1)
    {
        double distance_to_current = haversine_distance(
            extra->gps_position.lat, extra->gps_position.lon,
            extra->turn_instructions[current_idx].lat, extra->turn_instructions[current_idx].lon);

        bool close_enough = distance_to_current < 50.0;
        bool moving_away = extra->route_progress.last_distance_to_turn > 0.0 &&
                           distance_to_current > extra->route_progress.last_distance_to_turn &&
                           extra->route_progress.last_distance_to_turn < 50.0;

        if (close_enough && moving_away)
        {
            current_idx++;
            extra->route_progress.next_turn_index = current_idx;
            extra->route_progress.last_distance_to_turn = 0.0;
        }
        else
        {
            extra->route_progress.last_distance_to_turn = distance_to_current;
            break;
        }
    }

    if (current_idx >= extra->turn_count)
    {
        current_idx = extra->turn_count - 1;
        extra->route_progress.next_turn_index = current_idx;
    }

    extra->route_progress.distance_to_next_turn = haversine_distance(
        extra->gps_position.lat, extra->gps_position.lon,
        extra->turn_instructions[current_idx].lat, extra->turn_instructions[current_idx].lon);
    extra->route_progress.time_to_next_turn = extra->route_progress.distance_to_next_turn / 13.89;

    double remaining = 0;
    for (int i = current_idx; i < extra->turn_count - 1; i++)
    {
        remaining += haversine_distance(
            extra->turn_instructions[i].lat, extra->turn_instructions[i].lon,
            extra->turn_instructions[i + 1].lat, extra->turn_instructions[i + 1].lon);
    }
    extra->route_progress.distance_to_destination = remaining;
    extra->route_progress.time_to_destination = remaining / 13.89;
}

static bool load_osrm_binary(OSRMGraph *graph, const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
        return false;

    char header[16];
    if (fread(header, 1, 15, fp) != 15)
    {
        fclose(fp);
        return false;
    }
    header[15] = '\0';

    // Accept both old V2 and new V3 formats
    if (strcmp(header, "OSRM_PROD_V2") != 0 && strcmp(header, "OSRM_PROD_V3") != 0)
    {
        fclose(fp);
        return false;
    }

    if (fread(&graph->min_lat, sizeof(double), 1, fp) != 1)
        goto error;
    if (fread(&graph->min_lon, sizeof(double), 1, fp) != 1)
        goto error;
    if (fread(&graph->max_lat, sizeof(double), 1, fp) != 1)
        goto error;
    if (fread(&graph->max_lon, sizeof(double), 1, fp) != 1)
        goto error;
    if (fread(&graph->node_count, sizeof(uint32_t), 1, fp) != 1)
        goto error;

    graph->nodes = calloc(graph->node_count, sizeof(OSRMNode));
    if (!graph->nodes)
        goto error;

    for (uint32_t i = 0; i < graph->node_count; i++)
    {
        uint32_t node_id;  // Temporary - read from file but not stored
        uint8_t has_traffic_light;
        if (fread(&node_id, sizeof(uint32_t), 1, fp) != 1)
            goto error;
        if (fread(&graph->nodes[i].lat, sizeof(double), 1, fp) != 1)
            goto error;
        if (fread(&graph->nodes[i].lon, sizeof(double), 1, fp) != 1)
            goto error;
        if (fread(&has_traffic_light, sizeof(uint8_t), 1, fp) != 1)
            goto error;
        // node_id and has_traffic_light are read but not stored (dead fields)
    }

    if (fread(&graph->edge_count, sizeof(uint32_t), 1, fp) != 1)
        goto error;

    graph->edges = calloc(graph->edge_count, sizeof(OSRMEdge));
    if (!graph->edges)
        goto error;

    for (uint32_t i = 0; i < graph->edge_count; i++)
    {
        uint32_t edge_id;  
        uint8_t speed;     
        uint8_t priority;  
        uint32_t is_roundabout;
        if (fread(&edge_id, sizeof(uint32_t), 1, fp) != 1)
            goto error;
        if (fread(&graph->edges[i].from_node, sizeof(uint32_t), 1, fp) != 1)
            goto error;
        if (fread(&graph->edges[i].to_node, sizeof(uint32_t), 1, fp) != 1)
            goto error;
        if (fread(&graph->edges[i].weight, sizeof(double), 1, fp) != 1)
            goto error;
        if (fread(&graph->edges[i].distance, sizeof(double), 1, fp) != 1)
            goto error;
        if (fread(&speed, sizeof(uint8_t), 1, fp) != 1)
            goto error;
        if (fread(&priority, sizeof(uint8_t), 1, fp) != 1)
            goto error;
        if (fread(&is_roundabout, sizeof(uint32_t), 1, fp) != 1)
            goto error;

        graph->edges[i].is_roundabout = (is_roundabout != 0);
    }

    graph->adjacency_offset = calloc(graph->node_count + 1, sizeof(uint32_t));
    graph->adjacency_list = malloc(graph->edge_count * sizeof(uint32_t));
    if (!graph->adjacency_offset || !graph->adjacency_list)
        goto error;

    for (uint32_t i = 0; i < graph->edge_count; i++)
    {
        graph->adjacency_offset[graph->edges[i].from_node + 1]++;
    }

    for (uint32_t i = 1; i <= graph->node_count; i++)
    {
        graph->adjacency_offset[i] += graph->adjacency_offset[i - 1];
    }

    uint32_t *temp_offset = calloc(graph->node_count, sizeof(uint32_t));
    if (!temp_offset)
        goto error;

    for (uint32_t i = 0; i < graph->edge_count; i++)
    {
        uint32_t from = graph->edges[i].from_node;
        uint32_t idx = graph->adjacency_offset[from] + temp_offset[from];
        graph->adjacency_list[idx] = i;
        temp_offset[from]++;
    }
    free(temp_offset);

    fclose(fp);
    graph->is_loaded = true;
    return true;

error:
    fclose(fp);
    if (graph->nodes)
    {
        free(graph->nodes);
        graph->nodes = NULL;
    }
    if (graph->edges)
    {
        free(graph->edges);
        graph->edges = NULL;
    }
    if (graph->adjacency_offset)
    {
        free(graph->adjacency_offset);
        graph->adjacency_offset = NULL;
    }
    if (graph->adjacency_list)
    {
        free(graph->adjacency_list);
        graph->adjacency_list = NULL;
    }
    graph->node_count = 0;
    graph->edge_count = 0;
    return false;
}

static void free_osrm_graph(OSRMGraph *graph)
{
    if (graph->nodes)
    {
        free(graph->nodes);
        graph->nodes = NULL;
    }
    if (graph->edges)
    {
        free(graph->edges);
        graph->edges = NULL;
    }
    if (graph->adjacency_offset)
    {
        free(graph->adjacency_offset);
        graph->adjacency_offset = NULL;
    }
    if (graph->adjacency_list)
    {
        free(graph->adjacency_list);
        graph->adjacency_list = NULL;
    }
    graph->node_count = 0;
    graph->edge_count = 0;
    graph->is_loaded = false;
}

static void read_mbtiles_zoom_range(sqlite3 *db, int *min_zoom, int *max_zoom)
{
    if (!db || !min_zoom || !max_zoom)
        return;
    *min_zoom = 0;
    *max_zoom = 18;
    sqlite3_stmt *stmt;
    const char *sql = "SELECT value FROM metadata WHERE name = 'minzoom'";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char *val = (const char *)sqlite3_column_text(stmt, 0);
            if (val)
                *min_zoom = atoi(val);
        }
        sqlite3_finalize(stmt);
    }
    sql = "SELECT value FROM metadata WHERE name = 'maxzoom'";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char *val = (const char *)sqlite3_column_text(stmt, 0);
            if (val)
                *max_zoom = atoi(val);
        }
        sqlite3_finalize(stmt);
    }
    if (*min_zoom < 0)
        *min_zoom = 0;
    if (*max_zoom > 18)
        *max_zoom = 18;
    if (*max_zoom < *min_zoom)
        *max_zoom = *min_zoom;
}

static void clamp_zoom_to_mbtiles(struct AromaMapExtra *extra)
{
    if (!extra || !extra->use_mbtiles_zoom)
        return;
    if (extra->zoom < extra->min_zoom)
        extra->zoom = extra->min_zoom;
    if (extra->zoom > extra->max_zoom)
        extra->zoom = extra->max_zoom;
    if (extra->display_zoom < extra->min_zoom)
        extra->display_zoom = extra->min_zoom;
    if (extra->display_zoom > extra->max_zoom)
        extra->display_zoom = extra->max_zoom;
}

static void parse_photon_json(const char *json, GeocodeResult *results, int *count)
{
    *count = 0;
    if (!json || !json[0] || !results || !count)
        return;
    const char *features_start = strstr(json, "\"features\"");
    if (!features_start)
        return;
    const char *ptr = features_start;
    while (*count < MAX_GEOCODE_RESULTS)
    {
        const char *type = strstr(ptr, "\"type\":\"Feature\"");
        if (!type)
            break;
        const char *coords = strstr(type, "\"coordinates\"");
        if (!coords)
        {
            ptr = type + 15;
            continue;
        }
        coords = strchr(coords, '[');
        if (!coords)
        {
            ptr = type + 15;
            continue;
        }
        coords++;
        char *endptr;
        results[*count].lon = strtod(coords, &endptr);
        coords = endptr;
        coords = strchr(coords, ',');
        if (!coords)
        {
            ptr = type + 15;
            continue;
        }
        coords++;
        results[*count].lat = strtod(coords, &endptr);
        const char *properties = strstr(type, "\"properties\"");
        if (!properties)
        {
            ptr = type + 15;
            continue;
        }
        const char *name_key = strstr(properties, "\"name\"");
        if (!name_key)
        {
            ptr = type + 15;
            continue;
        }
        name_key = strchr(name_key, ':');
        if (!name_key)
        {
            ptr = type + 15;
            continue;
        }
        name_key++;
        while (*name_key == ' ' || *name_key == '"')
            name_key++;
        const char *name_end = name_key;
        while (*name_end && *name_end != '"')
            name_end++;
        size_t len = name_end - name_key;
        if (len > 255)
            len = 255;
        memcpy(results[*count].display_name, name_key, len);
        results[*count].display_name[len] = '\0';
        strcpy(results[*count].category, "Location");
        strcpy(results[*count].type, "place");
        ptr = type + 15;
        (*count)++;
    }
}

static void decode_polyline(const char *encoded, double **lats, double **lons, int *count)
{
    if (!encoded || !lats || !lons || !count)
        return;
    int cap = MAX_ROUTE_POINTS;
    *lats = malloc(cap * sizeof(double));
    *lons = malloc(cap * sizeof(double));
    if (!*lats || !*lons)
    {
        free(*lats);
        free(*lons);
        *lats = NULL;
        *lons = NULL;
        *count = 0;
        return;
    }
    *count = 0;
    int index = 0, len = strlen(encoded), lat = 0, lon = 0;
    while (index < len && *count < cap)
    {
        int b, shift = 0, result = 0;
        do
        {
            if (index >= len)
                break;
            b = encoded[index++] - 63;
            result |= (b & 0x1f) << shift;
            shift += 5;
        } while (b >= 0x20 && index < len);
        int dlat = ((result & 1) ? ~(result >> 1) : (result >> 1));
        lat += dlat;
        shift = 0;
        result = 0;
        do
        {
            if (index >= len)
                break;
            b = encoded[index++] - 63;
            result |= (b & 0x1f) << shift;
            shift += 5;
        } while (b >= 0x20 && index < len);
        int dlon = ((result & 1) ? ~(result >> 1) : (result >> 1));
        lon += dlon;
        (*lats)[*count] = lat / 1e5;
        (*lons)[*count] = lon / 1e5;
        (*count)++;
    }
}

static bool __map_apply_route_response(struct AromaMapExtra *extra, AromaNode *node, const char *response)
{
    if (!extra || !response)
        return false;
    char *response_copy = strdup(response);
    if (!response_copy)
        return false;
    char *geom_start = strstr(response_copy, "\"geometry\":\"");
    if (!geom_start)
    {
        free(response_copy);
        return false;
    }
    geom_start += 12;
    char *geom_end = strchr(geom_start, '"');
    if (!geom_end)
    {
        free(response_copy);
        return false;
    }
    *geom_end = '\0';
    double *rlats = NULL, *rlons = NULL;
    int rcount = 0;
    decode_polyline(geom_start, &rlats, &rlons, &rcount);
    if (rcount == 0 || !rlats || !rlons)
    {
        free(response_copy);
        free(rlats);
        free(rlons);
        return false;
    }
    for (int n = 0; n < rcount; n++)
    {
        double lat_rad = rlats[n] * M_PI / 180.0;
        rlats[n] = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0;
        rlons[n] = (rlons[n] + 180.0) / 360.0;
    }
    MAP_ROUTE_MUTEX_LOCK(extra);
    if (extra->route_lats)
        free(extra->route_lats);
    if (extra->route_lons)
        free(extra->route_lons);
    extra->route_lats = rlats;
    extra->route_lons = rlons;
    extra->route_point_count = rcount;
    extra->route_active = true;
    extra->route_loading = false;
    extra->route_progress.last_distance_to_turn = 0.0;
    extra->route_progress.next_turn_index = 0;
    MAP_ROUTE_MUTEX_UNLOCK(extra);
    free(response_copy);
    if (node)
    {
        AromaEvent *ev = aroma_event_create_custom(node->node_id, 999, NULL, NULL);
        if (ev)
            aroma_event_queue(ev);
    }
    return true;
}

#ifndef __EMSCRIPTEN__
static void *route_fetch_worker(void *arg)
{
    RouteRequest *req = (RouteRequest *)arg;
    if (!req || !req->extra)
    {
        if (req)
            free(req);
        return NULL;
    }
    struct AromaMapExtra *extra = req->extra;
    char url[512];
    snprintf(url, sizeof(url), "https://routing.openstreetmap.de/routed-car/route/v1/driving/%f,%f;%f,%f?overview=full&geometries=polyline",
             req->start_lon, req->start_lat, req->end_lon, req->end_lat);
    CURL *curl = curl_easy_init();
    if (curl)
    {
        struct MemoryStruct chunk;
        chunk.memory = malloc(1);
        chunk.size = 0;
        if (chunk.memory)
        {
            chunk.memory[0] = '\0';
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "AromaUI/1.0");
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            CURLcode res = curl_easy_perform(curl);
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (res == CURLE_OK && http_code == 200 && chunk.size > 0)
                __map_apply_route_response(extra, req->node, chunk.memory);
            else
            {
                MAP_ROUTE_MUTEX_LOCK(extra);
                extra->route_loading = false;
                extra->route_active = false;
                MAP_ROUTE_MUTEX_UNLOCK(extra);
            }
            free(chunk.memory);
        }
        curl_easy_cleanup(curl);
    }
    else
    {
        MAP_ROUTE_MUTEX_LOCK(extra);
        extra->route_loading = false;
        MAP_ROUTE_MUTEX_UNLOCK(extra);
    }
    free(req);
    return NULL;
}

static void *geocode_fetch_worker(void *arg)
{
    GeocodeRequest *req = (GeocodeRequest *)arg;
    if (!req)
        return NULL;
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        if (req->callback)
            req->callback(NULL, 0, req->user_data);
        free(req);
        return NULL;
    }
    char *encoded_query = curl_easy_escape(curl, req->query, 0);
    if (!encoded_query)
    {
        curl_easy_cleanup(curl);
        if (req->callback)
            req->callback(NULL, 0, req->user_data);
        free(req);
        return NULL;
    }
    char url[1024];
    snprintf(url, sizeof(url), "https://photon.komoot.io/api/?q=%s&limit=%d&lang=en", encoded_query, MAX_GEOCODE_RESULTS);
    curl_free(encoded_query);
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;
    if (!chunk.memory)
    {
        curl_easy_cleanup(curl);
        if (req->callback)
            req->callback(NULL, 0, req->user_data);
        free(req);
        return NULL;
    }
    chunk.memory[0] = '\0';
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AromaInfotainment/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    GeocodeResult results[MAX_GEOCODE_RESULTS];
    memset(results, 0, sizeof(results));
    int count = 0;
    if (res == CURLE_OK && http_code == 200 && chunk.memory && chunk.size > 0)
        parse_photon_json(chunk.memory, results, &count);
    if (req->callback)
        req->callback(results, count, req->user_data);
    free(chunk.memory);
    free(req);
    return NULL;
}

extern unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);
extern void stbi_image_free(void *retval_from_stbi_load);

static void *tile_fetch_worker(void *arg)
{
    (void)arg;
    while (worker_running)
    {
        TileRequest req;
        bool has_req = false;
        pthread_mutex_lock(&queue_mutex);
        while (queue_head == queue_tail && worker_running)
        {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            pthread_cond_timedwait(&queue_cond, &queue_mutex, &ts);
        }
        if (!worker_running)
        {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        if (queue_head != queue_tail)
        {
            req = fetch_queue[queue_head];
            queue_head = (queue_head + 1) % MAX_QUEUE;
            has_req = true;
        }
        pthread_mutex_unlock(&queue_mutex);
        if (has_req)
        {
            req.image_data = NULL;
            req.image_size = 0;
            if (access(req.filepath, F_OK) != -1)
            {
                AromaEvent *ev = aroma_event_create_custom(req.node_id, 999, NULL, NULL);
                if (ev)
                    aroma_event_queue(ev);
                continue;
            }
            bool loaded_from_mbtiles = false;
            struct AromaMapExtra *extra = (struct AromaMapExtra *)req.extra;
            if (extra)
            {
                pthread_mutex_lock(&extra->mbtiles_mutex);
                if (extra->mbtiles_db && extra->mbtiles_stmt_tile)
                {
                    int tms_y = (1 << req.z) - 1 - req.y;
                    sqlite3_reset(extra->mbtiles_stmt_tile);
                    sqlite3_bind_int(extra->mbtiles_stmt_tile, 1, req.z);
                    sqlite3_bind_int(extra->mbtiles_stmt_tile, 2, req.x);
                    sqlite3_bind_int(extra->mbtiles_stmt_tile, 3, tms_y);
                    if (sqlite3_step(extra->mbtiles_stmt_tile) == SQLITE_ROW)
                    {
                        const void *blob = sqlite3_column_blob(extra->mbtiles_stmt_tile, 0);
                        int size = sqlite3_column_bytes(extra->mbtiles_stmt_tile, 0);
                        if (blob && size > 0)
                        {
                            int w, h, c;
                            unsigned char *rgba = stbi_load_from_memory((const unsigned char *)blob, size, &w, &h, &c, 4);
                            if (rgba)
                            {
                                req.image_data = rgba;
                                req.img_w = w;
                                req.img_h = h;
                                loaded_from_mbtiles = true;
                            }
                        }
                    }
                }
                pthread_mutex_unlock(&extra->mbtiles_mutex);
            }
            TileRequest *event_req = malloc(sizeof(TileRequest));
            if (event_req)
            {
                *event_req = req;
                AromaEvent *ev = aroma_event_create_custom(req.node_id, 999, event_req, NULL);
                if (ev)
                    aroma_event_queue(ev);
                else
                {
                    if (event_req->image_data)
                        free(event_req->image_data);
                    free(event_req);
                }
            }
            if (loaded_from_mbtiles || (extra && extra->mbtiles_db))
            {
                continue;
            }
            char url[512];
            if (req.is_dark)
                snprintf(url, sizeof(url), "https://a.basemaps.cartocdn.com/dark_all/%d/%d/%d.png", req.z, req.x, req.y);
            else
                snprintf(url, sizeof(url), "https://tile.openstreetmap.org/%d/%d/%d.png", req.z, req.x, req.y);
            char tmp_path[512];
            snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", req.filepath);
            CURL *curl = curl_easy_init();
            if (curl)
            {
                FILE *fp = fopen(tmp_path, "wb");
                if (fp)
                {
                    curl_easy_setopt(curl, CURLOPT_URL, url);
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
                    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AromaUI/1.0");
                    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
                    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
                    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
                    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
                    CURLcode res = curl_easy_perform(curl);
                    fclose(fp);
                    if (res == CURLE_OK)
                    {
                        rename(tmp_path, req.filepath);
                        TileRequest *event_req = malloc(sizeof(TileRequest));
                        if (event_req)
                        {
                            *event_req = req;
                            AromaEvent *ev = aroma_event_create_custom(req.node_id, 999, event_req, NULL);
                            if (ev)
                                aroma_event_queue(ev);
                            else
                            {
                                if (event_req->image_data)
                                    free(event_req->image_data);
                                free(event_req);
                            }
                        }
                    }
                    else
                        unlink(tmp_path);
                }
                curl_easy_cleanup(curl);
            }
        }
    }
    return NULL;
}
#endif

static void parse_poi_from_stmt(sqlite3_stmt *stmt, PointOfInterest *poi)
{
    memset(poi, 0, sizeof(PointOfInterest));
    poi->osm_id = sqlite3_column_int(stmt, 0);
    const char *category_name = (const char *)sqlite3_column_text(stmt, 1);
    const char *name = (const char *)sqlite3_column_text(stmt, 2);
    int name_len = sqlite3_column_bytes(stmt, 2);
    const char *name_en = (const char *)sqlite3_column_text(stmt, 3);
    int name_en_len = sqlite3_column_bytes(stmt, 3);
    poi->lat = sqlite3_column_double(stmt, 4);
    poi->lon = sqlite3_column_double(stmt, 5);
    const char *street = (const char *)sqlite3_column_text(stmt, 6);
    int street_len = sqlite3_column_bytes(stmt, 6);
    const char *street_en = (const char *)sqlite3_column_text(stmt, 7);
    int street_en_len = sqlite3_column_bytes(stmt, 7);
    const char *area = (const char *)sqlite3_column_text(stmt, 8);
    int area_len = sqlite3_column_bytes(stmt, 8);
    const char *area_en = (const char *)sqlite3_column_text(stmt, 9);
    int area_en_len = sqlite3_column_bytes(stmt, 9);
    const char *area_type = (const char *)sqlite3_column_text(stmt, 10);
    int area_type_len = sqlite3_column_bytes(stmt, 10);
    const char *city = (const char *)sqlite3_column_text(stmt, 11);
    int city_len = sqlite3_column_bytes(stmt, 11);
    const char *city_en = (const char *)sqlite3_column_text(stmt, 12);
    int city_en_len = sqlite3_column_bytes(stmt, 12);
    const char *address = (const char *)sqlite3_column_text(stmt, 13);
    int address_len = sqlite3_column_bytes(stmt, 13);
    const char *phone = (const char *)sqlite3_column_text(stmt, 14);
    int phone_len = sqlite3_column_bytes(stmt, 14);
    const char *website = (const char *)sqlite3_column_text(stmt, 15);
    int website_len = sqlite3_column_bytes(stmt, 15);
    const char *opening_hours = (const char *)sqlite3_column_text(stmt, 16);
    int opening_hours_len = sqlite3_column_bytes(stmt, 16);
    const char *operator_name = (const char *)sqlite3_column_text(stmt, 17);
    int operator_name_len = sqlite3_column_bytes(stmt, 17);
    const char *brand = (const char *)sqlite3_column_text(stmt, 18);
    int brand_len = sqlite3_column_bytes(stmt, 18);
    const char *fuel_types = (const char *)sqlite3_column_text(stmt, 19);

    poi->category = POI_CATEGORY_OTHER_BUSINESS;

    if (category_name && category_name[0])
    {
        if (strcmp(category_name, "gas_stations") == 0 || strcmp(category_name, "fuel") == 0)
            poi->category = POI_CATEGORY_GAS_STATION;
        else if (strcmp(category_name, "restaurants") == 0 || strcmp(category_name, "restaurant") == 0)
            poi->category = POI_CATEGORY_RESTAURANT;
        else if (strcmp(category_name, "cafes") == 0 || strcmp(category_name, "cafe") == 0)
            poi->category = POI_CATEGORY_CAFE;
        else if (strcmp(category_name, "fast_food") == 0)
            poi->category = POI_CATEGORY_FAST_FOOD;
        else if (strcmp(category_name, "shops") == 0 || strcmp(category_name, "shop") == 0)
            poi->category = POI_CATEGORY_SHOP;
        else if (strcmp(category_name, "supermarkets") == 0 || strcmp(category_name, "supermarket") == 0)
            poi->category = POI_CATEGORY_SUPERMARKET;
        else if (strcmp(category_name, "convenience") == 0)
            poi->category = POI_CATEGORY_CONVENIENCE;
        else if (strcmp(category_name, "hotels") == 0 || strcmp(category_name, "hotel") == 0)
            poi->category = POI_CATEGORY_HOTEL;
        else if (strcmp(category_name, "banks") == 0 || strcmp(category_name, "bank") == 0)
            poi->category = POI_CATEGORY_BANK;
        else if (strcmp(category_name, "atms") == 0 || strcmp(category_name, "atm") == 0)
            poi->category = POI_CATEGORY_ATM;
        else if (strcmp(category_name, "pharmacies") == 0 || strcmp(category_name, "pharmacy") == 0)
            poi->category = POI_CATEGORY_PHARMACY;
        else if (strcmp(category_name, "hospitals") == 0 || strcmp(category_name, "hospital") == 0)
            poi->category = POI_CATEGORY_HOSPITAL;
        else if (strcmp(category_name, "clinics") == 0 || strcmp(category_name, "clinic") == 0)
            poi->category = POI_CATEGORY_CLINIC;
        else if (strcmp(category_name, "schools") == 0 || strcmp(category_name, "school") == 0)
            poi->category = POI_CATEGORY_SCHOOL;
        else if (strcmp(category_name, "universities") == 0 || strcmp(category_name, "university") == 0)
            poi->category = POI_CATEGORY_UNIVERSITY;
        else if (strcmp(category_name, "parking") == 0)
            poi->category = POI_CATEGORY_PARKING;
        else if (strcmp(category_name, "charging_stations") == 0 || strcmp(category_name, "charging_station") == 0)
            poi->category = POI_CATEGORY_CHARGING_STATION;
        else if (strcmp(category_name, "car_repair") == 0)
            poi->category = POI_CATEGORY_CAR_REPAIR;
        else if (strcmp(category_name, "car_wash") == 0)
            poi->category = POI_CATEGORY_CAR_WASH;
    }

    if (name)
        safe_str_copy_len(poi->name, 128, name, (size_t)name_len);
    if (name_en && name_en[0])
        safe_str_copy_len(poi->name_en, 128, name_en, (size_t)name_en_len);
    else if (name)
        safe_str_copy_len(poi->name_en, 128, name, (size_t)name_len);
    if (street)
        safe_str_copy_len(poi->street, 128, street, (size_t)street_len);
    if (street_en && street_en[0])
        safe_str_copy_len(poi->street_en, 128, street_en, (size_t)street_en_len);
    else if (street)
        safe_str_copy_len(poi->street_en, 128, street, (size_t)street_len);
    if (area)
        safe_str_copy_len(poi->area, 128, area, (size_t)area_len);
    if (area_en && area_en[0])
        safe_str_copy_len(poi->area_en, 128, area_en, (size_t)area_en_len);
    else if (area)
        safe_str_copy_len(poi->area_en, 128, area, (size_t)area_len);
    if (area_type)
        safe_str_copy_len(poi->area_type, 32, area_type, (size_t)area_type_len);
    if (city)
        safe_str_copy_len(poi->city, 128, city, (size_t)city_len);
    if (city_en && city_en[0])
        safe_str_copy_len(poi->city_en, 128, city_en, (size_t)city_en_len);
    else if (city)
        safe_str_copy_len(poi->city_en, 128, city, (size_t)city_len);
    if (address)
        safe_str_copy_len(poi->address, 256, address, (size_t)address_len);
    if (phone)
        safe_str_copy_len(poi->phone, 32, phone, (size_t)phone_len);
    if (website)
        safe_str_copy_len(poi->website, 128, website, (size_t)website_len);
    if (opening_hours)
        safe_str_copy_len(poi->opening_hours, 64, opening_hours, (size_t)opening_hours_len);
    if (operator_name)
        safe_str_copy_len(poi->operator_name, 64, operator_name, (size_t)operator_name_len);
    if (brand)
        safe_str_copy_len(poi->brand, 64, brand, (size_t)brand_len);

    if (fuel_types && fuel_types[0])
    {
        poi->fuel_types = strdup(fuel_types);
        poi->has_fuel_types = true;
    }
}

static void init_chunks(struct AromaMapExtra *extra)
{
    if (!extra)
        return;
    memset(extra->chunks, 0, sizeof(extra->chunks));
    extra->chunk_count = 0;
    extra->chunk_access_counter = 0;
}

static MapChunk *find_chunk(struct AromaMapExtra *extra, int chunk_x, int chunk_y, int z)
{
    for (int i = 0; i < MAX_CHUNKS; i++)
    {
        if (extra->chunks[i].is_loaded &&
            extra->chunks[i].chunk_x == chunk_x &&
            extra->chunks[i].chunk_y == chunk_y &&
            extra->chunks[i].z == z)
        {
            return &extra->chunks[i];
        }
    }
    return NULL;
}

static void unload_chunk(struct AromaMapExtra *extra, MapChunk *chunk)
{
    if (!extra || !chunk || !chunk->is_loaded)
        return;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();

    for (int i = 0; i < chunk->tile_count; i++)
    {
        if (chunk->tiles[i])
        {
            if (chunk->tiles[i]->is_ready && chunk->tiles[i]->texture_id != 0 &&
                gfx && gfx->unload_image)
            {
                gfx->unload_image(chunk->tiles[i]->texture_id);
            }
            chunk->tiles[i]->texture_id = 0;
            chunk->tiles[i]->is_ready = false;
            chunk->tiles[i]->is_loading = false;
            chunk->tiles[i]->valid = false;
            chunk->tiles[i] = NULL;
        }
    }

    chunk->is_loaded = false;
    chunk->is_loading = false;
    chunk->tile_count = 0;
}

static MapChunk *get_empty_chunk_slot(struct AromaMapExtra *extra)
{
    uint64_t oldest_access = UINT64_MAX;
    int oldest_idx = -1;

    for (int i = 0; i < MAX_CHUNKS; i++)
    {
        if (!extra->chunks[i].is_loaded)
        {
            return &extra->chunks[i];
        }
        if (extra->chunks[i].is_loading)
            continue;
        if (extra->chunks[i].last_access < oldest_access)
        {
            oldest_access = extra->chunks[i].last_access;
            oldest_idx = i;
        }
    }

    if (oldest_idx != -1)
    {
        unload_chunk(extra, &extra->chunks[oldest_idx]);
        return &extra->chunks[oldest_idx];
    }

    return NULL;
}

static bool is_chunk_in_view(struct AromaMapExtra *extra, int chunk_x, int chunk_y,
                             int center_chunk_x, int center_chunk_y)
{
    int dx = abs(chunk_x - center_chunk_x);
    int dy = abs(chunk_y - center_chunk_y);
    return (dx <= CHUNK_LOAD_DISTANCE && dy <= CHUNK_LOAD_DISTANCE);
}

static void update_chunks(struct AromaMapExtra *extra, int center_tile_x, int center_tile_y, int z)
{
    if (!extra)
        return;

    int wrapped_center_tile_x = ((center_tile_x % (1 << z)) + (1 << z)) % (1 << z);
    int center_chunk_x = wrapped_center_tile_x / CHUNK_SIZE;
    int center_chunk_y = center_tile_y / CHUNK_SIZE;

    for (int i = 0; i < MAX_CHUNKS; i++)
    {
        if (extra->chunks[i].is_loaded)
        {
            if (!is_chunk_in_view(extra, extra->chunks[i].chunk_x, extra->chunks[i].chunk_y,
                                  center_chunk_x, center_chunk_y) ||
                extra->chunks[i].z != z)
            {
                unload_chunk(extra, &extra->chunks[i]);
            }
        }
    }

    for (int dy = -CHUNK_LOAD_DISTANCE; dy <= CHUNK_LOAD_DISTANCE; dy++)
    {
        for (int dx = -CHUNK_LOAD_DISTANCE; dx <= CHUNK_LOAD_DISTANCE; dx++)
        {
            int chunk_x = center_chunk_x + dx;
            int chunk_y = center_chunk_y + dy;

            if (find_chunk(extra, chunk_x, chunk_y, z) == NULL)
            {
                MapChunk *chunk = get_empty_chunk_slot(extra);
                if (chunk)
                {
                    chunk->chunk_x = chunk_x;
                    chunk->chunk_y = chunk_y;
                    chunk->z = z;
                    chunk->is_loaded = true;
                    chunk->is_loading = true;
                    chunk->tile_count = 0;
                    chunk->last_access = ++extra->chunk_access_counter;
                }
            }
        }
    }
}

bool aroma_map_load_poi_database(AromaNode *node, const char *db_path)
{
    if (!node || !node->node_widget_ptr || !db_path)
        return false;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return false;

#ifndef __EMSCRIPTEN__
    POIManager *manager = &extra->poi_manager;
    pthread_mutex_lock(&manager->poi_mutex);

    if (manager->poi_db)
    {
        sqlite3_close(manager->poi_db);
        manager->poi_db = NULL;
    }

    strncpy(manager->poi_db_path, db_path, sizeof(manager->poi_db_path) - 1);
    manager->poi_db_path[sizeof(manager->poi_db_path) - 1] = '\0';

    int rc = sqlite3_open_v2(manager->poi_db_path, &manager->poi_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, NULL);
    if (rc != SQLITE_OK)
    {
        printf("Failed to open POI DB read-write (%s). Falling back to read-only.\n", sqlite3_errmsg(manager->poi_db));
        if (manager->poi_db)
            sqlite3_close(manager->poi_db);

        rc = sqlite3_open_v2(manager->poi_db_path, &manager->poi_db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, NULL);
        if (rc != SQLITE_OK)
        {
            printf("Failed to open POI database: %s\n", sqlite3_errmsg(manager->poi_db));
            if (manager->poi_db)
                sqlite3_close(manager->poi_db);
            manager->poi_db = NULL;
            pthread_mutex_unlock(&manager->poi_mutex);
            return false;
        }
    }

    sqlite3_exec(manager->poi_db, "PRAGMA synchronous=OFF;", NULL, NULL, NULL);
    sqlite3_exec(manager->poi_db, "PRAGMA temp_store=MEMORY;", NULL, NULL, NULL);

    printf("Creating/checking POI spatial indices...\n");
    sqlite3_exec(manager->poi_db, "CREATE INDEX IF NOT EXISTS idx_pois_lat_lon ON pois(lat, lon);", NULL, NULL, NULL);
    sqlite3_exec(manager->poi_db, "CREATE INDEX IF NOT EXISTS idx_pois_name ON pois(name);", NULL, NULL, NULL);

    manager->pois_loaded = true;
    pthread_mutex_unlock(&manager->poi_mutex);

    return true;
#else
    (void)node;
    (void)db_path;
    return false;
#endif
}

static void *osrm_loading_worker(void *arg)
{
    OSRMLoadRequest *req = (OSRMLoadRequest *)arg;
    if (!req || !req->node || !req->node->node_widget_ptr)
    {
        if (req)
            free(req);
        return NULL;
    }

    AromaMap *map = (AromaMap *)req->node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
    {
        free(req);
        return NULL;
    }

    OSRMGraph temp_graph;
    memset(&temp_graph, 0, sizeof(OSRMGraph));

    bool loaded = load_osrm_binary(&temp_graph, req->filepath);

    if (loaded)
    {
        if (extra->osrm_graph.is_loaded)
        {
            free_osrm_graph(&extra->osrm_graph);
            free_osrm_grid(&extra->osrm_grid);
        }

        extra->osrm_graph = temp_graph;
        init_osrm_grid(&extra->osrm_graph, &extra->osrm_grid);
    }

    extra->osrm_loading = false;

    if (req->node)
        aroma_node_invalidate(req->node);

    free(req);
    return NULL;
}

static bool request_tile_download(int z, int x, int y, bool is_dark, const char *filepath, uint64_t node_id, struct AromaMapExtra *extra)
{
#ifdef __EMSCRIPTEN__
    (void)filepath;
    (void)node_id;
    if (!extra)
        return false;
    EmscriptenTileRequest *req = malloc(sizeof(EmscriptenTileRequest));
    if (!req)
        return false;
    req->z = z;
    req->x = x;
    req->y = y;
    req->is_dark = is_dark;
    req->extra = extra;
    char url[512];
    if (is_dark)
        snprintf(url, sizeof(url), "https://a.basemaps.cartocdn.com/dark_all/%d/%d/%d.png", z, x, y);
    else
        snprintf(url, sizeof(url), "https://tile.openstreetmap.org/%d/%d/%d.png", z, x, y);
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
    if (next_tail != queue_head)
    {
        bool exists = false;
        for (int i = queue_head; i != queue_tail; i = (i + 1) % MAX_QUEUE)
        {
            if (fetch_queue[i].z == z && fetch_queue[i].x == x && fetch_queue[i].y == y && fetch_queue[i].is_dark == is_dark)
            {
                exists = true;
                break;
            }
        }
        if (!exists)
        {
            fetch_queue[queue_tail].z = z;
            fetch_queue[queue_tail].x = x;
            fetch_queue[queue_tail].y = y;
            fetch_queue[queue_tail].is_dark = is_dark;
            fetch_queue[queue_tail].node_id = node_id;
            fetch_queue[queue_tail].extra = extra;
            strncpy(fetch_queue[queue_tail].filepath, filepath, 255);
            fetch_queue[queue_tail].filepath[255] = '\0';
            queue_tail = next_tail;
            pthread_cond_signal(&queue_cond);
        }
        queued = true;
    }
    pthread_mutex_unlock(&queue_mutex);
    return queued;
#endif
}

static void __map_anim_tick(void *user_data)
{
    if (!user_data)
        return;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)user_data;
    if (!extra->node_ptr || !extra->node_ptr->node_widget_ptr)
        return;
    AromaNode *curr = extra->node_ptr;
    bool is_visible = true;
    while (curr)
    {
        if (curr->is_hidden)
        {
            is_visible = false;
            break;
        }
        curr = curr->parent_node;
    }
    if (!is_visible)
        return;
    AromaMap *map = (AromaMap *)extra->node_ptr->node_widget_ptr;
    bool changed = false;
    if (!extra->animations_enabled)
    {
        if (extra->display_zoom != extra->zoom)
        {
            extra->display_zoom = extra->zoom;
            changed = true;
        }
        if (extra->display_px_x != extra->center_px_x || extra->display_px_y != extra->center_px_y)
        {
            extra->display_px_x = extra->center_px_x;
            extra->display_px_y = extra->center_px_y;
            changed = true;
        }
        if (extra->velocity_x != 0 || extra->velocity_y != 0)
        {
            extra->velocity_x = 0;
            extra->velocity_y = 0;
            changed = true;
        }
        if (changed)
        {
            double z_factor = (1 << extra->zoom) * 256.0;
            map->center_lon = (extra->display_px_x / z_factor) * 360.0 - 180.0;
            double n = M_PI - 2.0 * M_PI * (extra->display_px_y / z_factor);
            map->center_lat = 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));
            aroma_node_invalidate(extra->node_ptr);
        }
        return;
    }
    if (fabs(extra->display_zoom - extra->zoom) > 0.001)
    {
        extra->display_zoom += (extra->zoom - extra->display_zoom) * 0.15;
        changed = true;
    }
    else
        extra->display_zoom = extra->zoom;
    if (!map->is_dragging)
    {
        if (fabs(extra->velocity_x) > 0.1 || fabs(extra->velocity_y) > 0.1)
        {
            extra->center_px_x += extra->velocity_x;
            extra->center_px_y += extra->velocity_y;
            extra->velocity_x *= 0.94;
            extra->velocity_y *= 0.94;
            changed = true;
        }
        else
        {
            extra->velocity_x = 0;
            extra->velocity_y = 0;
        }
    }
    else
    {
        extra->velocity_x = 0;
        extra->velocity_y = 0;
        changed = true;
    }
    double diff_x = extra->center_px_x - extra->display_px_x;
    double diff_y = extra->center_px_y - extra->display_px_y;
    if (fabs(diff_x) > 0.1 || fabs(diff_y) > 0.1)
    {
        extra->display_px_x += diff_x * 0.4;
        extra->display_px_y += diff_y * 0.4;
        changed = true;
    }
    else
    {
        extra->display_px_x = extra->center_px_x;
        extra->display_px_y = extra->center_px_y;
    }
    if (changed)
    {
        double z_factor = (1 << extra->zoom) * 256.0;
        map->center_lon = (extra->display_px_x / z_factor) * 360.0 - 180.0;
        double n = M_PI - 2.0 * M_PI * (extra->display_px_y / z_factor);
        map->center_lat = 180.0 / M_PI * atan(0.5 * (exp(n) - exp(-n)));

        aroma_node_invalidate(extra->node_ptr);
    }
}

static void unload_old_zoom_tiles(struct AromaMapExtra *extra)
{
    if (!extra)
        return;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    for (int i = 0; i < MAX_CHUNKS; i++)
    {
        if (extra->chunks[i].is_loaded)
        {
            unload_chunk(extra, &extra->chunks[i]);
        }
    }
    for (int i = 0; i < MAX_TILES_MEM; i++)
    {
        if (extra->tiles[i].valid)
        {
            if (extra->tiles[i].is_ready && extra->tiles[i].texture_id != 0)
            {
                if (gfx && gfx->unload_image)
                    gfx->unload_image(extra->tiles[i].texture_id);
            }
            extra->tiles[i].valid = false;
            extra->tiles[i].is_ready = false;
            extra->tiles[i].is_loading = false;
            extra->tiles[i].texture_id = 0;
        }
    }
    extra->access_counter = 0;
}

static void init_poi_manager(POIManager *manager)
{
    if (!manager)
        return;
    memset(manager, 0, sizeof(POIManager));
    manager->poi_capacity = POI_CHUNK_SIZE;
    manager->pois = malloc(manager->poi_capacity * sizeof(PointOfInterest));
    manager->poi_count = 0;
    manager->pois_loaded = false;
    manager->pois_visible = true;
    manager->pois_loading = false;
    manager->poi_thread = 0;
    manager->loading_node = NULL;
    manager->visible_category_count = 0;
    pthread_mutex_init(&manager->poi_mutex, NULL);
    pthread_cond_init(&manager->worker_done_cond, NULL);
    manager->shutting_down = false;
    manager->worker_active = false;
    manager->poi_db = NULL;
    manager->poi_db_path[0] = '\0';

    for (int i = 0; i < POI_CATEGORY_COUNT; i++)
    {
        manager->visible_categories[i] = (POICategory)i;
    }
    manager->visible_category_count = POI_CATEGORY_COUNT;
}

static void free_poi_manager(POIManager *manager)
{
    if (!manager)
        return;
    pthread_mutex_lock(&manager->poi_mutex);
    manager->shutting_down = true;
    while (manager->worker_active)
    {
        pthread_cond_wait(&manager->worker_done_cond, &manager->poi_mutex);
    }
    if (manager->pois)
    {
        for (int i = 0; i < manager->poi_count; i++)
        {
            if (manager->pois[i].fuel_types)
            {
                free(manager->pois[i].fuel_types);
            }
        }
        free(manager->pois);
        manager->pois = NULL;
    }
    manager->poi_count = 0;
    manager->poi_capacity = 0;
    if (manager->poi_db)
    {
        sqlite3_close(manager->poi_db);
        manager->poi_db = NULL;
    }
    pthread_mutex_unlock(&manager->poi_mutex);
    pthread_mutex_destroy(&manager->poi_mutex);
    pthread_cond_destroy(&manager->worker_done_cond);
}

const char *aroma_map_get_poi_category_name(POICategory category)
{
    switch (category)
    {
    case POI_CATEGORY_GAS_STATION:
        return "Gas Station";
    case POI_CATEGORY_RESTAURANT:
        return "Restaurant";
    case POI_CATEGORY_CAFE:
        return "Cafe";
    case POI_CATEGORY_FAST_FOOD:
        return "Fast Food";
    case POI_CATEGORY_SHOP:
        return "Shop";
    case POI_CATEGORY_SUPERMARKET:
        return "Supermarket";
    case POI_CATEGORY_CONVENIENCE:
        return "Convenience Store";
    case POI_CATEGORY_HOTEL:
        return "Hotel";
    case POI_CATEGORY_BANK:
        return "Bank";
    case POI_CATEGORY_ATM:
        return "ATM";
    case POI_CATEGORY_PHARMACY:
        return "Pharmacy";
    case POI_CATEGORY_HOSPITAL:
        return "Hospital";
    case POI_CATEGORY_CLINIC:
        return "Clinic";
    case POI_CATEGORY_SCHOOL:
        return "School";
    case POI_CATEGORY_UNIVERSITY:
        return "University";
    case POI_CATEGORY_PARKING:
        return "Parking";
    case POI_CATEGORY_CHARGING_STATION:
        return "Charging Station";
    case POI_CATEGORY_CAR_REPAIR:
        return "Car Repair";
    case POI_CATEGORY_CAR_WASH:
        return "Car Wash";
    case POI_CATEGORY_OTHER_BUSINESS:
        return "Business";
    default:
        return "Unknown";
    }
}

uint32_t aroma_map_get_poi_category_color(POICategory category)
{
    switch (category)
    {
    case POI_CATEGORY_GAS_STATION:
        return 0xFFFF6600;
    case POI_CATEGORY_RESTAURANT:
        return 0xFFFF3333;
    case POI_CATEGORY_CAFE:
        return 0xFF8B4513;
    case POI_CATEGORY_FAST_FOOD:
        return 0xFFFF6600;
    case POI_CATEGORY_SHOP:
        return 0xFF9933FF;
    case POI_CATEGORY_SUPERMARKET:
        return 0xFF00CC00;
    case POI_CATEGORY_CONVENIENCE:
        return 0xFF33CC33;
    case POI_CATEGORY_HOTEL:
        return 0xFF0066CC;
    case POI_CATEGORY_BANK:
        return 0xFF003366;
    case POI_CATEGORY_ATM:
        return 0xFF006699;
    case POI_CATEGORY_PHARMACY:
        return 0xFF009933;
    case POI_CATEGORY_HOSPITAL:
        return 0xFFFF0000;
    case POI_CATEGORY_CLINIC:
        return 0xFFCC0000;
    case POI_CATEGORY_SCHOOL:
        return 0xFFFFCC00;
    case POI_CATEGORY_UNIVERSITY:
        return 0xFFFF9900;
    case POI_CATEGORY_PARKING:
        return 0xFF3366FF;
    case POI_CATEGORY_CHARGING_STATION:
        return 0xFF00FF00;
    case POI_CATEGORY_CAR_REPAIR:
        return 0xFF666666;
    case POI_CATEGORY_CAR_WASH:
        return 0xFF3399FF;
    case POI_CATEGORY_OTHER_BUSINESS:
        return 0xFF999999;
    default:
        return 0xFFCCCCCC;
    }
}

static inline void safe_str_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len >= dst_size)
        len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static inline void safe_str_copy_len(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    if (!dst || dst_size == 0)
        return;
    if (!src || src_len == 0)
    {
        dst[0] = '\0';
        return;
    }
    size_t len = src_len;
    if (len >= dst_size)
        len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static bool is_category_visible(POIManager *manager, POICategory category)
{
    for (int i = 0; i < manager->visible_category_count; i++)
    {
        if (manager->visible_categories[i] == category)
            return true;
    }
    return false;
}

bool aroma_map_is_poi_loading(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return false;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return false;
    return extra->poi_manager.pois_loading;
}

void aroma_map_unload_poi_database(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;

    POIManager *manager = &extra->poi_manager;
    pthread_mutex_lock(&manager->poi_mutex);

    if (manager->pois)
    {
        for (int i = 0; i < manager->poi_count; i++)
        {
            if (manager->pois[i].fuel_types)
                free(manager->pois[i].fuel_types);
        }
        free(manager->pois);
        manager->pois = NULL;
    }
    manager->poi_count = 0;
    manager->poi_capacity = 0;
    manager->pois_loaded = false;
    manager->pois_loading = false;

    if (manager->poi_db)
    {
        sqlite3_close(manager->poi_db);
        manager->poi_db = NULL;
    }
    manager->poi_db_path[0] = '\0';

    pthread_mutex_unlock(&manager->poi_mutex);
    aroma_node_invalidate(node);
}

void aroma_map_set_pois_visible(AromaNode *node, bool visible)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;

    POIManager *manager = &extra->poi_manager;
    pthread_mutex_lock(&manager->poi_mutex);
    manager->pois_visible = visible;
    pthread_mutex_unlock(&manager->poi_mutex);
    aroma_node_invalidate(node);
}

void aroma_map_set_poi_categories_visible(AromaNode *node, POICategory *categories, int count)
{
    if (!node || !node->node_widget_ptr || !categories || count <= 0)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;

    POIManager *manager = &extra->poi_manager;
    pthread_mutex_lock(&manager->poi_mutex);

    manager->visible_category_count = count < POI_CATEGORY_COUNT ? count : POI_CATEGORY_COUNT;
    for (int i = 0; i < manager->visible_category_count; i++)
    {
        manager->visible_categories[i] = categories[i];
    }

    pthread_mutex_unlock(&manager->poi_mutex);
    aroma_node_invalidate(node);
}

PointOfInterest *aroma_map_query_pois_in_viewport(AromaNode *node, double min_lat, double max_lat, double min_lon, double max_lon, int *count)
{
    if (!node || !node->node_widget_ptr)
        return NULL;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra || !extra->poi_manager.poi_db)
        return NULL;

    POIManager *manager = &extra->poi_manager;
    sqlite3 *db = manager->poi_db;

    bool has_categories_table = false;
    sqlite3_stmt *check_table;
    if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name='poi_categories'", -1, &check_table, NULL) == SQLITE_OK)
    {
        if (sqlite3_step(check_table) == SQLITE_ROW)
            has_categories_table = true;
        sqlite3_finalize(check_table);
    }

    bool has_name_en = false, has_name_fr = false;
    bool has_street = false, has_street_en = false, has_street_fr = false;
    bool has_area = false, has_area_en = false, has_area_fr = false;
    bool has_area_type = false;
    bool has_city = false, has_city_en = false, has_city_fr = false;
    bool has_address = false, has_phone = false, has_website = false;
    bool has_opening_hours = false, has_operator = false, has_brand = false;
    bool has_fuel_types = false;
    bool has_category_id = false;

    sqlite3_stmt *check_stmt;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(pois)", -1, &check_stmt, NULL) == SQLITE_OK)
    {
        while (sqlite3_step(check_stmt) == SQLITE_ROW)
        {
            const char *col_name = (const char *)sqlite3_column_text(check_stmt, 1);
            if (!col_name)
                continue;

            if (strcmp(col_name, "name_en") == 0)
                has_name_en = true;
            else if (strcmp(col_name, "name_fr") == 0)
                has_name_fr = true;
            else if (strcmp(col_name, "street") == 0)
                has_street = true;
            else if (strcmp(col_name, "street_en") == 0)
                has_street_en = true;
            else if (strcmp(col_name, "street_fr") == 0)
                has_street_fr = true;
            else if (strcmp(col_name, "area") == 0)
                has_area = true;
            else if (strcmp(col_name, "area_en") == 0)
                has_area_en = true;
            else if (strcmp(col_name, "area_fr") == 0)
                has_area_fr = true;
            else if (strcmp(col_name, "area_type") == 0)
                has_area_type = true;
            else if (strcmp(col_name, "city") == 0)
                has_city = true;
            else if (strcmp(col_name, "city_en") == 0)
                has_city_en = true;
            else if (strcmp(col_name, "city_fr") == 0)
                has_city_fr = true;
            else if (strcmp(col_name, "address") == 0)
                has_address = true;
            else if (strcmp(col_name, "phone") == 0)
                has_phone = true;
            else if (strcmp(col_name, "website") == 0)
                has_website = true;
            else if (strcmp(col_name, "opening_hours") == 0)
                has_opening_hours = true;
            else if (strcmp(col_name, "operator") == 0)
                has_operator = true;
            else if (strcmp(col_name, "brand") == 0)
                has_brand = true;
            else if (strcmp(col_name, "fuel_types") == 0)
                has_fuel_types = true;
            else if (strcmp(col_name, "category_id") == 0)
                has_category_id = true;
        }
        sqlite3_finalize(check_stmt);
    }

    char category_expr[128] = "''";
    char from_clause[256] = "FROM pois p";
    if (has_categories_table && has_category_id)
    {
        strcat(from_clause, " LEFT JOIN poi_categories c ON p.category_id = c.category_id");
        snprintf(category_expr, sizeof(category_expr), "COALESCE(c.category_name, '')");
    }

    char name_display[128] = "COALESCE(p.name, '')";
    if (has_name_fr)
        snprintf(name_display, sizeof(name_display), "COALESCE(p.name_fr, p.name, '')");
    else if (has_name_en)
        snprintf(name_display, sizeof(name_display), "COALESCE(p.name_en, p.name, '')");

    char name_alt[128] = "COALESCE(p.name, '')";
    if (has_name_fr)
        snprintf(name_alt, sizeof(name_alt), "COALESCE(p.name, p.name_fr, '')");
    else if (has_name_en)
        snprintf(name_alt, sizeof(name_alt), "COALESCE(p.name, p.name_en, '')");

    char street_display[128] = "''";
    if (has_street_fr)
        snprintf(street_display, sizeof(street_display), "COALESCE(p.street_fr, p.street, '')");
    else if (has_street_en)
        snprintf(street_display, sizeof(street_display), "COALESCE(p.street_en, p.street, '')");
    else if (has_street)
        snprintf(street_display, sizeof(street_display), "COALESCE(p.street, '')");

    char street_alt[128] = "''";
    if (has_street_fr)
        snprintf(street_alt, sizeof(street_alt), "COALESCE(p.street, p.street_fr, '')");
    else if (has_street_en)
        snprintf(street_alt, sizeof(street_alt), "COALESCE(p.street, p.street_en, '')");
    else if (has_street)
        snprintf(street_alt, sizeof(street_alt), "COALESCE(p.street, '')");

    char area_display[128] = "''";
    if (has_area_fr)
        snprintf(area_display, sizeof(area_display), "COALESCE(p.area_fr, p.area, '')");
    else if (has_area_en)
        snprintf(area_display, sizeof(area_display), "COALESCE(p.area_en, p.area, '')");
    else if (has_area)
        snprintf(area_display, sizeof(area_display), "COALESCE(p.area, '')");

    char area_alt[128] = "''";
    if (has_area_fr)
        snprintf(area_alt, sizeof(area_alt), "COALESCE(p.area, p.area_fr, '')");
    else if (has_area_en)
        snprintf(area_alt, sizeof(area_alt), "COALESCE(p.area, p.area_en, '')");
    else if (has_area)
        snprintf(area_alt, sizeof(area_alt), "COALESCE(p.area, '')");

    char area_type_expr[128] = "''";
    if (has_area_type)
        snprintf(area_type_expr, sizeof(area_type_expr), "COALESCE(p.area_type, '')");

    char city_display[128] = "''";
    if (has_city_fr)
        snprintf(city_display, sizeof(city_display), "COALESCE(p.city_fr, p.city, '')");
    else if (has_city_en)
        snprintf(city_display, sizeof(city_display), "COALESCE(p.city_en, p.city, '')");
    else if (has_city)
        snprintf(city_display, sizeof(city_display), "COALESCE(p.city, '')");

    char city_alt[128] = "''";
    if (has_city_fr)
        snprintf(city_alt, sizeof(city_alt), "COALESCE(p.city, p.city_fr, '')");
    else if (has_city_en)
        snprintf(city_alt, sizeof(city_alt), "COALESCE(p.city, p.city_en, '')");
    else if (has_city)
        snprintf(city_alt, sizeof(city_alt), "COALESCE(p.city, '')");

    char address_expr[128] = "''";
    if (has_address)
        snprintf(address_expr, sizeof(address_expr), "COALESCE(p.address, '')");

    char phone_expr[128] = "''";
    if (has_phone)
        snprintf(phone_expr, sizeof(phone_expr), "COALESCE(p.phone, '')");

    char website_expr[128] = "''";
    if (has_website)
        snprintf(website_expr, sizeof(website_expr), "COALESCE(p.website, '')");

    char opening_hours_expr[128] = "''";
    if (has_opening_hours)
        snprintf(opening_hours_expr, sizeof(opening_hours_expr), "COALESCE(p.opening_hours, '')");

    char operator_expr[128] = "''";
    if (has_operator)
        snprintf(operator_expr, sizeof(operator_expr), "COALESCE(p.operator, '')");

    char brand_expr[128] = "''";
    if (has_brand)
        snprintf(brand_expr, sizeof(brand_expr), "COALESCE(p.brand, '')");

    char fuel_expr[128] = "''";
    if (has_fuel_types)
        snprintf(fuel_expr, sizeof(fuel_expr), "COALESCE(p.fuel_types, '')");

    double center_lat = (min_lat + max_lat) / 2.0;
    double center_lon = (min_lon + max_lon) / 2.0;

    char sql[4096];
    snprintf(sql, sizeof(sql),
             "SELECT p.osm_id, %s AS category_name, %s AS name, %s AS name_en, p.lat, p.lon, "
             "%s AS street, %s AS street_en, %s AS area, %s AS area_en, "
             "%s AS area_type, %s AS city, %s AS city_en, %s AS address, %s AS phone, "
             "%s AS website, %s AS opening_hours, %s AS operator_name, %s AS brand, %s AS fuel_types %s "
             "WHERE p.lat >= %f AND p.lat <= %f AND p.lon >= %f AND p.lon <= %f "
             "ORDER BY ((p.lat - %f) * (p.lat - %f) + (p.lon - %f) * (p.lon - %f)) ASC "
             "LIMIT 5000",
             category_expr, name_display, name_alt,
             street_display, street_alt, area_display, area_alt,
             area_type_expr, city_display, city_alt, address_expr, phone_expr,
             website_expr, opening_hours_expr, operator_expr, brand_expr, fuel_expr,
             from_clause,
             min_lat, max_lat, min_lon, max_lon,
             center_lat, center_lat, center_lon, center_lon);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        printf("Failed to prepare viewport POI query: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    pthread_mutex_lock(&manager->poi_mutex);

    int results_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        if (results_count >= extra->dynamic_pois_capacity)
        {
            int new_capacity = extra->dynamic_pois_capacity == 0 ? 500 : extra->dynamic_pois_capacity * 2;
            PointOfInterest *new_buf = realloc(extra->dynamic_pois, new_capacity * sizeof(PointOfInterest));
            if (!new_buf)
                break;
            extra->dynamic_pois = new_buf;
            extra->dynamic_pois_capacity = new_capacity;
        }

        parse_poi_from_stmt(stmt, &extra->dynamic_pois[results_count]);
        results_count++;
    }
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&manager->poi_mutex);

    if (count)
        *count = results_count;
    return extra->dynamic_pois;
}

PointOfInterest *aroma_map_query_pois_by_name(AromaNode *node, const char *name_query, int limit, int *count)
{
    if (!node || !node->node_widget_ptr || !name_query)
        return NULL;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra || !extra->poi_manager.poi_db)
        return NULL;

    POIManager *manager = &extra->poi_manager;
    sqlite3 *db = manager->poi_db;

    bool has_categories_table = false;
    sqlite3_stmt *check_table;
    if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table' AND name='poi_categories'", -1, &check_table, NULL) == SQLITE_OK)
    {
        if (sqlite3_step(check_table) == SQLITE_ROW)
            has_categories_table = true;
        sqlite3_finalize(check_table);
    }

    bool has_name_en = false, has_name_fr = false;
    bool has_category_id = false;

    sqlite3_stmt *check_stmt;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(pois)", -1, &check_stmt, NULL) == SQLITE_OK)
    {
        while (sqlite3_step(check_stmt) == SQLITE_ROW)
        {
            const char *col_name = (const char *)sqlite3_column_text(check_stmt, 1);
            if (!col_name)
                continue;

            if (strcmp(col_name, "name_en") == 0)
                has_name_en = true;
            else if (strcmp(col_name, "name_fr") == 0)
                has_name_fr = true;
            else if (strcmp(col_name, "category_id") == 0)
                has_category_id = true;
        }
        sqlite3_finalize(check_stmt);
    }

    char category_expr[128] = "''";
    char from_clause[128] = "FROM pois p";
    if (has_categories_table && has_category_id)
    {
        strcat(from_clause, " LEFT JOIN poi_categories c ON p.category_id = c.category_id");
        snprintf(category_expr, sizeof(category_expr), "COALESCE(c.category_name, '')");
    }

    char name_display[128] = "COALESCE(p.name, '')";
    if (has_name_fr)
        snprintf(name_display, sizeof(name_display), "COALESCE(p.name_fr, p.name, '')");
    else if (has_name_en)
        snprintf(name_display, sizeof(name_display), "COALESCE(p.name_en, p.name, '')");

    char name_alt[128] = "COALESCE(p.name, '')";
    if (has_name_fr)
        snprintf(name_alt, sizeof(name_alt), "COALESCE(p.name, p.name_fr, '')");
    else if (has_name_en)
        snprintf(name_alt, sizeof(name_alt), "COALESCE(p.name, p.name_en, '')");

    char street_display[128] = "''";
    char street_alt[128] = "''";
    if (sqlite3_table_column_metadata(db, "main", "pois", "street_fr", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
    {
        snprintf(street_display, sizeof(street_display), "COALESCE(p.street_fr, p.street, '')");
        snprintf(street_alt, sizeof(street_alt), "COALESCE(p.street, p.street_fr, '')");
    }
    else if (sqlite3_table_column_metadata(db, "main", "pois", "street_en", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
    {
        snprintf(street_display, sizeof(street_display), "COALESCE(p.street_en, p.street, '')");
        snprintf(street_alt, sizeof(street_alt), "COALESCE(p.street, p.street_en, '')");
    }
    else if (sqlite3_table_column_metadata(db, "main", "pois", "street", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
    {
        snprintf(street_display, sizeof(street_display), "COALESCE(p.street, '')");
        snprintf(street_alt, sizeof(street_alt), "COALESCE(p.street, '')");
    }

    char area_display[128] = "''";
    char area_alt[128] = "''";
    if (sqlite3_table_column_metadata(db, "main", "pois", "area_fr", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
    {
        snprintf(area_display, sizeof(area_display), "COALESCE(p.area_fr, p.area, '')");
        snprintf(area_alt, sizeof(area_alt), "COALESCE(p.area, p.area_fr, '')");
    }
    else if (sqlite3_table_column_metadata(db, "main", "pois", "area_en", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
    {
        snprintf(area_display, sizeof(area_display), "COALESCE(p.area_en, p.area, '')");
        snprintf(area_alt, sizeof(area_alt), "COALESCE(p.area, p.area_en, '')");
    }
    else if (sqlite3_table_column_metadata(db, "main", "pois", "area", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
    {
        snprintf(area_display, sizeof(area_display), "COALESCE(p.area, '')");
        snprintf(area_alt, sizeof(area_alt), "COALESCE(p.area, '')");
    }

    char area_type_expr[128] = "''";
    if (sqlite3_table_column_metadata(db, "main", "pois", "area_type", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
        snprintf(area_type_expr, sizeof(area_type_expr), "COALESCE(p.area_type, '')");

    char city_display[128] = "''";
    char city_alt[128] = "''";
    if (sqlite3_table_column_metadata(db, "main", "pois", "city_fr", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
    {
        snprintf(city_display, sizeof(city_display), "COALESCE(p.city_fr, p.city, '')");
        snprintf(city_alt, sizeof(city_alt), "COALESCE(p.city, p.city_fr, '')");
    }
    else if (sqlite3_table_column_metadata(db, "main", "pois", "city_en", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
    {
        snprintf(city_display, sizeof(city_display), "COALESCE(p.city_en, p.city, '')");
        snprintf(city_alt, sizeof(city_alt), "COALESCE(p.city, p.city_en, '')");
    }
    else if (sqlite3_table_column_metadata(db, "main", "pois", "city", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
    {
        snprintf(city_display, sizeof(city_display), "COALESCE(p.city, '')");
        snprintf(city_alt, sizeof(city_alt), "COALESCE(p.city, '')");
    }

    char address_expr[128] = "''";
    if (sqlite3_table_column_metadata(db, "main", "pois", "address", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
        snprintf(address_expr, sizeof(address_expr), "COALESCE(p.address, '')");

    char phone_expr[128] = "''";
    if (sqlite3_table_column_metadata(db, "main", "pois", "phone", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
        snprintf(phone_expr, sizeof(phone_expr), "COALESCE(p.phone, '')");

    char website_expr[128] = "''";
    if (sqlite3_table_column_metadata(db, "main", "pois", "website", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
        snprintf(website_expr, sizeof(website_expr), "COALESCE(p.website, '')");

    char opening_hours_expr[128] = "''";
    if (sqlite3_table_column_metadata(db, "main", "pois", "opening_hours", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
        snprintf(opening_hours_expr, sizeof(opening_hours_expr), "COALESCE(p.opening_hours, '')");

    char operator_expr[128] = "''";
    if (sqlite3_table_column_metadata(db, "main", "pois", "operator", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
        snprintf(operator_expr, sizeof(operator_expr), "COALESCE(p.operator, '')");

    char brand_expr[128] = "''";
    if (sqlite3_table_column_metadata(db, "main", "pois", "brand", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
        snprintf(brand_expr, sizeof(brand_expr), "COALESCE(p.brand, '')");

    char fuel_expr[128] = "''";
    if (sqlite3_table_column_metadata(db, "main", "pois", "fuel_types", NULL, NULL, NULL, NULL, NULL) == SQLITE_OK)
        snprintf(fuel_expr, sizeof(fuel_expr), "COALESCE(p.fuel_types, '')");

    const char *name_search_col = has_name_fr ? "p.name_fr" : (has_name_en ? "p.name_en" : "p.name");

    char sql[4096];
    snprintf(sql, sizeof(sql),
             "SELECT p.osm_id, %s AS category_name, %s AS name, %s AS name_en, p.lat, p.lon, "
             "%s AS street, %s AS street_en, %s AS area, %s AS area_en, "
             "%s AS area_type, %s AS city, %s AS city_en, %s AS address, %s AS phone, "
             "%s AS website, %s AS opening_hours, %s AS operator_name, %s AS brand, %s AS fuel_types %s "
             "WHERE (%s LIKE ? OR p.name LIKE ?) LIMIT ?",
             category_expr, name_display, name_alt,
             street_display, street_alt, area_display, area_alt,
             area_type_expr, city_display, city_alt, address_expr, phone_expr,
             website_expr, opening_hours_expr, operator_expr, brand_expr, fuel_expr,
             from_clause,
             name_search_col);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        printf("Failed to prepare name POI query: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    char like_pattern[512];
    snprintf(like_pattern, sizeof(like_pattern), "%%%s%%", name_query);

    sqlite3_bind_text(stmt, 1, like_pattern, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, like_pattern, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, limit > 0 ? limit : 50);

    pthread_mutex_lock(&manager->poi_mutex);

    int results_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        if (results_count >= extra->dynamic_pois_capacity)
        {
            int new_capacity = extra->dynamic_pois_capacity == 0 ? 500 : extra->dynamic_pois_capacity * 2;
            PointOfInterest *new_buf = realloc(extra->dynamic_pois, new_capacity * sizeof(PointOfInterest));
            if (!new_buf)
                break;
            extra->dynamic_pois = new_buf;
            extra->dynamic_pois_capacity = new_capacity;
        }

        parse_poi_from_stmt(stmt, &extra->dynamic_pois[results_count]);
        results_count++;
    }
    sqlite3_finalize(stmt);

    pthread_mutex_unlock(&manager->poi_mutex);

    if (count)
        *count = results_count;
    return extra->dynamic_pois;
}

void aroma_map_set_poi_draw_callback(AromaNode *node, POIDrawCallback callback, void *user_data)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    extra->poi_draw_callback = callback;
    extra->poi_draw_user_data = user_data;
    aroma_node_invalidate(node);
}

void aroma_map_set_poi_hit_test_callback(AromaNode *node, POIHitTestCallback callback, void *user_data)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    extra->poi_hit_test_callback = callback;
    extra->poi_hit_test_user_data = user_data;
}

static void draw_pois(AromaNode *node, size_t window_id, struct AromaMapExtra *extra, double view_tl_x, double view_tl_y)
{
    if (!extra || !extra->poi_manager.pois_visible)
        return;

    if (!extra->poi_draw_callback)
        return;

    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;

    POIManager *manager = &extra->poi_manager;

    if (!manager->poi_db)
        return;

    double z = extra->display_zoom;
    double z_factor = pow(2.0, z) * TILE_SIZE;

    double view_left = view_tl_x;
    double view_top = view_tl_y;
    double view_right = view_tl_x + map->rect.width;
    double view_bottom = view_tl_y + map->rect.height;

    double min_lon, min_lat, max_lon, max_lat;
    pixel_to_latlon(view_left, view_top, (int)round(z), &max_lat, &min_lon);
    pixel_to_latlon(view_right, view_bottom, (int)round(z), &min_lat, &max_lon);

    double lat_margin = (max_lat - min_lat) * 0.5;
    double lon_margin = (max_lon - min_lon) * 0.5;
    min_lat -= lat_margin;
    max_lat += lat_margin;
    min_lon -= lon_margin;
    max_lon += lon_margin;

    int poi_count = 0;
    PointOfInterest *pois = aroma_map_query_pois_in_viewport(node, min_lat, max_lat, min_lon, max_lon, &poi_count);

    if (!pois || poi_count == 0)
        return;

    for (int i = 0; i < poi_count; i++)
    {
        PointOfInterest *poi = &pois[i];

        double lat_rad = poi->lat * M_PI / 180.0;
        double px_x = (poi->lon + 180.0) / 360.0 * z_factor;
        double px_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * z_factor;

        int draw_x = map->rect.x + (int)(px_x - view_tl_x);
        int draw_y = map->rect.y + (int)(px_y - view_tl_y);

        if (draw_x >= map->rect.x - 20 && draw_x <= map->rect.x + map->rect.width + 20 &&
            draw_y >= map->rect.y - 20 && draw_y <= map->rect.y + map->rect.height + 20)
        {
            extra->poi_draw_callback(node, window_id, poi, draw_x, draw_y, extra->poi_draw_user_data);
        }
    }
}

static bool __map_event_handler(AromaEvent *event, void *user_data)
{
    if (!event || !event->target_node || !event->target_node->node_widget_ptr)
        return false;
    AromaMap *map = (AromaMap *)event->target_node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)user_data;
    if (!extra)
        return false;
    int adjusted_x = event->data.mouse.x;
    int adjusted_y = event->data.mouse.y;
    AromaNode *cur = event->target_node->parent_node;
    while (cur)
    {
        if (aroma_container_is_scrollable(cur))
        {
            int scroll_x, scroll_y;
            aroma_container_get_scroll(cur, &scroll_x, &scroll_y);
            adjusted_x += scroll_x;
            adjusted_y += scroll_y;
        }
        cur = cur->parent_node;
    }
    switch (event->event_type)
    {
    case EVENT_TYPE_MOUSE_DOUBLE_CLICK:
        if (adjusted_x >= map->rect.x && adjusted_x <= map->rect.x + map->rect.width &&
            adjusted_y >= map->rect.y && adjusted_y <= map->rect.y + map->rect.height)
        {
            aroma_map_zoom_in(event->target_node);
            return true;
        }
        break;
    case EVENT_TYPE_MOUSE_CLICK:
        if (adjusted_x >= map->rect.x && adjusted_x <= map->rect.x + map->rect.width &&
            adjusted_y >= map->rect.y && adjusted_y <= map->rect.y + map->rect.height)
        {
            int clicked_marker = -1;
            int clicked_poi = -1;
            double center_x = extra->display_px_x * pow(2.0, extra->display_zoom - extra->zoom);
            double center_y = extra->display_px_y * pow(2.0, extra->display_zoom - extra->zoom);
            double view_tl_x = center_x - map->rect.width / 2.0;
            double view_tl_y = center_y - map->rect.height / 2.0;

            for (int i = 0; i < extra->marker_count; i++)
            {
                double lat_rad = extra->markers[i].lat * M_PI / 180.0;
                double px_x = (extra->markers[i].lon + 180.0) / 360.0 * pow(2.0, extra->display_zoom) * TILE_SIZE;
                double px_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * pow(2.0, extra->display_zoom) * TILE_SIZE;
                int draw_x = map->rect.x + (int)(px_x - view_tl_x);
                int draw_y = map->rect.y + (int)(px_y - view_tl_y);
                if (adjusted_x >= draw_x - 20 && adjusted_x <= draw_x + 20 &&
                    adjusted_y >= draw_y - 20 && adjusted_y <= draw_y + 20)
                {
                    clicked_marker = i;
                    break;
                }
            }

            if (clicked_marker == -1 && extra->poi_manager.pois_visible &&
                (extra->poi_hit_test_callback || extra->poi_draw_callback) &&
                extra->poi_manager.poi_db)
            {
                double z_factor = pow(2.0, extra->display_zoom) * TILE_SIZE;

                double min_lon, min_lat, max_lon, max_lat;
                pixel_to_latlon(view_tl_x, view_tl_y, (int)round(extra->display_zoom), &max_lat, &min_lon);
                pixel_to_latlon(view_tl_x + map->rect.width, view_tl_y + map->rect.height,
                                (int)round(extra->display_zoom), &min_lat, &max_lon);

                double lat_margin = (max_lat - min_lat) * 0.5;
                double lon_margin = (max_lon - min_lon) * 0.5;
                min_lat -= lat_margin;
                max_lat += lat_margin;
                min_lon -= lon_margin;
                max_lon += lon_margin;

                int poi_count = 0;
                PointOfInterest *pois = aroma_map_query_pois_in_viewport(event->target_node,
                                                                         min_lat, max_lat,
                                                                         min_lon, max_lon,
                                                                         &poi_count);

                for (int i = 0; i < poi_count; i++)
                {
                    PointOfInterest *poi = &pois[i];
                    double lat_rad = poi->lat * M_PI / 180.0;
                    double px_x = (poi->lon + 180.0) / 360.0 * z_factor;
                    double px_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * z_factor;
                    int draw_x = map->rect.x + (int)(px_x - view_tl_x);
                    int draw_y = map->rect.y + (int)(px_y - view_tl_y);

                    bool hit = false;
                    if (extra->poi_hit_test_callback)
                    {
                        hit = extra->poi_hit_test_callback(poi, draw_x, draw_y, adjusted_x, adjusted_y, extra->poi_hit_test_user_data);
                    }
                    else
                    {
                        int hit_radius = (poi->category == POI_CATEGORY_GAS_STATION ||
                                          poi->category == POI_CATEGORY_CHARGING_STATION)
                                             ? 12
                                             : 8;
                        hit = (adjusted_x >= draw_x - hit_radius && adjusted_x <= draw_x + hit_radius &&
                               adjusted_y >= draw_y - hit_radius && adjusted_y <= draw_y + hit_radius);
                    }

                    if (hit)
                    {
                        clicked_poi = i;
                        break;
                    }
                }
            }

            if (clicked_marker != -1)
            {
                if (extra->active_popup_idx == clicked_marker)
                    extra->active_popup_idx = -1;
                else
                    extra->active_popup_idx = clicked_marker;
            }
            else if (clicked_poi != -1)
            {
                if (extra->active_popup_idx == clicked_poi)
                    extra->active_popup_idx = -1;
                else
                    extra->active_popup_idx = clicked_poi;
            }
            else
            {
                map->is_dragging = true;
                map->last_mouse_x = adjusted_x;
                map->last_mouse_y = adjusted_y;
                extra->active_popup_idx = -1;
            }
            aroma_node_invalidate(event->target_node);
            return true;
        }
        break;
    case EVENT_TYPE_MOUSE_MOVE:
        if (map->is_dragging)
        {
            int dx = adjusted_x - map->last_mouse_x;
            int dy = adjusted_y - map->last_mouse_y;
            extra->center_px_x -= dx;
            extra->center_px_y -= dy;
            extra->display_px_x -= dx;
            extra->display_px_y -= dy;
            if (extra->animations_enabled)
            {
                extra->velocity_x = -dx * 0.8;
                extra->velocity_y = -dy * 0.8;
            }
            else
            {
                extra->velocity_x = 0;
                extra->velocity_y = 0;
            }
            map->last_mouse_x = adjusted_x;
            map->last_mouse_y = adjusted_y;
            aroma_node_invalidate(event->target_node);
            return true;
        }
        break;
    case EVENT_TYPE_MOUSE_RELEASE:
        if (map->is_dragging)
        {
            map->is_dragging = false;
            aroma_node_invalidate(event->target_node);
            return true;
        }
        break;
    case EVENT_TYPE_MOUSE_SCROLL:
        if (adjusted_x >= map->rect.x && adjusted_x <= map->rect.x + map->rect.width &&
            adjusted_y >= map->rect.y && adjusted_y <= map->rect.y + map->rect.height)
        {
            if (event->data.mouse.scroll_y > 0)
                aroma_map_zoom_in(event->target_node);
            else if (event->data.mouse.scroll_y < 0)
                aroma_map_zoom_out(event->target_node);
            return true;
        }
        break;
    case EVENT_TYPE_CUSTOM:
        if (event->data.custom.custom_type == 999 || event->data.custom.custom_type == 998)
        {
            TileRequest *req = (TileRequest *)event->data.custom.data;
            if (req)
            {
                for (int i = 0; i < MAX_TILES_MEM; i++)
                {
                    if (extra->tiles[i].valid && extra->tiles[i].z == req->z && extra->tiles[i].x == req->x && extra->tiles[i].y == req->y && extra->tiles[i].is_dark == req->is_dark)
                    {
                        extra->tiles[i].is_loading = false;
                        extra->tiles[i].is_ready = true;

                        if (req->image_data)
                        {
#ifndef __EMSCRIPTEN__
                            AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
                            if (gfx && gfx->load_image_from_rgba)
                            {
                                extra->tiles[i].texture_id = gfx->load_image_from_rgba(req->image_data, req->img_w, req->img_h);
                            }
#endif
                        }
                        else
                        {
                            extra->tiles[i].texture_id = 0;
                        }
                        break;
                    }
                }
#ifndef __EMSCRIPTEN__
                if (req->image_data)
                    stbi_image_free(req->image_data);
#endif
                free(req);
            }
            if (event->data.custom.custom_type == 999 && extra->node_ptr)
            {
                AromaNode *curr = extra->node_ptr;
                bool is_visible = true;
                while (curr)
                {
                    if (curr->is_hidden)
                    {
                        is_visible = false;
                        break;
                    }
                    curr = curr->parent_node;
                }
                if (is_visible)
                    aroma_node_invalidate(event->target_node);
            }
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

static void __map_draw(AromaNode *node, size_t window_id)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;
    AromaTheme theme = aroma_ui_get_theme();
    uint32_t bg_color = theme.colors.surface;
    uint8_t r = (bg_color >> 16) & 0xFF;
    uint8_t g = (bg_color >> 8) & 0xFF;
    uint8_t b = bg_color & 0xFF;
    bool theme_is_dark = ((r * 299 + g * 587 + b * 114) / 1000) < 128;
    gfx->fill_rectangle(window_id, map->rect.x, map->rect.y, map->rect.width, map->rect.height, bg_color, false, 0.0f);
    gfx->graphics_set_clip(map->rect.x, map->rect.y, map->rect.width, map->rect.height);
    int z = (int)round(extra->display_zoom);
    if (z < extra->min_zoom)
        z = extra->min_zoom;
    if (z > extra->max_zoom)
        z = extra->max_zoom;
    double scale = pow(2.0, extra->display_zoom - z);
    double center_x = extra->display_px_x * pow(2.0, extra->display_zoom - extra->zoom);
    double center_y = extra->display_px_y * pow(2.0, extra->display_zoom - extra->zoom);
    double view_tl_x = center_x - map->rect.width / 2.0;
    double view_tl_y = center_y - map->rect.height / 2.0;
    double current_tile_size = TILE_SIZE * scale;
    int center_tile_x = (int)floor((view_tl_x + map->rect.width / 2.0) / current_tile_size);
    int center_tile_y = (int)floor((view_tl_y + map->rect.height / 2.0) / current_tile_size);
    update_chunks(extra, center_tile_x, center_tile_y, z);
    int tx_start = (int)floor(view_tl_x / current_tile_size) - 1;
    int ty_start = (int)floor(view_tl_y / current_tile_size) - 1;
    int tx_end = (int)floor((view_tl_x + map->rect.width) / current_tile_size) + 1;
    int ty_end = (int)floor((view_tl_y + map->rect.height) / current_tile_size) + 1;
    for (int y = ty_start; y <= ty_end; y++)
    {
        for (int x = tx_start; x <= tx_end; x++)
        {
            if (y < 0 || y >= (1 << z))
                continue;
            int wrapped_x = (x % (1 << z) + (1 << z)) % (1 << z);
            int chunk_x = x / CHUNK_SIZE;
            int chunk_y = y / CHUNK_SIZE;
            MapChunk *chunk = find_chunk(extra, chunk_x, chunk_y, z);
            if (!chunk || !chunk->is_loaded)
                continue;
            chunk->last_access = ++extra->chunk_access_counter;
            MapTile *found_tile = NULL;
            for (int i = 0; i < chunk->tile_count; i++)
            {
                if (chunk->tiles[i] &&
                    chunk->tiles[i]->z == z &&
                    chunk->tiles[i]->x == wrapped_x &&
                    chunk->tiles[i]->y == y)
                {
                    found_tile = chunk->tiles[i];
                    break;
                }
            }
            if (!found_tile && chunk->tile_count < CHUNK_SIZE * CHUNK_SIZE)
            {
                for (int i = 0; i < MAX_TILES_MEM; i++)
                {
                    if (!extra->tiles[i].valid)
                    {
                        found_tile = &extra->tiles[i];
                        found_tile->valid = true;
                        found_tile->is_dark = theme_is_dark;
                        found_tile->z = z;
                        found_tile->x = wrapped_x;
                        found_tile->y = y;
                        found_tile->is_loading = false;
                        found_tile->is_ready = false;
                        found_tile->access_seq = ++extra->access_counter;
                        found_tile->texture_id = 0;
                        chunk->tiles[chunk->tile_count++] = found_tile;
                        break;
                    }
                }
            }
            if (found_tile && !found_tile->is_ready && !found_tile->is_loading)
            {
                found_tile->is_loading = true;
                char filepath[256];
                snprintf(filepath, sizeof(filepath), "%s/osm_%s_%d_%d_%d.png",
                         TILE_CACHE_DIR, theme_is_dark ? "dark" : "light", z, wrapped_x, y);
#ifdef __EMSCRIPTEN__
                if (!request_tile_download(z, wrapped_x, y, theme_is_dark, filepath,
                                           node->node_id, extra))
                    found_tile->is_loading = false;
#else
                if (access(filepath, F_OK) != -1)
                {
                    if (gfx && gfx->load_image)
                    {
                        found_tile->texture_id = gfx->load_image(filepath);
                        if (found_tile->texture_id != 0)
                            found_tile->is_ready = true;
                        else
                        {
                            unlink(filepath);
                            found_tile->is_loading = false;
                        }
                    }
                }
                else if (!request_tile_download(z, wrapped_x, y, theme_is_dark, filepath,
                                                node->node_id, extra))
                    found_tile->is_loading = false;
#endif
            }
            int draw_x = map->rect.x + (int)(x * current_tile_size - view_tl_x);
            int draw_y = map->rect.y + (int)(y * current_tile_size - view_tl_y);
            int draw_size = (int)(current_tile_size) + 1;
            if (found_tile && found_tile->is_ready && found_tile->texture_id != 0 &&
                gfx && gfx->draw_image)
            {
                gfx->draw_image(window_id, draw_x, draw_y, draw_size, draw_size,
                                found_tile->texture_id);
            }
            else
            {
                bool drawn_fallback = false;
                if (z > 0 && gfx && gfx->draw_image)
                {
                    int pz = z - 1;
                    int px = wrapped_x / 2;
                    int py = y / 2;
                    MapTile *fallback = NULL;
                    for (int i = 0; i < MAX_TILES_MEM; i++)
                    {
                        if (extra->tiles[i].valid && extra->tiles[i].z == pz &&
                            extra->tiles[i].x == px && extra->tiles[i].y == py &&
                            extra->tiles[i].is_dark == theme_is_dark && extra->tiles[i].is_ready)
                        {
                            fallback = &extra->tiles[i];
                            break;
                        }
                    }
                    if (fallback)
                    {
                        int p_draw_x = map->rect.x + (int)(px * 2.0 * TILE_SIZE - view_tl_x);
                        int p_draw_y = map->rect.y + (int)(py * 2.0 * TILE_SIZE - view_tl_y);
                        int cx = draw_x < map->rect.x ? map->rect.x : draw_x;
                        int cy = draw_y < map->rect.y ? map->rect.y : draw_y;
                        int cw = draw_x + TILE_SIZE > map->rect.x + map->rect.width ? map->rect.x + map->rect.width - cx : draw_x + TILE_SIZE - cx;
                        int ch = draw_y + TILE_SIZE > map->rect.y + map->rect.height ? map->rect.y + map->rect.height - cy : draw_y + TILE_SIZE - cy;
                        if (cw > 0 && ch > 0)
                        {
                            gfx->graphics_set_clip(cx, cy, cw, ch);
                            gfx->draw_image(window_id, p_draw_x, p_draw_y, TILE_SIZE * 2, TILE_SIZE * 2, fallback->texture_id);
                            gfx->graphics_set_clip(map->rect.x, map->rect.y, map->rect.width, map->rect.height);
                            drawn_fallback = true;
                        }
                    }
                }
            }
        }
    }

    draw_pois(node, window_id, extra, view_tl_x, view_tl_y);

    MAP_ROUTE_MUTEX_LOCK(extra);
    if (extra->route_active && extra->route_point_count > 1 && gfx && gfx->draw_line)
    {
        double z_factor = pow(2.0, extra->display_zoom) * TILE_SIZE;
        for (int i = 0; i < extra->route_point_count - 1; i++)
        {
            double px_x1 = extra->route_lons[i] * z_factor;
            double px_y1 = extra->route_lats[i] * z_factor;
            double px_x2 = extra->route_lons[i + 1] * z_factor;
            double px_y2 = extra->route_lats[i + 1] * z_factor;
            int draw_x1 = map->rect.x + (int)(px_x1 - view_tl_x);
            int draw_y1 = map->rect.y + (int)(px_y1 - view_tl_y);
            int draw_x2 = map->rect.x + (int)(px_x2 - view_tl_x);
            int draw_y2 = map->rect.y + (int)(px_y2 - view_tl_y);
            if (draw_x1 >= map->rect.x - 50 && draw_x1 <= map->rect.x + map->rect.width + 50 &&
                draw_y1 >= map->rect.y - 50 && draw_y1 <= map->rect.y + map->rect.height + 50 &&
                draw_x2 >= map->rect.x - 50 && draw_x2 <= map->rect.x + map->rect.width + 50 &&
                draw_y2 >= map->rect.y - 50 && draw_y2 <= map->rect.y + map->rect.height + 50)
            {
                gfx->draw_line(window_id, draw_x1, draw_y1, draw_x2, draw_y2, extra->route_color, 4.0f, true);
            }
        }
    }
    MAP_ROUTE_MUTEX_UNLOCK(extra);
    double z_factor_m = pow(2.0, extra->display_zoom) * TILE_SIZE;
    for (int i = 0; i < extra->marker_count; i++)
    {
        double lat = extra->markers[i].lat;
        double lon = extra->markers[i].lon;
        double lat_rad = lat * M_PI / 180.0;
        double px_x = (lon + 180.0) / 360.0 * z_factor_m;
        double px_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * z_factor_m;
        int draw_x = map->rect.x + (int)(px_x - view_tl_x);
        int draw_y = map->rect.y + (int)(px_y - view_tl_y);

        if (draw_x >= map->rect.x - 20 && draw_x <= map->rect.x + map->rect.width + 20 &&
            draw_y >= map->rect.y - 20 && draw_y <= map->rect.y + map->rect.height + 20)
        {
            if (extra->markers[i].icon_code && extra->markers[i].icon_font)
            {
                if (gfx && gfx->render_text)
                {
                    int icon_w = aroma_font_get_line_width(extra->markers[i].icon_font, extra->markers[i].icon_code);
                    int icon_h = aroma_font_get_line_height(extra->markers[i].icon_font);

                    int icon_draw_x = draw_x - icon_w / 2;
                    int icon_draw_y = draw_y - icon_h / 2;

                    gfx->render_text(window_id, extra->markers[i].icon_font, extra->markers[i].icon_code,
                                     icon_draw_x + 1, icon_draw_y + 1,
                                     0x80000000, 1.0f);

                    gfx->render_text(window_id, extra->markers[i].icon_font, extra->markers[i].icon_code,
                                     icon_draw_x, icon_draw_y,
                                     extra->markers[i].color, 1.0f);
                }
            }
            else
            {
                if (gfx && gfx->fill_rectangle)
                {
                    gfx->fill_rectangle(window_id, draw_x - 8, draw_y - 8, 16, 16, extra->markers[i].color, true, 8.0f);
                    gfx->fill_rectangle(window_id, draw_x - 6, draw_y - 6, 12, 12, 0xFFFFFFFF, true, 6.0f);
                    gfx->fill_rectangle(window_id, draw_x - 4, draw_y - 4, 8, 8, extra->markers[i].color, true, 4.0f);
                }
            }

            if (extra->active_popup_idx == i && extra->markers[i].popup_text && gfx && gfx->render_text && extra->font)
            {
                int text_w = aroma_font_get_line_width(extra->font, extra->markers[i].popup_text);
                int text_h = aroma_font_get_line_height(extra->font);
                int padding = 6;
                int bg_w = text_w + padding * 2;
                int bg_h = text_h + padding * 2;
                int popup_y = draw_y - 20 - bg_h;

                if (gfx->fill_rectangle)
                {
                    gfx->fill_rectangle(window_id, draw_x - bg_w / 2, popup_y, bg_w, bg_h, 0xFFFFFFFF, true, 4.0f);
                }

                gfx->render_text(window_id, extra->font, extra->markers[i].popup_text,
                                 draw_x - text_w / 2, popup_y + padding, 0xFF000000, 1.0f);
            }
        }
    }
    if (map->show_osm_attribution && extra->font && gfx && gfx->render_text)
    {
        const char *text = "Powered by OpenStreetMap";
        int text_w = aroma_font_get_line_width(extra->font, text);
        int text_h = aroma_font_get_line_height(extra->font);
        int padding = 4;
        int bg_w = text_w + padding * 2;
        int bg_h = text_h + padding * 2;
        int bg_x = map->rect.x + map->rect.width - bg_w - 4;
        int bg_y = map->rect.y + map->rect.height - bg_h - 4;
        uint32_t bg_attr_color = theme_is_dark ? 0xAA000000 : 0xAAFFFFFF;
        if (gfx->fill_rectangle)
            gfx->fill_rectangle(window_id, bg_x, bg_y, bg_w, bg_h, bg_attr_color, true, 4.0f);
        uint32_t text_color = theme_is_dark ? 0xFFFFFFFF : 0xFF000000;
        gfx->render_text(window_id, extra->font, text, bg_x + padding, bg_y + padding, text_color, 1.0f);
    }
    gfx->graphics_clear_clip();
}

void aroma_map_destroy(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (extra && extra->root_id)
        aroma_event_unsubscribe(extra->root_id, EVENT_TYPE_KEY_PRESS, __map_event_handler_global);
    if (extra)
    {
        if (extra->anim_timer)
            aroma_timer_cancel(extra->anim_timer);
        AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
        for (int i = 0; i < MAX_CHUNKS; i++)
        {
            if (extra->chunks[i].is_loaded)
            {
                unload_chunk(extra, &extra->chunks[i]);
            }
        }
        for (int i = 0; i < MAX_TILES_MEM; i++)
        {
            if (extra->tiles[i].valid && extra->tiles[i].is_ready && gfx && gfx->unload_image)
                gfx->unload_image(extra->tiles[i].texture_id);
        }
        if (extra->markers)
        {
            for (int i = 0; i < extra->marker_count; i++)
            {
                if (extra->markers[i].icon_code)
                {
                    free(extra->markers[i].icon_code);
                    extra->markers[i].icon_code = NULL;
                }
                if (extra->markers[i].popup_text)
                {
                    free(extra->markers[i].popup_text);
                    extra->markers[i].popup_text = NULL;
                }
            }
            free(extra->markers);
            extra->markers = NULL;
        }
        if (extra->font)
            aroma_font_destroy(extra->font);
        if (extra->icon_font)
            aroma_font_destroy(extra->icon_font);
        if (extra->route_lats)
            free(extra->route_lats);
        if (extra->route_lons)
            free(extra->route_lons);
        if (extra->turn_instructions)
            free(extra->turn_instructions);
        free_osrm_graph(&extra->osrm_graph);
        free_osrm_grid(&extra->osrm_grid);
        free_poi_manager(&extra->poi_manager);
#ifndef __EMSCRIPTEN__
        pthread_mutex_lock(&extra->mbtiles_mutex);
        if (extra->mbtiles_db)
        {
            sqlite3_close(extra->mbtiles_db);
            extra->mbtiles_db = NULL;
        }
        pthread_mutex_unlock(&extra->mbtiles_mutex);
        pthread_mutex_destroy(&extra->mbtiles_mutex);
#endif
        MAP_ROUTE_MUTEX_DESTROY(extra);
        MAP_GEOCODE_MUTEX_DESTROY(extra);
        MAP_GPS_MUTEX_DESTROY(extra);
        free(extra);
        map->extra = NULL;
    }
    free(node->node_widget_ptr);
    node->node_widget_ptr = NULL;
    __destroy_node(node);
}

static bool __map_event_handler_global(AromaEvent *event, void *user_data)
{
    if (!event || !user_data)
        return false;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)user_data;
    if (event->event_type == EVENT_TYPE_KEY_PRESS)
    {
        if (event->data.key.key_code == 'z' || event->data.key.key_code == 'Z' || event->data.key.key_code == '=')
        {
            aroma_map_zoom_in(extra->node_ptr);
            return true;
        }
        else if (event->data.key.key_code == 'x' || event->data.key.key_code == 'X' || event->data.key.key_code == '-')
        {
            aroma_map_zoom_out(extra->node_ptr);
            return true;
        }
    }
    return false;
}

void aroma_map_zoom_in(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    if (extra->zoom < extra->max_zoom)
    {
        extra->zoom++;
        map->zoom = extra->zoom;
        extra->center_px_x *= 2.0;
        extra->display_px_x *= 2.0;
        extra->center_px_y *= 2.0;
        extra->display_px_y *= 2.0;
        unload_old_zoom_tiles(extra);
    }
    aroma_node_invalidate(node);
}

void aroma_map_zoom_out(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    if (extra->zoom > extra->min_zoom)
    {
        extra->zoom--;
        map->zoom = extra->zoom;
        extra->center_px_x /= 2.0;
        extra->display_px_x /= 2.0;
        extra->center_px_y /= 2.0;
        extra->display_px_y /= 2.0;
        unload_old_zoom_tiles(extra);

        if (extra->zoom < 15)
        {
            aroma_map_clear_markers(node);
            extra->poi_manager.pois_visible = false;
        }
    }
    aroma_node_invalidate(node);
}

void aroma_map_set_center(AromaNode *node, double lat, double lon)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    double lat_rad = lat * M_PI / 180.0;
    double px_x = (lon + 180.0) / 360.0 * (1 << extra->zoom) * TILE_SIZE;
    double px_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * (1 << extra->zoom) * TILE_SIZE;
    extra->center_px_x = px_x;
    extra->center_px_y = px_y;
    map->center_lat = lat;
    map->center_lon = lon;
    aroma_node_invalidate(node);
}

void aroma_map_pan_to(AromaNode *node, double lat, double lon)
{
    aroma_map_set_center(node, lat, lon);
}

void aroma_map_set_center_instant(AromaNode *node, double lat, double lon)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
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

void aroma_map_set_zoom(AromaNode *node, int zoom)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    if (zoom < extra->min_zoom)
        zoom = extra->min_zoom;
    if (zoom > extra->max_zoom)
        zoom = extra->max_zoom;
    if (extra->zoom == zoom)
        return;
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

void aroma_map_set_animations_enabled(AromaNode *node, bool enabled)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    extra->animations_enabled = enabled;
    if (!enabled)
    {
        extra->display_zoom = extra->zoom;
        extra->display_px_x = extra->center_px_x;
        extra->display_px_y = extra->center_px_y;
        extra->velocity_x = 0;
        extra->velocity_y = 0;
        aroma_node_invalidate(node);
    }
}

bool aroma_map_get_animations_enabled(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return true;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    return extra ? extra->animations_enabled : true;
}

void aroma_map_set_show_attribution(AromaNode *node, bool show)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    if (map->show_osm_attribution != show)
    {
        map->show_osm_attribution = show;
        aroma_node_invalidate(node);
    }
}

void aroma_map_add_marker(AromaNode *node, double lat, double lon, uint32_t color)
{
    if (!node || node->node_type != NODE_TYPE_WIDGET)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    if (!map || !map->extra)
        return;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;

    if (extra->marker_count >= extra->marker_capacity)
    {
        extra->marker_capacity *= 2;
        MapMarker *new_markers = realloc(extra->markers, extra->marker_capacity * sizeof(MapMarker));
        if (!new_markers)
            return;
        extra->markers = new_markers;
    }

    extra->markers[extra->marker_count].lat = lat;
    extra->markers[extra->marker_count].lon = lon;
    extra->markers[extra->marker_count].color = color;
    extra->markers[extra->marker_count].icon_code = NULL;
    extra->markers[extra->marker_count].icon_font = NULL;
    extra->markers[extra->marker_count].popup_text = NULL;
    extra->marker_count++;
    aroma_node_invalidate(node);
}

void aroma_map_add_icon_marker_with_font(AromaNode *node, double lat, double lon, uint32_t color, const char *icon_code, AromaFont *icon_font)
{
    if (!node || node->node_type != NODE_TYPE_WIDGET)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    if (!map || !map->extra)
        return;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;

    if (extra->marker_count >= extra->marker_capacity)
    {
        extra->marker_capacity *= 2;
        MapMarker *new_markers = realloc(extra->markers, extra->marker_capacity * sizeof(MapMarker));
        if (!new_markers)
            return;
        extra->markers = new_markers;
    }

    extra->markers[extra->marker_count].lat = lat;
    extra->markers[extra->marker_count].lon = lon;
    extra->markers[extra->marker_count].color = color;
    extra->markers[extra->marker_count].icon_code = icon_code ? strdup(icon_code) : NULL;
    extra->markers[extra->marker_count].icon_font = icon_font;
    extra->markers[extra->marker_count].popup_text = NULL;
    extra->marker_count++;
    aroma_node_invalidate(node);
}

void aroma_map_add_icon_popup_marker(AromaNode *node, double lat, double lon, uint32_t color,
                                     const char *icon_code, AromaFont *icon_font, const char *popup_text)
{
    if (!node || node->node_type != NODE_TYPE_WIDGET)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    if (!map || !map->extra)
        return;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;

    if (extra->marker_count >= extra->marker_capacity)
    {
        extra->marker_capacity *= 2;
        MapMarker *new_markers = realloc(extra->markers, extra->marker_capacity * sizeof(MapMarker));
        if (!new_markers)
            return;
        extra->markers = new_markers;
    }

    extra->markers[extra->marker_count].lat = lat;
    extra->markers[extra->marker_count].lon = lon;
    extra->markers[extra->marker_count].color = color;
    extra->markers[extra->marker_count].icon_code = icon_code ? strdup(icon_code) : NULL;
    extra->markers[extra->marker_count].icon_font = icon_font;
    extra->markers[extra->marker_count].popup_text = popup_text ? strdup(popup_text) : NULL;
    extra->marker_count++;
    aroma_node_invalidate(node);
}

void aroma_map_add_popup_marker(AromaNode *node, double lat, double lon, uint32_t color, const char *popup_text)
{
    if (!node || node->node_type != NODE_TYPE_WIDGET)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    if (!map || !map->extra)
        return;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;

    if (extra->marker_count >= extra->marker_capacity)
    {
        extra->marker_capacity *= 2;
        MapMarker *new_markers = realloc(extra->markers, extra->marker_capacity * sizeof(MapMarker));
        if (!new_markers)
            return;
        extra->markers = new_markers;
    }

    extra->markers[extra->marker_count].lat = lat;
    extra->markers[extra->marker_count].lon = lon;
    extra->markers[extra->marker_count].color = color;
    extra->markers[extra->marker_count].icon_code = NULL;
    extra->markers[extra->marker_count].icon_font = NULL;
    extra->markers[extra->marker_count].popup_text = popup_text ? strdup(popup_text) : NULL;
    extra->marker_count++;
    aroma_node_invalidate(node);
}

void aroma_map_clear_markers(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    for (int i = 0; i < extra->marker_count; i++)
    {
        if (extra->markers[i].icon_code)
        {
            free(extra->markers[i].icon_code);
            extra->markers[i].icon_code = NULL;
        }
        if (extra->markers[i].popup_text)
        {
            free(extra->markers[i].popup_text);
            extra->markers[i].popup_text = NULL;
        }
    }
    extra->marker_count = 0;
    extra->active_popup_idx = -1;
    aroma_node_invalidate(node);
}

void aroma_map_geocode_search(AromaNode *node, const char *query,
                              void (*callback)(GeocodeResult *results, int count, void *user_data),
                              void *user_data)
{
    if (!node || !node->node_widget_ptr || !query || !callback)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    MAP_GEOCODE_MUTEX_LOCK(extra);
    if (extra->geocode_loading)
    {
        MAP_GEOCODE_MUTEX_UNLOCK(extra);
        return;
    }
    extra->geocode_loading = true;
    MAP_GEOCODE_MUTEX_UNLOCK(extra);
#ifdef __EMSCRIPTEN__
    EmscriptenGeocodeRequest *req = malloc(sizeof(EmscriptenGeocodeRequest));
    if (!req)
    {
        MAP_GEOCODE_MUTEX_LOCK(extra);
        extra->geocode_loading = false;
        MAP_GEOCODE_MUTEX_UNLOCK(extra);
        return;
    }
    strncpy(req->query, query, 255);
    req->query[255] = '\0';
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
    MAP_GEOCODE_MUTEX_LOCK(extra);
    extra->geocode_loading = false;
    MAP_GEOCODE_MUTEX_UNLOCK(extra);
#else
    GeocodeRequest *req = malloc(sizeof(GeocodeRequest));
    if (!req)
    {
        MAP_GEOCODE_MUTEX_LOCK(extra);
        extra->geocode_loading = false;
        MAP_GEOCODE_MUTEX_UNLOCK(extra);
        return;
    }
    strncpy(req->query, query, 255);
    req->query[255] = '\0';
    req->callback = callback;
    req->user_data = user_data;
    req->node_id = node->node_id;
    pthread_t fetch_thread;
    if (pthread_create(&fetch_thread, NULL, geocode_fetch_worker, req) != 0)
    {
        free(req);
        MAP_GEOCODE_MUTEX_LOCK(extra);
        extra->geocode_loading = false;
        MAP_GEOCODE_MUTEX_UNLOCK(extra);
        return;
    }
    pthread_detach(fetch_thread);
    MAP_GEOCODE_MUTEX_LOCK(extra);
    extra->geocode_loading = false;
    MAP_GEOCODE_MUTEX_UNLOCK(extra);
#endif
}

void aroma_map_set_mbtiles(AromaNode *node, const char *filepath)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
#ifndef __EMSCRIPTEN__
    pthread_mutex_lock(&extra->mbtiles_mutex);
    if (extra->mbtiles_db)
    {
        if (extra->mbtiles_stmt_tile)
        {
            sqlite3_finalize(extra->mbtiles_stmt_tile);
            extra->mbtiles_stmt_tile = NULL;
        }
        sqlite3_close(extra->mbtiles_db);
        extra->mbtiles_db = NULL;
    }
    strncpy(extra->mbtiles_path, filepath ? filepath : "", sizeof(extra->mbtiles_path) - 1);
    extra->mbtiles_path[sizeof(extra->mbtiles_path) - 1] = '\0';
    if (strlen(extra->mbtiles_path) > 0)
    {
        if (sqlite3_open_v2(extra->mbtiles_path, &extra->mbtiles_db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_SHAREDCACHE, NULL) != SQLITE_OK)
        {
            extra->mbtiles_db = NULL;
            extra->mbtiles_stmt_tile = NULL;
            extra->use_mbtiles_zoom = false;
            extra->min_zoom = 0;
            extra->max_zoom = 18;
        }
        else
        {
            const char *sql = "SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?";
            if (sqlite3_prepare_v2(extra->mbtiles_db, sql, -1, &extra->mbtiles_stmt_tile, NULL) != SQLITE_OK)
                extra->mbtiles_stmt_tile = NULL;
            read_mbtiles_zoom_range(extra->mbtiles_db, &extra->min_zoom, &extra->max_zoom);
            extra->use_mbtiles_zoom = true;
            clamp_zoom_to_mbtiles(extra);
            if (extra->node_ptr)
            {
                AromaMap *map_ptr = (AromaMap *)extra->node_ptr->node_widget_ptr;
                if (map_ptr)
                    map_ptr->zoom = extra->zoom;
            }
            unload_old_zoom_tiles(extra);
        }
    }
    else
    {
        extra->use_mbtiles_zoom = false;
        extra->min_zoom = 0;
        extra->max_zoom = 18;
    }
    pthread_mutex_unlock(&extra->mbtiles_mutex);
    aroma_node_invalidate(node);
#else
    (void)filepath;
#endif
}

bool aroma_map_load_osrm_data(AromaNode *node, const char *binary_file)
{
    if (!node || !node->node_widget_ptr || !binary_file)
        return false;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return false;

    if (extra->osrm_loading)
        return false;

    extra->osrm_loading = true;

#ifndef __EMSCRIPTEN__
    OSRMLoadRequest *req = malloc(sizeof(OSRMLoadRequest));
    if (!req)
    {
        extra->osrm_loading = false;
        return false;
    }
    req->node = node;
    strncpy(req->filepath, binary_file, sizeof(req->filepath) - 1);
    req->filepath[sizeof(req->filepath) - 1] = '\0';

    pthread_t osrm_thread;
    if (pthread_create(&osrm_thread, NULL, osrm_loading_worker, req) != 0)
    {
        free(req);
        extra->osrm_loading = false;
        return false;
    }
    pthread_detach(osrm_thread);
    extra->osrm_thread = osrm_thread;
    return true;
#else
    if (extra->osrm_graph.is_loaded)
    {
        free_osrm_graph(&extra->osrm_graph);
        free_osrm_grid(&extra->osrm_grid);
    }
    bool loaded = load_osrm_binary(&extra->osrm_graph, binary_file);
    if (loaded)
    {
        init_osrm_grid(&extra->osrm_graph, &extra->osrm_grid);
    }
    extra->osrm_loading = false;
    return loaded;
#endif
}

void aroma_map_unload_osrm_data(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    free_osrm_graph(&extra->osrm_graph);
    free_osrm_grid(&extra->osrm_grid);
}

bool aroma_map_is_osrm_loaded(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return false;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    return extra ? extra->osrm_graph.is_loaded : false;
}

uint32_t aroma_map_get_osrm_node_count(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return 0;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    return extra ? extra->osrm_graph.node_count : 0;
}

uint32_t aroma_map_get_osrm_edge_count(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return 0;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    return extra ? extra->osrm_graph.edge_count : 0;
}

void aroma_map_set_gps_position(AromaNode *node, double lat, double lon, double heading, double speed_kmh)
{
    if (!node || !node->node_widget_ptr)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    MAP_GPS_MUTEX_LOCK(extra);
    extra->gps_position.lat = lat;
    extra->gps_position.lon = lon;
    extra->gps_position.heading = heading;
    extra->gps_position.speed_kmh = speed_kmh;
    extra->gps_position.has_fix = true;
    MAP_GPS_MUTEX_UNLOCK(extra);

    if (extra->route_active)
    {
        update_route_progress(extra);
    }
}

int aroma_map_get_route_points(AromaNode *node, double **lats, double **lons)
{
    if (!node || !node->node_widget_ptr || !lats || !lons)
        return 0;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra || !extra->route_active)
        return 0;

    *lats = extra->route_lats;
    *lons = extra->route_lons;
    return extra->route_point_count;
}

void aroma_map_get_route_progress(AromaNode *node, RouteProgress *progress)
{
    if (!node || !node->node_widget_ptr || !progress)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    memcpy(progress, &extra->route_progress, sizeof(RouteProgress));
}

void aroma_map_get_next_turn(AromaNode *node, TurnInstruction *turn)
{
    if (!node || !node->node_widget_ptr || !turn)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra || extra->turn_count == 0)
        return;
    int idx = extra->route_progress.next_turn_index;
    if (idx >= 0 && idx < extra->turn_count)
    {
        memcpy(turn, &extra->turn_instructions[idx], sizeof(TurnInstruction));
    }
}

void aroma_map_set_route_offline(AromaNode *node, double start_lat, double start_lon,
                                 double end_lat, double end_lon, uint32_t route_color)
{
    if (!node || node->node_type != NODE_TYPE_WIDGET)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    if (!extra->osrm_graph.is_loaded)
        return;

    uint32_t start_node = find_nearest_osrm_node(&extra->osrm_graph, &extra->osrm_grid, start_lat, start_lon);
    uint32_t end_node = find_nearest_osrm_node(&extra->osrm_graph, &extra->osrm_grid, end_lat, end_lon);

    if (start_node == end_node)
        return;

    uint32_t *path = NULL;
    uint32_t path_len = 0;
    osrm_dijkstra(&extra->osrm_graph, start_node, end_node, &path, &path_len);

    MAP_ROUTE_MUTEX_LOCK(extra);
    extra->route_color = route_color;

    if (extra->route_lats)
        free(extra->route_lats);
    if (extra->route_lons)
        free(extra->route_lons);
    if (extra->turn_instructions)
    {
        free(extra->turn_instructions);
        extra->turn_instructions = NULL;
    }
    extra->turn_count = 0;

    extra->route_lats = NULL;
    extra->route_lons = NULL;
    extra->route_point_count = 0;
    extra->route_active = false;

    if (path_len > 1 && path)
    {
        extra->route_lats = malloc(path_len * sizeof(double));
        extra->route_lons = malloc(path_len * sizeof(double));

        if (extra->route_lats && extra->route_lons)
        {
            extra->route_point_count = path_len;
            for (uint32_t i = 0; i < path_len; i++)
            {
                double lat = extra->osrm_graph.nodes[path[i]].lat;
                double lon = extra->osrm_graph.nodes[path[i]].lon;
                double lat_rad = lat * M_PI / 180.0;
                extra->route_lats[i] = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0;
                extra->route_lons[i] = (lon + 180.0) / 360.0;
            }
            extra->route_active = true;
            extra->route_progress.next_turn_index = 0;
            extra->route_progress.last_distance_to_turn = 0.0;
            generate_turn_instructions(extra, path, path_len);
        }
    }
    MAP_ROUTE_MUTEX_UNLOCK(extra);

    if (path)
        free(path);

    aroma_node_invalidate(node);
}

AromaNode *aroma_map_create(AromaNode *parent, int x, int y, int width, int height)
{
#ifdef __ANDROID__
    x = aroma_android_dp_to_px(x);
    y = aroma_android_dp_to_px(y);
    width = aroma_android_dp_to_px(width);
    height = aroma_android_dp_to_px(height);
#endif
#ifndef __EMSCRIPTEN__
    if (!curl_initialized)
    {
        curl_global_init(CURL_GLOBAL_ALL);
        curl_initialized = true;
        mkdir(TILE_CACHE_DIR, 0777);
#if defined(_SC_NPROCESSORS_ONLN)
        int cores = sysconf(_SC_NPROCESSORS_ONLN);
        if (cores > 0)
            num_active_workers = cores * 2;
#endif
#if defined(ESP32)
        num_active_workers = 1;
#endif
        if (num_active_workers > MAX_WORKER_THREADS)
            num_active_workers = MAX_WORKER_THREADS;
        if (num_active_workers < 1)
            num_active_workers = 1;
        worker_running = true;
        for (int i = 0; i < num_active_workers; i++)
        {
            if (pthread_create(&worker_threads[i], NULL, tile_fetch_worker, NULL) != 0)
            {
            }
            else
                pthread_detach(worker_threads[i]);
        }
    }
#endif

    AromaMap *map = (AromaMap *)calloc(1, sizeof(AromaMap));
    if (!map)
        return NULL;
    map->rect.x = x;
    map->rect.y = y;
    map->rect.width = width;
    map->rect.height = height;
    map->zoom = 6;
    map->center_lat = 0.0;
    map->center_lon = 0.0;
    map->show_osm_attribution = false;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)calloc(1, sizeof(struct AromaMapExtra));
    if (!extra)
    {
        free(map);
        return NULL;
    }
    extra->zoom = map->zoom;
    extra->min_zoom = 0;
    extra->max_zoom = 18;
    extra->use_mbtiles_zoom = false;
    memset(&extra->osrm_graph, 0, sizeof(OSRMGraph));
    memset(&extra->osrm_grid, 0, sizeof(OSRMGridIndex));
    extra->osrm_loading = false;
    extra->osrm_thread = 0;
    extra->route_progress.next_turn_index = 0;
    extra->route_progress.last_distance_to_turn = 0.0;
    init_chunks(extra);
#ifndef __EMSCRIPTEN__
    pthread_mutex_init(&extra->mbtiles_mutex, NULL);
    extra->mbtiles_db = NULL;
    extra->mbtiles_path[0] = '\0';
#endif
    MAP_ROUTE_MUTEX_INIT(extra);
    MAP_GEOCODE_MUTEX_INIT(extra);
    MAP_GPS_MUTEX_INIT(extra);
    init_poi_manager(&extra->poi_manager);
    extra->route_lats = NULL;
    extra->route_lons = NULL;
    extra->route_point_count = 0;
    extra->route_color = 0;
    extra->route_active = false;
    extra->route_loading = false;
    extra->geocode_loading = false;
    extra->turn_instructions = NULL;
    extra->turn_count = 0;
    extra->center_px_x = 8192.0;
    extra->center_px_y = 8192.0;
    extra->display_px_x = 8192.0;
    extra->display_px_y = 8192.0;
    extra->display_zoom = map->zoom;
    extra->velocity_x = 0;
    extra->velocity_y = 0;
    extra->animations_enabled = true;
    extra->anim_timer = aroma_timer_create(16, true, __map_anim_tick, extra);
    extra->font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 12);
    extra->icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 18);
    extra->poi_draw_callback = NULL;
    extra->poi_draw_user_data = NULL;
    extra->poi_hit_test_callback = NULL;
    extra->poi_hit_test_user_data = NULL;
    extra->marker_capacity = INITIAL_MARKER_CAPACITY;
    extra->markers = calloc(extra->marker_capacity, sizeof(MapMarker));
    extra->marker_count = 0;
    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, map);
    if (node)
        extra->node_ptr = node;
    AromaNode *root = aroma_event_get_root();
    extra->root_id = root ? root->node_id : 0;
    if (extra->root_id)
        aroma_event_subscribe(extra->root_id, EVENT_TYPE_KEY_PRESS, __map_event_handler_global, extra, 90);
    if (!node)
    {
        if (extra->markers)
            free(extra->markers);
        if (extra->font)
            aroma_font_destroy(extra->font);
        if (extra->icon_font)
            aroma_font_destroy(extra->icon_font);
        if (extra->anim_timer)
            aroma_timer_cancel(extra->anim_timer);
        free_poi_manager(&extra->poi_manager);
        MAP_ROUTE_MUTEX_DESTROY(extra);
        MAP_GEOCODE_MUTEX_DESTROY(extra);
        MAP_GPS_MUTEX_DESTROY(extra);
        free(extra);
        free(map);
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

void aroma_map_set_route(AromaNode *node, double start_lat, double start_lon, double end_lat, double end_lon, uint32_t color)
{
    if (!node || node->node_type != NODE_TYPE_WIDGET)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    MAP_ROUTE_MUTEX_LOCK(extra);
    extra->route_color = color;
    if (extra->route_loading)
    {
        MAP_ROUTE_MUTEX_UNLOCK(extra);
        return;
    }
    extra->route_loading = true;
    MAP_ROUTE_MUTEX_UNLOCK(extra);
#ifdef __EMSCRIPTEN__
    EmscriptenRouteRequest *req = malloc(sizeof(EmscriptenRouteRequest));
    if (!req)
    {
        MAP_ROUTE_MUTEX_LOCK(extra);
        extra->route_loading = false;
        MAP_ROUTE_MUTEX_UNLOCK(extra);
        return;
    }
    req->start_lat = start_lat;
    req->start_lon = start_lon;
    req->end_lat = end_lat;
    req->end_lon = end_lon;
    req->node = node;
    req->extra = extra;
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
    RouteRequest *req = malloc(sizeof(RouteRequest));
    if (!req)
    {
        MAP_ROUTE_MUTEX_LOCK(extra);
        extra->route_loading = false;
        MAP_ROUTE_MUTEX_UNLOCK(extra);
        return;
    }
    req->start_lat = start_lat;
    req->start_lon = start_lon;
    req->end_lat = end_lat;
    req->end_lon = end_lon;
    req->node = node;
    req->extra = extra;
    pthread_t fetch_thread;
    if (pthread_create(&fetch_thread, NULL, route_fetch_worker, req) != 0)
    {
        free(req);
        MAP_ROUTE_MUTEX_LOCK(extra);
        extra->route_loading = false;
        MAP_ROUTE_MUTEX_UNLOCK(extra);
        return;
    }
    pthread_detach(fetch_thread);
#endif
}

void aroma_map_clear_route(AromaNode *node)
{
    if (!node || node->node_type != NODE_TYPE_WIDGET)
        return;
    AromaMap *map = (AromaMap *)node->node_widget_ptr;
    struct AromaMapExtra *extra = (struct AromaMapExtra *)map->extra;
    if (!extra)
        return;
    MAP_ROUTE_MUTEX_LOCK(extra);
    extra->route_active = false;
    extra->route_point_count = 0;
    if (extra->route_lats)
    {
        free(extra->route_lats);
        extra->route_lats = NULL;
    }
    if (extra->route_lons)
    {
        free(extra->route_lons);
        extra->route_lons = NULL;
    }
    if (extra->turn_instructions)
    {
        free(extra->turn_instructions);
        extra->turn_instructions = NULL;
    }
    extra->turn_count = 0;
    extra->route_progress.next_turn_index = 0;
    extra->route_progress.last_distance_to_turn = 0.0;
    MAP_ROUTE_MUTEX_UNLOCK(extra);
    aroma_node_invalidate(node);
}

#endif