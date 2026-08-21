#include "vehicle_view.h"
#include "app_state.h"
#include "aroma_animation.h"
#include "bt_speaker_api.h"
#include "bt_speaker_hfp.h"
#include "navigation_geo.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>

#define SWUPDATE_CTRL_SOCKET_DEFAULT "/tmp/sockinstctrl"
#define SWUPDATE_PROGRESS_SOCKET_DEFAULT "/tmp/swupdateprog"
#define SWUPDATE_PID_FILE "/var/run/swupdate.pid"
#define SWUPDATE_WEB_PORT_ENV "SWUPDATE_WEBSERVER_PORT"
#define SWUPDATE_WEB_PORT_DEFAULT 8080
#define Z_LAYER_CARDS_TOP 1000
void update_media_card_display(void);
#define SWUPDATE_IPC_MAGIC 0x1002003
typedef enum
{
    SWU_REQ_INSTALL = 1,
} swupdate_ipc_req_type_t;

typedef struct
{
    int magic;
    int type;
    union
    {
        char buf[128];
    } data;
} swupdate_ipc_message_t;

typedef enum
{
    SWU_IDLE,
    SWU_START,
    SWU_RUN,
    SWU_SUCCESS,
    SWU_FAILURE,
    SWU_DOWNLOAD,
    SWU_DONE,
    SWU_SUBPROCESS,
    SWU_PROGRESS,
} swupdate_progress_status_t;

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
} swupdate_progress_msg_t;
#include <time.h>

#define POI_QUERY_MIN_INTERVAL_MS 400.0  
#define POI_QUERY_MOVE_THRESHOLD_DEG 0.0008 
#define POI_QUERY_ZOOM_THRESHOLD 0.25

static bool poi_query_in_flight = false;
static double last_poi_query_time_ms = 0.0;

static double monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}
static bool dark_mode_enabled = false;
static AromaNode *settings_dark_mode_switch = NULL;

void populate_contact_listview(AromaNode *listview);
static void start_navigation(double from_lat, double from_lon, double to_lat, double to_lon);
bool open_maps(AromaNode *node, void *user_data);
static bool open_phone(AromaNode *node, void *user_data);
static bool open_music(AromaNode *node, void *user_data);
static bool open_settings(AromaNode *node, void *user_data);
void close_music(void *user_data);
void close_settings(void *user_data);
static void update_music_now_playing_display(void);
static void update_music_device_display(void);
static void on_music_icon_click(void *user_data);

#define MEDIA_UPDATE_INTERVAL_US 500000
#define CONTACTS_PER_PAGE 7
#define MAX_DIALER_DIGITS 32
#define BOTTOM_BAR_X_COLLAPSED 20
#define BOTTOM_BAR_X_EXPANDED 20
#define EARTH_RADIUS 6371.0

#define GMAPS_COLOR_PRIMARY 0xFF1A73E8
#define GMAPS_COLOR_PRIMARY_DARK 0xFF1967D2
#define GMAPS_COLOR_SURFACE 0xFFFFFFFF
#define GMAPS_COLOR_SURFACE_VARIANT 0xFFF1F3F4
#define GMAPS_COLOR_ON_SURFACE 0xFF202124
#define GMAPS_COLOR_ON_SURFACE_VARIANT 0xFF5F6368
#define GMAPS_COLOR_OUTLINE 0xFFDADCE0
#define GMAPS_COLOR_DESTINATION 0xFFEA4335
#define GMAPS_COLOR_START 0xFF34A853
#define GMAPS_COLOR_SCRIM 0xDD000000

#define ANDROID_COLOR_BACKGROUND 0xFFF5F5F5
#define ANDROID_COLOR_SURFACE 0xFFFFFFFF
#define ANDROID_COLOR_PRIMARY 0xFF1A73E8
#define ANDROID_COLOR_ON_SURFACE 0xFF202124
#define ANDROID_COLOR_ON_SURFACE_VARIANT 0xFF5F6368
#define ANDROID_COLOR_OUTLINE 0xFFDADCE0

#define IOS_COLOR_BACKGROUND 0xFFF2F2F7
#define IOS_COLOR_GROUPED_BACKGROUND 0xFFFFFFFF
#define IOS_COLOR_SEPARATOR 0xFFC6C6C8
#define IOS_COLOR_LABEL 0xFF000000
#define IOS_COLOR_SECONDARY_LABEL 0xFF3C3C43
#define IOS_COLOR_TERTIARY_LABEL 0xFF48484A
#define IOS_COLOR_BLUE 0xFF007AFF
#define IOS_COLOR_GREEN 0xFF34C759
#define IOS_COLOR_RED 0xFFFF3B30
#define IOS_COLOR_ORANGE 0xFFFF9500
#define IOS_COLOR_PURPLE 0xFFAF52DE
#define IOS_COLOR_GRAY 0xFF8E8E93
#define IOS_COLOR_TINT 0xFF007AFF

#define DARK_COLOR_BACKGROUND 0xFF1C1C1E
#define DARK_COLOR_SURFACE 0xFF2C2C2E
#define DARK_COLOR_LABEL 0xFFFFFFFF
#define DARK_COLOR_SECONDARY_LABEL 0xFF8E8E93
#define DARK_COLOR_SEPARATOR 0xFF38383A

#define OFF_ROUTE_THRESHOLD_M 55.0
#define OFF_ROUTE_CONFIRM_FRAMES 20
#define RE_ROUTE_COOLDOWN_FRAMES 120
#define ITEMS_PER_PAGE 6
#define MAX_SUGGESTIONS 512
#define NUM_POI_CATEGORIES 14

static bool lock_screen_active = true;
static AromaNode *lock_screen_root = NULL;
static AromaNode *lock_screen_time = NULL;
static AromaNode *lock_screen_date = NULL;
static AromaNode *lock_screen_unlock_btn = NULL;

static bt_state_t g_bt_state = BT_STATE_IDLE;
static bt_device_info_t g_bt_device_info = {0};
static bt_media_info_t g_bt_media_info = {0};
static bt_stats_t g_bt_stats = {0};
static bool g_bt_initialized = false;
static bool g_bt_connected = false;
static pthread_mutex_t g_bt_mutex = PTHREAD_MUTEX_INITIALIZER;

static void on_bt_state_changed(bt_state_t old_state, bt_state_t new_state, void *user_data);
static void on_bt_device_changed(const bt_device_info_t *device, bool connected, void *user_data);
static void on_bt_error(bt_error_t error, const char *message, void *user_data);
static void on_bt_log(const char *level, const char *message, void *user_data);
static void on_bt_audio_changed(bool started, void *user_data);
static void on_bt_avrcp_changed(const bt_media_info_t *media, void *user_data);
static void on_bt_call_changed(const bt_call_info_t *call, bool removed, void *user_data);

typedef struct
{
    const char *name;
    const char *icon;
    POICategory category;
} POICategoryInfo;

static const POICategoryInfo poi_categories[] = {
    {"Gas Station", AROMA_ICON_LOCAL_GAS_STATION, POI_CATEGORY_GAS_STATION},
    {"Restaurant", AROMA_ICON_RESTAURANT, POI_CATEGORY_RESTAURANT},
    {"Cafe", AROMA_ICON_LOCAL_CAFE, POI_CATEGORY_CAFE},
    {"Fast Food", AROMA_ICON_RESTAURANT, POI_CATEGORY_FAST_FOOD},
    {"Shop", AROMA_ICON_SHOP, POI_CATEGORY_SHOP},
    {"Supermarket", AROMA_ICON_LOCAL_GROCERY_STORE, POI_CATEGORY_SUPERMARKET},
    {"Hotel", AROMA_ICON_LOCAL_HOTEL, POI_CATEGORY_HOTEL},
    {"Bank", AROMA_ICON_ACCOUNT_BALANCE, POI_CATEGORY_BANK},
    {"ATM", AROMA_ICON_LOCAL_ATM, POI_CATEGORY_ATM},
    {"Pharmacy", AROMA_ICON_LOCAL_PHARMACY, POI_CATEGORY_PHARMACY},
    {"Hospital", AROMA_ICON_LOCAL_HOSPITAL, POI_CATEGORY_HOSPITAL},
    {"Parking", AROMA_ICON_LOCAL_PARKING, POI_CATEGORY_PARKING},
    {"Charging", AROMA_ICON_EV_STATION, POI_CATEGORY_CHARGING_STATION},
    {"Business", AROMA_ICON_PLACE, POI_CATEGORY_OTHER_BUSINESS},
};

static AromaNode *app_drawer = NULL;
static AromaNode *app_drawer_grid = NULL;
static AromaNode *app_drawer_close_btn = NULL;
static bool app_drawer_visible = false;
static bool app_drawer_behind_app = false;
#define APP_DRAWER_Z_INDEX (Z_LAYER_STATUS_BAR +1)

static void send_app_drawer_behind(void)
{
    if (!app_drawer)
        return;
    aroma_node_set_z_index(app_drawer, Z_LAYER_STATUS_BAR + 5);
    app_drawer_visible = true;
    app_drawer_behind_app = true;
}

static AromaNode *map_options_card = NULL;
static bool map_options_visible = false;

static bool on_satellite_switch_changed(AromaNode *switch_node, void *user_data)
{
    (void)user_data;
    if (aroma_switch_get_state(switch_node))
    {
        aroma_map_set_mbtiles(state.map_node,
        #ifdef __EMSCRIPTEN__
            "/assets/ariana_sat.mbtiles"
        #elif defined(__arm__) || defined(__aarch64__)
            "/usr/share/infotainment/assets/ariana_sat.mbtiles"
        #else
            "../assets/ariana_sat.mbtiles"
        #endif
        );
    }
    else
    {
        aroma_map_set_mbtiles(state.map_node, 
        #ifdef __EMSCRIPTEN__
            "/assets/ariana_3d.mbtiles"
        #elif defined(__arm__) || defined(__aarch64__)
            "/usr/share/infotainment/assets/ariana_3d.mbtiles"
        #else
            "../assets/ariana_3d.mbtiles"
        #endif
        );
    }
    aroma_node_invalidate(state.map_node);
    return true;
}

static void on_map_options_click(void *user_data)
{
    (void)user_data;
    map_options_visible = !map_options_visible;
    if (map_options_card)
    {
        aroma_node_set_hidden(map_options_card, !map_options_visible);
    }
}

static void on_map_options_close_click(void *user_data)
{
    (void)user_data;
    map_options_visible = false;
    if (map_options_card)
    {
        aroma_node_set_hidden(map_options_card, true);
    }
}

static void restore_app_drawer_from_behind(void)
{
    if (!app_drawer_behind_app)
        return;
    app_drawer_behind_app = false;
    if (!app_drawer)
        return;
    aroma_node_set_z_index(app_drawer, APP_DRAWER_Z_INDEX);
    app_drawer_visible = true;
}

static void apply_theme_colors(void)
{
    if(dark_mode_enabled)
    {
        state.theme = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_BLUE);
        state.theme.colors.surface = 0xFF000000;
        aroma_image_set_source(state.backroad,
        #ifdef __EMSCRIPTEN__
            "/assets/bg_dark.jpeg"
        #elif defined(__arm__) || defined(__aarch64__) 
            "/usr/share/infotainment/assets/bg_dark.jpeg"
        #else
            "../assets/bg_dark.jpeg"
        #endif
        );
    }
    else
    {
        state.theme = aroma_theme_create_material_blue();
        aroma_image_set_source(state.backroad, 
        #ifdef __EMSCRIPTEN__
            "/assets/backroad_blur.png"
        #elif defined(__arm__) || defined(__aarch64__)
            "/usr/share/infotainment/assets/backroad_blur.png"
        #else
            "../assets/backroad_blur.png"
        #endif
        );
    }
    aroma_ui_set_theme(&state.theme);
}

void on_preset_item_click(int index, void *user_data)
{
    (void)user_data;
    if (index < 0 || index >= NUM_POI_CATEGORIES)
        return;
    
}

static bool on_dark_mode_switch_changed(AromaNode *switch_node, void *user_data)
{
    (void)user_data;
    dark_mode_enabled = aroma_switch_get_state(switch_node);
    apply_theme_colors();
    return true;
}

typedef struct {
    const char *name;
    const char *icon;
    uint32_t card_color;
    bool (*open_func)(AromaNode *node, void *user_data);
    void *user_data;
    AromaNode *drawer_icon;
    AromaNode *drawer_card;
    AromaNode *app_root;
} AppDefinition;

static AppDefinition app_definitions[4];
#define APP_COUNT (sizeof(app_definitions) / sizeof(app_definitions[0]))

static const char *swupdate_ctrl_socket_path(void)
{
    const char *env = getenv("SWUPDATE_SOCKET_PATH");
    return (env && env[0]) ? env : SWUPDATE_CTRL_SOCKET_DEFAULT;
}

static const char *swupdate_progress_socket_path(void)
{
    const char *env = getenv("SWUPDATE_PROGRESS_SOCKET_PATH");
    return (env && env[0]) ? env : SWUPDATE_PROGRESS_SOCKET_DEFAULT;
}

static int swupdate_connect_socket(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    return fd;
}

static bool swupdate_send_install_request(char *err_buf, size_t err_buf_len)
{
    const char *sock_path = swupdate_ctrl_socket_path();
    int fd = swupdate_connect_socket(sock_path);
    if (fd < 0)
    {
        snprintf(err_buf, err_buf_len,
                 "cannot reach swupdate control socket %s: %s (is swupdate running on this device?)",
                 sock_path, strerror(errno));
        return false;
    }

    swupdate_ipc_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.magic = SWUPDATE_IPC_MAGIC;
    msg.type = SWU_REQ_INSTALL;

    ssize_t sent = write(fd, &msg, sizeof(msg));
    if (sent != (ssize_t)sizeof(msg))
    {
        snprintf(err_buf, err_buf_len, "short write to swupdate control socket: %s", strerror(errno));
        close(fd);
        return false;
    }

    swupdate_ipc_message_t ack;
    ssize_t got = read(fd, &ack, sizeof(ack));
    close(fd);

    if (got != (ssize_t)sizeof(ack))
    {
        snprintf(err_buf, err_buf_len, "no ACK from swupdate control socket (connection closed early)");
        return false;
    }
    return true;
}

static bool swupdate_poll_progress(swupdate_progress_msg_t *out_msg, int timeout_ms)
{
    const char *sock_path = swupdate_progress_socket_path();
    int fd = swupdate_connect_socket(sock_path);
    if (fd < 0)
        return false;

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t got = read(fd, out_msg, sizeof(*out_msg));
    close(fd);

    if (got != (ssize_t)sizeof(*out_msg))
        return false;

    return true;
}

static bool swupdate_is_running(void)
{
    int fd = swupdate_connect_socket(swupdate_ctrl_socket_path());
    if (fd >= 0)
    {
        close(fd);
        return true;
    }
    
    FILE *pid_file = fopen(SWUPDATE_PID_FILE, "r");
    if (!pid_file)
        return false;
    
    pid_t pid;
    if (fscanf(pid_file, "%d", &pid) != 1)
    {
        fclose(pid_file);
        return false;
    }
    fclose(pid_file);
    
    if (kill(pid, 0) == 0)
        return true;
    
    return false;
}

static int swupdate_get_web_port(void)
{
    const char *env_port = getenv(SWUPDATE_WEB_PORT_ENV);
    if (env_port && env_port[0])
    {
        int port = atoi(env_port);
        if (port > 0 && port < 65536)
            return port;
    }
    return SWUPDATE_WEB_PORT_DEFAULT;
}

static bool bottom_bar_app_open = false;
static pthread_mutex_t app_open_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t contact_list_lock = PTHREAD_MUTEX_INITIALIZER;

static char dialer_number[MAX_DIALER_DIGITS] = "";
static AromaNode *dialer_display_label = NULL;
static AromaNode *dialer_card = NULL;
static int sorted_to_original[100];

static AromaNode *music_app_tabs = NULL;
static bool music_app_open = false;
static int music_active_tab = 0;

static AromaNode *music_now_playing_card = NULL;
static AromaNode *music_art_placeholder = NULL;
static AromaNode *music_track_title_label = NULL;
static AromaNode *music_track_artist_label = NULL;
static AromaNode *music_track_album_label = NULL;
static AromaNode *music_status_label = NULL;
static AromaNode *music_prev_button = NULL;
static AromaNode *music_play_pause_button = NULL;
static AromaNode *music_next_button = NULL;
static AromaNode *music_no_media_label = NULL;
static AromaNode *music_open_btn = NULL;

static AromaNode *music_device_card = NULL;
static AromaNode *music_device_status_icon = NULL;
static AromaNode *music_device_status_label = NULL;
static AromaNode *music_device_name_label = NULL;
static AromaNode *music_device_address_label = NULL;
static AromaNode *music_device_stats_label = NULL;
static AromaNode *music_device_no_phone_label = NULL;

static AromaNode *settings_sidebar = NULL;
static AromaNode *settings_page_general = NULL;
static AromaNode *settings_page_display = NULL;
static AromaNode *settings_page_updates = NULL;
static AromaNode *settings_bluetooth_switch = NULL;
static AromaNode *settings_autolock_label = NULL;

static AromaNode *bt_info_card = NULL;
static AromaNode *bt_info_name_label = NULL;
static AromaNode *bt_info_address_label = NULL;
static AromaNode *bt_info_status_label = NULL;

static AromaNode *settings_ota_autoinstall_switch = NULL;
static AromaNode *settings_ota_status_label = NULL;
static AromaNode *settings_ota_version_label = NULL;
static AromaNode *settings_ota_check_btn = NULL;
static AromaNode *settings_ota_icon = NULL;
static AromaNode *settings_ota_progress_bar = NULL;
static AromaNode *settings_ota_progress_label = NULL;
static AromaNode *settings_swupdate_status_label = NULL;
static AromaNode *settings_swupdate_port_label = NULL;
static AromaNode *settings_swupdate_card = NULL;
static AromaNode *settings_map_options_card = NULL;

typedef enum
{
    OTA_STATE_IDLE,
    OTA_STATE_CONNECTING,
    OTA_STATE_RUNNING,
    OTA_STATE_SUCCESS,
    OTA_STATE_FAILURE,
    OTA_STATE_DAEMON_UNREACHABLE,
} OtaState;
static OtaState settings_ota_state = OTA_STATE_IDLE;
static bool settings_ota_autoinstall = true;
static int ota_progress = 0;
static char ota_status_detail[256] = "";
static char ota_current_image[256] = "";
static pthread_mutex_t ota_state_lock = PTHREAD_MUTEX_INITIALIZER;
static bool ota_poll_thread_running = false;

static AromaNode *incoming_call_overlay = NULL;
static AromaNode *incoming_call_name_label = NULL;
static AromaNode *incoming_call_number_label = NULL;
static AromaNode *incoming_call_accept_btn = NULL;
static AromaNode *incoming_call_reject_btn = NULL;
static AromaNode *incoming_call_end_btn = NULL;
static bool call_overlay_visible = false;
static char current_call_name[128] = "";
static char current_call_number[64] = "";
static char current_call_path[256] = "";
static pthread_mutex_t call_state_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct
{
    AromaNode *media_card;
    AromaNode *media_title_label;
    AromaNode *media_artist_label;
    AromaNode *media_prev_button;
    AromaNode *media_play_pause_button;
    AromaNode *media_next_button;
    bool is_playing;
    bool bottom_bar_expanded;
    bool ui_initialized;
    bool first_media_check_done;
} MediaPlayerUI;

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

static NavigationState map_nav = {0};

static AromaNode *map_search_surface = NULL;
static AromaNode *map_search_placeholder_label = NULL;
static AromaNode *map_search_back_btn = NULL;
static bool map_search_expanded = false;
static bool maps_screen_open = false;

static AromaNode *map_from_entry = NULL;
static AromaNode *map_to_entry = NULL;
static AromaNode *map_swap_btn = NULL;
static AromaNode *map_go_btn = NULL;

static AromaNode *map_route_sheet = NULL;
static AromaNode *map_distance_label = NULL;
static AromaNode *map_time_label = NULL;
static AromaNode *map_route_dest_label = NULL;
static AromaNode *map_end_nav_btn = NULL;

static AromaNode *nav_banner_card = NULL;
static AromaNode *nav_turn_icon = NULL;
static AromaNode *nav_banner_label = NULL;
static AromaNode *nav_banner_sub = NULL;
static AromaNode *nav_eta_label = NULL;
static AromaNode *nav_dist_label = NULL;
static AromaNode *nav_speed_label = NULL;
static AromaNode *nav_turn_dist_label = NULL;
static AromaNode *nav_bottom_card = NULL;

static AromaNode *map_search_results_list = NULL;
static GeocodeResult map_geocode_results[MAX_GEOCODE_RESULTS];
static int map_geocode_result_count = 0;
static char last_search_query[256] = "";
static pthread_mutex_t search_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool search_results_visible = false;
static int focused_entry = 0;

#define SEARCH_DEBOUNCE_US 350000
static pthread_mutex_t debounce_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned long search_generation = 0;
static char pending_search_query[256] = "";
static int pending_search_focused_entry = 0;

typedef struct
{
    int x, y, w, h;
} LocalOffset;

static const LocalOffset maps_icon_offset = {30, 15, 48, 48};
static const LocalOffset phone_icon_offset = {100, 15, 48, 48};
static const LocalOffset music_icon_offset = {170, 15, 48, 48};
static const LocalOffset settings_icon_offset = {240, 15, 48, 48};

static MediaPlayerUI media_ui = {
    .is_playing = false,
    .bottom_bar_expanded = false,
    .ui_initialized = false,
    .first_media_check_done = false};

static int contact_page = 0;
static int total_pages = 0;
static AromaNode *prev_page_btn = NULL;
static AromaNode *next_page_btn = NULL;
static AromaNode *page_label = NULL;
static AromaNode *pagination_card = NULL;

static AromaNode *suggestion_cards[ITEMS_PER_PAGE];
static AromaNode *suggestion_name_labels[ITEMS_PER_PAGE];
static AromaNode *suggestion_desc_labels[ITEMS_PER_PAGE];
static AromaNode *suggestion_pick_buttons[ITEMS_PER_PAGE];
static AromaNode *suggestion_page;
static AromaNode *page_label_suggestions;
static PointOfInterest *filtered_pois;
static int filtered_poi_count = 0;
static bool selecting_from = false;
static bool category_enabled[NUM_POI_CATEGORIES];
static int current_page = 0;
static int total_pages_suggestions = 1;
static double last_center_lat = 0.0, last_center_lon = 0.0, last_zoom = 0.0;
static int poi_update_counter = 0;
static bool poi_refresh_forced = false;

typedef struct
{
    NavigationState *state;
    int slot_index;
} SuggestionSlotContext;

static void update_pois_markers(void);
static SuggestionSlotContext suggestion_slot_contexts[ITEMS_PER_PAGE];

static bool on_pois_switch_changed(AromaNode *switch_node, void *user_data)
{
    (void)user_data;
    bool enabled = aroma_switch_get_state(switch_node);
    
    static bool saved_categories[NUM_POI_CATEGORIES] = {false};
    static bool categories_saved = false;
    
    if (!enabled)
    {
        for (int i = 0; i < NUM_POI_CATEGORIES; i++)
        {
            saved_categories[i] = category_enabled[i];
        }
        categories_saved = true;
        
        for (int i = 0; i < NUM_POI_CATEGORIES; i++)
        {
            category_enabled[i] = false;
        }
    }
    else
    {
        if (categories_saved)
        {
            for (int i = 0; i < NUM_POI_CATEGORIES; i++)
            {
                category_enabled[i] = saved_categories[i];
            }
        }
        else
        {
            for (int i = 0; i < NUM_POI_CATEGORIES; i++)
            {
                category_enabled[i] = true;
            }
        }
    }
    
    poi_refresh_forced = true;
    last_center_lat = 0.0;
    last_center_lon = 0.0;
    last_zoom = 0.0;
    
    if (maps_screen_open && state.map_node && !map_nav.navigation_active)
    {
        update_pois_markers();
    }
    
    return true;
}

static void update_bt_info_card(void)
{
    if (!bt_info_card || !bt_info_name_label || !bt_info_address_label || !bt_info_status_label)
        return;
    
    pthread_mutex_lock(&g_bt_mutex);
    bt_device_info_t device = g_bt_device_info;
    bt_state_t bt_state = g_bt_state;
    pthread_mutex_unlock(&g_bt_mutex);
    
    if (device.connected && device.name[0])
    {
        char name_buf[128];
        snprintf(name_buf, sizeof(name_buf), "Name: %s", device.name);
        aroma_label_set_text(bt_info_name_label, name_buf);
        
        char addr_buf[128];
        snprintf(addr_buf, sizeof(addr_buf), "Address: %s", device.address[0] ? device.address : "Unknown");
        aroma_label_set_text(bt_info_address_label, addr_buf);
        
        const char *status_text = "Connected";
        if (bt_state == BT_STATE_PLAYING)
            status_text = "Connected - Playing Audio";
        else if (bt_state == BT_STATE_CONNECTED)
            status_text = "Connected";
        aroma_label_set_text(bt_info_status_label, status_text);
        
        aroma_node_set_hidden(bt_info_card, false);
    }
    else
    {
        aroma_node_set_hidden(bt_info_card, true);
    }
}

static bool on_settings_bluetooth_changed(AromaNode *switch_node, void *user_data)
{
    (void)user_data;
    bool enabled = aroma_switch_get_state(switch_node);
    
    if (enabled)
    {
        if (!g_bt_initialized)
        {
            bt_config_t config = {
                .device_name = "Aroma Infotainment",
                .pin_code = "0000",
                .verbose = true,
                .state_cb = on_bt_state_changed,
                .state_cb_data = NULL,
                .device_cb = on_bt_device_changed,
                .device_cb_data = NULL,
                .error_cb = on_bt_error,
                .error_cb_data = NULL,
                .audio_cb = on_bt_audio_changed,
                .audio_cb_data = NULL,
                .log_cb = on_bt_log,
                .log_cb_data = NULL,
                .avrcp_cb = on_bt_avrcp_changed,
                .avrcp_cb_data = NULL,
            };
            if (bt_speaker_init(&config) != 0)
            {
                fprintf(stderr, "[BT] init failed: %s\n",
                        bt_speaker_get_last_error_message());
            }
            else if (bt_speaker_get_state() != BT_STATE_ADVERTISING)
            {
                /* init() returned 0 but didn't reach ADVERTISING — most
                 * likely register_agent() failed (see on_bt_log above for
                 * why). Pairing attempts will be silently rejected by
                 * BlueZ until this resolves, since there's no agent to
                 * answer the PIN/confirmation request. */
                fprintf(stderr,
                        "[BT] warning: state is '%s' after init, not "
                        "advertising — pairing attempts may be rejected "
                        "with no visible error until the agent registers.\n",
                        bt_speaker_get_state_string());
            }
            bt_hfp_init();
            bt_hfp_set_call_callback(on_bt_call_changed, NULL);
            g_bt_initialized = true;
        }
        bt_speaker_start();
    }
    else
    {
        bt_speaker_stop();
        bt_speaker_cleanup();
        bt_hfp_cleanup();
        g_bt_initialized = false;
        g_bt_connected = false;
        memset(&g_bt_device_info, 0, sizeof(bt_device_info_t));
        memset(&g_bt_media_info, 0, sizeof(bt_media_info_t));
    }
    
    update_bt_info_card();
    update_media_card_display();
    
    return true;
}

static double calculate_distance_km(double lat1, double lon1, double lat2, double lon2)
{
    double dlat = (lat2 - lat1) * M_PI / 180.0;
    double dlon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
                   sin(dlon / 2) * sin(dlon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return EARTH_RADIUS * c;
}

static int estimate_eta_minutes(double distance_km)
{
    return (int)((distance_km / 50.0) * 60.0) + 1;
}

static void format_time_string(int minutes, char *buf, size_t size)
{
    if (minutes < 60)
    {
        snprintf(buf, size, "%d min", minutes);
    }
    else
    {
        int hours = minutes / 60;
        int mins = minutes % 60;
        snprintf(buf, size, "%d hr %d min", hours, mins);
    }
}

static void format_distance_string(double km, char *buf, size_t size)
{
    if (km < 1.0)
    {
        snprintf(buf, size, "%d m", (int)(km * 1000));
    }
    else
    {
        snprintf(buf, size, "%.1f km", km);
    }
}

static void truncate_for_listview(const char *input, char *output, size_t output_size)
{
    if (!input || output_size == 0)
    {
        if (output_size > 0)
            output[0] = '\0';
        return;
    }

    size_t len = strlen(input);
    const size_t ellipsis_len = 3;
    if (len > output_size - 1 && output_size > ellipsis_len + 1)
    {
        size_t cut = output_size - 1 - ellipsis_len;
        memcpy(output, input, cut);
        memcpy(output + cut, "...", ellipsis_len);
        output[cut + ellipsis_len] = '\0';
    }
    else
    {
        strncpy(output, input, output_size - 1);
        output[output_size - 1] = '\0';
    }
}
static void update_pois_markers(void)
{
    if (!state.map_node)
        return;

    if (map_nav.navigation_active)
        return;

    AromaMap *map_widget = (AromaMap *)state.map_node->node_widget_ptr;
    if (!map_widget) return;

    double center_lat = map_widget->center_lat;
    double center_lon = map_widget->center_lon;
    /* Was hardcoded to 18.0 — that made fabs(zoom - last_zoom) always
     * converge to 0 after the first call (last_zoom gets set to the same
     * constant every time), so the POI refresh's zoom-change trigger was
     * permanently dead, and the bounding-box query below used a fixed
     * zoom that didn't match what was actually on screen. */
    double zoom = aroma_map_get_zoom(state.map_node);

    if (poi_query_in_flight)
        return;

    double now_ms = monotonic_ms();
    bool time_elapsed = (now_ms - last_poi_query_time_ms) >= POI_QUERY_MIN_INTERVAL_MS;
    bool moved_enough =
        fabs(center_lat - last_center_lat) > POI_QUERY_MOVE_THRESHOLD_DEG ||
        fabs(center_lon - last_center_lon) > POI_QUERY_MOVE_THRESHOLD_DEG ||
        fabs(zoom - last_zoom) > POI_QUERY_ZOOM_THRESHOLD;

    if (!poi_refresh_forced && !moved_enough)
        return;

    if (!time_elapsed && !poi_refresh_forced)
        return;

    poi_query_in_flight = true;
    poi_refresh_forced = false;
    last_poi_query_time_ms = now_ms;

    poi_update_counter++;

    last_center_lat = center_lat;
    last_center_lon = center_lon;
    last_zoom = zoom;

    AromaRect *map_rect = aroma_node_get_rect(state.map_node);
    double half_w = map_rect ? map_rect->width / 2.0 : 512.0;
    double half_h = map_rect ? map_rect->height / 2.0 : 300.0;

    double lat_rad = center_lat * M_PI / 180.0;
    double z_factor = pow(2.0, zoom) * 256.0;

    double px_x = (center_lon + 180.0) / 360.0 * z_factor;
    double px_y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * z_factor;

    double margin = 1.5;
    double min_px_x = px_x - half_w * margin;
    double max_px_x = px_x + half_w * margin;
    double min_px_y = px_y - half_h * margin;
    double max_px_y = px_y + half_h * margin;

    double view_min_lon = (min_px_x / z_factor) * 360.0 - 180.0;
    double view_max_lon = (max_px_x / z_factor) * 360.0 - 180.0;

    double n_min = M_PI - 2.0 * M_PI * (min_px_y / z_factor);
    double n_max = M_PI - 2.0 * M_PI * (max_px_y / z_factor);

    double view_max_lat = 180.0 / M_PI * (2.0 * atan(exp(n_min)) - M_PI / 2.0);
    double view_min_lat = 180.0 / M_PI * (2.0 * atan(exp(n_max)) - M_PI / 2.0);

    if (view_max_lat < view_min_lat) { double tmp = view_max_lat; view_max_lat = view_min_lat; view_min_lat = tmp; }
    if (view_max_lon < view_min_lon) { double tmp = view_max_lon; view_max_lon = view_min_lon; view_min_lon = tmp; }

    view_min_lat = fmax(view_min_lat, -85.0);
    view_max_lat = fmin(view_max_lat, 85.0);
    view_min_lon = fmax(view_min_lon, -180.0);
    view_max_lon = fmin(view_max_lon, 180.0);

    int enabled_category_count = 0;
    for (int j = 0; j < NUM_POI_CATEGORIES; j++)
    {
        if (category_enabled[j])
            enabled_category_count++;
    }

    if (enabled_category_count == 0)
    {
        aroma_map_clear_markers(state.map_node);
        aroma_node_invalidate(state.map_node);
        poi_query_in_flight = false;
        return;
    }

    int result_count = 0;
    PointOfInterest *pois = aroma_map_query_pois_in_viewport(
        state.map_node, view_min_lat, view_max_lat, view_min_lon, view_max_lon, &result_count);

    if (!pois || result_count == 0)
    {
        poi_query_in_flight = false;
        return;
    }

    typedef struct
    {
        double lat;
        double lon;
        uint32_t color;
        const char *icon_code;
    } MarkerCandidate;

    MarkerCandidate candidates[200];
    int candidate_count = 0;

    int max_markers = 200;

    for (int i = 0; i < result_count && candidate_count < max_markers; i++)
    {
        PointOfInterest *poi = &pois[i];

        bool cat_enabled = false;
        for (int j = 0; j < NUM_POI_CATEGORIES; j++)
        {
            if (poi->category == poi_categories[j].category && category_enabled[j])
            {
                cat_enabled = true;
                break;
            }
        }

        if (!cat_enabled) continue;
        if (!poi->name[0]) continue;

        uint32_t color = 0xFF999999;
        const char *icon_code = AROMA_ICON_PLACE;

        switch (poi->category)
        {
        case POI_CATEGORY_GAS_STATION: color = 0xFFFF6600; icon_code = AROMA_ICON_LOCAL_GAS_STATION; break;
        case POI_CATEGORY_RESTAURANT:
        case POI_CATEGORY_FAST_FOOD: color = 0xFFFF3333; icon_code = AROMA_ICON_RESTAURANT; break;
        case POI_CATEGORY_CAFE: color = 0xFF8B4513; icon_code = AROMA_ICON_LOCAL_CAFE; break;
        case POI_CATEGORY_PARKING: color = 0xFF3366FF; icon_code = AROMA_ICON_LOCAL_PARKING; break;
        case POI_CATEGORY_CHARGING_STATION: color = 0xFF00FF00; icon_code = AROMA_ICON_EV_STATION; break;
        case POI_CATEGORY_HOSPITAL: color = 0xFFFF0000; icon_code = AROMA_ICON_LOCAL_HOSPITAL; break;
        case POI_CATEGORY_PHARMACY: color = 0xFF009933; icon_code = AROMA_ICON_LOCAL_PHARMACY; break;
        case POI_CATEGORY_HOTEL: color = 0xFF0066CC; icon_code = AROMA_ICON_LOCAL_HOTEL; break;
        case POI_CATEGORY_ATM: color = 0xFF006699; icon_code = AROMA_ICON_LOCAL_ATM; break;
        case POI_CATEGORY_BANK: color = 0xFF003366; icon_code = AROMA_ICON_ACCOUNT_BALANCE; break;
        case POI_CATEGORY_SHOP: color = 0xFF9933FF; icon_code = AROMA_ICON_SHOP; break;
        case POI_CATEGORY_SUPERMARKET: color = 0xFF00CC00; icon_code = AROMA_ICON_LOCAL_GROCERY_STORE; break;
        default: color = 0xFF999999; icon_code = AROMA_ICON_PLACE; break;
        }

        candidates[candidate_count].lat = poi->lat;
        candidates[candidate_count].lon = poi->lon;
        candidates[candidate_count].color = color;
        candidates[candidate_count].icon_code = icon_code;
        candidate_count++;
    }

    if (candidate_count == 0)
    {
        poi_query_in_flight = false;
        return;
    }

    aroma_map_clear_markers(state.map_node);
    for (int i = 0; i < candidate_count; i++)
    {
        aroma_map_add_icon_marker_with_font(state.map_node,
                                            candidates[i].lat,
                                            candidates[i].lon,
                                            candidates[i].color,
                                            candidates[i].icon_code,
                                            state.icon_font);
    }

    aroma_node_invalidate(state.map_node);

    poi_query_in_flight = false;
}
static void populate_suggestion_cards(void)
{
    int start = current_page * ITEMS_PER_PAGE;
    int end = start + ITEMS_PER_PAGE;
    if (end > filtered_poi_count) end = filtered_poi_count;

    for (int slot = 0; slot < ITEMS_PER_PAGE; slot++)
    {
        int i = start + slot;
        if (i >= end)
        {
            aroma_node_set_hidden(suggestion_cards[slot], true);
            continue;
        }

        PointOfInterest *poi = &filtered_pois[i];
        const char *name = poi->name[0] ? poi->name : "Unnamed";
        const char *street = poi->street[0] ? poi->street : NULL;
        const char *area = poi->area[0] ? poi->area : NULL;

        char description[256];
        if (street && area && street[0] && area[0])
            snprintf(description, sizeof(description), "%s, %s", street, area);
        else if (street && street[0])
            snprintf(description, sizeof(description), "%s", street);
        else if (area && area[0])
            snprintf(description, sizeof(description), "%s", area);
        else if (poi->address[0])
            snprintf(description, sizeof(description), "%s", poi->address);
        else
            snprintf(description, sizeof(description), "%.6f, %.6f", poi->lat, poi->lon);

        aroma_label_set_text(suggestion_name_labels[slot], name);
        aroma_label_set_text(suggestion_desc_labels[slot], description);
        aroma_node_set_hidden(suggestion_cards[slot], false);
    }

    char page_text[64];
    snprintf(page_text, sizeof(page_text), "Page %d/%d", current_page + 1, total_pages_suggestions);
    aroma_label_set_text(page_label_suggestions, page_text);
}

static void update_suggestions(const char *query)
{
    if (filtered_pois) { free(filtered_pois); filtered_pois = NULL; }
    filtered_poi_count = 0;
    current_page = 0;

    if (!query || query[0] == '\0')
    {
        aroma_node_set_hidden(suggestion_page, true);
        return;
    }

    int result_count = 0;
    PointOfInterest *results = aroma_map_query_pois_by_name(state.map_node, query, MAX_SUGGESTIONS, &result_count);

    if (result_count > 0 && results)
    {
        filtered_pois = malloc(result_count * sizeof(PointOfInterest));
        if (filtered_pois)
        {
            memcpy(filtered_pois, results, result_count * sizeof(PointOfInterest));
            filtered_poi_count = result_count;
        }
    }

    if (filtered_poi_count > 0)
    {
        total_pages_suggestions = (filtered_poi_count + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        populate_suggestion_cards();
        aroma_node_invalidate(suggestion_page);
        aroma_node_set_hidden(suggestion_page, false);
    }
    else
    {
        aroma_node_set_hidden(suggestion_page, true);
    }
}

static bool on_suggestion_pick(AromaNode *btn, void *user_data)
{
    (void)btn;
    SuggestionSlotContext *ctx = (SuggestionSlotContext *)user_data;
    if (!ctx) return true;

    int actual_index = current_page * ITEMS_PER_PAGE + ctx->slot_index;
    if (actual_index >= filtered_poi_count) return true;

    PointOfInterest *poi = &filtered_pois[actual_index];
    const char *name = poi->name[0] ? poi->name : "Unnamed";

    if (selecting_from)
    {
        aroma_textbox_set_text(map_from_entry, name);
        map_nav.from_lat = poi->lat;
        map_nav.from_lon = poi->lon;
        strncpy(map_nav.from_text, name, sizeof(map_nav.from_text) - 1);
        map_nav.from_text[sizeof(map_nav.from_text) - 1] = '\0';
    }
    else
    {
        aroma_textbox_set_text(map_to_entry, name);
        map_nav.to_lat = poi->lat;
        map_nav.to_lon = poi->lon;
        strncpy(map_nav.to_text, name, sizeof(map_nav.to_text) - 1);
        map_nav.to_text[sizeof(map_nav.to_text) - 1] = '\0';
    }

    aroma_node_set_hidden(suggestion_page, true);
    return true;
}

static bool on_from_text_changed(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    selecting_from = true;
    update_suggestions(text);
    return true;
}

static bool on_to_text_changed(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    selecting_from = false;
    update_suggestions(text);
    return true;
}

static bool on_prev_page(AromaNode *btn, void *user_data)
{
    (void)btn;
    (void)user_data;
    if (current_page <= 0) return true;
    current_page--;
    populate_suggestion_cards();
    return true;
}

static bool on_next_page(AromaNode *btn, void *user_data)
{
    (void)btn;
    (void)user_data;
    if (current_page >= total_pages_suggestions - 1) return true;
    current_page++;
    populate_suggestion_cards();
    return true;
}

static bool on_close_suggestions(AromaNode *btn, void *user_data)
{
    (void)btn;
    (void)user_data;
    aroma_node_set_hidden(suggestion_page, true);
    return true;
}

static void on_geocode_results(GeocodeResult *results, int count, void *user_data)
{
    (void)user_data;

    pthread_mutex_lock(&search_mutex);

    map_geocode_result_count = count;
    if (count > MAX_GEOCODE_RESULTS)
        count = MAX_GEOCODE_RESULTS;

    for (int i = 0; i < count && i < MAX_GEOCODE_RESULTS; i++)
    {
        map_geocode_results[i] = results[i];
    }

    if (map_search_results_list)
    {
        aroma_listview_clear(map_search_results_list);

        if (count == 0)
        {
            aroma_listview_add_item_with_icon(map_search_results_list,
                                              "No results found", "Try a different search term",
                                              AROMA_ICON_SEARCH, NULL);
        }
        else
        {
            for (int i = 0; i < count; i++)
            {
                char display_name[256];
                char subtitle[256];

                truncate_for_listview(results[i].display_name, display_name, sizeof(display_name));

                if (results[i].category[0])
                {
                    snprintf(subtitle, sizeof(subtitle), "%s - %s",
                             results[i].category,
                             results[i].type[0] ? results[i].type : "place");
                }
                else
                {
                    snprintf(subtitle, sizeof(subtitle), "%s",
                             results[i].type[0] ? results[i].type : "place");
                }

                aroma_listview_add_item_with_icon(map_search_results_list,
                                                  display_name, subtitle,
                                                  AROMA_ICON_PLACE, (void *)(intptr_t)i);
            }
        }

        aroma_node_set_hidden(map_search_results_list, false);
        search_results_visible = true;
    }

    pthread_mutex_unlock(&search_mutex);
}

static void perform_map_search(const char *query)
{
    if (!query || !query[0] || strlen(query) < 2)
    {
        if (map_search_results_list)
        {
            aroma_node_set_hidden(map_search_results_list, true);
            search_results_visible = false;
        }
        return;
    }

    if (strcmp(query, last_search_query) == 0)
    {
        return;
    }

    strncpy(last_search_query, query, sizeof(last_search_query) - 1);
    last_search_query[sizeof(last_search_query) - 1] = '\0';

    aroma_map_geocode_search(state.map_node, query, on_geocode_results, NULL);
}

typedef struct
{
    unsigned long generation;
    char query[256];
    int focused_entry;
} DebouncedSearchArgs;

static void *debounced_search_thread_func(void *arg)
{
    DebouncedSearchArgs *args = (DebouncedSearchArgs *)arg;
    usleep(SEARCH_DEBOUNCE_US);
    pthread_mutex_lock(&debounce_mutex);
    bool still_current = (args->generation == search_generation);
    pthread_mutex_unlock(&debounce_mutex);
    if (still_current)
    {
        focused_entry = args->focused_entry;
        perform_map_search(args->query);
    }
    free(args);
    return NULL;
}

static void perform_map_search_debounced(const char *query, int entry)
{
    if (!query)
        query = "";
    pthread_mutex_lock(&debounce_mutex);
    search_generation++;
    unsigned long my_generation = search_generation;
    snprintf(pending_search_query, sizeof(pending_search_query), "%s", query);
    pending_search_focused_entry = entry;
    pthread_mutex_unlock(&debounce_mutex);
    if (!query[0] || strlen(query) < 2)
    {
        if (map_search_results_list)
        {
            aroma_node_set_hidden(map_search_results_list, true);
            search_results_visible = false;
        }
        return;
    }
    DebouncedSearchArgs *args = malloc(sizeof(DebouncedSearchArgs));
    if (!args)
    {
        focused_entry = entry;
        perform_map_search(query);
        return;
    }
    args->generation = my_generation;
    snprintf(args->query, sizeof(args->query), "%s", query);
    args->focused_entry = entry;
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&thread, &attr, debounced_search_thread_func, args) != 0)
    {
        free(args);
        focused_entry = entry;
        perform_map_search(query);
    }
    pthread_attr_destroy(&attr);
}

static void on_search_result_click(int index, void *user_data)
{
    (void)user_data;
    pthread_mutex_lock(&search_mutex);
    if (index >= 0 && index < map_geocode_result_count)
    {
        const GeocodeResult *result = &map_geocode_results[index];
        char display_name[256];
        truncate_for_listview(result->display_name, display_name, sizeof(display_name));
        if (focused_entry == 1)
        {
            aroma_textbox_set_text(map_to_entry, display_name);
            map_nav.to_lat = result->lat;
            map_nav.to_lon = result->lon;
            strncpy(map_nav.to_text, result->display_name, sizeof(map_nav.to_text) - 1);
            map_nav.to_text[sizeof(map_nav.to_text) - 1] = '\0';
        }
        else
        {
            aroma_textbox_set_text(map_from_entry, display_name);
            map_nav.from_lat = result->lat;
            map_nav.from_lon = result->lon;
            strncpy(map_nav.from_text, result->display_name, sizeof(map_nav.from_text) - 1);
            map_nav.from_text[sizeof(map_nav.from_text) - 1] = '\0';
        }
        if (map_search_results_list)
        {
            aroma_node_set_hidden(map_search_results_list, true);
            search_results_visible = false;
        }
    }
    pthread_mutex_unlock(&search_mutex);
}

static void show_route_panel(void)
{
    if (map_route_sheet)
    {
        aroma_node_set_hidden(map_route_sheet, false);
        aroma_node_set_hidden(map_end_nav_btn, false);
    }
    if (map_search_surface)
    {
        aroma_node_set_hidden(map_search_surface, true);
        map_search_expanded = false;
    }
    if (map_search_results_list)
    {
        aroma_node_set_hidden(map_search_results_list, true);
        search_results_visible = false;
    }
}

static void hide_route_panel(void)
{
    if (map_route_sheet)
    {
        aroma_node_set_hidden(map_route_sheet, true);
    }
    if (map_end_nav_btn)
    {
        aroma_node_set_hidden(map_end_nav_btn, true);
    }
    if (map_search_surface && !map_nav.active)
    {
        aroma_node_set_hidden(map_search_surface, false);
        map_search_expanded = true;
    }
}

static bool recalculate_route_from_current_position(void)
{
    if (!state.map_node)
        return false;

    if (!aroma_map_is_osrm_loaded(state.map_node))
        return false;

    aroma_map_set_route_offline(state.map_node, map_nav.current_lat, map_nav.current_lon,
                                map_nav.to_lat, map_nav.to_lon, GMAPS_COLOR_PRIMARY);

    double *route_lats = NULL;
    double *route_lons = NULL;
    int new_count = aroma_map_get_route_points(state.map_node, &route_lats, &route_lons);
    if (new_count <= 1 || !route_lats || !route_lons)
        return false;

    double *new_path_lat = malloc(sizeof(double) * new_count);
    double *new_path_lon = malloc(sizeof(double) * new_count);
    if (!new_path_lat || !new_path_lon)
    {
        if (new_path_lat) free(new_path_lat);
        if (new_path_lon) free(new_path_lon);
        return false;
    }

    for (int i = 0; i < new_count; i++)
    {
        new_path_lat[i] = nav_mercator_to_lat(route_lats[i]);
        new_path_lon[i] = nav_mercator_to_lon(route_lons[i]);
    }

    if (map_nav.path_lat) free(map_nav.path_lat);
    if (map_nav.path_lon) free(map_nav.path_lon);
    map_nav.path_lat = new_path_lat;
    map_nav.path_lon = new_path_lon;
    map_nav.route_point_count = new_count;
    map_nav.seg_index = 0;
    map_nav.seg_progress_m = 0.0;
    map_nav.seg_length_m = nav_haversine_m(map_nav.path_lat[0], map_nav.path_lon[0],
                                          map_nav.path_lat[1], map_nav.path_lon[1]);
    map_nav.off_route_counter = 0;
    map_nav.reroute_cooldown_frames = RE_ROUTE_COOLDOWN_FRAMES;
    return true;
}

static void update_navigation_display(void)
{
    if (!map_nav.navigation_active || !map_nav.route_ready)
        return;

    RouteProgress progress;
    aroma_map_get_route_progress(state.map_node, &progress);
    TurnInstruction turn;
    aroma_map_get_next_turn(state.map_node, &turn);

    const char *icon_code = AROMA_ICON_ARROW_UPWARD;
    char banner_text[128];
    char banner_sub_text[128];
    char turn_dist_str[32];

    if (progress.distance_to_next_turn < 100 && turn.type != MANEUVER_ARRIVE && turn.type != MANEUVER_NONE)
    {
        if (turn.type == MANEUVER_ROUNDABOUT) {
            strcpy(banner_text, "Roundabout");
            snprintf(banner_sub_text, sizeof(banner_sub_text), "Take exit %d", turn.roundabout_exit);
            icon_code = AROMA_ICON_REFRESH;
        }
        else if (turn.type == MANEUVER_TURN_LEFT) {
            strcpy(banner_text, "Turn left");
            snprintf(banner_sub_text, sizeof(banner_sub_text), "%.0f m", progress.distance_to_next_turn);
            icon_code = AROMA_ICON_ARROW_BACK;
        }
        else if (turn.type == MANEUVER_TURN_RIGHT) {
            strcpy(banner_text, "Turn right");
            snprintf(banner_sub_text, sizeof(banner_sub_text), "%.0f m", progress.distance_to_next_turn);
            icon_code = AROMA_ICON_ARROW_FORWARD;
        }
        else if (turn.type == MANEUVER_UTURN) {
            strcpy(banner_text, "Make U-turn");
            snprintf(banner_sub_text, sizeof(banner_sub_text), "%.0f m", progress.distance_to_next_turn);
            icon_code = AROMA_ICON_REFRESH;
        }
        else {
            strcpy(banner_text, "Continue");
            snprintf(banner_sub_text, sizeof(banner_sub_text), "%.0f m", progress.distance_to_next_turn);
            icon_code = AROMA_ICON_ARROW_UPWARD;
        }
        snprintf(turn_dist_str, sizeof(turn_dist_str), "%.0f m", progress.distance_to_next_turn);
    }
    else if (progress.distance_to_next_turn < 20 || turn.type == MANEUVER_ARRIVE)
    {
        strcpy(banner_text, "Arriving");
        strcpy(banner_sub_text, "At destination");
        strcpy(turn_dist_str, "Now");
        icon_code = AROMA_ICON_PLACE;
    }
    else
    {
        strcpy(banner_text, "Continue straight");
        strcpy(banner_sub_text, "");
        strcpy(turn_dist_str, "--");
        icon_code = AROMA_ICON_ARROW_UPWARD;
    }

    aroma_icon_set_text(nav_turn_icon, icon_code, state.icon_font);
    aroma_label_set_text(nav_banner_label, banner_text);
    aroma_label_set_text(nav_banner_sub, banner_sub_text);

    char eta_str[32], dist_str[32], speed_str[32];
    snprintf(eta_str, sizeof(eta_str), "%.0f min", progress.time_to_destination / 60.0);
    snprintf(dist_str, sizeof(dist_str), "%.1f km", progress.distance_to_destination / 1000.0);
    snprintf(speed_str, sizeof(speed_str), "%.0f km/h", map_nav.speed);
    aroma_label_set_text(nav_eta_label, eta_str);
    aroma_label_set_text(nav_dist_label, dist_str);
    aroma_label_set_text(nav_speed_label, speed_str);
    aroma_label_set_text(nav_turn_dist_label, turn_dist_str);
}

static void start_navigation(double from_lat, double from_lon, double to_lat, double to_lon)
{
    map_nav.active = true;
    map_nav.from_lat = from_lat;
    map_nav.from_lon = from_lon;
    map_nav.to_lat = to_lat;
    map_nav.to_lon = to_lon;
    map_nav.distance_km = calculate_distance_km(from_lat, from_lon, to_lat, to_lon);
    map_nav.eta_minutes = estimate_eta_minutes(map_nav.distance_km);

    bool osrm_loaded = aroma_map_is_osrm_loaded(state.map_node);

    aroma_map_clear_markers(state.map_node);
    aroma_map_clear_route(state.map_node);

    if (osrm_loaded)
    {
        aroma_map_set_route_offline(state.map_node, from_lat, from_lon, to_lat, to_lon, GMAPS_COLOR_PRIMARY);
        double *route_lats = NULL;
        double *route_lons = NULL;
        map_nav.route_point_count = aroma_map_get_route_points(state.map_node, &route_lats, &route_lons);

        if (map_nav.route_point_count > 1)
        {
            map_nav.route_ready = true;
            map_nav.simulation_started = true;
            map_nav.navigation_active = true;
            map_nav.frame = 0;
            map_nav.seg_index = 0;
            map_nav.seg_progress_m = 0.0;
            map_nav.have_heading = false;
            map_nav.speed = 0.0;
            map_nav.current_lat = from_lat;
            map_nav.current_lon = from_lon;
            map_nav.off_route_counter = 0;
            map_nav.reroute_cooldown_frames = 0;

            if (map_nav.path_lat) { free(map_nav.path_lat); map_nav.path_lat = NULL; }
            if (map_nav.path_lon) { free(map_nav.path_lon); map_nav.path_lon = NULL; }

            map_nav.path_lat = malloc(sizeof(double) * map_nav.route_point_count);
            map_nav.path_lon = malloc(sizeof(double) * map_nav.route_point_count);
            for (int i = 0; i < map_nav.route_point_count; i++)
            {
                map_nav.path_lat[i] = nav_mercator_to_lat(route_lats[i]);
                map_nav.path_lon[i] = nav_mercator_to_lon(route_lons[i]);
            }
            map_nav.seg_length_m = nav_haversine_m(map_nav.path_lat[0], map_nav.path_lon[0],
                                                  map_nav.path_lat[1], map_nav.path_lon[1]);

            aroma_map_add_popup_marker(state.map_node, to_lat, to_lon, GMAPS_COLOR_DESTINATION, "Destination");
            aroma_map_add_marker(state.map_node, map_nav.current_lat, map_nav.current_lon, GMAPS_COLOR_PRIMARY);

            if (nav_banner_card)
            {
                aroma_node_set_hidden(nav_banner_card, false);
                aroma_node_set_hidden(nav_bottom_card, false);
            }

            aroma_map_set_center_instant(state.map_node, map_nav.current_lat, map_nav.current_lon);
            aroma_map_set_zoom(state.map_node, 18);

            show_route_panel();
            return;
        }
    }

    aroma_map_set_route(state.map_node, from_lat, from_lon, to_lat, to_lon, GMAPS_COLOR_PRIMARY);
    aroma_map_add_popup_marker(state.map_node, from_lat, from_lon, GMAPS_COLOR_START, "Start");
    aroma_map_add_popup_marker(state.map_node, to_lat, to_lon, GMAPS_COLOR_DESTINATION, "Destination");
    map_nav.route_ready = false;
    map_nav.navigation_active = false;
    map_nav.simulation_started = false;

    aroma_map_set_zoom(state.map_node, 18);

    if (map_route_sheet)
    {
        char dist_str[32], time_str[32];
        format_distance_string(map_nav.distance_km, dist_str, sizeof(dist_str));
        format_time_string(map_nav.eta_minutes, time_str, sizeof(time_str));

        if (map_distance_label)
            aroma_label_set_text(map_distance_label, dist_str);
        if (map_time_label)
            aroma_label_set_text(map_time_label, time_str);
        if (map_route_dest_label)
        {
            char dest_display[288];
            char truncated_dest[40];
            truncate_for_listview(map_nav.to_text, truncated_dest, sizeof(truncated_dest));
            snprintf(dest_display, sizeof(dest_display), "To %s",
                     map_nav.to_text[0] ? truncated_dest : "destination");
            aroma_label_set_text(map_route_dest_label, dest_display);
        }

        show_route_panel();
    }
}

static void clear_navigation(void)
{
    map_nav.active = false;
    map_nav.navigation_active = false;
    map_nav.simulation_started = false;
    map_nav.route_ready = false;
    if (map_nav.path_lat) { free(map_nav.path_lat); map_nav.path_lat = NULL; }
    if (map_nav.path_lon) { free(map_nav.path_lon); map_nav.path_lon = NULL; }
    map_nav.route_point_count = 0;
    aroma_map_clear_route(state.map_node);
    aroma_map_clear_markers(state.map_node);
    hide_route_panel();
    aroma_map_set_center(state.map_node, 36.8625f, 10.1956f);
    aroma_map_set_zoom(state.map_node, 18);

    if (nav_banner_card)
    {
        aroma_node_set_hidden(nav_banner_card, true);
        aroma_node_set_hidden(nav_bottom_card, true);
    }
}

static void on_swap_click(void *user_data)
{
    (void)user_data;
    const char *from_text = aroma_textbox_get_text(map_from_entry);
    const char *to_text = aroma_textbox_get_text(map_to_entry);
    char temp_from[256], temp_to[256];
    strncpy(temp_from, from_text ? from_text : "", sizeof(temp_from) - 1);
    temp_from[sizeof(temp_from) - 1] = '\0';
    strncpy(temp_to, to_text ? to_text : "", sizeof(temp_to) - 1);
    temp_to[sizeof(temp_to) - 1] = '\0';
    aroma_textbox_set_text(map_from_entry, temp_to);
    aroma_textbox_set_text(map_to_entry, temp_from);
    double temp_lat = map_nav.from_lat;
    double temp_lon = map_nav.from_lon;
    char temp_text[256];
    strncpy(temp_text, map_nav.from_text, sizeof(temp_text) - 1);
    temp_text[sizeof(temp_text) - 1] = '\0';
    map_nav.from_lat = map_nav.to_lat;
    map_nav.from_lon = map_nav.to_lon;
    strncpy(map_nav.from_text, map_nav.to_text, sizeof(map_nav.from_text) - 1);
    map_nav.from_text[sizeof(map_nav.from_text) - 1] = '\0';
    map_nav.to_lat = temp_lat;
    map_nav.to_lon = temp_lon;
    strncpy(map_nav.to_text, temp_text, sizeof(map_nav.to_text) - 1);
    map_nav.to_text[sizeof(map_nav.to_text) - 1] = '\0';
    if (map_nav.active)
    {
        start_navigation(map_nav.from_lat, map_nav.from_lon,
                         map_nav.to_lat, map_nav.to_lon);
    }
}

static bool on_go_click(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    const char *from_text = aroma_textbox_get_text(map_from_entry);
    const char *to_text = aroma_textbox_get_text(map_to_entry);
    if (map_nav.from_text[0] == '\0')
    {
        map_nav.from_lat = 36.8625f;
        map_nav.from_lon = 10.1956f;
        snprintf(map_nav.from_text, sizeof(map_nav.from_text), "%s",
                 (from_text && from_text[0]) ? from_text : "Current location");
    }
    if (map_nav.to_text[0] == '\0' || !to_text || !to_text[0])
    {
        if (map_search_placeholder_label)
        {
            aroma_label_set_text(map_search_placeholder_label,
                                 "Pick a destination first");
        }
        return true;
    }
    start_navigation(map_nav.from_lat, map_nav.from_lon,
                     map_nav.to_lat, map_nav.to_lon);
    return true;
}

static void on_end_nav_click(void *user_data)
{
    (void)user_data;
    clear_navigation();
}

static void on_search_pill_click(void *user_data)
{
    (void)user_data;
    map_search_expanded = !map_search_expanded;
    if (map_search_surface)
    {
        aroma_node_set_hidden(map_search_surface, !map_search_expanded);
    }
}

static void on_search_back_click(void *user_data)
{
    (void)user_data;
    map_search_expanded = false;
    if (map_search_surface)
    {
        aroma_node_set_hidden(map_search_surface, true);
    }
    if (map_search_results_list)
    {
        aroma_node_set_hidden(map_search_results_list, true);
        search_results_visible = false;
    }
}

static void on_accept_call_click(void *user_data)
{
    (void)user_data;
    char call_path_copy[256] = "";
    pthread_mutex_lock(&call_state_lock);
    strncpy(call_path_copy, current_call_path, sizeof(call_path_copy) - 1);
    call_path_copy[sizeof(call_path_copy) - 1] = '\0';
    pthread_mutex_unlock(&call_state_lock);
    
    if (call_path_copy[0] != '\0')
    {
        bt_hfp_answer(call_path_copy);
    }
    
    if (incoming_call_overlay)
    {
        aroma_node_set_hidden(incoming_call_accept_btn, true);
        aroma_node_set_hidden(incoming_call_reject_btn, true);
        aroma_node_set_hidden(incoming_call_end_btn, false);
        if (incoming_call_name_label)
        {
            char display[256];
            snprintf(display, sizeof(display), "Active Call: %s", current_call_name);
            aroma_label_set_text(incoming_call_name_label, display);
        }
    }
    
    pthread_mutex_lock(&call_state_lock);
    current_call_path[0] = '\0';
    pthread_mutex_unlock(&call_state_lock);
}

static void on_reject_call_click(void *user_data)
{
    (void)user_data;
    char call_path_copy[256] = "";
    pthread_mutex_lock(&call_state_lock);
    strncpy(call_path_copy, current_call_path, sizeof(call_path_copy) - 1);
    call_path_copy[sizeof(call_path_copy) - 1] = '\0';
    pthread_mutex_unlock(&call_state_lock);
    
    if (call_path_copy[0] != '\0')
    {
        bt_hfp_hangup(call_path_copy);
    }
    
    if (incoming_call_overlay)
    {
        aroma_node_set_hidden(incoming_call_overlay, true);
    }
    
    pthread_mutex_lock(&call_state_lock);
    call_overlay_visible = false;
    current_call_path[0] = '\0';
    pthread_mutex_unlock(&call_state_lock);
}

static void on_end_call_click(void *user_data)
{
    (void)user_data;
    bt_hfp_hangup_all();
    
    if (incoming_call_overlay)
    {
        aroma_node_set_hidden(incoming_call_overlay, true);
    }
    
    pthread_mutex_lock(&call_state_lock);
    call_overlay_visible = false;
    current_call_path[0] = '\0';
    pthread_mutex_unlock(&call_state_lock);
}

static void on_floating_dialer_click(void *user_data)
{
    (void)user_data;
    if (state.phone_app_tabs)
    {
        aroma_tabs_set_selected(state.phone_app_tabs, 1);
    }
}

void show_incoming_call_screen(const char *name, const char *number, const char *call_path)
{
    if (!incoming_call_overlay)
        return;
    
    pthread_mutex_lock(&call_state_lock);
    safe_str_copy(current_call_name, name ? name : "Unknown", sizeof(current_call_name));
    safe_str_copy(current_call_number, number ? number : "", sizeof(current_call_number));
    safe_str_copy(current_call_path, call_path ? call_path : "", sizeof(current_call_path));
    call_overlay_visible = true;
    pthread_mutex_unlock(&call_state_lock);
    
    if (incoming_call_name_label)
    {
        char display[256];
        snprintf(display, sizeof(display), "Incoming Call: %s", current_call_name);
        aroma_label_set_text(incoming_call_name_label, display);
    }
    if (incoming_call_number_label)
    {
        aroma_label_set_text(incoming_call_number_label, current_call_number);
    }
    aroma_node_set_hidden(incoming_call_accept_btn, false);
    aroma_node_set_hidden(incoming_call_reject_btn, false);
    aroma_node_set_hidden(incoming_call_end_btn, true);
    aroma_node_set_hidden(incoming_call_overlay, false);
}

static void *call_monitor_thread_func(void *arg)
{
    (void)arg;
    usleep(5000000);
    bt_call_info_t prev_calls[10];
    int prev_count = 0;
    while (1)
    {
        usleep(1000000);
        bt_call_info_t curr_calls[10];
        int curr_count = bt_hfp_get_active_calls(curr_calls, 10);
        
        bool found_incoming = false;
        for (int i = 0; i < curr_count; i++)
        {
            bool is_new = true;
            for (int j = 0; j < prev_count; j++)
            {
                if (strcmp(curr_calls[i].path, prev_calls[j].path) == 0)
                {
                    is_new = false;
                    break;
                }
            }
            if (is_new && curr_calls[i].state == BT_CALL_STATE_INCOMING)
            {
                show_incoming_call_screen(curr_calls[i].name,
                                          curr_calls[i].line_id,
                                          curr_calls[i].path);
                found_incoming = true;
                break;
            }
        }
        
        if (!found_incoming)
        {
            bool should_hide = false;
            pthread_mutex_lock(&call_state_lock);
            if (curr_count == 0 && call_overlay_visible)
            {
                should_hide = true;
                call_overlay_visible = false;
            }
            pthread_mutex_unlock(&call_state_lock);
            
            if (should_hide && incoming_call_overlay)
            {
                aroma_node_set_hidden(incoming_call_overlay, true);
            }
        }
        
        memcpy(prev_calls, curr_calls, sizeof(curr_calls));
        prev_count = curr_count;
    }
    return NULL;
}

static bool on_dialer_delete_click_icon(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    size_t len = strlen(dialer_number);
    if (len > 0)
    {
        dialer_number[len - 1] = '\0';
        if (dialer_display_label)
        {
            aroma_label_set_text(dialer_display_label, dialer_number[0] ? dialer_number : "Enter number");
        }
    }
    return true;
}

static bool on_dialer_call_click_icon(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    if (dialer_number[0] != '\0')
    {
        pthread_mutex_lock(&g_bt_mutex);
        bool connected = g_bt_connected;
        pthread_mutex_unlock(&g_bt_mutex);
        if (connected)
        {
            bt_hfp_dial(dialer_number);
        }
        dialer_number[0] = '\0';
        if (dialer_display_label)
        {
            aroma_label_set_text(dialer_display_label, "Enter number");
        }
    }
    return true;
}

static bool on_dialer_button_click(AromaNode *node, void *user_data)
{
    (void)node;
    const char *digit = (const char *)user_data;
    if (!digit || strlen(dialer_number) >= MAX_DIALER_DIGITS - 1)
        return true;
    strcat(dialer_number, digit);
    if (dialer_display_label)
    {
        aroma_label_set_text(dialer_display_label, dialer_number);
    }
    return true;
}

static void on_tab_changed(AromaNode *tabs, int tab_index, void *user_data)
{
    (void)tabs;
    (void)user_data;
    if (pagination_card)
    {
        aroma_node_set_hidden(pagination_card, tab_index != 0);
    }
    if (dialer_card)
    {
        aroma_node_set_hidden(dialer_card, tab_index != 1);
    }
    if (state.contact_listview)
    {
        aroma_node_set_hidden(state.contact_listview, tab_index != 0);
    }
    if (tab_index == 0)
    {
        contact_page = 0;
        populate_contact_listview(state.contact_listview);
    }
}

static void restore_home_rect(AromaNode *node, const LocalOffset *offset)
{
    if (!node || !offset || !state.bottom_bar)
        return;
    AromaRect *bar_rect = aroma_node_get_rect(state.bottom_bar);
    AromaRect *node_rect = aroma_node_get_rect(node);
    if (!bar_rect || !node_rect)
        return;
    node_rect->x = bar_rect->x + offset->x;
    node_rect->y = bar_rect->y + offset->y;
    node_rect->width = offset->w;
    node_rect->height = offset->h;
}

static void restore_icon_and_card(AromaNode *icon, AromaNode *card, const LocalOffset *offset)
{
    restore_home_rect(icon, offset);
    restore_home_rect(card, offset);
}

static int compare_contacts(const void *a, const void *b)
{
    const ContactInfo *ca = (const ContactInfo *)a;
    const ContactInfo *cb = (const ContactInfo *)b;
    char name_a[128], name_b[128];
    safe_str_copy(name_a, ca->name, sizeof(name_a));
    safe_str_copy(name_b, cb->name, sizeof(name_b));
    if (name_a[0] == '\0')
    {
        safe_str_copy(name_a, ca->number, sizeof(name_a));
    }
    if (name_b[0] == '\0')
    {
        safe_str_copy(name_b, cb->number, sizeof(name_b));
    }
    return strcasecmp(name_a, name_b);
}

static char get_first_letter(const char *str)
{
    if (!str || !str[0])
        return '#';
    char c = toupper(str[0]);
    if (c >= 'A' && c <= 'Z')
        return c;
    return '#';
}

void populate_contact_listview(AromaNode *listview)
{
    if (!listview)
        return;
    pthread_mutex_lock(&contact_list_lock);
    aroma_listview_clear(listview);
    if (!state.contacts_fetched)
    {
        aroma_listview_add_item_with_icon(listview, "Connecting to phone...", "Make sure Bluetooth is enabled in settings.", AROMA_ICON_BLUETOOTH_DISABLED, NULL);
        if (pagination_card)
            aroma_node_set_hidden(pagination_card, true);
        pthread_mutex_unlock(&contact_list_lock);
        return;
    }
    if (state.contact_count == 0)
    {
        aroma_listview_add_item_with_icon(listview, "No contacts found", "Connect a phone with PBAP", AROMA_ICON_PERSON, NULL);
        if (pagination_card)
            aroma_node_set_hidden(pagination_card, true);
        pthread_mutex_unlock(&contact_list_lock);
        return;
    }
    ContactInfo *sorted_contacts = malloc(sizeof(ContactInfo) * state.contact_count);
    if (!sorted_contacts)
    {
        aroma_listview_add_item_with_icon(listview, "Memory error", "", AROMA_ICON_PERSON, NULL);
        pthread_mutex_unlock(&contact_list_lock);
        return;
    }
    int *orig_indices = malloc(sizeof(int) * state.contact_count);
    for (int i = 0; i < state.contact_count; i++)
    {
        orig_indices[i] = i;
    }
    memcpy(sorted_contacts, state.contacts, sizeof(ContactInfo) * state.contact_count);
    for (int i = 0; i < state.contact_count - 1; i++)
    {
        for (int j = i + 1; j < state.contact_count; j++)
        {
            if (compare_contacts(&sorted_contacts[i], &sorted_contacts[j]) > 0)
            {
                ContactInfo temp = sorted_contacts[i];
                sorted_contacts[i] = sorted_contacts[j];
                sorted_contacts[j] = temp;
                int tempidx = orig_indices[i];
                orig_indices[i] = orig_indices[j];
                orig_indices[j] = tempidx;
            }
        }
    }
    total_pages = (state.contact_count + CONTACTS_PER_PAGE - 1) / CONTACTS_PER_PAGE;
    if (contact_page >= total_pages)
        contact_page = total_pages - 1;
    if (contact_page < 0)
        contact_page = 0;
    int start_idx = contact_page * CONTACTS_PER_PAGE;
    int end_idx = start_idx + CONTACTS_PER_PAGE;
    if (end_idx > state.contact_count)
        end_idx = state.contact_count;
    char current_header = 0;
    for (int i = start_idx; i < end_idx; i++)
    {
        char display_name[256];
        char display_number[64];
        sorted_to_original[i] = orig_indices[i];
        const char *name = sorted_contacts[i].name;
        const char *number = sorted_contacts[i].number;
        char letter = get_first_letter(name[0] ? name : number);
        if (letter != current_header)
        {
            current_header = letter;
            char header_text[4] = {letter, '\0'};
            aroma_listview_add_header(listview, header_text);
        }
        if (name[0] == '\0')
        {
            snprintf(display_name, sizeof(display_name), "%s", number);
            display_number[0] = '\0';
        }
        else
        {
            snprintf(display_name, sizeof(display_name), "%s", name);
            snprintf(display_number, sizeof(display_number), "%s", number);
        }
        aroma_listview_add_item_with_icon(listview, display_name, display_number, AROMA_ICON_PERSON, NULL);
    }
    free(sorted_contacts);
    free(orig_indices);
    if (prev_page_btn)
    {
        aroma_node_set_hidden(prev_page_btn, contact_page == 0);
    }
    if (next_page_btn)
    {
        aroma_node_set_hidden(next_page_btn, contact_page >= total_pages - 1);
    }
    if (page_label)
    {
        char label_text[32];
        snprintf(label_text, sizeof(label_text), "%d/%d", contact_page + 1, total_pages);
        aroma_label_set_text(page_label, label_text);
    }
    if (pagination_card)
    {
        aroma_node_set_hidden(pagination_card, total_pages <= 1);
    }
    pthread_mutex_unlock(&contact_list_lock);
}

static void on_prev_page_click(void *user_data)
{
    (void)user_data;
    if (contact_page > 0)
    {
        contact_page--;
        populate_contact_listview(state.contact_listview);
    }
}

static void on_next_page_click(void *user_data)
{
    (void)user_data;
    if (contact_page < total_pages - 1)
    {
        contact_page++;
        populate_contact_listview(state.contact_listview);
    }
}

static void update_play_pause_button_icon(void)
{
    if (!media_ui.media_play_pause_button)
        return;
    if (media_ui.is_playing)
    {
        aroma_iconbutton_set_icon(media_ui.media_play_pause_button, AROMA_ICON_PAUSE);
    }
    else
    {
        aroma_iconbutton_set_icon(media_ui.media_play_pause_button, AROMA_ICON_PLAY_ARROW);
    }
}

static void on_media_prev_click(void *user_data)
{
    (void)user_data;
    bt_speaker_avrcp_previous();
}

static void on_media_play_pause_click(void *user_data)
{
    (void)user_data;
    if (media_ui.is_playing)
    {
        bt_speaker_avrcp_pause();
        media_ui.is_playing = false;
    }
    else
    {
        bt_speaker_avrcp_play();
        media_ui.is_playing = true;
    }
    update_play_pause_button_icon();
}

static void on_media_next_click(void *user_data)
{
    (void)user_data;
    bt_speaker_avrcp_next();
}

static void on_music_icon_click(void *user_data)
{
    (void)user_data;
    if (app_drawer_visible)
    {
        send_app_drawer_behind();
    }
    open_music(NULL, app_definitions[2].app_root);
}

static void apply_deferred_bottom_bar_position(void)
{
    if (!state.bottom_bar)
        return;
    AromaRect *rect = aroma_node_get_rect(state.bottom_bar);
    if (!rect)
        return;
    int target_y = media_ui.bottom_bar_expanded ? 20 : 20;
    if (rect->y == target_y)
        return;
    AromaAnimation *anim = aroma_animation_start(state.bottom_bar, AROMA_ANIM_SLIDE_Y, rect->y, target_y, 1200);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
}

static void set_bottom_bar_expanded(bool expanded)
{
    if (!state.bottom_bar)
        return;
    if (media_ui.bottom_bar_expanded == expanded)
        return;
    pthread_mutex_lock(&app_open_lock);
    bool app_open = bottom_bar_app_open;
    pthread_mutex_unlock(&app_open_lock);
    if (app_open)
    {
        media_ui.bottom_bar_expanded = expanded;
        return;
    }
    int from_y = media_ui.bottom_bar_expanded ? 20 : 20;
    int to_y = expanded ? 20 : 20;
    AromaAnimation *anim = aroma_animation_start(state.bottom_bar, AROMA_ANIM_SLIDE_Y, from_y, to_y, 1200);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
    media_ui.bottom_bar_expanded = expanded;
}

static bool is_any_app_open(void)
{
    pthread_mutex_lock(&app_open_lock);
    bool result = bottom_bar_app_open;
    pthread_mutex_unlock(&app_open_lock);
    return result;
}

static void set_app_open(bool open)
{
    pthread_mutex_lock(&app_open_lock);
    bottom_bar_app_open = open;
    pthread_mutex_unlock(&app_open_lock);
}

void update_media_card_display(void)
{
    if (!media_ui.ui_initialized || !media_ui.media_card)
        return;
    
    pthread_mutex_lock(&g_bt_mutex);
    bt_media_info_t media = g_bt_media_info;
    bt_state_t current_state = g_bt_state;
    pthread_mutex_unlock(&g_bt_mutex);
    
    bool is_playing = (current_state == BT_STATE_PLAYING);
    bool is_connected = (current_state == BT_STATE_CONNECTED || is_playing);
    bool has_media = (media.title[0] != '\0' || media.artist[0] != '\0');
    
    if (!is_connected || !has_media)
    {
        aroma_node_set_hidden(media_ui.media_card, true);
        media_ui.first_media_check_done = false;
        set_bottom_bar_expanded(false);
        return;
    }
    if (!media_ui.first_media_check_done)
    {
        media_ui.first_media_check_done = true;
        set_bottom_bar_expanded(true);
    }
    if (!is_any_app_open())
    {
        aroma_node_set_hidden(media_ui.media_card, false);
    }
    else
    {
        aroma_node_set_hidden(media_ui.media_card, true);
    }
    if (strcmp(media.status, "playing") == 0)
    {
        if (!media_ui.is_playing)
        {
            media_ui.is_playing = true;
            update_play_pause_button_icon();
        }
    }
    else if (strcmp(media.status, "paused") == 0)
    {
        if (media_ui.is_playing)
        {
            media_ui.is_playing = false;
            update_play_pause_button_icon();
        }
    }
    if (media_ui.media_title_label && media.title[0])
        aroma_label_set_text(media_ui.media_title_label, media.title);
    if (media_ui.media_artist_label)
    {
        if (media.artist[0])
            aroma_label_set_text(media_ui.media_artist_label, media.artist);
        else
            aroma_label_set_text(media_ui.media_artist_label, "Unknown Artist");
    }
}

static void *media_monitor_thread_func(void *arg)
{
    (void)arg;
    usleep(3000000);
    while (media_ui.ui_initialized)
    {
        update_media_card_display();
        update_bt_info_card();
        if (music_app_open)
        {
            if (music_active_tab == 0)
                update_music_now_playing_display();
            else
                update_music_device_display();
        }
        usleep(MEDIA_UPDATE_INTERVAL_US);
    }
    return NULL;
}

static void ac_temp_up_callback(void *user_data)
{
    (void)user_data;
    if (state.current_ac_temp < 30)
        state.current_ac_temp++;
    char buf[16];
    snprintf(buf, sizeof(buf), "%dC", state.current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
}

static void ac_temp_down_callback(void *user_data)
{
    (void)user_data;
    if (state.current_ac_temp > 16)
        state.current_ac_temp--;
    char buf[16];
    snprintf(buf, sizeof(buf), "%dC", state.current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
}

static bool car_frontdoor_open(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    aroma_image_set_source(state.overlay,
#ifdef __EMSCRIPTEN__
                           "/assets/car_frontdoor.png"
#elif defined(__arm__) || defined(__aarch64__)
                           "/usr/share/infotainment/assets/car_frontdoor.png"
#else
                           "../assets/car_frontdoor.png"
#endif
    );
    return true;
}

static void on_contact_click(int index, void *user_data)
{
    (void)user_data;
    int start_idx = contact_page * CONTACTS_PER_PAGE;
    int actual_index = start_idx + index;
    if (actual_index >= 0 && actual_index < state.contact_count)
    {
        int orig_idx = sorted_to_original[actual_index];
        if (orig_idx >= 0 && orig_idx < state.contact_count)
        {
            char number[64];
            safe_str_copy(number, state.contacts[orig_idx].number, sizeof(number));
            if (number[0] != '\0')
            {
                pthread_mutex_lock(&g_bt_mutex);
                bool connected = g_bt_connected;
                pthread_mutex_unlock(&g_bt_mutex);
                if (connected)
                {
                    bt_hfp_dial(number);
                }
            }
        }
    }
}

void opening_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *maps_rect = aroma_node_get_rect(state.map_node);
    if (!maps_rect)
        return;
    
    int start_y = WIN_H;
    int end_y = 0;
    
    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;
    
    maps_rect->x = rect->x;
    maps_rect->y = rect->y;
    maps_rect->width = rect->width;
    maps_rect->height = rect->height;

    if (progress >= 0.92f)
    {
        aroma_node_set_hidden(state.map_close_btn, false);
        aroma_node_set_hidden(map_search_surface, false);
    }
    aroma_node_invalidate(state.map_node);
    aroma_node_invalidate(target);
}

bool open_maps(AromaNode *node, void *user_data)
{
    (void)node;
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return false;

    if (app_drawer_visible)
    {
        send_app_drawer_behind();
    }

    map_geocode_result_count = 0;
    last_search_query[0] = '\0';
    search_results_visible = false;
    focused_entry = 0;
    if (!map_nav.active)
    {
        memset(&map_nav, 0, sizeof(map_nav));
        if (map_from_entry)
            aroma_textbox_set_text(map_from_entry, "");
        if (map_to_entry)
            aroma_textbox_set_text(map_to_entry, "");
    }
    aroma_node_set_hidden(card_node, false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 300, opening_anim, NULL);
    if (!anim)
        return false;
    set_app_open(true);
    maps_screen_open = true;
    poi_refresh_forced = true;
    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
    aroma_node_set_hidden(state.map_node, false);
    aroma_node_set_hidden(state.map_close_btn, false);
    aroma_node_set_z_index(card_node, Z_LAYER_STATUS_BAR + 10);
    aroma_map_set_zoom(state.map_node, 18);
    if (map_search_surface)
    {
        map_search_expanded = true;
        aroma_node_set_hidden(map_search_surface, false);
        if (map_search_placeholder_label)
        {
            aroma_label_set_text(map_search_placeholder_label, "Search for a location");
        }
    }
    
    return true;
}

void closing_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *maps_rect = aroma_node_get_rect(state.map_node);
    if (!maps_rect)
        return;
    
    int start_y = 0;
    int end_y = WIN_H;
    
    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;
    
    maps_rect->x = rect->x;
    maps_rect->y = rect->y;
    maps_rect->width = rect->width;
    maps_rect->height = rect->height;
    
    if (progress >= 0.92f)
    {
        aroma_node_set_z_index(target, 1);
        aroma_node_set_hidden(state.map_close_btn, true);
        aroma_node_set_hidden(map_search_surface, true);
        aroma_node_set_hidden(map_route_sheet, true);
        aroma_node_set_hidden(map_end_nav_btn, true);
        aroma_node_set_hidden(map_options_card, true);
        aroma_node_set_hidden(state.map_node, true);
        aroma_node_set_hidden(target, true);
        maps_screen_open = false;
        if (map_search_results_list)
        {
            aroma_node_set_hidden(map_search_results_list, true);
            search_results_visible = false;
        }
        if (suggestion_page)
        {
            aroma_node_set_hidden(suggestion_page, true);
        }
        if (nav_banner_card)
        {
            aroma_node_set_hidden(nav_banner_card, true);
            aroma_node_set_hidden(nav_bottom_card, true);
        }
    }
    if (progress >= 1.0f)
    {
        set_app_open(false);
        apply_deferred_bottom_bar_position();
        update_media_card_display();
        map_search_expanded = false;
        map_options_visible = false;
        restore_app_drawer_from_behind();

        if (!map_nav.active)
        {
            clear_navigation();
        }
    }
    aroma_node_invalidate(state.map_node);
    aroma_node_invalidate(target);
}

void close_maps(void *user_data)
{
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return;
    /* Clear eagerly at close-initiation rather than relying solely on
     * closing_anim's progress>=1.0f branch (~30 lines below) — if that
     * animation callback is ever interrupted or never reaches exactly
     * 1.0 (e.g. a new open is requested mid-close), bottom_bar_app_open
     * was left stuck true, which silently blocks every other app's open
     * button via is_any_app_open(). Harmless to also clear it again once
     * the animation completes. */
    set_app_open(false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 300, closing_anim, NULL);
    aroma_node_set_hidden(map_search_surface, true);
    aroma_animation_set_easing(anim, AROMA_EASE_IN_OUT_QUAD);
}

void phone_opening_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *phone_rect = aroma_node_get_rect(state.phone_node);
    if (!phone_rect)
        return;
    AromaRect *phone_tabs_rect = aroma_node_get_rect(state.phone_app_tabs);
    if (!phone_tabs_rect)
        return;
    
    int start_y = WIN_H;
    int end_y = 0;
    
    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;
    
    phone_rect->x = rect->x;
    phone_rect->y = rect->y;
    phone_rect->width = rect->width;
    phone_rect->height = rect->height;
    phone_tabs_rect->x = rect->x;
    phone_tabs_rect->y = rect->y;
    phone_tabs_rect->width = rect->width;
    phone_tabs_rect->height = 100;
    aroma_node_invalidate(state.phone_node);
    aroma_node_invalidate(target);
}

static bool open_phone(AromaNode *node, void *user_data)
{
    (void)node;
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return false;
    if (app_drawer_visible)
    {
        send_app_drawer_behind();
    }

    aroma_node_set_hidden(card_node, false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 300, phone_opening_anim, NULL);
    if (!anim)
        return false;
    set_app_open(true);
    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
    aroma_node_set_hidden(state.phone_node, false);
    aroma_node_set_hidden(state.phone_close_btn, false);
    aroma_node_set_hidden(state.phone_app_tabs, false);
    aroma_node_set_hidden(state.contact_listview, false);
    if (pagination_card)
        aroma_node_set_hidden(pagination_card, false);
    if (dialer_card)
        aroma_node_set_hidden(dialer_card, true);
    aroma_node_set_z_index(card_node, Z_LAYER_STATUS_BAR + 10);
    contact_page = 0;
    populate_contact_listview(state.contact_listview);
    
    return true;
}

void phone_closing_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *phone_rect = aroma_node_get_rect(state.phone_node);
    if (!phone_rect)
        return;
    AromaRect *phone_tabs_rect = aroma_node_get_rect(state.phone_app_tabs);
    if (!phone_tabs_rect)
        return;
    
    int start_y = 0;
    int end_y = WIN_H;
    
    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;
    
    phone_rect->x = rect->x;
    phone_rect->y = rect->y;
    phone_rect->width = rect->width;
    phone_rect->height = rect->height;
    phone_tabs_rect->x = rect->x;
    phone_tabs_rect->y = rect->y + 90;
    phone_tabs_rect->width = rect->width;
    phone_tabs_rect->height = 100;
    if (progress >= 0.92f)
    {
        aroma_node_set_z_index(target, 1);
        aroma_node_set_hidden(state.phone_close_btn, true);
        aroma_node_set_hidden(state.phone_node, true);
        aroma_node_set_hidden(target, true);
    }
    if (progress >= 1.0f)
    {
        set_app_open(false);
        apply_deferred_bottom_bar_position();
        update_media_card_display();
        restore_app_drawer_from_behind();
    }
    aroma_node_invalidate(state.phone_node);
    aroma_node_invalidate(target);
}

void close_phone(void *user_data)
{
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return;
    /* See close_maps for why this is cleared here, not only inside
     * phone_closing_anim's progress>=1.0f branch. */
    set_app_open(false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 300, phone_closing_anim, NULL);
    aroma_node_set_hidden(state.phone_app_tabs, true);
    aroma_node_set_hidden(state.contact_listview, true);
    if (pagination_card)
        aroma_node_set_hidden(pagination_card, true);
    if (dialer_card)
        aroma_node_set_hidden(dialer_card, true);
    aroma_animation_set_easing(anim, AROMA_EASE_IN_OUT_QUAD);
}

static void update_music_play_pause_icon(void)
{
    if (!music_play_pause_button)
        return;
    aroma_iconbutton_set_icon(music_play_pause_button,
                              media_ui.is_playing ? AROMA_ICON_PAUSE : AROMA_ICON_PLAY_ARROW);
}

static void on_music_prev_click(void *user_data)
{
    (void)user_data;
    bt_speaker_avrcp_previous();
}

static void on_music_play_pause_click(void *user_data)
{
    (void)user_data;
    if (media_ui.is_playing)
    {
        bt_speaker_avrcp_pause();
        media_ui.is_playing = false;
    }
    else
    {
        bt_speaker_avrcp_play();
        media_ui.is_playing = true;
    }
    update_play_pause_button_icon();
    update_music_play_pause_icon();
}

static void on_music_next_click(void *user_data)
{
    (void)user_data;
    bt_speaker_avrcp_next();
}

static void update_music_now_playing_display(void)
{
    if (!music_now_playing_card)
        return;
    
    pthread_mutex_lock(&g_bt_mutex);
    bt_media_info_t media = g_bt_media_info;
    bt_state_t current_state = g_bt_state;
    pthread_mutex_unlock(&g_bt_mutex);
    
    bool is_playing = (current_state == BT_STATE_PLAYING);
    bool is_connected = (current_state == BT_STATE_CONNECTED || is_playing);
    bool has_media = (media.title[0] != '\0' || media.artist[0] != '\0');

    if (!is_connected || !has_media)
    {
        if (music_no_media_label)
            aroma_node_set_hidden(music_no_media_label, false);
        if (music_art_placeholder)
            aroma_node_set_hidden(music_art_placeholder, true);
        if (music_track_title_label)
            aroma_node_set_hidden(music_track_title_label, true);
        if (music_track_artist_label)
            aroma_node_set_hidden(music_track_artist_label, true);
        if (music_track_album_label)
            aroma_node_set_hidden(music_track_album_label, true);
        if (music_status_label)
            aroma_node_set_hidden(music_status_label, true);
        if (music_prev_button)
            aroma_node_set_hidden(music_prev_button, true);
        if (music_play_pause_button)
            aroma_node_set_hidden(music_play_pause_button, true);
        if (music_next_button)
            aroma_node_set_hidden(music_next_button, true);
        return;
    }

    if (music_no_media_label)
        aroma_node_set_hidden(music_no_media_label, true);
    if (music_art_placeholder)
        aroma_node_set_hidden(music_art_placeholder, false);
    if (music_prev_button)
        aroma_node_set_hidden(music_prev_button, false);
    if (music_play_pause_button)
        aroma_node_set_hidden(music_play_pause_button, false);
    if (music_next_button)
        aroma_node_set_hidden(music_next_button, false);

    if (music_track_title_label)
    {
        aroma_node_set_hidden(music_track_title_label, false);
        aroma_label_set_text(music_track_title_label, media.title[0] ? media.title : "Unknown Track");
    }
    if (music_track_artist_label)
    {
        aroma_node_set_hidden(music_track_artist_label, false);
        aroma_label_set_text(music_track_artist_label, media.artist[0] ? media.artist : "Unknown Artist");
    }
    if (music_track_album_label)
    {
        if (media.album[0])
        {
            aroma_node_set_hidden(music_track_album_label, false);
            aroma_label_set_text(music_track_album_label, media.album);
        }
        else
        {
            aroma_node_set_hidden(music_track_album_label, true);
        }
    }
    if (music_status_label)
    {
        aroma_node_set_hidden(music_status_label, false);
        if (strcmp(media.status, "playing") == 0)
        {
            aroma_label_set_text(music_status_label, "Playing");
            aroma_label_set_color(music_status_label, 0xFF4CAF50);
        }
        else if (strcmp(media.status, "paused") == 0)
        {
            aroma_label_set_text(music_status_label, "Paused");
            aroma_label_set_color(music_status_label, 0xFFFF9800);
        }
        else
        {
            aroma_label_set_text(music_status_label, "Connected");
            aroma_label_set_color(music_status_label, 0xFF9E9E9E);
        }
    }

    update_music_play_pause_icon();
}

static void update_music_device_display(void)
{
    if (!music_device_card)
        return;
    
    pthread_mutex_lock(&g_bt_mutex);
    bt_device_info_t device = g_bt_device_info;
    bt_state_t current_state = g_bt_state;
    pthread_mutex_unlock(&g_bt_mutex);
    
    bool is_connected = (current_state == BT_STATE_CONNECTED ||
                         current_state == BT_STATE_PLAYING);

    if (!is_connected || !device.connected || !device.name[0])
    {
        if (music_device_no_phone_label)
            aroma_node_set_hidden(music_device_no_phone_label, false);
        if (music_device_name_label)
            aroma_node_set_hidden(music_device_name_label, true);
        if (music_device_address_label)
            aroma_node_set_hidden(music_device_address_label, true);
        if (music_device_stats_label)
            aroma_node_set_hidden(music_device_stats_label, true);
        if (music_device_status_label)
            aroma_label_set_text(music_device_status_label, "No Phone Connected");
        if (music_device_status_icon)
            aroma_icon_set_color(music_device_status_icon, 0xFF9E9E9E);
        return;
    }

    if (music_device_no_phone_label)
        aroma_node_set_hidden(music_device_no_phone_label, true);

    if (music_device_status_label)
        aroma_label_set_text(music_device_status_label,
                             current_state == BT_STATE_PLAYING ? "Connected - Playing" : "Connected");
    if (music_device_status_icon)
        aroma_icon_set_color(music_device_status_icon, 0xFF4CAF50);

    if (music_device_name_label)
    {
        char name_buf[128];
        snprintf(name_buf, sizeof(name_buf), "Name: %s", device.name);
        aroma_node_set_hidden(music_device_name_label, false);
        aroma_label_set_text(music_device_name_label, name_buf);
    }
    if (music_device_address_label)
    {
        char address_buf[128];
        snprintf(address_buf, sizeof(address_buf), "Address: %s", device.address[0] ? device.address : "Unknown");
        aroma_node_set_hidden(music_device_address_label, false);
        aroma_label_set_text(music_device_address_label, address_buf);
    }
    if (music_device_stats_label)
    {
        pthread_mutex_lock(&g_bt_mutex);
        bt_stats_t stats = g_bt_stats;
        pthread_mutex_unlock(&g_bt_mutex);
        char stats_buf[160];
        unsigned long minutes = stats.connected_time_sec / 60;
        unsigned long seconds = stats.connected_time_sec % 60;
        if (stats.audio_active && stats.audio_time_sec > 0)
        {
            unsigned long audio_min = stats.audio_time_sec / 60;
            unsigned long audio_sec = stats.audio_time_sec % 60;
            snprintf(stats_buf, sizeof(stats_buf),
                     "Connected: %lumin %lus | Audio: %lumin %lus",
                     minutes, seconds, audio_min, audio_sec);
        }
        else
        {
            snprintf(stats_buf, sizeof(stats_buf), "Connected: %lumin %lus", minutes, seconds);
        }
        aroma_node_set_hidden(music_device_stats_label, false);
        aroma_label_set_text(music_device_stats_label, stats_buf);
    }
}

static void on_music_tab_changed(AromaNode *tabs, int tab_index, void *user_data)
{
    (void)tabs;
    (void)user_data;
    music_active_tab = tab_index;
    if (music_now_playing_card)
        aroma_node_set_hidden(music_now_playing_card, tab_index != 0);
    if (music_device_card)
        aroma_node_set_hidden(music_device_card, tab_index != 1);
    if (tab_index == 0)
        update_music_now_playing_display();
    else
        update_music_device_display();
}

void music_opening_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *tabs_rect = aroma_node_get_rect(music_app_tabs);
    if (!tabs_rect)
        return;
    
    int start_y = WIN_H;
    int end_y = 0;
    
    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;
    
    tabs_rect->x = rect->x;
    tabs_rect->y = rect->y;
    tabs_rect->width = rect->width;
    tabs_rect->height = 100;
    aroma_node_invalidate(target);
}

static bool open_music(AromaNode *node, void *user_data)
{
    (void)node;
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return false;
    if (app_drawer_visible)
    {
        send_app_drawer_behind();
    }

    aroma_node_set_hidden(card_node, false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 300, music_opening_anim, NULL);
    if (!anim)
        return false;
    set_app_open(true);
    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
    aroma_node_set_hidden(music_app_tabs, false);
    aroma_node_set_hidden(music_now_playing_card, false);
    aroma_node_set_hidden(music_device_card, true);
    if (music_app_tabs)
        aroma_tabs_set_selected(music_app_tabs, 0);
    aroma_node_set_z_index(card_node, Z_LAYER_STATUS_BAR + 10);
    music_app_open = true;
    music_active_tab = 0;
    update_music_now_playing_display();
    
    return true;
}

void music_closing_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    AromaRect *tabs_rect = aroma_node_get_rect(music_app_tabs);
    if (!tabs_rect)
        return;
    
    int start_y = 0;
    int end_y = WIN_H;
    
    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;
    
    tabs_rect->x = rect->x;
    tabs_rect->y = rect->y + 90;
    tabs_rect->width = rect->width;
    tabs_rect->height = 100;
    if (progress >= 0.92f)
    {
        aroma_node_set_z_index(target, 1);
        aroma_node_set_hidden(music_app_tabs, true);
        aroma_node_set_hidden(target, true);
    }
    if (progress >= 1.0f)
    {
        set_app_open(false);
        music_app_open = false;
        apply_deferred_bottom_bar_position();
        update_media_card_display();
        restore_app_drawer_from_behind();
    }
    aroma_node_invalidate(target);
}

void close_music(void *user_data)
{
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return;
    /* See close_maps for why this is cleared here, not only inside
     * music_closing_anim's progress>=1.0f branch. Clearing both flags
     * to match what that callback clears on completion. */
    set_app_open(false);
    music_app_open = false;
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 300, music_closing_anim, NULL);
    aroma_node_set_hidden(music_now_playing_card, true);
    aroma_node_set_hidden(music_device_card, true);
    aroma_animation_set_easing(anim, AROMA_EASE_IN_OUT_QUAD);
}

static void update_swupdate_service_status(void)
{
    if (!settings_swupdate_status_label || !settings_swupdate_port_label)
        return;
    
    bool running = swupdate_is_running();
    int port = swupdate_get_web_port();
    
    if (running)
    {
        char status_text[128];
        snprintf(status_text, sizeof(status_text), "Running on port %d", port);
        aroma_label_set_text(settings_swupdate_status_label, status_text);
        aroma_label_set_color(settings_swupdate_status_label, IOS_COLOR_GREEN);
        
        char port_text[64];
        snprintf(port_text, sizeof(port_text), "http://localhost:%d", port);
        aroma_label_set_text(settings_swupdate_port_label, port_text);
        aroma_label_set_color(settings_swupdate_port_label, IOS_COLOR_BLUE);
    }
    else
    {
        aroma_label_set_text(settings_swupdate_status_label, "Not running");
        aroma_label_set_color(settings_swupdate_status_label, IOS_COLOR_RED);
        aroma_label_set_text(settings_swupdate_port_label, "Service stopped");
        aroma_label_set_color(settings_swupdate_port_label, IOS_COLOR_SECONDARY_LABEL);
    }
}

static void on_settings_sidebar_select(AromaNode *sidebar, int index, void *user_data)
{
    (void)sidebar;
    (void)user_data;
    
    aroma_node_set_hidden(settings_page_general, index != 0);
    aroma_node_set_hidden(settings_page_display, index != 1);
    aroma_node_set_hidden(settings_page_updates, index != 2);
    
    if (index == 2)
    {
        update_swupdate_service_status();
    }
}

static void on_settings_autolock_click(void *user_data)
{
    (void)user_data;
    int settings_autolock_index = 0;
    const char *settings_autolock_options[] = {"30 Seconds", "1 Minute", "2 Minutes", "5 Minutes", "Never"};
    settings_autolock_index = (settings_autolock_index + 1) % 5;
    if (settings_autolock_label)
    {
        aroma_label_set_text(settings_autolock_label, settings_autolock_options[settings_autolock_index]);
    }
}

static void update_ota_display(void)
{
    if (!settings_ota_status_label || !settings_ota_check_btn)
        return;

    OtaState state_snapshot;
    int progress_snapshot;
    char detail_snapshot[256];
    char image_snapshot[256];

    pthread_mutex_lock(&ota_state_lock);
    state_snapshot = settings_ota_state;
    progress_snapshot = ota_progress;
    strncpy(detail_snapshot, ota_status_detail, sizeof(detail_snapshot) - 1);
    detail_snapshot[sizeof(detail_snapshot) - 1] = '\0';
    strncpy(image_snapshot, ota_current_image, sizeof(image_snapshot) - 1);
    image_snapshot[sizeof(image_snapshot) - 1] = '\0';
    pthread_mutex_unlock(&ota_state_lock);


    switch (state_snapshot)
    {
    case OTA_STATE_IDLE:
        aroma_label_set_text(settings_ota_status_label, "Not checked this session");
        aroma_label_set_color(settings_ota_status_label, IOS_COLOR_SECONDARY_LABEL);
        aroma_label_set_text(settings_ota_check_btn, "Check Now");
        aroma_label_set_color(settings_ota_check_btn, IOS_COLOR_BLUE);
        if (settings_ota_progress_bar)
            aroma_node_set_hidden(settings_ota_progress_bar, true);
        if (settings_ota_progress_label)
            aroma_node_set_hidden(settings_ota_progress_label, true);
        break;

    case OTA_STATE_CONNECTING:
        aroma_label_set_text(settings_ota_status_label, "Connecting to swupdate...");
        aroma_label_set_color(settings_ota_status_label, IOS_COLOR_SECONDARY_LABEL);
        aroma_label_set_text(settings_ota_check_btn, "Connecting...");
        aroma_label_set_color(settings_ota_check_btn, IOS_COLOR_GRAY);
        if (settings_ota_progress_bar)
            aroma_node_set_hidden(settings_ota_progress_bar, true);
        if (settings_ota_progress_label)
            aroma_node_set_hidden(settings_ota_progress_label, true);
        break;

    case OTA_STATE_RUNNING:
    {
        char status_text[300];
        if (image_snapshot[0])
            snprintf(status_text, sizeof(status_text), "Installing %s...", image_snapshot);
        else
            snprintf(status_text, sizeof(status_text), "Update running...");
        aroma_label_set_text(settings_ota_status_label, status_text);
        aroma_label_set_color(settings_ota_status_label, IOS_COLOR_ORANGE);
        aroma_label_set_text(settings_ota_check_btn, "Installing...");
        aroma_label_set_color(settings_ota_check_btn, IOS_COLOR_ORANGE);
        if (settings_ota_progress_bar)
        {
            aroma_node_set_hidden(settings_ota_progress_bar, false);
            aroma_progressbar_set_progress(settings_ota_progress_bar, progress_snapshot / 100.0f);
        }
        if (settings_ota_progress_label)
        {
            char progress_text[32];
            snprintf(progress_text, sizeof(progress_text), "%d%%", progress_snapshot);
            aroma_label_set_text(settings_ota_progress_label, progress_text);
            aroma_node_set_hidden(settings_ota_progress_label, false);
        }
        break;
    }

    case OTA_STATE_SUCCESS:
        aroma_label_set_text(settings_ota_status_label, "Update installed - reboot to apply");
        aroma_label_set_color(settings_ota_status_label, IOS_COLOR_GREEN);
        aroma_label_set_text(settings_ota_check_btn, "Check Now");
        aroma_label_set_color(settings_ota_check_btn, IOS_COLOR_BLUE);
        if (settings_ota_progress_bar)
            aroma_node_set_hidden(settings_ota_progress_bar, true);
        if (settings_ota_progress_label)
            aroma_node_set_hidden(settings_ota_progress_label, true);
        break;

    case OTA_STATE_FAILURE:
    {
        char status_text[300];
        if (detail_snapshot[0])
            snprintf(status_text, sizeof(status_text), "Update failed: %s", detail_snapshot);
        else
            snprintf(status_text, sizeof(status_text), "Update failed");
        aroma_label_set_text(settings_ota_status_label, status_text);
        aroma_label_set_color(settings_ota_status_label, IOS_COLOR_RED);
        aroma_label_set_text(settings_ota_check_btn, "Retry");
        aroma_label_set_color(settings_ota_check_btn, IOS_COLOR_BLUE);
        if (settings_ota_progress_bar)
            aroma_node_set_hidden(settings_ota_progress_bar, true);
        if (settings_ota_progress_label)
            aroma_node_set_hidden(settings_ota_progress_label, true);
        break;
    }

    case OTA_STATE_DAEMON_UNREACHABLE:
    {
        char status_text[300];
        snprintf(status_text, sizeof(status_text), "swupdate unreachable%s%s",
                 detail_snapshot[0] ? ": " : "", detail_snapshot);
        aroma_label_set_text(settings_ota_status_label, status_text);
        aroma_label_set_color(settings_ota_status_label, IOS_COLOR_RED);
        aroma_label_set_text(settings_ota_check_btn, "Retry");
        aroma_label_set_color(settings_ota_check_btn, IOS_COLOR_BLUE);
        if (settings_ota_progress_bar)
            aroma_node_set_hidden(settings_ota_progress_bar, true);
        if (settings_ota_progress_label)
            aroma_node_set_hidden(settings_ota_progress_label, true);
        break;
    }
    }
}

static void *ota_install_thread_func(void *arg)
{
    (void)arg;

    char err_buf[256] = "";
    bool ack_ok = swupdate_send_install_request(err_buf, sizeof(err_buf));

    if (!ack_ok)
    {
        pthread_mutex_lock(&ota_state_lock);
        settings_ota_state = OTA_STATE_DAEMON_UNREACHABLE;
        strncpy(ota_status_detail, err_buf, sizeof(ota_status_detail) - 1);
        ota_status_detail[sizeof(ota_status_detail) - 1] = '\0';
        ota_poll_thread_running = false;
        pthread_mutex_unlock(&ota_state_lock);
        update_ota_display();
        return NULL;
    }

    pthread_mutex_lock(&ota_state_lock);
    settings_ota_state = OTA_STATE_RUNNING;
    ota_progress = 0;
    pthread_mutex_unlock(&ota_state_lock);
    update_ota_display();

    int consecutive_empty_polls = 0;
    const int max_consecutive_empty_polls = 30;
    bool terminal = false;

    while (!terminal && consecutive_empty_polls < max_consecutive_empty_polls)
    {
        swupdate_progress_msg_t msg;
        bool got_frame = swupdate_poll_progress(&msg, 500);

        if (!got_frame)
        {
            consecutive_empty_polls++;
            continue;
        }
        consecutive_empty_polls = 0;

        pthread_mutex_lock(&ota_state_lock);
        int step_percent = (int)msg.cur_percent;
        if (step_percent < 0)
            step_percent = 0;
        if (step_percent > 100)
            step_percent = 100;
        ota_progress = step_percent;

        strncpy(ota_current_image, msg.cur_image, sizeof(ota_current_image) - 1);
        ota_current_image[sizeof(ota_current_image) - 1] = '\0';

        switch ((swupdate_progress_status_t)msg.status)
        {
        case SWU_SUCCESS:
        case SWU_DONE:
            settings_ota_state = OTA_STATE_SUCCESS;
            terminal = true;
            break;
        case SWU_FAILURE:
            settings_ota_state = OTA_STATE_FAILURE;
            if (msg.infolen > 0)
            {
                size_t copy_len = msg.infolen < sizeof(ota_status_detail) - 1
                                       ? msg.infolen
                                       : sizeof(ota_status_detail) - 1;
                memcpy(ota_status_detail, msg.info, copy_len);
                ota_status_detail[copy_len] = '\0';
            }
            terminal = true;
            break;
        case SWU_IDLE:
        case SWU_START:
        case SWU_RUN:
        case SWU_DOWNLOAD:
        case SWU_SUBPROCESS:
        case SWU_PROGRESS:
        default:
            settings_ota_state = OTA_STATE_RUNNING;
            break;
        }
        pthread_mutex_unlock(&ota_state_lock);
        update_ota_display();
    }

    if (!terminal)
    {
        pthread_mutex_lock(&ota_state_lock);
        settings_ota_state = OTA_STATE_DAEMON_UNREACHABLE;
        snprintf(ota_status_detail, sizeof(ota_status_detail),
                 "progress socket went silent mid-install (last known progress %d%%)", ota_progress);
        pthread_mutex_unlock(&ota_state_lock);
        update_ota_display();
    }

    pthread_mutex_lock(&ota_state_lock);
    ota_poll_thread_running = false;
    pthread_mutex_unlock(&ota_state_lock);

    return NULL;
}

static void on_settings_ota_check_click(void *user_data)
{
    (void)user_data;

    pthread_mutex_lock(&ota_state_lock);
    bool already_running = ota_poll_thread_running ||
                            settings_ota_state == OTA_STATE_CONNECTING ||
                            settings_ota_state == OTA_STATE_RUNNING;
    if (already_running)
    {
        pthread_mutex_unlock(&ota_state_lock);
        return;
    }
    ota_poll_thread_running = true;
    settings_ota_state = OTA_STATE_CONNECTING;
    ota_status_detail[0] = '\0';
    ota_current_image[0] = '\0';
    ota_progress = 0;
    pthread_mutex_unlock(&ota_state_lock);
    update_ota_display();

    pthread_t install_thread;
    pthread_attr_t install_attr;
    pthread_attr_init(&install_attr);
    pthread_attr_setdetachstate(&install_attr, PTHREAD_CREATE_DETACHED);
    int create_rc = pthread_create(&install_thread, &install_attr, ota_install_thread_func, NULL);
    pthread_attr_destroy(&install_attr);

    if (create_rc != 0)
    {
        pthread_mutex_lock(&ota_state_lock);
        settings_ota_state = OTA_STATE_DAEMON_UNREACHABLE;
        snprintf(ota_status_detail, sizeof(ota_status_detail), "could not start install thread: %s", strerror(create_rc));
        ota_poll_thread_running = false;
        pthread_mutex_unlock(&ota_state_lock);
        update_ota_display();
    }
}

static bool on_settings_ota_autoinstall_changed(AromaNode *switch_node, void *user_data)
{
    (void)user_data;
    settings_ota_autoinstall = aroma_switch_get_state(switch_node);
    return true;
}

void settings_opening_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    
    int start_y = WIN_H;
    int end_y = 0;
    
    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;
    aroma_node_invalidate(target);
}

static bool open_settings(AromaNode *node, void *user_data)
{
    (void)node;
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return false;
    if (app_drawer_visible)
    {
        send_app_drawer_behind();
    }

    aroma_node_set_hidden(card_node, false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 300, settings_opening_anim, NULL);
    if (!anim)
        return false;
    set_app_open(true);
    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
    aroma_node_set_hidden(settings_sidebar, false);
    aroma_node_set_hidden(settings_page_general, false);
    aroma_node_set_hidden(settings_page_display, true);
    aroma_node_set_hidden(settings_page_updates, true);
    aroma_node_set_z_index(card_node, Z_LAYER_STATUS_BAR + 10);
    update_bt_info_card();
    update_swupdate_service_status();
    
    return true;
}

void settings_closing_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    
    int start_y = 0;
    int end_y = WIN_H;
    
    rect->x = 0;
    rect->y = start_y + (int)((end_y - start_y) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;
    if (progress >= 0.92f)
    {
        aroma_node_set_z_index(target, 1);
        aroma_node_set_hidden(settings_sidebar, true);
        aroma_node_set_hidden(settings_page_general, true);
        aroma_node_set_hidden(settings_page_display, true);
        aroma_node_set_hidden(settings_page_updates, true);
        aroma_node_set_hidden(target, true);
    }
    if (progress >= 1.0f)
    {
        set_app_open(false);
        apply_deferred_bottom_bar_position();
        update_media_card_display();
        restore_app_drawer_from_behind();
    }
    aroma_node_invalidate(target);
}

void close_settings(void *user_data)
{
    AromaNode *card_node = (AromaNode *)user_data;
    if (!card_node)
        return;
    /* See close_maps for why this is cleared here, not only inside
     * settings_closing_anim's progress>=1.0f branch. */
    set_app_open(false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, 300, settings_closing_anim, NULL);
    aroma_node_set_hidden(settings_sidebar, true);
    aroma_node_set_hidden(settings_page_general, true);
    aroma_node_set_hidden(settings_page_display, true);
    aroma_node_set_hidden(settings_page_updates, true);
    aroma_animation_set_easing(anim, AROMA_EASE_IN_OUT_QUAD);
}

static bool on_app_drawer_click(AromaNode* node, void *user_data)
{
    (void)node;
    int app_index = (int)(intptr_t)user_data;
    if (app_index >= 0 && app_index < (int)APP_COUNT)
    {
        send_app_drawer_behind();
        if (app_definitions[app_index].open_func)
        {
            return app_definitions[app_index].open_func(NULL, app_definitions[app_index].user_data);
        }
    }
    return false;
}

static void on_app_drawer_button_click(void *user_data)
{
    (void)user_data;
    if (app_drawer_visible)
    {
        AromaAnimation *slide_down = aroma_animation_start(app_drawer, AROMA_ANIM_SLIDE_Y, 0, WIN_H, 300);
        aroma_animation_set_easing(slide_down, AROMA_EASE_OUT_CUBIC);
        app_drawer_visible = false;
    }
    else
    {
        aroma_node_set_hidden(app_drawer, false);
        AromaAnimation *slide_up = aroma_animation_start(app_drawer, AROMA_ANIM_SLIDE_Y, WIN_H, 0, 300);
        aroma_animation_set_easing(slide_up, AROMA_EASE_OUT_CUBIC);
        app_drawer_visible = true;
        aroma_node_set_z_index(app_drawer, APP_DRAWER_Z_INDEX);
    }
}

static void on_app_drawer_close_click(void *user_data)
{
    (void)user_data;
    AromaAnimation *slide_down = aroma_animation_start(app_drawer, AROMA_ANIM_SLIDE_Y, 0, WIN_H, 300);
    aroma_animation_set_easing(slide_down, AROMA_EASE_OUT_CUBIC);
    app_drawer_visible = false;
}

static void battery_diagnostics(void *user_data)
{
    (void)user_data;
    aroma_image_set_source(state.overlay,
#ifdef __EMSCRIPTEN__
                           "/assets/car_battery.png"
#elif defined(__arm__) || defined(__aarch64__)
                           "/usr/share/infotainment/assets/car_battery.png"
#else
                           "../assets/car_battery.png"
#endif
    );
    AromaAnimation *anim = aroma_animation_start(
        state.overlay, AROMA_ANIM_SLIDE_Y, 150, 100, 400);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_ELASTIC);
    aroma_node_set_hidden(state.vehicle_view_lock_divider, true);
    aroma_node_set_hidden(state.vehicle_view_charge_port_divider, true);
    aroma_node_set_hidden(state.vehicle_view_charge_port_icon, true);
    aroma_node_set_hidden(state.vehicle_view_frunk_header, true);
    aroma_node_set_hidden(state.vehicle_view_frunk_desc, true);
    aroma_node_set_hidden(state.vehicle_view_frunk_divider, true);
    aroma_node_set_hidden(state.vehicle_view_trunk_divider, true);
    aroma_node_set_hidden(state.vehicle_view_trunk_header, true);
    aroma_node_set_hidden(state.vehicle_view_trunk_desc, true);
    aroma_node_set_hidden(state.vehicle_view_lock_icon, true);
    aroma_node_set_hidden(state.vehicle_view_warning_message_card, true);
    aroma_node_set_hidden(state.vehicle_view_warning_message_label, true);
    aroma_node_set_hidden(state.vehicle_view_warning_warning_icon, true);
    aroma_node_set_hidden(state.vehicle_view_warning_message_action, true);
    aroma_node_set_hidden(state.battery_image, false);
    aroma_node_set_hidden(state.battery_health, false);
    aroma_node_set_hidden(state.battery_percentage, false);
    aroma_animation_start(state.battery_image, AROMA_ANIM_FADE, 0, 1, 1000);
    aroma_animation_start(state.battery_health, AROMA_ANIM_FADE, 0, 1, 1000);
    aroma_animation_start(state.battery_percentage, AROMA_ANIM_FADE, 0, 1, 1000);
}

void unlock_screen(void)
{
    if (!lock_screen_active)
        return;
    lock_screen_active = false;
    if (lock_screen_root)
    {
        aroma_animation_start(lock_screen_root, AROMA_ANIM_FADE, 1, 0, 400);
        aroma_node_set_hidden(lock_screen_root, true);
    }
    if (state.vehicle_view_root)
    {
        aroma_node_set_hidden(state.vehicle_view_root, false);
        aroma_animation_start(state.vehicle_view_root, AROMA_ANIM_FADE, 0, 1, 400);
    }
}

static void on_unlock_button_click(void *user_data)
{
    (void)user_data;
    unlock_screen();
}

static void update_lock_screen_clock(void)
{
    if (!lock_screen_active || !lock_screen_time)
        return;
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[16];
    char date_str[64];
    strftime(time_str, sizeof(time_str), "%I:%M", tm_info);
    if (time_str[0] == '0')
        time_str[0] = ' ';
    strftime(date_str, sizeof(date_str), "%A, %B %d", tm_info);
    aroma_label_set_text(lock_screen_time, time_str);
    if (lock_screen_date)
        aroma_label_set_text(lock_screen_date, date_str);
}

static void *lock_screen_clock_thread(void *arg)
{
    (void)arg;
    while (1)
    {
        update_lock_screen_clock();
        sleep(1);
    }
    return NULL;
}

static void build_lock_screen(AromaNode *window)
{
    lock_screen_root = aroma_ui_container(
        window, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_z_index(lock_screen_root,  9990);

    AromaNode *black_bg = aroma_ui_card(
        lock_screen_root, -10, -10, WIN_W + 20, WIN_H + 20, CARD_TYPE_FILLED);
    aroma_card_set_colors(black_bg, 0xFF000000, 0xFF000000);
    aroma_node_set_z_index(black_bg, 9991);

    lock_screen_time = aroma_ui_label(
        lock_screen_root, "12:00",
        WIN_W / 2 - 70, WIN_H / 2 - 100, LABEL_STYLE_LABEL_LARGE, state.clock_font);
    aroma_label_set_color(lock_screen_time, 0xFFFFFFFF);
    aroma_node_set_z_index(lock_screen_time, 9991);

    lock_screen_date = aroma_ui_label(
        lock_screen_root, "Monday, January 1",
        WIN_W / 2 - 100, WIN_H / 2, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_label_set_color(lock_screen_date, 0xCCFFFFFF);
    aroma_node_set_z_index(lock_screen_date, 9991);

    lock_screen_unlock_btn = aroma_ui_iconbutton(
        lock_screen_root, AROMA_ICON_LOCK_OPEN,
        WIN_W / 2 - 30, WIN_H / 2 + 60, 60, ICON_BUTTON_FILLED,
        on_unlock_button_click, NULL, state.icon_font);
    aroma_iconbutton_set_colors(lock_screen_unlock_btn, 0xFFFFFFFF, 0xFF2196F3);
    aroma_node_set_z_index(lock_screen_unlock_btn, 9991);

    AromaNode *hint_label = aroma_ui_label(
        lock_screen_root, "Tap to unlock",
        WIN_W / 2 - 57, WIN_H / 2 + 140, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_label_set_color(hint_label, 0x88FFFFFF);
    aroma_node_set_z_index(hint_label, 9991);

    update_lock_screen_clock();

    pthread_t clock_thread;
    pthread_attr_t clock_attr;
    pthread_attr_init(&clock_attr);
    pthread_attr_setdetachstate(&clock_attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&clock_thread, &clock_attr, lock_screen_clock_thread, NULL);
    pthread_attr_destroy(&clock_attr);

    lock_screen_active = true;
    aroma_node_set_hidden(state.vehicle_view_root, true);
}
static void on_bt_log(const char *level, const char *message, void *user_data)
{
    (void)user_data;
    /* register_agent() failures, D-Bus timeouts, and other warnings from
     * bt_speaker_api.c only reach log_cb — nothing was wired to it before,
     * so they were going nowhere. Surfacing to stderr at minimum so a
     * failed pairing has a visible cause instead of just "rejected". */
    fprintf(stderr, "[BT %s] %s\n", level ? level : "INFO", message ? message : "");
}

static void on_bt_state_changed(bt_state_t old_state, bt_state_t new_state, void *user_data)
{
    (void)user_data;
    (void)old_state;
    
    pthread_mutex_lock(&g_bt_mutex);
    g_bt_state = new_state;
    g_bt_connected = (new_state == BT_STATE_CONNECTED || new_state == BT_STATE_PLAYING);
    pthread_mutex_unlock(&g_bt_mutex);
    
    update_bt_info_card();
    update_media_card_display();
}
static void on_bt_device_changed(const bt_device_info_t *device, bool connected, void *user_data)
{
    (void)user_data;
    pthread_mutex_lock(&g_bt_mutex);
    g_bt_connected = connected;
    if (connected && device)
    {
        memcpy(&g_bt_device_info, device, sizeof(bt_device_info_t));
    }
    else
    {
        memset(&g_bt_device_info, 0, sizeof(bt_device_info_t));
    }
    pthread_mutex_unlock(&g_bt_mutex);
    update_bt_info_card();

    if (connected && device && device->path[0] && !state.contacts_fetched)
    {
        bt_contact_t bt_contacts[MAX_CONTACTS];
        int count = bt_hfp_fetch_contacts(device->path, bt_contacts, MAX_CONTACTS);
        if (count > 0)
        {
            state.contact_count = count;
            for (int i = 0; i < count && i < MAX_CONTACTS; i++)
            {
                strncpy(state.contacts[i].name, bt_contacts[i].name, sizeof(state.contacts[i].name) - 1);
                state.contacts[i].name[sizeof(state.contacts[i].name) - 1] = '\0';
                strncpy(state.contacts[i].number, bt_contacts[i].number, sizeof(state.contacts[i].number) - 1);
                state.contacts[i].number[sizeof(state.contacts[i].number) - 1] = '\0';
            }
            state.contacts_fetched = true;
            populate_contact_listview(state.contact_listview);
        }
    }
}

static void on_bt_error(bt_error_t error, const char *message, void *user_data)
{
    (void)user_data;
    (void)error;
    fprintf(stderr, "Bluetooth error: %s\n", message ? message : "unknown");
}

static void on_bt_audio_changed(bool started, void *user_data)
{
    (void)user_data;
    (void)started;
    update_media_card_display();
}

static void on_bt_avrcp_changed(const bt_media_info_t *media, void *user_data)
{
    (void)user_data;
    if (media)
    {
        pthread_mutex_lock(&g_bt_mutex);
        memcpy(&g_bt_media_info, media, sizeof(bt_media_info_t));
        pthread_mutex_unlock(&g_bt_mutex);
    }
    update_media_card_display();
    if (music_app_open && music_active_tab == 0)
    {
        update_music_now_playing_display();
    }
}

static void on_bt_call_changed(const bt_call_info_t *call, bool removed, void *user_data)
{
    (void)user_data;
    (void)removed;
    if (call && call->state == BT_CALL_STATE_INCOMING)
    {
        show_incoming_call_screen(call->name, call->line_id, call->path);
    }
    else if (call && call->state == BT_CALL_STATE_DISCONNECTED)
    {
        if (incoming_call_overlay)
        {
            aroma_node_set_hidden(incoming_call_overlay, true);
        }
        pthread_mutex_lock(&call_state_lock);
        call_overlay_visible = false;
        current_call_path[0] = '\0';
        pthread_mutex_unlock(&call_state_lock);
    }
}

void build_vehicle_view(AromaNode *window)
{
    state.vehicle_view_root = aroma_ui_container(
        window, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.vehicle_view_root, Z_LAYER_BACKGROUND);
    aroma_node_set_hidden(state.vehicle_view_root, true);

    state.backroad = aroma_ui_image(
        state.vehicle_view_root,
#ifdef __EMSCRIPTEN__
        "/assets/bg_dark.jpeg"
#elif defined(__arm__) || defined(__aarch64__)
        "/usr/share/infotainment/assets/bg_dark.jpeg"
#else
        "../assets/bg_dark.jpeg"
#endif
        ,
        0, 0, WIN_W, WIN_H);
    aroma_node_set_z_index(state.backroad, Z_LAYER_BACKGROUND);

    state.car_img = aroma_ui_image(
        state.vehicle_view_root,
#ifdef __EMSCRIPTEN__
        "/assets/car.png"
#elif defined(__arm__) || defined(__aarch64__)
        "/usr/share/infotainment/assets/car.png"
#else
        "../assets/car.png"
#endif
        ,
        150, 100, 700, 405);
    aroma_node_set_z_index(state.car_img, Z_LAYER_VEHICLE_IMAGE);

    state.overlay = aroma_ui_image(state.vehicle_view_root, NULL, 150, 100, 700, 405);
    aroma_node_set_z_index(state.overlay, Z_LAYER_VEHICLE_OVERLAYS);

    state.battery_button = aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_BATTERY_FULL,
        WIN_W - 260, 22, 40, ICON_BUTTON_OUTLINED,
        battery_diagnostics, NULL, state.icon_font);
    aroma_node_set_z_index(state.battery_button, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_large_clock = aroma_ui_label(
        state.vehicle_view_root, "12:45",
        WIN_W / 2 - 90, 35, LABEL_STYLE_LABEL_LARGE, state.clock_font);
    aroma_node_set_z_index(state.vehicle_view_large_clock, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_large_clock_pm_am = aroma_ui_label(
        state.vehicle_view_root, "PM",
        WIN_W / 2 + 68, 70, LABEL_STYLE_LABEL_MEDIUM, state.clock_pm_am_font);
    aroma_node_set_z_index(state.vehicle_view_large_clock_pm_am, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_frunk_divider = aroma_ui_divider(
        state.vehicle_view_root, 260, 260, 80, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(state.vehicle_view_frunk_divider, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_lock_divider = aroma_ui_divider(
        state.vehicle_view_root, 550, 165, 40, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(state.vehicle_view_lock_divider, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_lock_icon = aroma_ui_icon(
        state.vehicle_view_root, AROMA_ICON_LOCK, 563, 130, 24,
        state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(state.vehicle_view_lock_icon, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_frunk_header = aroma_ui_label(
        state.vehicle_view_root, "Frunk", 180, 250, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_frunk_header, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_frunk_desc = aroma_ui_label(
        state.vehicle_view_root, "Open", 180, 270, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_frunk_desc, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_trunk_divider = aroma_ui_divider(
        state.vehicle_view_root, 780, 220, 80, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(state.vehicle_view_trunk_divider, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_trunk_header = aroma_ui_label(
        state.vehicle_view_root, "Trunk", 800, 215, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_trunk_header, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_trunk_desc = aroma_ui_label(
        state.vehicle_view_root, "Closed", 800, 235, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_trunk_desc, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_charge_port_divider = aroma_ui_divider(
        state.vehicle_view_root, 810, 330, 40, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_z_index(state.vehicle_view_charge_port_divider, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_charge_port_icon = aroma_ui_icon(
        state.vehicle_view_root, AROMA_ICON_POWER, 880, 315, 24,
        state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(state.vehicle_view_charge_port_icon, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_warning_message_card = aroma_ui_card(
        state.vehicle_view_root, 330, WIN_H + 100, 600, 70, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.vehicle_view_warning_message_card, Z_LAYER_CARDS_BOTTOM + 50);
    aroma_node_set_hidden(state.vehicle_view_warning_message_card, true);

    state.vehicle_view_warning_warning_icon = aroma_ui_icon(
        state.vehicle_view_warning_message_card, AROMA_ICON_WARNING,
        65, 22, 24, 0xFFFFD600, state.icon_font);
    aroma_node_set_z_index(state.vehicle_view_warning_warning_icon, Z_LAYER_CARDS_BOTTOM + 51);

    state.vehicle_view_warning_message_label = aroma_ui_label(
        state.vehicle_view_warning_message_card,
        "Warning: The Frunk is Open. Close it before driving.",
        110, 15, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_warning_message_label, Z_LAYER_CARDS_BOTTOM + 51);

    state.battery_image = aroma_ui_image(
        state.vehicle_view_root,
#ifdef __EMSCRIPTEN__
        "/assets/charging.png"
#elif defined(__arm__) || defined(__aarch64__)
        "/usr/share/infotainment/assets/charging.png"
#else
        "../assets/charging.png"
#endif
        ,
        WIN_W / 2 - 180, 100, 128, 128);
    aroma_node_set_z_index(state.battery_image, Z_LAYER_VEHICLE_OVERLAYS + 10);
    aroma_node_set_hidden(state.battery_image, true);

    state.battery_health = aroma_ui_label(
        state.vehicle_view_root, "Battery Health: Good",
        WIN_W / 2 - 20, 150, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.battery_health, Z_LAYER_VEHICLE_OVERLAYS + 10);
    aroma_label_set_color(state.battery_health, 0xFF00C853);
    aroma_node_set_hidden(state.battery_health, true);

    /* Width = remaining space after the left dock (dock: x=20, w=80, plus
     * this card's own x=110 start) minus a 20px right margin, matching
     * the 20px margin the dock itself has on the screen's left edge.
     * Was a fixed 395 before; this now grows/shrinks with WIN_W instead
     * of leaving a gap (or overflowing) on wider/narrower screens. */
    media_ui.media_card = aroma_ui_card(
        state.vehicle_view_root, 110, WIN_H - 110, WIN_W - 110 - 20, 80, CARD_TYPE_GLASS);
    aroma_node_set_z_index(media_ui.media_card, Z_LAYER_VEHICLE_OVERLAYS + 2);
    aroma_node_set_hidden(media_ui.media_card, true);

    AromaNode *music_icon_btn = aroma_ui_iconbutton(
        media_ui.media_card, AROMA_ICON_MUSIC_NOTE,
        22, 22, 36, ICON_BUTTON_FILLED,
        on_music_icon_click, NULL, state.icon_font);
    aroma_node_set_z_index(music_icon_btn, Z_LAYER_VEHICLE_OVERLAYS + 3);

    media_ui.media_title_label = aroma_ui_label(
        media_ui.media_card, "No Track", 70, 12,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(media_ui.media_title_label, Z_LAYER_VEHICLE_OVERLAYS + 3);

    media_ui.media_artist_label = aroma_ui_label(
        media_ui.media_card, "No Artist", 70, 40,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(media_ui.media_artist_label, Z_LAYER_VEHICLE_OVERLAYS + 3);

    /* Anchored to the card's right edge (card is now WIN_W-110-20 wide,
     * not a fixed 395) instead of fixed x positions, so these stay
     * pinned to the right side instead of floating mid-card. Margin/
     * spacing (41px right margin, 44px between buttons) preserved from
     * the original fixed layout: next-button was at x=318 with a 36px
     * icon, so 395 - (318+36) = 41px margin; 274-230 = 318-274 = 44px
     * spacing. */
    int media_card_width = WIN_W - 110 - 20;
    int media_next_x = media_card_width - 41 - 36;
    int media_play_x = media_next_x - 44;
    int media_prev_x = media_play_x - 44;

    media_ui.media_prev_button = aroma_ui_iconbutton(
        media_ui.media_card, AROMA_ICON_SKIP_PREVIOUS,
        media_prev_x, 22, 36, ICON_BUTTON_OUTLINED,
        on_media_prev_click, NULL, state.icon_font);
    aroma_node_set_z_index(media_ui.media_prev_button, Z_LAYER_VEHICLE_OVERLAYS + 3);

    media_ui.media_play_pause_button = aroma_ui_iconbutton(
        media_ui.media_card, AROMA_ICON_PLAY_ARROW,
        media_play_x, 22, 36, ICON_BUTTON_OUTLINED,
        on_media_play_pause_click, NULL, state.icon_font);
    aroma_node_set_z_index(media_ui.media_play_pause_button, Z_LAYER_VEHICLE_OVERLAYS + 3);

    media_ui.media_next_button = aroma_ui_iconbutton(
        media_ui.media_card, AROMA_ICON_SKIP_NEXT,
        media_next_x, 22, 36, ICON_BUTTON_OUTLINED,
        on_media_next_click, NULL, state.icon_font);
    aroma_node_set_z_index(media_ui.media_next_button, Z_LAYER_VEHICLE_OVERLAYS + 3);

    state.bottom_bar = aroma_ui_card(state.vehicle_view_root, 20, 20, 80, WIN_H - 40, CARD_TYPE_GLASS);
    aroma_node_set_z_index(state.bottom_bar, Z_LAYER_VEHICLE_OVERLAYS + 2);
    media_ui.bottom_bar_expanded = false;

    AromaNode *app_drawer_btn = aroma_ui_iconbutton(
        state.bottom_bar, AROMA_ICON_APPS,
        16, 15, 48, ICON_BUTTON_FILLED,
        on_app_drawer_button_click, NULL, state.icon_font);
    aroma_node_set_z_index(app_drawer_btn, Z_LAYER_VEHICLE_OVERLAYS + 2);

    AromaNode *ac_minus = aroma_ui_iconbutton(state.bottom_bar, AROMA_ICON_REMOVE, 25, 180, 30, ICON_BUTTON_FILLED, ac_temp_down_callback, NULL, state.icon_font);
    aroma_node_set_z_index(ac_minus, Z_LAYER_VEHICLE_OVERLAYS + 2);
    AromaNode *ac_temp_label = aroma_ui_label(state.bottom_bar, "22C", 22, 225, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(ac_temp_label, Z_LAYER_VEHICLE_OVERLAYS + 2);
    state.ac_temp_label = ac_temp_label;
    AromaNode *ac_plus = aroma_ui_iconbutton(state.bottom_bar, AROMA_ICON_ADD, 25, 260, 30, ICON_BUTTON_FILLED, ac_temp_up_callback, NULL, state.icon_font);
    aroma_node_set_z_index(ac_plus, Z_LAYER_VEHICLE_OVERLAYS + 2);

    app_drawer = aroma_ui_card(
        state.vehicle_view_root, 0, WIN_H, WIN_W, WIN_H, CARD_TYPE_ELEVATED);
    aroma_node_set_z_index(app_drawer, APP_DRAWER_Z_INDEX);
    aroma_node_set_hidden(app_drawer, true);

    app_drawer_close_btn = aroma_ui_iconbutton(
        app_drawer, AROMA_ICON_CLOSE,
        WIN_W - 60, 20, 40, ICON_BUTTON_FILLED,
        on_app_drawer_close_click, NULL, state.icon_font);
    aroma_node_set_z_index(app_drawer_close_btn, APP_DRAWER_Z_INDEX + 2);

    AromaNode *app_drawer_title = aroma_ui_label(
        app_drawer, "All Apps",
        40, 20, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(app_drawer_title, APP_DRAWER_Z_INDEX + 1);

    app_definitions[0].name = "";
    app_definitions[0].icon = AROMA_ICON_MAP;
    app_definitions[0].open_func = open_maps;
    app_definitions[0].user_data = NULL;
    app_definitions[0].card_color = aroma_color_blend(ANDROID_COLOR_SURFACE, ANDROID_COLOR_PRIMARY, 0.1f);

    app_definitions[1].name = "";
    app_definitions[1].icon = AROMA_ICON_PHONE;
    app_definitions[1].open_func = open_phone;
    app_definitions[1].user_data = NULL;
    app_definitions[1].card_color = aroma_color_blend(0xFF00FF00, 0xFF00FF00, 0.5f);
    
    app_definitions[2].name = "";
    app_definitions[2].icon = AROMA_ICON_MUSIC_NOTE;
    app_definitions[2].open_func = open_music;
    app_definitions[2].user_data = NULL;
    app_definitions[2].card_color = aroma_color_blend(ANDROID_COLOR_SURFACE, ANDROID_COLOR_PRIMARY, 0.1f);

    app_definitions[3].name = "";
    app_definitions[3].icon = AROMA_ICON_SETTINGS;
    app_definitions[3].open_func = open_settings;
    app_definitions[3].user_data = NULL;
    app_definitions[3].card_color = aroma_color_blend(ANDROID_COLOR_SURFACE, ANDROID_COLOR_PRIMARY, 0.1f);

    for (int i = 0; i < (int)APP_COUNT; i++)
    {
        int row = i / 2;
        int col = i % 2;
        
        int card_x = 300 + col * 230;
        int card_y = 130 + row * 200;
        
        AromaNode *btn_card = aroma_ui_card(
            app_drawer, card_x, card_y, 180, 150, CARD_TYPE_ELEVATED);
        aroma_node_set_z_index(btn_card, APP_DRAWER_Z_INDEX + 1);
        app_definitions[i].drawer_icon = aroma_ui_icon(
            btn_card, app_definitions[i].icon,
            120, 40, 64, 0xFFFFFFFF, state.huge_icon_font);
        aroma_node_set_z_index(app_definitions[i].drawer_icon, APP_DRAWER_Z_INDEX + 3);
        
        AromaNode *app_label = aroma_ui_label(
            btn_card, app_definitions[i].name,
            50, 100, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
        aroma_label_set_color(app_label, ANDROID_COLOR_ON_SURFACE);
        aroma_node_set_z_index(app_label, APP_DRAWER_Z_INDEX + 3);
        aroma_label_set_color(app_label, 0xFFFFFFFF);
        app_definitions[i].drawer_card = btn_card;
        
        AromaNode *app_btn = aroma_ui_button(
            btn_card, "",
            0, 0, 180, 150,
            on_app_drawer_click,
            (void *)(intptr_t)i, state.ui_font);
        aroma_node_set_z_index(app_btn, APP_DRAWER_Z_INDEX + 2);
        aroma_button_set_colors(btn_card, 0xFF00FF00, 0xFF00FF00, 0xFF00FF00, 0xFF00FF00);
    }
    app_definitions[0].app_root = aroma_ui_card(
        state.vehicle_view_root, 0, 0, WIN_W, WIN_H, CARD_TYPE_ELEVATED);
    aroma_node_set_z_index(app_definitions[0].app_root, APP_DRAWER_Z_INDEX + 10);
    aroma_node_set_hidden(app_definitions[0].app_root, true);

    app_definitions[1].app_root = aroma_ui_card(
        state.vehicle_view_root, 0, 0, WIN_W, WIN_H, CARD_TYPE_ELEVATED);
    aroma_node_set_z_index(app_definitions[1].app_root, APP_DRAWER_Z_INDEX + 10);
    aroma_node_set_hidden(app_definitions[1].app_root, true);

    app_definitions[2].app_root = aroma_ui_card(
        state.vehicle_view_root, 0, 0, WIN_W, WIN_H, CARD_TYPE_ELEVATED);
    aroma_node_set_z_index(app_definitions[2].app_root, APP_DRAWER_Z_INDEX + 10);
    aroma_node_set_hidden(app_definitions[2].app_root, true);

    app_definitions[3].app_root = aroma_ui_card(
        state.vehicle_view_root, 0, 0, WIN_W, WIN_H, CARD_TYPE_ELEVATED);
    aroma_node_set_z_index(app_definitions[3].app_root, APP_DRAWER_Z_INDEX + 10);
    aroma_node_set_hidden(app_definitions[3].app_root, true);

    app_definitions[0].user_data = app_definitions[0].app_root;
    app_definitions[1].user_data = app_definitions[1].app_root;
    app_definitions[2].user_data = app_definitions[2].app_root;
    app_definitions[3].user_data = app_definitions[3].app_root;

    state.maps_app_icon = app_definitions[0].drawer_icon;
    state.phone_app_icon = app_definitions[1].drawer_icon;

    state.map_node = aroma_ui_map(app_definitions[0].app_root, 0, 0, 48, 48);
    aroma_map_set_mbtiles(state.map_node, 
    #ifdef __EMSCRIPTEN__
        "/assets/tunisia.mbtiles"
    #elif defined(__arm__) || defined(__aarch64__)
        "/usr/share/infotainment/assets/ariana_3d.mbtiles"
    #else
        "../assets/ariana_3d.mbtiles"
    #endif
    );
    aroma_map_load_osrm_data(state.map_node,
    #ifdef __EMSCRIPTEN__
        "/assets/routing_data.bin"
    #elif defined(__arm__) || defined(__aarch64__)
        "/usr/share/infotainment/assets/routing_data.bin"
    #else
        "../assets/routing_data.bin"
    #endif
    );
    aroma_map_load_poi_database(state.map_node, 
    #ifdef __EMSCRIPTEN__
        "/assets/tunisia_pois.db"
    #elif defined(__arm__) || defined(__aarch64__)
        "/usr/share/infotainment/assets/tunisia_pois.db"
    #else
        "../assets/tunisia_pois.db"
    #endif
    );
    aroma_map_set_center(state.map_node, 36.8625, 10.1956);
    aroma_map_set_animations_enabled(state.map_node, false);
    
    aroma_node_set_z_index(state.map_node, Z_LAYER_STATUS_BAR + 11);
    state.map_close_btn = aroma_ui_iconbutton(app_definitions[0].app_root, AROMA_ICON_CLOSE, 20, 20, 48, ICON_BUTTON_FILLED, close_maps, app_definitions[0].app_root, state.icon_font);
    aroma_node_set_z_index(state.map_close_btn, Z_LAYER_STATUS_BAR + 20);
    aroma_node_set_hidden(state.map_node, true);
    aroma_node_set_hidden(state.map_close_btn, true);

    for (int i = 0; i < NUM_POI_CATEGORIES; i++)
        category_enabled[i] = false;

    category_enabled[0] = true;
    category_enabled[5] = true;
    category_enabled[10] = true;
    category_enabled[11] = true;
    category_enabled[12] = true;

    last_center_lat = 0.0;
    last_center_lon = 0.0;
    last_zoom = 0.0;
    poi_update_counter = 0;
    current_page = 0;
    total_pages_suggestions = 1;
    poi_refresh_forced = true;

    AromaNode *map_options_btn = aroma_ui_iconbutton(
        app_definitions[0].app_root, AROMA_ICON_MORE_VERT, WIN_W - 70, 20, 48, ICON_BUTTON_FILLED,
        on_map_options_click, NULL, state.icon_font);
    aroma_node_set_z_index(map_options_btn, Z_LAYER_STATUS_BAR + 20);

    map_options_card = aroma_ui_card(
        app_definitions[0].app_root, WIN_W - 320, 80, 300, 200, CARD_TYPE_ELEVATED);
    aroma_node_set_z_index(map_options_card, Z_LAYER_STATUS_BAR + 21);
    aroma_node_set_hidden(map_options_card, true);

    AromaNode *map_options_close_btn = aroma_ui_iconbutton(
        map_options_card, AROMA_ICON_CLOSE, 260, 8, 32, ICON_BUTTON_OUTLINED,
        on_map_options_close_click, NULL, state.icon_font);
    aroma_node_set_z_index(map_options_close_btn, Z_LAYER_STATUS_BAR + 22);

    AromaNode *map_options_title = aroma_ui_label(
        map_options_card, "Map Options", 16, 16, LABEL_STYLE_LABEL_MEDIUM, state.settings_font);
    aroma_node_set_z_index(map_options_title, Z_LAYER_STATUS_BAR + 22);

    AromaNode *satellite_switch = aroma_ui_switch(
        map_options_card, 210, 60, 60, 30,
        false, on_satellite_switch_changed, NULL);
    aroma_node_set_z_index(satellite_switch, Z_LAYER_STATUS_BAR + 22);

    AromaNode *satellite_label = aroma_ui_label(
        map_options_card, "Satellite View", 16, 65, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(satellite_label, Z_LAYER_STATUS_BAR + 22);
    
    AromaNode *pois_switch = aroma_ui_switch(
        map_options_card, 210, 110, 60, 30,
        true, on_pois_switch_changed, NULL);
    aroma_node_set_z_index(pois_switch, Z_LAYER_STATUS_BAR + 22);
    AromaNode *pois_label = aroma_ui_label(
        map_options_card, "Show POIs", 16, 115, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(pois_label, Z_LAYER_STATUS_BAR + 22);

    map_search_surface = aroma_ui_card(app_definitions[0].app_root, 0, 80, 340, 520, CARD_TYPE_ELEVATED);
    aroma_node_set_z_index(map_search_surface, Z_LAYER_STATUS_BAR + 15);
    aroma_node_set_hidden(map_search_surface, true);

    map_search_placeholder_label = aroma_ui_label(
        map_search_surface, "Search here", 60, 18, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(map_search_placeholder_label, Z_LAYER_STATUS_BAR + 16);

    map_search_back_btn = aroma_ui_iconbutton(
        map_search_surface, AROMA_ICON_ARROW_BACK, 8, 8, 40, ICON_BUTTON_OUTLINED,
        on_search_back_click, NULL, state.icon_font);
    aroma_iconbutton_set_colors(map_search_back_btn, GMAPS_COLOR_SURFACE, GMAPS_COLOR_ON_SURFACE_VARIANT);
    aroma_node_set_z_index(map_search_back_btn, Z_LAYER_STATUS_BAR + 16);
    aroma_node_set_hidden(map_search_back_btn, true);

    AromaNode *dir_divider = aroma_ui_divider(map_search_surface, 16, 64, 308, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_z_index(dir_divider, Z_LAYER_STATUS_BAR + 16);

    map_from_entry = aroma_ui_textbox(map_search_surface, 15, 72, 280, 40, "Search from...", on_from_text_changed, NULL, state.ui_font);
    aroma_textbox_enable_virtual_keyboard(map_from_entry, true);
    aroma_node_set_z_index(map_from_entry, Z_LAYER_STATUS_BAR + 16);

    map_to_entry = aroma_ui_textbox(map_search_surface, 15, 122, 280, 40, "Search to...", on_to_text_changed, NULL, state.ui_font);
    aroma_textbox_enable_virtual_keyboard(map_to_entry, true);
    aroma_node_set_z_index(map_to_entry, Z_LAYER_STATUS_BAR + 16);

    map_go_btn = aroma_ui_button(map_search_surface, "Directions", 16, 172, 308, 40, on_go_click, NULL, state.settings_font);
    aroma_node_set_z_index(map_go_btn, Z_LAYER_STATUS_BAR + 16);

    AromaNode *preset_title = aroma_ui_label(map_search_surface, "Presets", 16, 232, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(preset_title, Z_LAYER_STATUS_BAR + 16);

    AromaNode* preset_listview = aroma_listview_create(map_search_surface, 16, 260, 308, 240);
    aroma_listview_add_item_with_icon(preset_listview, "Home", "", AROMA_ICON_HOME, NULL);
    aroma_listview_add_item_with_icon(preset_listview, "Parad'Ice", "", AROMA_ICON_LOCAL_DINING, NULL);
    aroma_listview_add_item_with_icon(preset_listview, "ISI", "", AROMA_ICON_BOOK, NULL);
    aroma_listview_add_item_with_icon(preset_listview , "Agile", "", AROMA_ICON_LOCAL_GAS_STATION, NULL);
    aroma_listview_set_font(preset_listview, state.ui_font);
    aroma_listview_set_icon_font(preset_listview, state.icon_font);
    aroma_node_set_z_index(preset_listview, Z_LAYER_STATUS_BAR + 16);
    aroma_listview_set_callback(preset_listview, on_preset_item_click, NULL);

    suggestion_page = aroma_ui_card(app_definitions[0].app_root, 320, 80, 704, 520, CARD_TYPE_ELEVATED);
    aroma_node_set_hidden(suggestion_page, true);
    aroma_node_set_z_index(suggestion_page, Z_LAYER_STATUS_BAR + 40);

    AromaNode *suggestion_title = aroma_ui_label(suggestion_page, "Select Location", 280, 10, LABEL_STYLE_LABEL_MEDIUM, state.settings_font);
    aroma_node_set_z_index(suggestion_title, Z_LAYER_STATUS_BAR + 41);

    for (int slot = 0; slot < ITEMS_PER_PAGE; slot++)
    {
        int card_y = 50 + slot * 55;

        AromaNode *card = aroma_ui_card(suggestion_page, 16, card_y, 672, 50, CARD_TYPE_ELEVATED);
        aroma_node_set_z_index(card, Z_LAYER_STATUS_BAR + 41);

        AromaNode *name_label = aroma_ui_label(card, "", 10, 5, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
        AromaNode *desc_label = aroma_ui_label(card, "", 10, 25, LABEL_STYLE_LABEL_SMALL, state.ui_font);
        aroma_label_set_color(desc_label, GMAPS_COLOR_ON_SURFACE_VARIANT);
        aroma_node_set_z_index(name_label, Z_LAYER_STATUS_BAR + 42);
        aroma_node_set_z_index(desc_label, Z_LAYER_STATUS_BAR + 42);

        suggestion_slot_contexts[slot].state = &map_nav;
        suggestion_slot_contexts[slot].slot_index = slot;

        AromaNode *pick_btn = aroma_ui_button_with_icon(card, "Pick", 520, 8, 90, 34,
                                              on_suggestion_pick, &suggestion_slot_contexts[slot], state.ui_font, AROMA_ICON_CHECK, state.icon_font);
        aroma_node_set_z_index(pick_btn, Z_LAYER_STATUS_BAR + 42);

        aroma_node_set_hidden(card, true);

        suggestion_cards[slot] = card;
        suggestion_name_labels[slot] = name_label;
        suggestion_desc_labels[slot] = desc_label;
        suggestion_pick_buttons[slot] = pick_btn;
    }
#
    page_label_suggestions = aroma_ui_label(suggestion_page, "Page 1/1", 320, 490, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(page_label_suggestions, Z_LAYER_STATUS_BAR + 41);

    AromaNode *prev_btn = aroma_ui_button_with_icon(suggestion_page, "Prev", 110, 400, 80, 50, on_prev_page, NULL, state.ui_font, AROMA_ICON_ARROW_LEFT, state.icon_font);
    AromaNode *next_btn = aroma_ui_button_with_icon(suggestion_page, "Next", 290, 400, 80, 50, on_next_page, NULL, state.ui_font, AROMA_ICON_ARROW_RIGHT, state.icon_font);
    AromaNode *close_btn = aroma_ui_button_with_icon(suggestion_page, "Close", 470, 400, 80, 50, on_close_suggestions, NULL, state.ui_font, AROMA_ICON_CLOSE, state.icon_font);
    aroma_node_set_z_index(prev_btn, Z_LAYER_STATUS_BAR + 41);
    aroma_node_set_z_index(next_btn, Z_LAYER_STATUS_BAR + 41);
    aroma_node_set_z_index(close_btn, Z_LAYER_STATUS_BAR + 41);

    nav_banner_card = aroma_ui_card(app_definitions[0].app_root, 80, 10, 864, 80, CARD_TYPE_ELEVATED);
    nav_turn_icon = aroma_ui_icon(nav_banner_card, AROMA_ICON_ARROW_UPWARD, 30, 20, 40, GMAPS_COLOR_PRIMARY, state.icon_font);
    nav_banner_label = aroma_ui_label(nav_banner_card, "Starting navigation...", 65, 10, LABEL_STYLE_LABEL_MEDIUM, state.settings_font);
    nav_banner_sub = aroma_ui_label(nav_banner_card, "", 65, 50, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_hidden(nav_banner_card, true);
    aroma_node_set_z_index(nav_banner_card, Z_LAYER_STATUS_BAR + 30);
    aroma_node_set_z_index(nav_turn_icon, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(nav_banner_label, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(nav_banner_sub, Z_LAYER_STATUS_BAR + 31);
    nav_bottom_card = aroma_ui_card(app_definitions[0].app_root, 80, WIN_H - 90, 864, 80, CARD_TYPE_ELEVATED);
    AromaNode *eta_icon = aroma_ui_icon(nav_bottom_card, AROMA_ICON_SCHEDULE, 45, 32, 28, 0xFF00C853, state.icon_font);
    nav_eta_label = aroma_ui_label(nav_bottom_card, "-- min", 80, 32, LABEL_STYLE_LABEL_MEDIUM, state.settings_font);
    AromaNode *dist_icon2 = aroma_ui_icon(nav_bottom_card, AROMA_ICON_PLACE, 230, 32, 28, 0xFFFF6D00, state.icon_font);
    nav_dist_label = aroma_ui_label(nav_bottom_card, "-- km", 265, 32, LABEL_STYLE_LABEL_MEDIUM, state.settings_font);
    AromaNode *speed_icon = aroma_ui_icon(nav_bottom_card, AROMA_ICON_GRAPHIC_EQ, 420, 32, 28, 0xFF2979FF, state.icon_font);
    nav_speed_label = aroma_ui_label(nav_bottom_card, "-- km/h", 455, 32, LABEL_STYLE_LABEL_MEDIUM, state.settings_font);
    AromaNode *turn_dist_icon = aroma_ui_icon(nav_bottom_card, AROMA_ICON_NAVIGATION, 595, 32, 28, 0xFFD50000, state.icon_font);
    nav_turn_dist_label = aroma_ui_label(nav_bottom_card, "--", 630, 32, LABEL_STYLE_LABEL_MEDIUM, state.settings_font);
    aroma_node_set_hidden(nav_bottom_card, true);
    aroma_node_set_z_index(nav_bottom_card, Z_LAYER_STATUS_BAR + 30);
    aroma_node_set_z_index(eta_icon, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(nav_eta_label, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(dist_icon2, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(nav_dist_label, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(speed_icon, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(nav_speed_label, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(turn_dist_icon, Z_LAYER_STATUS_BAR + 31);
    aroma_node_set_z_index(nav_turn_dist_label, Z_LAYER_STATUS_BAR + 31);
    state.phone_node = aroma_ui_container(
        app_definitions[1].app_root, 0, 0, 48, 48,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.phone_node, Z_LAYER_STATUS_BAR + 11);
    aroma_node_set_hidden(state.phone_node, true);

    state.phone_close_btn = aroma_ui_iconbutton(
        app_definitions[1].app_root, AROMA_ICON_CLOSE, 20, 20, 48, ICON_BUTTON_FILLED,
        close_phone, app_definitions[1].app_root, state.icon_font);
    aroma_node_set_z_index(state.phone_close_btn, Z_LAYER_STATUS_BAR + 16);
    aroma_node_set_hidden(state.phone_close_btn, true);

    state.contact_listview = aroma_ui_listview(
        app_definitions[1].app_root, 16, 110, 988, 380,
        on_contact_click, NULL, state.ui_font);
    aroma_listview_set_icon_font(state.contact_listview, state.big_icon_font);
    aroma_node_set_z_index(state.contact_listview, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(state.contact_listview, true);

    dialer_card = aroma_ui_card(app_definitions[1].app_root, 16, 110, 988, 460, CARD_TYPE_OUTLINED);
    aroma_node_set_z_index(dialer_card, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(dialer_card, true);

    dialer_display_label = aroma_ui_label(dialer_card, "Enter number",
                                          430, 50, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(dialer_display_label, Z_LAYER_STATUS_BAR + 13);

    AromaNode *dialer_grid = aroma_ui_container(
        dialer_card, (988 - 280) / 2, 100, 280, 290,
        AROMA_LAYOUT_MODE_GRID, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_z_index(dialer_grid, Z_LAYER_STATUS_BAR + 13);
    aroma_node_set_grid_cols(dialer_grid, 3);
    aroma_node_set_grid_rows(dialer_grid, 5);
    aroma_node_set_gap(dialer_grid, 12);

    const int btn_size = 72;
    const char *dialer_digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};

    for (int i = 0; i < 12; i++)
    {
        AromaNode *btn = aroma_ui_button(
            dialer_grid, dialer_digits[i],
            0, 0, btn_size, btn_size,
            on_dialer_button_click,
            (void *)dialer_digits[i], state.settings_font);
        aroma_node_set_z_index(btn, Z_LAYER_STATUS_BAR + 14);
        aroma_iconbutton_set_colors(btn, 0xFF424242, 0xFFFFFFFF);
    }

    AromaNode *del_btn = aroma_ui_button(
        dialer_grid, AROMA_ICON_BACKSPACE,
        0, 0, btn_size, btn_size,
        on_dialer_delete_click_icon, NULL, state.icon_font);
    aroma_node_set_z_index(del_btn, Z_LAYER_STATUS_BAR + 14);
    aroma_iconbutton_set_colors(del_btn, 0xFF424242, 0xFFFFFFFF);

    AromaNode *call_btn = aroma_ui_button(
        dialer_grid, AROMA_ICON_CALL,
        0, 0, btn_size, btn_size,
        on_dialer_call_click_icon, NULL, state.icon_font);
    aroma_node_set_z_index(call_btn, Z_LAYER_STATUS_BAR + 14);
    aroma_iconbutton_set_colors(call_btn, 0xFF4CAF50, 0xFFFFFFFF);

    pagination_card = aroma_ui_card(app_definitions[1].app_root, 800, 540, 200, 50, CARD_TYPE_FILLED);
    aroma_node_set_z_index(pagination_card, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(pagination_card, true);

    prev_page_btn = aroma_ui_iconbutton(
        pagination_card, AROMA_ICON_ARROW_BACK,
        16, 8, 34, ICON_BUTTON_OUTLINED,
        on_prev_page_click, NULL, state.icon_font);
    aroma_node_set_z_index(prev_page_btn, Z_LAYER_STATUS_BAR + 13);

    page_label = aroma_ui_label(
        pagination_card, "1/1",
        70, 12, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(page_label, Z_LAYER_STATUS_BAR + 13);

    next_page_btn = aroma_ui_iconbutton(
        pagination_card, AROMA_ICON_ARROW_FORWARD,
        130, 8, 34, ICON_BUTTON_OUTLINED,
        on_next_page_click, NULL, state.icon_font);
    aroma_node_set_z_index(next_page_btn, Z_LAYER_STATUS_BAR + 13);

    state.phone_app_tabs = aroma_ui_tabs_with_icons(
        app_definitions[1].app_root, 0, 0, 1024, 50,
        (const char *[]){"Contacts", "Dialer"},
        (const char *[]){AROMA_ICON_CONTACTS, AROMA_ICON_DIALER_SIP},
        2, on_tab_changed, NULL, state.settings_font, state.big_icon_font);
    aroma_node_set_z_index(state.phone_app_tabs, Z_LAYER_STATUS_BAR + 15);
    aroma_node_set_hidden(state.phone_app_tabs, true);

    AromaNode *content[] = {state.contact_listview, dialer_card};
    aroma_tabs_set_content(state.phone_app_tabs, 0, content, 2);

    AromaNode *music_close_btn = aroma_ui_iconbutton(
        app_definitions[2].app_root, AROMA_ICON_CLOSE, 20, 20, 48, ICON_BUTTON_FILLED,
        close_music, app_definitions[2].app_root, state.icon_font);
    aroma_node_set_z_index(music_close_btn, Z_LAYER_STATUS_BAR + 16);

    music_app_tabs = aroma_ui_tabs_with_icons(
        app_definitions[2].app_root, 0, 0, 1024, 50,
        (const char *[]){"Now Playing", "Connected Phone"},
        (const char *[]){AROMA_ICON_MUSIC_NOTE, AROMA_ICON_PHONE},
        2, on_music_tab_changed, NULL, state.settings_font, state.big_icon_font);
    aroma_node_set_z_index(music_app_tabs, Z_LAYER_STATUS_BAR + 15);
    aroma_node_set_hidden(music_app_tabs, true);

    music_now_playing_card = aroma_ui_card(
        app_definitions[2].app_root, 16, 110, 988, 460, CARD_TYPE_OUTLINED);
    aroma_node_set_z_index(music_now_playing_card, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(music_now_playing_card, true);
    AromaNode *music_art_placeholder_card = aroma_ui_card(
        music_now_playing_card, 10, 10, 960, 215, CARD_TYPE_FILLED);
    aroma_card_set_colors(music_art_placeholder_card, 0xFF2196F3, 0xFF2196F3);
    aroma_node_set_z_index(music_art_placeholder_card, Z_LAYER_STATUS_BAR + 13);
    music_art_placeholder = aroma_ui_icon(
        music_now_playing_card, AROMA_ICON_MUSIC_NOTE,
        (948) / 2, 30, 200, 0xFFFFFFFF, state.huge_icon_font);
    aroma_node_set_z_index(music_art_placeholder, Z_LAYER_STATUS_BAR + 14);

    music_track_title_label = aroma_ui_label(
        music_now_playing_card, "No Track",
        60, 250, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(music_track_title_label, Z_LAYER_STATUS_BAR + 13);

    music_track_artist_label = aroma_ui_label(
        music_now_playing_card, "No Artist",
        60, 285, LABEL_STYLE_LABEL_MEDIUM, state.settings_font);
    aroma_node_set_z_index(music_track_artist_label, Z_LAYER_STATUS_BAR + 13);
    aroma_label_set_color(music_track_artist_label, 0xFFAAAAAA);

    music_track_album_label = aroma_ui_label(
        music_now_playing_card, "",
        60, 315, LABEL_STYLE_LABEL_SMALL, state.settings_font);
    aroma_node_set_z_index(music_track_album_label, Z_LAYER_STATUS_BAR + 13);
    aroma_label_set_color(music_track_album_label, 0xFFAAAAAA);

    music_status_label = aroma_ui_label(
        music_now_playing_card, "",
        60, 345, LABEL_STYLE_LABEL_SMALL, state.settings_font);
    aroma_node_set_z_index(music_status_label, Z_LAYER_STATUS_BAR + 13);

    music_no_media_label = aroma_ui_label(
        music_now_playing_card, "No media playing",
        (988 - 220) / 2, 220, LABEL_STYLE_LABEL_MEDIUM, state.settings_font);
    aroma_node_set_z_index(music_no_media_label, Z_LAYER_STATUS_BAR + 13);
    aroma_label_set_color(music_no_media_label, 0xFFAAAAAA);
    aroma_node_set_hidden(music_no_media_label, true);

    music_prev_button = aroma_ui_iconbutton(
        music_now_playing_card, AROMA_ICON_SKIP_PREVIOUS,
        (988 - 160) / 2, 390, 44, ICON_BUTTON_OUTLINED,
        on_music_prev_click, NULL, state.icon_font);
    aroma_node_set_z_index(music_prev_button, Z_LAYER_STATUS_BAR + 13);

    music_play_pause_button = aroma_ui_iconbutton(
        music_now_playing_card, AROMA_ICON_PLAY_ARROW,
        (988 - 160) / 2 + 58, 390, 44, ICON_BUTTON_FILLED,
        on_music_play_pause_click, NULL, state.icon_font);
    aroma_node_set_z_index(music_play_pause_button, Z_LAYER_STATUS_BAR + 13);

    music_next_button = aroma_ui_iconbutton(
        music_now_playing_card, AROMA_ICON_SKIP_NEXT,
        (988 - 160) / 2 + 116, 390, 44, ICON_BUTTON_OUTLINED,
        on_music_next_click, NULL, state.icon_font);
    aroma_node_set_z_index(music_next_button, Z_LAYER_STATUS_BAR + 13);

    music_device_card = aroma_ui_card(
        app_definitions[2].app_root, 16, 110, 988, 460, CARD_TYPE_OUTLINED);
    aroma_node_set_z_index(music_device_card, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(music_device_card, true);

    AromaNode *music_device_status_bg = aroma_ui_card(
        music_device_card, 25, 30, 40, 40, CARD_TYPE_FILLED);
    aroma_node_set_z_index(music_device_status_bg, Z_LAYER_STATUS_BAR + 13);
    music_device_status_icon = aroma_ui_icon(
        music_device_status_bg, AROMA_ICON_BLUETOOTH_CONNECTED,
        27, 4, 32, 0xFF9E9E9E, state.icon_font);
    aroma_node_set_z_index(music_device_status_icon, Z_LAYER_STATUS_BAR + 14);

    music_device_status_label = aroma_ui_label(
        music_device_card, "No Phone Connected",
        85, 40, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(music_device_status_label, Z_LAYER_STATUS_BAR + 13);

    music_device_name_label = aroma_ui_label(
        music_device_card, "Name: None",
        60, 130, LABEL_STYLE_LABEL_MEDIUM, state.settings_font);
    aroma_node_set_z_index(music_device_name_label, Z_LAYER_STATUS_BAR + 13);
    aroma_node_set_hidden(music_device_name_label, true);

    music_device_address_label = aroma_ui_label(
        music_device_card, "Address: None",
        60, 165, LABEL_STYLE_LABEL_SMALL, state.settings_font);
    aroma_node_set_z_index(music_device_address_label, Z_LAYER_STATUS_BAR + 13);
    aroma_label_set_color(music_device_address_label, 0xFFAAAAAA);
    aroma_node_set_hidden(music_device_address_label, true);

    music_device_stats_label = aroma_ui_label(
        music_device_card, "",
        60, 195, LABEL_STYLE_LABEL_SMALL, state.settings_font);
    aroma_node_set_z_index(music_device_stats_label, Z_LAYER_STATUS_BAR + 13);
    aroma_label_set_color(music_device_stats_label, 0xFFAAAAAA);
    aroma_node_set_hidden(music_device_stats_label, true);

    music_device_no_phone_label = aroma_ui_label(
        music_device_card, "Connect a phone via Bluetooth to see it here",
        60, 130, LABEL_STYLE_LABEL_MEDIUM, state.settings_font);
    aroma_node_set_z_index(music_device_no_phone_label, Z_LAYER_STATUS_BAR + 13);
    aroma_label_set_color(music_device_no_phone_label, 0xFFAAAAAA);

    AromaNode *music_tab_content[] = {music_now_playing_card, music_device_card};
    aroma_tabs_set_content(music_app_tabs, 0, music_tab_content, 2);

    AromaNode *settings_close_btn = aroma_ui_iconbutton(
        app_definitions[3].app_root, AROMA_ICON_CLOSE, 20, 20, 48, ICON_BUTTON_FILLED,
        close_settings, app_definitions[3].app_root, state.icon_font);
    aroma_node_set_z_index(settings_close_btn, Z_LAYER_STATUS_BAR + 16);

    settings_sidebar = aroma_ui_sidebar_with_icons(
        app_definitions[3].app_root, 0, 80, 200, WIN_H - 80,
        (const char *[]){"General", "Display", "Updates"},
        (const char *[]){AROMA_ICON_SETTINGS, AROMA_ICON_BRIGHTNESS_6, AROMA_ICON_REFRESH},
        3, on_settings_sidebar_select, NULL, state.settings_font, state.icon_font);
    aroma_node_set_z_index(settings_sidebar, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(settings_sidebar, true);

    settings_page_general = aroma_ui_container(
        app_definitions[3].app_root, 210, 80, WIN_W - 210, WIN_H - 80,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(settings_page_general, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(settings_page_general, true);

    AromaNode *general_title = aroma_ui_label(
        settings_page_general, "General",
        80, 20, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(general_title, Z_LAYER_STATUS_BAR + 13);

    AromaNode *general_card = aroma_ui_card(
        settings_page_general, 20, 70, WIN_W - 250, 80, CARD_TYPE_FILLED);
    aroma_node_set_z_index(general_card, Z_LAYER_STATUS_BAR + 13);

    AromaNode *bt_icon = aroma_ui_icon(general_card, AROMA_ICON_BLUETOOTH, 40, 25, 32, IOS_COLOR_BLUE, state.icon_font);
    AromaNode *bt_label = aroma_ui_label(general_card, "Bluetooth", 90, 30, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    settings_bluetooth_switch = aroma_ui_switch(general_card, WIN_W - 320, 20, 60, 30, true, on_settings_bluetooth_changed, NULL);
    aroma_node_set_z_index(bt_icon, Z_LAYER_STATUS_BAR + 14);
    aroma_node_set_z_index(bt_label, Z_LAYER_STATUS_BAR + 14);
    aroma_node_set_z_index(settings_bluetooth_switch, Z_LAYER_STATUS_BAR + 14);

    bt_info_card = aroma_ui_card(
        settings_page_general, 20, 180, WIN_W - 250, 120, CARD_TYPE_FILLED);
    aroma_node_set_z_index(bt_info_card, Z_LAYER_STATUS_BAR + 13);
    aroma_node_set_hidden(bt_info_card, true);

    AromaNode *bt_info_icon = aroma_ui_icon(bt_info_card, AROMA_ICON_BLUETOOTH_CONNECTED, 30, 15, 32, IOS_COLOR_BLUE, state.icon_font);
    aroma_node_set_z_index(bt_info_icon, Z_LAYER_STATUS_BAR + 14);

    bt_info_status_label = aroma_ui_label(bt_info_card, "Connected", 80, 20, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(bt_info_status_label, Z_LAYER_STATUS_BAR + 14);

    bt_info_name_label = aroma_ui_label(bt_info_card, "Name: None", 30, 55, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(bt_info_name_label, Z_LAYER_STATUS_BAR + 14);

    bt_info_address_label = aroma_ui_label(bt_info_card, "Address: None", 30, 80, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(bt_info_address_label, Z_LAYER_STATUS_BAR + 14);

    settings_page_display = aroma_ui_container(
        app_definitions[3].app_root, 210, 80, WIN_W - 210, WIN_H - 80,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(settings_page_display, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(settings_page_display, true);

    AromaNode *display_title = aroma_ui_label(
        settings_page_display, "Display",
        80, 20, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(display_title, Z_LAYER_STATUS_BAR + 13);

    AromaNode *display_card = aroma_ui_card(
        settings_page_display, 20, 70, WIN_W - 250, 80, CARD_TYPE_FILLED);
    aroma_node_set_z_index(display_card, Z_LAYER_STATUS_BAR + 13);

    AromaNode *dark_mode_icon = aroma_ui_icon(display_card, AROMA_ICON_PALETTE, 40, 25, 32, IOS_COLOR_PURPLE, state.icon_font);
    AromaNode *dark_mode_label = aroma_ui_label(display_card, "Dark Mode", 90, 30, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    settings_dark_mode_switch = aroma_ui_switch(display_card, WIN_W - 320, 20, 60, 30, true, on_dark_mode_switch_changed, NULL);
    aroma_node_set_z_index(dark_mode_icon, Z_LAYER_STATUS_BAR + 14);
    aroma_node_set_z_index(dark_mode_label, Z_LAYER_STATUS_BAR + 14);
    aroma_node_set_z_index(settings_dark_mode_switch, Z_LAYER_STATUS_BAR + 14);

    settings_page_updates = aroma_ui_container(
        app_definitions[3].app_root, 210, 80, WIN_W - 210, WIN_H - 80,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(settings_page_updates, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(settings_page_updates, true);

    AromaNode *updates_title = aroma_ui_label(
        settings_page_updates, "About & Updates",
        80, 20, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(updates_title, Z_LAYER_STATUS_BAR + 13);

    AromaNode *updates_card = aroma_ui_card(
        settings_page_updates, 20, 70, WIN_W - 250, 340, CARD_TYPE_FILLED);
    aroma_node_set_z_index(updates_card, Z_LAYER_STATUS_BAR + 13);

    AromaNode *logo_icon = aroma_ui_icon(updates_card, AROMA_ICON_MEMORY, (WIN_W - 200) / 2 , 20, 60, IOS_COLOR_BLUE, state.huge_icon_font);
    aroma_node_set_z_index(logo_icon, Z_LAYER_STATUS_BAR + 14);

    AromaNode *os_name_label = aroma_ui_label(updates_card, "Aroma OS", (WIN_W - 350) / 2, 100, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(os_name_label, Z_LAYER_STATUS_BAR + 14);

    AromaNode *swupdate_icon = aroma_ui_icon(updates_card, AROMA_ICON_FILE_DOWNLOAD, 60, 175, 32, IOS_COLOR_BLUE, state.icon_font);
    AromaNode *swupdate_label = aroma_ui_label(updates_card, "SWUpdate 2023.12", 90, 180, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(swupdate_icon, Z_LAYER_STATUS_BAR + 14);
    aroma_node_set_z_index(swupdate_label, Z_LAYER_STATUS_BAR + 14);


    settings_swupdate_status_label = aroma_ui_label(updates_card, "SWUpdate: Stopped", 40, 265, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(settings_swupdate_status_label, Z_LAYER_STATUS_BAR + 14);

    settings_swupdate_port_label = aroma_ui_label(updates_card, "Port: --", 40, 285, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(settings_swupdate_port_label, Z_LAYER_STATUS_BAR + 14);

    settings_ota_progress_bar = aroma_ui_progressbar(updates_card, 40, 310, WIN_W - 330, 12, PROGRESS_TYPE_DETERMINATE, 0.0f);
    aroma_node_set_hidden(settings_ota_progress_bar, true);

    settings_ota_progress_label = aroma_ui_label(updates_card, "0%", WIN_W - 300, 308, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(settings_ota_progress_label, Z_LAYER_STATUS_BAR + 14);
    aroma_node_set_hidden(settings_ota_progress_label, true);

    update_ota_display();
    update_swupdate_service_status();

    incoming_call_overlay = aroma_ui_card(
        state.vehicle_view_root, 0, 0, WIN_W, WIN_H, CARD_TYPE_FILLED);
    aroma_node_set_z_index(incoming_call_overlay, Z_LAYER_VOICE_CARD);
    aroma_card_set_colors(incoming_call_overlay, 0xDD000000, 0xDD000000);
    aroma_node_set_hidden(incoming_call_overlay, true);

    incoming_call_name_label = aroma_ui_label(
        incoming_call_overlay, "Incoming Call",
        WIN_W / 2 - 200, 150, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(incoming_call_name_label, Z_LAYER_VOICE_CONTENT);
    aroma_label_set_color(incoming_call_name_label, 0xFFFFFFFF);

    incoming_call_number_label = aroma_ui_label(
        incoming_call_overlay, "",
        WIN_W / 2 - 150, 220, LABEL_STYLE_LABEL_MEDIUM, state.settings_font);
    aroma_node_set_z_index(incoming_call_number_label, Z_LAYER_VOICE_CONTENT);
    aroma_label_set_color(incoming_call_number_label, 0xFFAAAAAA);

    incoming_call_accept_btn = aroma_ui_iconbutton(
        incoming_call_overlay, AROMA_ICON_CALL,
        WIN_W / 2 - 120, 320, 80, ICON_BUTTON_FILLED,
        on_accept_call_click, NULL, state.icon_font);
    aroma_node_set_z_index(incoming_call_accept_btn, Z_LAYER_VOICE_CONTENT);
    aroma_iconbutton_set_colors(incoming_call_accept_btn, 0xFF4CAF50, 0xFFFFFFFF);

    incoming_call_reject_btn = aroma_ui_iconbutton(
        incoming_call_overlay, AROMA_ICON_CALL_END,
        WIN_W / 2 + 40, 320, 80, ICON_BUTTON_FILLED,
        on_reject_call_click, NULL, state.icon_font);
    aroma_node_set_z_index(incoming_call_reject_btn, Z_LAYER_VOICE_CONTENT);
    aroma_iconbutton_set_colors(incoming_call_reject_btn, 0xFFF44336, 0xFFFFFFFF);

    incoming_call_end_btn = aroma_ui_iconbutton(
        incoming_call_overlay, AROMA_ICON_CALL_END,
        WIN_W / 2 - 40, 320, 80, ICON_BUTTON_FILLED,
        on_end_call_click, NULL, state.icon_font);
    aroma_node_set_z_index(incoming_call_end_btn, Z_LAYER_VOICE_CONTENT);
    aroma_iconbutton_set_colors(incoming_call_end_btn, 0xFFF44336, 0xFFFFFFFF);
    aroma_node_set_hidden(incoming_call_end_btn, true);

    aroma_animation_start(state.vehicle_view_frunk_divider, AROMA_ANIM_SCALE_Y, 0, 40, 1200);
    aroma_animation_start(state.vehicle_view_trunk_divider, AROMA_ANIM_SCALE_Y, 0, 40, 1200);
    aroma_animation_start(state.vehicle_view_lock_divider, AROMA_ANIM_SCALE_Y, 0, 40, 1200);
    aroma_animation_start(state.vehicle_view_charge_port_divider, AROMA_ANIM_SCALE_X, 0, 20, 1200);

    media_ui.ui_initialized = true;

    bt_config_t config = {
        .device_name = "Aroma Infotainment",
        .pin_code = "0000",
        .verbose = true,
        .state_cb = on_bt_state_changed,
        .state_cb_data = NULL,
        .device_cb = on_bt_device_changed,
        .device_cb_data = NULL,
        .error_cb = on_bt_error,
        .error_cb_data = NULL,
        .audio_cb = on_bt_audio_changed,
        .audio_cb_data = NULL,
        .log_cb = on_bt_log,
        .log_cb_data = NULL,
        .avrcp_cb = on_bt_avrcp_changed,
        .avrcp_cb_data = NULL,
    };
    if (bt_speaker_init(&config) != 0)
    {
        fprintf(stderr, "[BT] init failed: %s\n",
                bt_speaker_get_last_error_message());
    }
    else if (bt_speaker_get_state() != BT_STATE_ADVERTISING)
    {
        fprintf(stderr,
                "[BT] warning: state is '%s' after init, not advertising — "
                "pairing attempts may be rejected with no visible error "
                "until the agent registers.\n",
                bt_speaker_get_state_string());
    }
    bt_hfp_init();
    bt_hfp_set_call_callback(on_bt_call_changed, NULL);
    g_bt_initialized = true;
    bt_speaker_start();

    pthread_t media_thread;
    pthread_attr_t media_attr;
    pthread_attr_init(&media_attr);
    pthread_attr_setdetachstate(&media_attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&media_thread, &media_attr, media_monitor_thread_func, NULL);
    pthread_attr_destroy(&media_attr);

    build_lock_screen(window);
}

void update_vehicle_view(void)
{
    if (lock_screen_active)
    {
        update_lock_screen_clock();
        return;
    }

    bt_hfp_poll();

    if (maps_screen_open && state.map_node && !map_nav.navigation_active)
    {
        update_pois_markers();
    }

    if (map_nav.simulation_started && map_nav.route_ready && map_nav.route_point_count > 1)
    {
        map_nav.frame++;

        if (map_nav.seg_index < map_nav.route_point_count - 1)
        {
            double dist_remaining_estimate = 0.0;
            for (int i = map_nav.seg_index; i < map_nav.route_point_count - 1; i++)
                dist_remaining_estimate += nav_haversine_m(map_nav.path_lat[i], map_nav.path_lon[i],
                                                            map_nav.path_lat[i + 1], map_nav.path_lon[i + 1]);
            dist_remaining_estimate -= map_nav.seg_progress_m;

            double target_speed = 35.0;
            if (map_nav.frame < 90) target_speed = 35.0 * (map_nav.frame / 90.0);
            if (dist_remaining_estimate < 60.0) target_speed = fmin(target_speed, 35.0 * (dist_remaining_estimate / 60.0));
            if (target_speed < 5.0 && dist_remaining_estimate > 2.0) target_speed = 5.0;
            map_nav.speed += (target_speed - map_nav.speed) * 0.05;
            if (map_nav.speed < 0.0) map_nav.speed = 0.0;

            double meters_per_frame = (map_nav.speed * 1000.0 / 3600.0) / 60.0;
            map_nav.seg_progress_m += meters_per_frame;

            while (map_nav.seg_progress_m >= map_nav.seg_length_m && map_nav.seg_index < map_nav.route_point_count - 2)
            {
                map_nav.seg_progress_m -= map_nav.seg_length_m;
                map_nav.seg_index++;
                map_nav.seg_length_m = nav_haversine_m(map_nav.path_lat[map_nav.seg_index], map_nav.path_lon[map_nav.seg_index],
                                                      map_nav.path_lat[map_nav.seg_index + 1], map_nav.path_lon[map_nav.seg_index + 1]);
            }

            double t = (map_nav.seg_length_m > 0.0001) ? (map_nav.seg_progress_m / map_nav.seg_length_m) : 0.0;
            if (t > 1.0) t = 1.0;

            double a_lat = map_nav.path_lat[map_nav.seg_index], a_lon = map_nav.path_lon[map_nav.seg_index];
            double b_lat = map_nav.path_lat[map_nav.seg_index + 1], b_lon = map_nav.path_lon[map_nav.seg_index + 1];

            map_nav.current_lat = a_lat + (b_lat - a_lat) * t;
            map_nav.current_lon = a_lon + (b_lon - a_lon) * t;

            double raw_bearing = nav_bearing_deg(a_lat, a_lon, b_lat, b_lon);
            if (!map_nav.have_heading)
            {
                map_nav.display_heading = raw_bearing;
                map_nav.have_heading = true;
            }
            else
                map_nav.display_heading += nav_shortest_angle_diff(map_nav.display_heading, raw_bearing) * 0.15;
            map_nav.display_heading = fmod(map_nav.display_heading + 360.0, 360.0);

            aroma_map_set_gps_position(state.map_node, map_nav.current_lat, map_nav.current_lon,
                                       map_nav.display_heading, map_nav.speed);
            aroma_map_set_center(state.map_node, map_nav.current_lat, map_nav.current_lon);

            if (map_nav.reroute_cooldown_frames > 0)
                map_nav.reroute_cooldown_frames--;

            double off_route_distance = nav_min_distance_to_route_m(map_nav.path_lat, map_nav.path_lon,
                                                                    map_nav.route_point_count,
                                                                    map_nav.current_lat, map_nav.current_lon);
            if (off_route_distance > OFF_ROUTE_THRESHOLD_M)
                map_nav.off_route_counter++;
            else
                map_nav.off_route_counter = 0;

            if (map_nav.off_route_counter >= OFF_ROUTE_CONFIRM_FRAMES && map_nav.reroute_cooldown_frames == 0)
            {
                bool rerouted = recalculate_route_from_current_position();
                if (rerouted)
                {
                    aroma_label_set_text(nav_banner_label, "Route recalculated");
                    aroma_label_set_text(nav_banner_sub, "You are back on route");
                }
                else
                {
                    aroma_label_set_text(nav_banner_label, "Recalculate failed");
                    aroma_label_set_text(nav_banner_sub, "Keep driving to recover signal");
                    map_nav.reroute_cooldown_frames = RE_ROUTE_COOLDOWN_FRAMES;
                    map_nav.off_route_counter = 0;
                }
            }

            bool reached_end = (map_nav.seg_index >= map_nav.route_point_count - 2 && t >= 1.0);

            if (map_nav.frame % 15 == 0 || reached_end)
            {
                aroma_map_clear_markers(state.map_node);
                aroma_map_add_popup_marker(state.map_node, map_nav.to_lat, map_nav.to_lon, GMAPS_COLOR_DESTINATION, "Destination");
                aroma_map_add_marker(state.map_node, map_nav.current_lat, map_nav.current_lon, GMAPS_COLOR_PRIMARY);
            }

            if (map_nav.frame % 15 == 0)
            {
                update_navigation_display();
            }

            if (reached_end)
            {
                aroma_label_set_text(nav_banner_label, "Arrived");
                aroma_label_set_text(nav_banner_sub, "Destination reached");
                aroma_icon_set_text(nav_turn_icon, AROMA_ICON_PLACE, state.icon_font);
                map_nav.simulation_started = false;
                map_nav.navigation_active = false;
                map_nav.active = false;
                aroma_map_clear_markers(state.map_node);
                aroma_node_set_hidden(nav_banner_card, true);
                aroma_node_set_hidden(nav_bottom_card, true);
                aroma_node_set_hidden(map_search_surface, false);
                map_search_expanded = true;
                aroma_node_set_hidden(map_route_sheet, false);
                aroma_node_set_hidden(map_end_nav_btn, false);
            }
        }
    }
}