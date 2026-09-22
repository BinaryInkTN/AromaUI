#ifndef VEHICLE_VIEW_H
#define VEHICLE_VIEW_H

#include "aroma.h"
#include "app_state.h"
#include "lock_screen.h"
#include "vehicle_camera.h"
#include "apps/media/media_controls.h"

#include "app_registry.h"

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

void opening_anim(AromaNode *target, float progress, void *user_data);
void closing_anim(AromaNode *target, float progress, void *user_data);

/* App open/close animation convention (all apps + packages):
 * 300ms, ease-out-cubic in, ease-in-out-quad out. */
#define APP_ANIM_MS 300
#define APP_ANIM_OPEN_EASE AROMA_EASE_OUT_CUBIC
#define APP_ANIM_CLOSE_EASE AROMA_EASE_IN_OUT_QUAD

/* Third-party packages: create (or reveal) the drawer card + app root for
 * an installed package id. Unknown ids fail. Removal is handled inside
 * package_manager_uninstall (full teardown); this only re-packs the grid. */
bool vehicle_view_add_package_card(const char *id);
void vehicle_view_remove_package_card(const char *id);

/* Open an installed package by id. */
bool vehicle_view_open_package(const char *id);

/* Visual-test hook (example only, zero impact unless AROMA_DEBUG_SCREEN
 * is set): programmatically opens drawer/store/packages/wizard for
 * headless screenshot verification. */
void vehicle_view_debug_open(const char *what);

/* Settings-owned Bluetooth card refresh (called from media home thread). */
void update_bt_info_card(void);

/* Enable/disable the Bluetooth speaker stack. Shared by the settings
 * toggle and the first-run setup wizard. */
bool vehicle_view_set_bluetooth_enabled(bool enabled);
bool vehicle_view_is_bluetooth_enabled(void);

/* Raise every descendant of root above the root card itself (see above).
 * Needed for Incense-mounted or otherwise z-unset content. */
void vehicle_view_raise_subtree(AromaNode *root);

#endif
