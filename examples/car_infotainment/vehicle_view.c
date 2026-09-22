#include "theme_manager.h"
#include "vehicle_view.h"
#include "setup_store.h"

#include "app_state.h"
#include "aroma_animation.h"
#include "apps/phone/bt_speaker_api.h"
#include "apps/phone/bt_speaker_hfp.h"
#include "widgets/aroma_loading.h"
#include "widgets/aroma_gauge.h"
#include "lock_screen.h"
#include "vehicle_camera.h"
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
#define CONTACTS_RETRY_INTERVAL_SEC 5
#define MAX_CONTACTS_RETRIES 20
#define MIN_EMPTY_RESULT_RETRIES 3

const char *resolve_asset_path(const char *filename)
{
    static char resolved[512];
    // 1. Try the path exactly as given (handles absolute paths and
    //    correct relative paths regardless of CWD).
    FILE *f = fopen(filename, "rb");
    if (f)
    {
        fclose(f);
        snprintf(resolved, sizeof(resolved), "%s", filename);
        return resolved;
    }

    // 2. Fall back to the bare basename in the known asset locations.
    //    This covers running from either the example dir (assets/...)
    //    or its build dir (../assets/...).
    const char *basename = strrchr(filename, '/');
    if (basename)
        basename++;
    else
        basename = filename;

    const char *prefixes[] = {
        "assets/",
        "../assets/",
        "examples/car_infotainment/assets/",
        "/usr/share/infotainment/assets/",
        NULL};

    for (int i = 0; prefixes[i]; i++)
    {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", prefixes[i], basename);
        f = fopen(path, "rb");
        if (f)
        {
            fclose(f);
            snprintf(resolved, sizeof(resolved), "%s", path);
            return resolved;
        }
    }
    // Not found anywhere: return the original so the caller logs a
    // meaningful path instead of a doubly-concatenated one.
    return filename;
}

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

double monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static bool dark_mode_enabled = true;
static AromaNode *settings_dark_mode_switch = NULL;

bt_state_t g_bt_state = BT_STATE_IDLE;
bt_device_info_t g_bt_device_info = {0};
bt_media_info_t g_bt_media_info = {0};
bt_stats_t g_bt_stats = {0};
bool g_bt_initialized = false;
bool g_bt_connected = false;
pthread_mutex_t g_bt_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t contact_fetch_mutex = PTHREAD_MUTEX_INITIALIZER;


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
bool app_drawer_visible = false;
static bool app_drawer_behind_app = false;
#define APP_DRAWER_Z_INDEX (Z_LAYER_STATUS_BAR + 1)
/* Frosted-glass blur for the main-menu (app drawer) background. Stronger
 * than the default card frost so the home screen reads through softly. */
#define AROMA_DRAWER_FROST_RADIUS 20.0f

void send_app_drawer_behind(void)
{
    if (!app_drawer)
        return;
    aroma_node_set_z_index(app_drawer, Z_LAYER_STATUS_BAR + 5);
    app_drawer_visible = true;
    app_drawer_behind_app = true;
}

void restore_app_drawer_from_behind(void)
{
    if (!app_drawer_behind_app)
        return;
    app_drawer_behind_app = false;
    if (!app_drawer)
        return;
    aroma_node_set_z_index(app_drawer, APP_DRAWER_Z_INDEX);
    app_drawer_visible = true;
}

static void update_clock_gauge_colors(void);

void apply_theme_colors(void)
{
    if (dark_mode_enabled)
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

    update_clock_gauge_colors();
}

static void update_clock_gauge_colors(void)
{
    if (!state.vehicle_view_clock_gauge)
        return;

    if (dark_mode_enabled)
    {
        aroma_gauge_set_colors(state.vehicle_view_clock_gauge, 0x33FFFFFF, 0x00FFFFFF);
        aroma_gauge_set_ticks(state.vehicle_view_clock_gauge, true, 12, 5, 10, 6, 0xFFFFFFFF, 2);
        aroma_gauge_set_needle(state.vehicle_view_clock_gauge, true, 0xFFFFFFFF, 4);
        aroma_gauge_set_secondary_hand(state.vehicle_view_clock_gauge, true, 0xFFFF4444, 2, 0.85f);
        aroma_gauge_set_extra_hand(state.vehicle_view_clock_gauge, true, 0xFFFFFFFF, 3, 0.55f);
        aroma_gauge_set_hub(state.vehicle_view_clock_gauge, true, 8, 0xFFFFFFFF, 2);
    }
    else
    {
        aroma_gauge_set_colors(state.vehicle_view_clock_gauge, 0x33000000, 0x00000000);
        aroma_gauge_set_ticks(state.vehicle_view_clock_gauge, true, 12, 5, 10, 6, 0xFF000000, 2);
        aroma_gauge_set_needle(state.vehicle_view_clock_gauge, true, 0xFF000000, 4);
        aroma_gauge_set_secondary_hand(state.vehicle_view_clock_gauge, true, 0xFFFF4444, 2, 0.85f);
        aroma_gauge_set_extra_hand(state.vehicle_view_clock_gauge, true, 0xFF000000, 3, 0.55f);
        aroma_gauge_set_hub(state.vehicle_view_clock_gauge, true, 8, 0xFF000000, 2);
    }
}


static bool on_dark_mode_switch_changed(AromaNode *switch_node, void *user_data)
{
    (void)user_data;
    dark_mode_enabled = aroma_switch_get_state(switch_node);
    apply_theme_colors();
    return true;
}

#include "app_registry.h"
#include "package_manager.h"
#include "aroma_incense_loader.h"

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

AromaNode *incoming_call_overlay = NULL;
AromaNode *incoming_call_name_label = NULL;
AromaNode *incoming_call_number_label = NULL;
AromaNode *incoming_call_accept_btn = NULL;
AromaNode *incoming_call_reject_btn = NULL;
AromaNode *incoming_call_end_btn = NULL;
bool call_overlay_visible = false;
char current_call_name[128] = "";
char current_call_number[64] = "";
char current_call_path[256] = "";
pthread_mutex_t call_state_lock = PTHREAD_MUTEX_INITIALIZER;


static bool on_settings_bluetooth_changed(AromaNode *switch_node, void *user_data)
{
    (void)user_data;
    bool enabled = aroma_switch_get_state(switch_node);
    return vehicle_view_set_bluetooth_enabled(enabled);
}

bool vehicle_view_is_bluetooth_enabled(void)
{
    return g_bt_initialized;
}

bool vehicle_view_set_bluetooth_enabled(bool enabled)
{
    if (enabled)
    {
        if (!g_bt_initialized)
        {
            bt_config_t config = {
                .device_name = setup_store_get("device_name", "Aroma Infotainment"),
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
        state.contacts_fetched = false;
        state.contact_count = 0;
    }

    update_bt_info_card();
    update_media_card_display();

    return true;
}



static int estimate_eta_minutes(double distance_km)
{
    return (int)((distance_km / 50.0) * 60.0) + 1;
}

void format_time_string(int minutes, char *buf, size_t size)
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

void format_distance_string(double km, char *buf, size_t size)
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

void truncate_for_listview(const char *input, char *output, size_t output_size)
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


void show_incoming_call_screen(const char *name, const char *number, const char *call_path)
{
    if (!incoming_call_overlay)
        return;

    pthread_mutex_lock(&call_state_lock);
    if (name)
        strncpy(current_call_name, name, sizeof(current_call_name) - 1);
    if (number)
        strncpy(current_call_number, number, sizeof(current_call_number) - 1);
    if (call_path)
        strncpy(current_call_path, call_path, sizeof(current_call_path) - 1);
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

void apply_deferred_bottom_bar_position(void)
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

bool is_any_app_open(void)
{
    pthread_mutex_lock(&app_open_lock);
    bool result = bottom_bar_app_open;
    pthread_mutex_unlock(&app_open_lock);
    return result;
}

void set_app_open(bool open)
{
    pthread_mutex_lock(&app_open_lock);
    bottom_bar_app_open = open;
    pthread_mutex_unlock(&app_open_lock);
}

static void ac_temp_up_callback(void *user_data)
{
    (void)user_data;
    if (state.current_ac_temp < 30)
        state.current_ac_temp++;
    char buf[16];
    snprintf(buf, sizeof(buf), "%dC", state.current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
    if (state.ac_controls_temp_label)
        aroma_label_set_text(state.ac_controls_temp_label, buf);
}

static void ac_temp_down_callback(void *user_data)
{
    (void)user_data;
    if (state.current_ac_temp > 16)
        state.current_ac_temp--;
    char buf[16];
    snprintf(buf, sizeof(buf), "%dC", state.current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
    if (state.ac_controls_temp_label)
        aroma_label_set_text(state.ac_controls_temp_label, buf);
}

static void toggle_seat_controls_callback(void *user_data)
{
    (void)user_data;
    if (!state.seat_controls_card)
        return;

    bool hidden = aroma_node_is_hidden(state.seat_controls_card);
    if (hidden)
    {
        aroma_node_set_hidden(state.seat_controls_card, false);
    }
    else
    {
        aroma_node_set_hidden(state.seat_controls_card, true);
    }
}

static void toggle_ac_controls_callback(void *user_data)
{
    (void)user_data;
    if (!state.ac_controls_card)
        return;

    bool hidden = aroma_node_is_hidden(state.ac_controls_card);
    if (hidden)
    {
        aroma_node_set_hidden(state.ac_controls_card, false);
    }
    else
    {
        aroma_node_set_hidden(state.ac_controls_card, true);
    }
}

static bool ac_power_callback(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    state.ac_auto_mode = !state.ac_auto_mode;
    return true;
}

static void fan_up_callback(void *user_data)
{
    (void)user_data;
    if (state.current_fan_speed < 5)
        state.current_fan_speed++;
}

static void fan_down_callback(void *user_data)
{
    (void)user_data;
    if (state.current_fan_speed > 0)
        state.current_fan_speed--;
}

static bool ac_mode_callback(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    state.ac_auto_mode = !state.ac_auto_mode;
    return true;
}

void update_bt_info_card(void)
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

void update_swupdate_service_status(void)
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

bool open_settings(AromaNode *node, void *user_data)
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
        card_node, 0.0f, 1.0f, APP_ANIM_MS, settings_opening_anim, NULL);
    if (!anim)
        return false;
    set_app_open(true);
    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);
    aroma_animation_set_easing(anim, APP_ANIM_OPEN_EASE);
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
    set_app_open(false);
    AromaAnimation *anim = aroma_animation_start_custom(
        card_node, 0.0f, 1.0f, APP_ANIM_MS, settings_closing_anim, NULL);
    aroma_node_set_hidden(settings_sidebar, true);
    aroma_node_set_hidden(settings_page_general, true);
    aroma_node_set_hidden(settings_page_display, true);
    aroma_node_set_hidden(settings_page_updates, true);
    aroma_animation_set_easing(anim, APP_ANIM_CLOSE_EASE);
}

static bool on_app_drawer_click(AromaNode *node, void *user_data)
{
    (void)node;
    int app_index = (int)(intptr_t)user_data;
    if (app_index >= 0 && app_index < app_registry_get_app_count())
    {
        AromaAppPlugin *app = app_registry_get_app(app_index);
        if (app && app->show)
            return app->show(app, app->app_root);
    }
    return false;
}

static void on_app_drawer_button_click(void *user_data)
{
    (void)user_data;
    if (app_drawer_visible)
    {
        AromaAnimation *slide_down = aroma_animation_start(app_drawer, AROMA_ANIM_SLIDE_Y, 0, WIN_H, APP_ANIM_MS);
        aroma_animation_set_easing(slide_down, AROMA_EASE_OUT_CUBIC);
        app_drawer_visible = false;
    }
    else
    {
        aroma_node_set_hidden(app_drawer, false);
        AromaAnimation *slide_up = aroma_animation_start(app_drawer, AROMA_ANIM_SLIDE_Y, WIN_H, 0, APP_ANIM_MS);
        aroma_animation_set_easing(slide_up, AROMA_EASE_OUT_CUBIC);
        app_drawer_visible = true;
        aroma_node_set_z_index(app_drawer, APP_DRAWER_Z_INDEX);
    }
}

static void on_app_drawer_close_click(void *user_data)
{
    (void)user_data;
    AromaAnimation *slide_down = aroma_animation_start(app_drawer, AROMA_ANIM_SLIDE_Y, 0, WIN_H, APP_ANIM_MS);
    aroma_animation_set_easing(slide_down, AROMA_EASE_OUT_CUBIC);
    app_drawer_visible = false;
}

/* --- Third-party packages (.apak) ------------------------------------- */

#define DRAWER_COLS 4

static void drawer_slot_pos(int slot, int *x, int *y)
{
    *x = 40 + (slot % DRAWER_COLS) * 236;
    *y = 80 + (slot / DRAWER_COLS) * 170;
}

/* Package drawer cards always occupy the slots right after the registry
 * apps. layout_package_cards() compacts the whole grid (registry cards
 * first, then installed packages) into contiguous slots 0,1,2... after
 * every install/uninstall/startup, so a missing card (e.g. a package that
 * failed to instantiate) can never leave a hole behind and cards never
 * overlap. In the nominal case this reproduces the creation slots
 * exactly, so it is a no-op. */
static void layout_package_cards(void)
{
    int slot = 0;
    int rc = app_registry_get_app_count();
    for (int i = 0; i < rc; i++)
    {
        AromaAppPlugin *app = app_registry_get_app(i);
        if (!app || !app->drawer_card)
            continue;
        int card_x = 0, card_y = 0;
        drawer_slot_pos(slot++, &card_x, &card_y);
        AromaRect *rect = aroma_node_get_rect(app->drawer_card);
        if (rect)
        {
            rect->x = card_x;
            rect->y = card_y;
        }
        aroma_node_invalidate(app->drawer_card);
    }
    for (int i = 0; i < package_manager_count(); i++)
    {
        InstalledPackage *pkg = package_manager_get(i);
        if (!pkg || !pkg->drawer_card)
            continue;
        int card_x = 0, card_y = 0;
        drawer_slot_pos(slot++, &card_x, &card_y);
        AromaRect *rect = aroma_node_get_rect(pkg->drawer_card);
        if (rect)
        {
            rect->x = card_x;
            rect->y = card_y;
        }
        aroma_node_invalidate(pkg->drawer_card);
    }
}

/* The renderer sorts draw tasks globally by z-index, so every node inside an
 * app root must sit above the root card itself. C-built apps set z per
 * widget; Incense-mounted content defaults to z=0 and would be painted over
 * by its own opaque root (black screen). Raising preserves sibling order via
 * the renderer's node-id tie-break (creation == document order). */
static void raise_children_z(AromaNode *node, int32_t z)
{
    if (!node)
        return;
    for (uint64_t i = 0; i < node->child_count; i++)
    {
        AromaNode *c = node->child_nodes[i];
        if (!c)
            continue;
        aroma_node_set_z_index(c, z);
        raise_children_z(c, z);
    }
}

void vehicle_view_raise_subtree(AromaNode *root)
{
    if (!root)
        return;
    raise_children_z(root, root->z_index + 1);
}

static AromaNode *drawer_card_create(int slot,
                                     const char *icon_code, uint32_t color,
                                     bool (*on_click)(AromaNode *, void *),
                                     void *user_data)
{
    int card_x = 0, card_y = 0;
    drawer_slot_pos(slot, &card_x, &card_y);
    /* Icon-only card: center the 64px icon box in the 180px card.
     * (An earlier revision pre-shifted the box right by 64px to compensate
     * for aroma_font_get_line_width measuring multi-byte icon glyphs ~3x
     * too wide; the loader now measures per codepoint, so the glyph draws
     * centered from a truly centered box.) */
#define DRAWER_CARD_W 180
#define DRAWER_CARD_H 150
#define DRAWER_ICON_PX 64
    AromaNode *btn_card = aroma_ui_card(app_drawer, card_x, card_y,
                                        DRAWER_CARD_W, DRAWER_CARD_H,
                                        CARD_TYPE_ELEVATED);
    aroma_node_set_z_index(btn_card, APP_DRAWER_Z_INDEX + 1);

    AromaNode *icon = aroma_ui_icon(btn_card, icon_code,
                                    (DRAWER_CARD_W - DRAWER_ICON_PX) / 2,
                                    (DRAWER_CARD_H - DRAWER_ICON_PX) / 2,
                                    DRAWER_ICON_PX,
                                    0xFFFFFFFF, state.huge_icon_font);
    aroma_node_set_z_index(icon, APP_DRAWER_Z_INDEX + 3);

    AromaNode *btn = aroma_ui_button(btn_card, "", 0, 0,
                                     DRAWER_CARD_W, DRAWER_CARD_H, on_click,
                                     user_data, state.ui_font);
    aroma_node_set_z_index(btn, APP_DRAWER_Z_INDEX + 2);
    aroma_button_set_colors(btn_card, color, color, color, color);
    return btn_card;
}

static void package_opening_anim(AromaNode *target, float progress, void *user_data)
{
    (void)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    rect->x = 0;
    rect->y = WIN_H + (int)((0 - WIN_H) * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;
    aroma_node_invalidate(target);
}

static void package_closing_anim(AromaNode *target, float progress, void *user_data)
{
    InstalledPackage *pkg = (InstalledPackage *)user_data;
    AromaRect *rect = aroma_node_get_rect(target);
    if (!rect)
        return;
    rect->x = 0;
    rect->y = (int)(WIN_H * progress);
    rect->width = WIN_W;
    rect->height = WIN_H;
    if (progress >= 0.92f)
    {
        aroma_node_set_z_index(target, 1);
        if (pkg && pkg->close_btn)
            aroma_node_set_hidden(pkg->close_btn, true);
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

static bool package_self_managed(const InstalledPackage *pkg)
{
    /* Native plugins with chrome:"self" own the full open chrome (drawer,
     * z-order, slide animation) exactly like the built-in apps they replace.
     * UI-only packages and chrome:"host" plugins get generic host chrome. */
    return pkg && pkg->hooks && pkg->hooks->show &&
           strcmp(pkg->manifest.chrome, "self") == 0;
}

static bool open_package(InstalledPackage *pkg)
{
    if (!pkg || !pkg->loaded || !pkg->app_root)
        return false;
    if (package_self_managed(pkg))
        return package_manager_show(pkg);
    if (app_drawer_visible)
        send_app_drawer_behind();
    if (!package_manager_show(pkg))
        return false;
    aroma_node_set_hidden(pkg->app_root, false);
    AromaAnimation *anim = aroma_animation_start_custom(
        pkg->app_root, 0.0f, 1.0f, APP_ANIM_MS, package_opening_anim, pkg);
    if (!anim)
        return false;
    set_app_open(true);
    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);
    aroma_animation_set_easing(anim, APP_ANIM_OPEN_EASE);
    aroma_node_set_z_index(pkg->app_root, APP_DRAWER_Z_INDEX + 10);
    vehicle_view_raise_subtree(pkg->app_root);
    if (pkg->close_btn)
        aroma_node_set_hidden(pkg->close_btn, false);
    return true;
}

void package_manager_close_cb(void *user_data)
{
    InstalledPackage *pkg = (InstalledPackage *)user_data;
    if (!pkg || !pkg->app_root)
        return;
    package_manager_hide(pkg);
    set_app_open(false);
    AromaAnimation *anim = aroma_animation_start_custom(
        pkg->app_root, 0.0f, 1.0f, APP_ANIM_MS, package_closing_anim, pkg);
    if (anim)
        aroma_animation_set_easing(anim, APP_ANIM_CLOSE_EASE);
}

static bool on_package_drawer_click(AromaNode *node, void *user_data)
{
    (void)node;
    return open_package((InstalledPackage *)user_data);
}

/* Open an installed package by id (used by host UI affordances such as the
 * mini media card). Returns false when the package is missing or closed. */
bool vehicle_view_open_package(const char *id)
{
    InstalledPackage *pkg = package_manager_find(id);
    if (!pkg)
        return false;
    return open_package(pkg);
}

void vehicle_view_debug_open(const char *what)
{
    if (!what || !app_drawer)
        return;
    if (strcmp(what, "drawer") == 0)
    {
        /* Drawer rests off-screen at y=WIN_H and slides up on open;
         * place it directly for the screenshot. */
        AromaRect *rect = aroma_node_get_rect(app_drawer);
        if (rect)
        {
            rect->x = 0;
            rect->y = 0;
        }
        aroma_node_set_hidden(app_drawer, false);
        app_drawer_visible = true;
        aroma_node_set_z_index(app_drawer, APP_DRAWER_Z_INDEX);
        aroma_node_invalidate(app_drawer);
    }
    else if (strcmp(what, "store") == 0 || strcmp(what, "settings") == 0 ||
             strcmp(what, "store-online") == 0)
    {
        const char *want = (strcmp(what, "settings") == 0) ? "com.aroma.settings"
                                                           : "com.aroma.store";
        for (int i = 0; i < app_registry_get_app_count(); i++)
        {
            AromaAppPlugin *app = app_registry_get_app(i);
            if (app && app->id && strcmp(app->id, want) == 0 && app->show)
            {
                app->show(app, app->app_root);
                break;
            }
        }
    }
    else
    {
        /* Treat anything else as a package id. */
        vehicle_view_open_package(what);
    }
    aroma_ui_request_redraw(NULL);
}

static AromaPackageHost package_host_ctx(void)
{
    AromaPackageHost host;
    memset(&host, 0, sizeof(host));
    host.ui_font = state.ui_font;
    host.icon_font = state.icon_font;
    host.settings_font = state.settings_font;
    host.screen_w = WIN_W;
    host.screen_h = WIN_H;
    return host;
}

/* Instantiate (if needed) + create a drawer card for one installed package.
 * Used at startup for every package and at runtime after an install. */
bool vehicle_view_add_package_card(const char *id)
{
    InstalledPackage *pkg = package_manager_find(id);
    if (!pkg || !app_drawer || !state.vehicle_view_root)
        return false;
    if (!pkg->loaded)
    {
        char err[256] = {0};
        AromaPackageHost host = package_host_ctx();
        if (!package_manager_instantiate(pkg, state.vehicle_view_root,
                                         &host, err, sizeof(err)))
        {
            fprintf(stderr, "[packages] cannot load %s: %s\n", id,
                    err[0] ? err : "unknown error");
            return false;
        }
        aroma_node_set_z_index(pkg->app_root, APP_DRAWER_Z_INDEX + 10);
        vehicle_view_raise_subtree(pkg->app_root);
    }
    if (pkg->drawer_card)
    {
        aroma_node_set_hidden(pkg->drawer_card, false);
    }
    else
    {
        int slot = app_registry_get_app_count();
        for (int i = 0; i < package_manager_count(); i++)
        {
            if (package_manager_get(i) == pkg)
            {
                slot += i;
                break;
            }
        }
        const char *icon = aroma_icon_codepoint_from_name(pkg->manifest.icon);
        uint32_t color = aroma_color_blend(0xFFFFFFFF, 0xFF1A73E8, 0.1f);
        pkg->drawer_card = drawer_card_create(slot, icon, color,
                                              on_package_drawer_click, pkg);
        if (!pkg->drawer_card)
            return false;
    }
    layout_package_cards();
    return true;
}

void vehicle_view_remove_package_card(const char *id)
{
    (void)id;
    /* Uninstall already tore the entry down (nodes destroyed, dl unloaded);
     * just close the grid gap left behind. Unknown ids are harmless no-ops. */
    layout_package_cards();
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
    aroma_node_set_hidden(state.vehicle_view_warning_message_card, true);
    aroma_node_set_hidden(state.vehicle_view_warning_message_label, true);
    aroma_node_set_hidden(state.vehicle_view_warning_warning_icon, true);
    aroma_node_set_hidden(state.vehicle_view_warning_message_action, true);
    aroma_node_set_hidden(state.battery_image, false);
    aroma_node_set_hidden(state.battery_health, false);
    aroma_node_set_hidden(state.battery_percentage, false);
}

static void on_bt_log(const char *level, const char *message, void *user_data)
{
    (void)user_data;
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

    /* The contacts package polls state.contacts_fetched and refetches on
     * change; just publish the new connection state here. */
    if (connected && device && device->name[0])
    {
        state.contacts_fetched = false;
    }
    else
    {
        state.contacts_fetched = false;
        state.contact_count = 0;
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

static void ensure_3d_initialized(void)
{
    static bool s_attempted = false;
    if (s_attempted)
        return;
    s_attempted = true;
    if (aroma_3d_init())
    {
        fprintf(stderr, "[vehicle_view] 3D resources initialized\n");
    }
    else
    {
        fprintf(stderr, "[vehicle_view] WARNING: 3D init failed\n");
    }
}

static void build_settings_ui(AromaNode *settings_root)
{
    if (!settings_root)
        return;
    AromaNode *settings_close_btn = aroma_ui_iconbutton(
        settings_root, AROMA_ICON_CLOSE, 20, 20, 48, ICON_BUTTON_FILLED,
        close_settings, settings_root, state.icon_font);
    aroma_node_set_z_index(settings_close_btn, Z_LAYER_STATUS_BAR + 16);

    settings_sidebar = aroma_ui_sidebar_with_icons(
        settings_root, 0, 80, 200, WIN_H - 80,
        (const char *[]){"General", "Display", "Updates"},
        (const char *[]){AROMA_ICON_SETTINGS, AROMA_ICON_BRIGHTNESS_6, AROMA_ICON_REFRESH},
        3, on_settings_sidebar_select, NULL, state.settings_font, state.icon_font);
    aroma_node_set_z_index(settings_sidebar, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(settings_sidebar, true);

    settings_page_general = aroma_ui_container(
        settings_root, 210, 80, WIN_W - 210, WIN_H - 80,
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
        settings_root, 210, 80, WIN_W - 210, WIN_H - 80,
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
        settings_root, 210, 80, WIN_W - 210, WIN_H - 80,
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

    AromaNode *logo_icon = aroma_ui_icon(updates_card, AROMA_ICON_MEMORY, (WIN_W - 200) / 2, 20, 60, IOS_COLOR_BLUE, state.huge_icon_font);
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

}

static void init_media_bt_services(void)
{
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

    media_ui.ui_initialized = true;

    bt_config_t config = {
        .device_name = setup_store_get("device_name", "Aroma Infotainment"),
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
    pthread_create(&media_thread, &media_attr, media_home_monitor_thread_func, NULL);
    pthread_attr_destroy(&media_attr);

    pthread_t call_thread;
    pthread_attr_t call_attr;
    pthread_attr_init(&call_attr);
    pthread_attr_setdetachstate(&call_attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&call_thread, &call_attr, call_monitor_thread_func, NULL);
    pthread_attr_destroy(&call_attr);

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
        resolve_asset_path("../assets/bg_dark.jpeg")
#endif
        ,
        0, 0, WIN_W, WIN_H);
    aroma_node_set_z_index(state.backroad, Z_LAYER_BACKGROUND);

    // Create viewer node (no GL calls here - context not yet current)
    // NOTE: aroma_3d_init() is safe to call here (no-ops without a GL
    // context) and viewer_draw() re-invokes it once the context is current.
    aroma_3d_init();
    state.viewer_3d = aroma_3d_viewer_create(
        state.vehicle_view_root, 0, 0, WIN_W, WIN_H);
    if (state.viewer_3d)
    {
        aroma_node_set_z_index(state.viewer_3d, Z_LAYER_BACKGROUND + 1);
        aroma_3d_viewer_set_interactive(state.viewer_3d, false);

        // Immediate placeholder so something is visible even while the
        // 85MB etron.glb parses off-thread (or if it is missing).
        Aroma3DModel *placeholder = aroma_3d_create_cube();
        if (placeholder)
            apply_vehicle_model_to_viewer(placeholder, 0.75f, 0.85f);

        // Start async model load (file read only, no GL needed)
        // etron.glb is the only vehicle model shipped in assets/.
        pending_vehicle_model_job = aroma_3d_load_model_async(
#ifdef __EMSCRIPTEN__
            "/assets/etron.glb"
#elif defined(__arm__) || defined(__aarch64__)
            "/usr/share/infotainment/assets/etron.glb"
#else
            resolve_asset_path("../assets/etron.glb")
#endif
        );
        if (!pending_vehicle_model_job)
        {
            fprintf(stderr, "[vehicle_view] aroma_3d_load_model_async failed to start, keeping cube placeholder\n");
        }

        vehicle_model_loading_spinner = aroma_loading_create(
            state.vehicle_view_root, WIN_W / 2, (WIN_H + 100) / 2,
            40, 6, 0xFFFFFFFF);
        if (vehicle_model_loading_spinner)
        {
            aroma_node_set_z_index(vehicle_model_loading_spinner, Z_LAYER_BACKGROUND + 2);
            aroma_node_set_hidden(vehicle_model_loading_spinner, !pending_vehicle_model_job);
        }
    }

    state.battery_button = aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_BATTERY_FULL,
        WIN_W - 260, 22, 40, ICON_BUTTON_OUTLINED,
        battery_diagnostics, NULL, state.icon_font);
    aroma_node_set_z_index(state.battery_button, Z_LAYER_VEHICLE_OVERLAYS + 1);

    state.vehicle_view_clock_gauge = aroma_ui_gauge(
        state.vehicle_view_root, WIN_W / 2 - 120, 0, 240, 240);
    aroma_node_set_z_index(state.vehicle_view_clock_gauge, Z_LAYER_VEHICLE_OVERLAYS + 2);
    aroma_gauge_set_angles(state.vehicle_view_clock_gauge, -1.5708f, 4.7124f);
    aroma_gauge_set_range(state.vehicle_view_clock_gauge, 0.0f, 60.0f);
    aroma_gauge_set_thickness(state.vehicle_view_clock_gauge, 3, 0);
    update_clock_gauge_colors();

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
        resolve_asset_path("../assets/charging.png")
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

    int media_card_width = WIN_W - 110 - 20;
    media_ui.media_card = aroma_ui_card(
        state.vehicle_view_root, 110, WIN_H - 110, media_card_width, 80, CARD_TYPE_GLASS);
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

    state.tire_button = aroma_ui_iconbutton(state.vehicle_view_root, AROMA_ICON_INFO, 400, 400, 30, ICON_BUTTON_FILLED, tire_cycle_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.tire_button, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.tire_card = aroma_ui_card(
        state.vehicle_view_root, (WIN_W - 220) / 2, 20, 220, 180, CARD_TYPE_GLASS);
    aroma_node_set_z_index(state.tire_card, Z_LAYER_CARDS_TOP);
    aroma_node_set_hidden(state.tire_card, true);

    state.tire_name_label = aroma_ui_label(
        state.tire_card, "Front Left", 20, 10,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.tire_name_label, Z_LAYER_CARDS_TOP + 1);

    state.tire_pressure_label = aroma_ui_label(
        state.tire_card, "32 psi", 20, 34,
        LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.tire_pressure_label, Z_LAYER_CARDS_TOP + 1);

    state.tire_close_btn = aroma_ui_iconbutton(
        state.tire_card, AROMA_ICON_CLOSE,
        190, 10, 24, ICON_BUTTON_OUTLINED,
        tire_exit_check_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.tire_close_btn, Z_LAYER_CARDS_TOP + 1);

    AromaNode *fl_btn = aroma_ui_button(state.tire_card, "FL", 30, 70, 100, 40, tire_select_callback, (void *)1, state.ui_font);
    aroma_node_set_z_index(fl_btn, Z_LAYER_CARDS_TOP + 1);

    AromaNode *fr_btn = aroma_ui_button(state.tire_card, "FR", 125, 70, 100, 40, tire_select_callback, (void *)0, state.ui_font);
    aroma_node_set_z_index(fr_btn, Z_LAYER_CARDS_TOP + 1);

    AromaNode *rl_btn = aroma_ui_button(state.tire_card, "RL", 30, 120, 100, 40, tire_select_callback, (void *)3, state.ui_font);
    aroma_node_set_z_index(rl_btn, Z_LAYER_CARDS_TOP + 1);

    AromaNode *rr_btn = aroma_ui_button(state.tire_card, "RR", 125, 120, 100, 40, tire_select_callback, (void *)2, state.ui_font);
    aroma_node_set_z_index(rr_btn, Z_LAYER_CARDS_TOP + 1);

    state.interior_ac_btn = aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_AC_UNIT,
        450, 280, 36, ICON_BUTTON_FILLED,
        ac_interior_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.interior_ac_btn, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.ac_controls_btn = aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_SETTINGS,
        90, 200, 36, ICON_BUTTON_FILLED,
        toggle_ac_controls_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.ac_controls_btn, Z_LAYER_VEHICLE_OVERLAYS + 2);
    aroma_node_set_hidden(state.ac_controls_btn, true);

    state.ac_controls_card = aroma_ui_card(
        state.vehicle_view_root, 90, 230, 260, 220, CARD_TYPE_GLASS);
    aroma_node_set_z_index(state.ac_controls_card, Z_LAYER_CARDS_TOP);
    aroma_node_set_hidden(state.ac_controls_card, true);

    AromaNode *ac_icon = aroma_ui_icon(
        state.ac_controls_card, AROMA_ICON_AC_UNIT, 50, 20, 28,
        state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(ac_icon, Z_LAYER_CARDS_TOP + 1);

    AromaNode *climate_label = aroma_ui_label(
        state.ac_controls_card, "Climate", 62, 18,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(climate_label, Z_LAYER_CARDS_TOP + 1);

    state.ac_controls_temp_label = aroma_ui_label(
        state.ac_controls_card, "22C", 120, 50,
        LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.ac_controls_temp_label, Z_LAYER_CARDS_TOP + 1);

    state.ac_temp_up_btn = aroma_ui_iconbutton(
        state.ac_controls_card, AROMA_ICON_ADD, 200, 45, 32, ICON_BUTTON_OUTLINED,
        ac_temp_up_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.ac_temp_up_btn, Z_LAYER_CARDS_TOP + 1);

    state.ac_temp_down_btn = aroma_ui_iconbutton(
        state.ac_controls_card, AROMA_ICON_REMOVE, 200, 85, 32, ICON_BUTTON_OUTLINED,
        ac_temp_down_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.ac_temp_down_btn, Z_LAYER_CARDS_TOP + 1);

    AromaNode *fan_speed_label = aroma_ui_label(
        state.ac_controls_card, "Fan Speed", 20, 100,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(fan_speed_label, Z_LAYER_CARDS_TOP + 1);

    state.fan_up_btn = aroma_ui_iconbutton(
        state.ac_controls_card, AROMA_ICON_ARROW_UPWARD, 200, 125, 32, ICON_BUTTON_OUTLINED,
        fan_up_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.fan_up_btn, Z_LAYER_CARDS_TOP + 1);

    state.fan_down_btn = aroma_ui_iconbutton(
        state.ac_controls_card, AROMA_ICON_ARROW_DOWNWARD, 200, 165, 32, ICON_BUTTON_OUTLINED,
        fan_down_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.fan_down_btn, Z_LAYER_CARDS_TOP + 1);

    state.ac_mode_btn = aroma_ui_button(
        state.ac_controls_card, "Auto", 20, 130, 90, 32,
        ac_mode_callback, NULL, state.ui_font);
    aroma_node_set_z_index(state.ac_mode_btn, Z_LAYER_CARDS_TOP + 1);

    state.ac_power_btn = aroma_ui_button(
        state.ac_controls_card, "AC", 120, 130, 60, 32,
        ac_power_callback, NULL, state.ui_font);
    aroma_node_set_z_index(state.ac_power_btn, Z_LAYER_CARDS_TOP + 1);

    state.seat_controls_btn = aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_AIRLINE_SEAT_LEGROOM_EXTRA,
        25, WIN_H - 70, 40, ICON_BUTTON_FILLED,
        toggle_seat_controls_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.seat_controls_btn, Z_LAYER_VEHICLE_OVERLAYS + 2);
    aroma_node_set_hidden(state.seat_controls_btn, true);

    state.seat_controls_card = aroma_ui_card(
        state.vehicle_view_root, 25, WIN_H - 220, 240, 180, CARD_TYPE_GLASS);
    aroma_node_set_z_index(state.seat_controls_card, Z_LAYER_CARDS_TOP);
    aroma_node_set_hidden(state.seat_controls_card, true);

    AromaNode *seat_icon = aroma_ui_icon(
        state.seat_controls_card, AROMA_ICON_AIRLINE_SEAT_LEGROOM_EXTRA, 20, 20, 28,
        state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(seat_icon, Z_LAYER_CARDS_TOP + 1);

    AromaNode *seat_label = aroma_ui_label(
        state.seat_controls_card, "Seat Position", 50, 20,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(seat_label, Z_LAYER_CARDS_TOP + 1);

    state.seat_position_slider = aroma_slider_create(
        state.seat_controls_card, 20, 55, 200, 40,
        0, 100, 50);
    aroma_node_set_z_index(state.seat_position_slider, Z_LAYER_CARDS_TOP + 1);
    aroma_slider_setup_events(state.seat_position_slider, aroma_ui_request_redraw, NULL);

    interior_close_btn = aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_CLOSE,
        WIN_W - 60, 20, 40, ICON_BUTTON_FILLED,
        close_interior_view_callback, NULL, state.icon_font);
    aroma_node_set_z_index(interior_close_btn, Z_LAYER_CARDS_TOP);
    aroma_node_set_hidden(interior_close_btn, true);

    app_drawer = aroma_ui_frosted_card(
        state.vehicle_view_root, 0, WIN_H, WIN_W, WIN_H,
        AROMA_DRAWER_FROST_RADIUS);
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


    int app_count = app_registry_get_app_count();
    for (int i = 0; i < app_count; i++) {
        AromaAppPlugin *app = app_registry_get_app(i);
        app->drawer_card = drawer_card_create(i, app->icon,
                                              app->card_color,
                                              on_app_drawer_click,
                                              (void *)(intptr_t)i);

        app->app_root = aroma_ui_card(state.vehicle_view_root, 0, 0, WIN_W, WIN_H, CARD_TYPE_ELEVATED);
        aroma_node_set_z_index(app->app_root, APP_DRAWER_Z_INDEX + 10);
        aroma_node_set_hidden(app->app_root, true);

        if (app->build_ui) {
            app->build_ui(app, app->app_root);
        }
    }
    layout_package_cards();

    /* Third-party packages (.apak installs). Failures are non-fatal: the
     * built-in apps must keep working when a package is broken. */
    for (int i = 0; i < package_manager_count(); i++) {
        InstalledPackage *pkg = package_manager_get(i);
        if (pkg && !vehicle_view_add_package_card(pkg->manifest.id))
            fprintf(stderr, "[packages] skipping %s\n", pkg->manifest.id);
    }


    // Settings app is index 3 (see main.c registration order). Look it up
    // by id so a reorder cannot silently parent the UI to the wrong root.
    AromaNode *settings_root = NULL;
    for (int i = 0; i < app_registry_get_app_count(); i++) {
        AromaAppPlugin *a = app_registry_get_app(i);
        if (a && a->id && strcmp(a->id, "com.aroma.settings") == 0) {
            settings_root = a->app_root;
            break;
        }
    }
    build_settings_ui(settings_root);
    init_media_bt_services();

    build_lock_screen(window);
}



void update_vehicle_view(void)
{
    for (int i = 0; i < app_registry_get_app_count(); i++) {
        AromaAppPlugin *app = app_registry_get_app(i);
        if (app && app->update) {
            app->update(app);
        }
    }

    package_manager_update_all();

    if (pending_vehicle_model_job && aroma_3d_load_model_poll(pending_vehicle_model_job))
    {
        Aroma3DModel *loaded_model = aroma_3d_load_model_finish(pending_vehicle_model_job);
        pending_vehicle_model_job = NULL;

        if (loaded_model)
        {
            apply_vehicle_model_to_viewer(loaded_model, 0.75f, 0.85f);
            if (vehicle_model_loading_spinner)
                aroma_node_set_hidden(vehicle_model_loading_spinner, true);
            if (vehicle_model_loaded_cb)
                vehicle_model_loaded_cb(true, vehicle_model_loaded_cb_user_data);
        }
        else
        {
            fprintf(stderr, "[vehicle_view] async vehicle model load failed, keeping cube placeholder\n");
            if (vehicle_model_loading_spinner)
                aroma_node_set_hidden(vehicle_model_loading_spinner, true);
            if (vehicle_model_loaded_cb)
                vehicle_model_loaded_cb(false, vehicle_model_loaded_cb_user_data);
        }
    }

    // Camera fly-to animation (startup sweep, interior/tire views).
    // Without this, state.camera_animating / state.startup_animating stay
    // true forever and block all camera callbacks.
    if (state.camera_animating && state.viewer_3d)
    {
        Aroma3DCamera cam;
        aroma_3d_viewer_get_camera(state.viewer_3d, &cam);
        float t = 0.08f;
        cam.theta += (state.anim_target_theta - cam.theta) * t;
        cam.phi += (state.anim_target_phi - cam.phi) * t;
        cam.radius += (state.anim_target_radius - cam.radius) * t;
        cam.target[0] += (state.anim_target_x - cam.target[0]) * t;
        cam.target[1] += (state.anim_target_y - cam.target[1]) * t;
        cam.target[2] += (state.anim_target_z - cam.target[2]) * t;

        float dtheta = state.anim_target_theta - cam.theta;
        float dphi = state.anim_target_phi - cam.phi;
        float dradius = state.anim_target_radius - cam.radius;
        float dtarget[3];
        dtarget[0] = state.anim_target_x - cam.target[0];
        dtarget[1] = state.anim_target_y - cam.target[1];
        dtarget[2] = state.anim_target_z - cam.target[2];
        float dist = sqrtf(dtheta * dtheta + dphi * dphi + dradius * dradius +
                           dtarget[0] * dtarget[0] + dtarget[1] * dtarget[1] +
                           dtarget[2] * dtarget[2]);

        if (dist < 0.001f)
        {
            cam.theta = state.anim_target_theta;
            cam.phi = state.anim_target_phi;
            cam.radius = state.anim_target_radius;
            cam.target[0] = state.anim_target_x;
            cam.target[1] = state.anim_target_y;
            cam.target[2] = state.anim_target_z;
            state.camera_animating = false;

            if (state.startup_animating)
            {
                state.startup_animating = false;
                aroma_3d_viewer_set_auto_rotate(state.viewer_3d, true);
            }
        }
        aroma_3d_viewer_set_camera(state.viewer_3d, &cam);
    }
    else if (state.viewer_3d && has_locked_vehicle_camera)
    {
        aroma_3d_viewer_set_camera(state.viewer_3d, &locked_vehicle_camera);
    }

    if (state.viewer_3d)
    {
        aroma_3d_viewer_update(state.viewer_3d);
    }
}


