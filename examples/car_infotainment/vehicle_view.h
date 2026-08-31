#ifndef VEHICLE_VIEW_H
#define VEHICLE_VIEW_H

#include "aroma.h"
#include "app_state.h"
#include "lock_screen.h"
#include "vehicle_camera.h"
#include "media_controls.h"

typedef struct
{
    const char *name;
    const char *icon;
    uint32_t card_color;
    bool (*open_func)(AromaNode *node, void *user_data);
    void *user_data;
    AromaNode *drawer_icon;
    AromaNode *drawer_card;
    AromaNode *app_root;
} AppDefinition;

extern AppDefinition app_definitions[];
#define APP_COUNT (sizeof(app_definitions) / sizeof(app_definitions[0]))

void build_vehicle_view(AromaNode *window);
void update_vehicle_view(void);

const char *resolve_asset_path(const char *filename);
double monotonic_ms(void);
double calculate_distance_km(double lat1, double lon1, double lat2, double lon2);
void format_time_string(int minutes, char *buf, size_t size);
void format_distance_string(double km, char *buf, size_t size);
void truncate_for_listview(const char *input, char *output, size_t output_size);
void apply_theme_colors(void);

void *media_monitor_thread_func(void *arg);
void set_app_open(bool open);
void send_app_drawer_behind(void);
bool is_any_app_open(void);
extern bool app_drawer_visible;

#define MEDIA_UPDATE_INTERVAL_US 500000

#endif
