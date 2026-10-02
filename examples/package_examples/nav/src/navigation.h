#ifndef NAVIGATION_H
#define NAVIGATION_H

#include "aroma.h"
#include "navigation_geo.h"

#define POI_QUERY_MIN_INTERVAL_MS 400.0
#define POI_QUERY_MOVE_THRESHOLD_DEG 0.0008
#define POI_QUERY_ZOOM_THRESHOLD 0.25
#define SEARCH_DEBOUNCE_US 350000
#define EARTH_RADIUS 6371.0
#define MAX_SUGGESTIONS 512
#define NUM_POI_CATEGORIES 14
#define ITEMS_PER_PAGE 6
#define OFF_ROUTE_THRESHOLD_M 55.0
#define OFF_ROUTE_CONFIRM_FRAMES 20
#define RE_ROUTE_COOLDOWN_FRAMES 120

typedef struct
{
    bool active;
    double from_lat, from_lon;
    double to_lat, to_lon;
    char from_text[256];
    char to_text[256];
    double distance_km;
    int eta_minutes;
    bool navigation_active;
    bool route_ready;
    bool simulation_started;
    int frame;
    int seg_index;
    double seg_progress_m;
    double seg_length_m;
    double *path_lat;
    double *path_lon;
    int route_point_count;
    double display_heading;
    bool have_heading;
    double speed;
    double current_lat, current_lon;
    int off_route_counter;
    int reroute_cooldown_frames;
} NavigationState;

typedef enum
{
    OTA_STATE_IDLE,
    OTA_START,
    OTA_RUN,
    OTA_SUCCESS,
    OTA_FAILURE,
    OTA_DOWNLOAD,
    OTA_DONE,
    OTA_SUBPROCESS,
    OTA_PROGRESS,
} OtaProgressStatus;

typedef struct
{
    unsigned int magic;
    unsigned int status;
    unsigned int dwl_percent;
    unsigned long long dwl_bytes;
    unsigned int nsteps;
    unsigned int cur_step;
    unsigned int cur_percent;
    char cur_image[256];
    char hnd_name[64];
    int source;
    unsigned int infolen;
    char info[2048];
} SwupdateProgressMsg;

bool open_maps(AromaNode *node, void *user_data);
void close_maps(void *user_data);



#endif
