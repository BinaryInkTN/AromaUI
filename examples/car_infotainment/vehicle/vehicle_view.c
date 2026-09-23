#include "theme_manager.h"
#include "vehicle_view.h"
#include "setup_store.h"

#include "app_state.h"
#include "aroma_animation.h"
#include "media_bt_service.h"
#include "contacts_bt_service.h"
#include "widgets/aroma_loading.h"
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
void refresh_drawer_list(void);
static void refresh_installed_list(void);
static bool on_pkg_uninstall_click(AromaNode *node, void *user_data);
const char *resolve_asset_path(const char *filename)
{
    static char resolved[512];
    FILE *f = fopen(filename, "rb");
    if (f)
    {
        fclose(f);
        snprintf(resolved, sizeof(resolved), "%s", filename);
        return resolved;
    }

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

static bool dark_mode_enabled = false;
static AromaNode *settings_dark_mode_switch = NULL;
static AromaNode *settings_aa_switch = NULL;

static bool s_bt_enabled = false;

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
static AromaNode *app_drawer_scroll = NULL;
static AromaNode *app_drawer_list = NULL;
static AromaNode *app_drawer_close_btn = NULL;
bool app_drawer_visible = false;
static bool app_drawer_behind_app = false;
#define APP_DRAWER_Z_INDEX (Z_LAYER_STATUS_BAR + 1)
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

static void update_digital_clock_colors(void);
static void on_drawer_grid_icon(void *user_data);
static void on_drawer_icon_long_press(void *user_data);

void apply_theme_colors(void)
{
    if (dark_mode_enabled)
    {
        state.theme = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_BLUE);
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

    update_digital_clock_colors();
}

static void update_digital_clock_colors(void)
{

    if (state.vehicle_view_clock_label)
        aroma_label_set_color(state.vehicle_view_clock_label,
                              state.theme.colors.text_primary);
    if (state.vehicle_view_clock_date_label)
        aroma_label_set_color(state.vehicle_view_clock_date_label,
                              state.theme.colors.text_secondary);
    if (state.vehicle_view_ampm_label)
        aroma_label_set_color(state.vehicle_view_ampm_label,
                              state.theme.colors.primary);
}

static bool on_dark_mode_switch_changed(AromaNode *switch_node, void *user_data)
{
    (void)user_data;
    dark_mode_enabled = aroma_switch_get_state(switch_node);
    apply_theme_colors();
    return true;
}

static bool on_settings_aa_changed(AromaNode *switch_node, void *user_data)
{
    (void)user_data;
    bool enabled = aroma_switch_get_state(switch_node);
    aroma_3d_set_antialiasing(enabled);
    setup_store_set_int("aa_3d", enabled ? 1 : 0);
    setup_store_save();
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

static AromaNode *settings_main_page = NULL;
static AromaNode *settings_page_general = NULL;
static AromaNode *settings_page_display = NULL;
static AromaNode *settings_page_updates = NULL;
static AromaNode *settings_page_packages = NULL;
static AromaNode *settings_list_scroll = NULL;
static AromaNode *settings_list = NULL;
static AromaNode *settings_current_page = NULL;
static AromaNode *settings_nav_from_page = NULL;
static bool settings_nav_animating = false;
static char s_settings_search[128] = "";
// Selectable-row -> detail page (0=general, 1=display, 2=updates, 3=packages)
static int s_settings_row_page[8];
static int s_settings_row_count = 0;
static AromaNode *settings_bluetooth_switch = NULL;
static AromaNode *settings_autolock_label = NULL;
static char s_pkg_apak_path[512] = "";
static AromaNode *s_pkg_status_label = NULL;
static AromaNode *s_installed_list = NULL;
static AromaNode *s_installed_card = NULL;
static AromaNode *s_settings_root = NULL;

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
    return s_bt_enabled;
}

static const MediaBtService *media_svc(void)
{
    typedef const MediaBtService *(*svc_fn)(void);
    svc_fn fn = (svc_fn)package_manager_symbol("com.aroma.media",
                                               "media_bt_service");
    return fn ? fn() : NULL;
}

static const ContactsBtService *contacts_svc(void)
{
    typedef const ContactsBtService *(*svc_fn)(void);
    svc_fn fn = (svc_fn)package_manager_symbol("com.aroma.contacts",
                                               "contacts_bt_service");
    return fn ? fn() : NULL;
}

int vehicle_view_set_bt_device_name(const char *name)
{
    const MediaBtService *m = media_svc();
    if (!m)
        return -1;
    return m->set_device_name(name);
}

bool vehicle_view_set_bluetooth_enabled(bool enabled)
{
    const MediaBtService *m = media_svc();
    const ContactsBtService *c = contacts_svc();
    if (!m || !c)
    {
        fprintf(stderr,
                "[BT] service unavailable (media/contacts packages missing?)\n");
        return false;
    }
    if (enabled)
    {
        if (!s_bt_enabled)
        {
            const char *name = setup_store_get("device_name",
                                               "Aroma Infotainment");
            if (m->set_enabled(true, name) != 0)
            {
                fprintf(stderr, "[BT] media stack failed to start\n");
                return false;
            }
            if (c->set_enabled(true) != 0)
                fprintf(stderr, "[BT] HFP stack failed to start\n");
            s_bt_enabled = true;
        }
    }
    else
    {
        m->set_enabled(false, NULL);
        c->set_enabled(false);
        s_bt_enabled = false;
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
        const ContactsBtService *svc = contacts_svc();
        if (svc)
            svc->answer(call_path_copy);
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
        const ContactsBtService *svc = contacts_svc();
        if (svc)
            svc->hangup(call_path_copy);
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
    const ContactsBtService *svc = contacts_svc();
    if (svc)
        svc->hangup_all();

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
        int curr_count = 0;
        const ContactsBtService *svc = contacts_svc();
        if (svc)
            curr_count = svc->active_calls(curr_calls, 10);

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
    const MediaBtService *svc = media_svc();
    if (svc)
        svc->avrcp_prev();
}

static void on_media_play_pause_click(void *user_data)
{
    (void)user_data;
    const MediaBtService *svc = media_svc();
    if (!svc)
        return;
    if (media_ui.is_playing)
    {
        svc->avrcp_pause();
        media_ui.is_playing = false;
    }
    else
    {
        svc->avrcp_play();
        media_ui.is_playing = true;
    }
    update_play_pause_button_icon();
}

static void on_media_next_click(void *user_data)
{
    (void)user_data;
    const MediaBtService *svc = media_svc();
    if (svc)
        svc->avrcp_next();
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

    const MediaBtService *svc = media_svc();
    if (!svc)
    {
        aroma_node_set_hidden(bt_info_card, true);
        return;
    }
    bt_device_info_t device;
    svc->device_info(&device);
    bt_state_t bt_state = svc->state();

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

// --- iOS-style settings navigation (list -> drill-in detail page) -----------
#define SETTINGS_NAV_MS 250

static void update_ota_display(void);

static void settings_nav_slide_cb(AromaNode *target, float val, void *user_data)
{
    (void)user_data;
    AromaRect *r = aroma_node_get_rect(target);
    if (!r)
        return;
    r->x = (int)val;
    aroma_node_invalidate(target);
}

static void settings_nav_complete(AromaNode *target, void *user_data)
{
    (void)target;
    (void)user_data;
    if (settings_nav_from_page)
    {
        aroma_node_set_hidden(settings_nav_from_page, true);
        aroma_node_invalidate(settings_nav_from_page);
        settings_nav_from_page = NULL;
    }
    if (settings_current_page)
    {
        AromaRect *r = aroma_node_get_rect(settings_current_page);
        if (r)
            r->x = 0;
        aroma_node_invalidate(settings_current_page);
    }
    settings_nav_animating = false;
    aroma_ui_request_redraw(NULL);
}

static void settings_navigate_to(AromaNode *target, bool is_back)
{
    if (!target || settings_nav_animating)
        return;
    AromaNode *current = settings_current_page ? settings_current_page : settings_main_page;
    if (!current || current == target)
        return;
    AromaRect *cur_rect = aroma_node_get_rect(current);
    AromaRect *tgt_rect = aroma_node_get_rect(target);
    if (!cur_rect || !tgt_rect)
        return;

    float cur_end = is_back ? (float)WIN_W : (float)-WIN_W;
    float tgt_start = is_back ? (float)-WIN_W : (float)WIN_W;

    settings_nav_animating = true;
    settings_nav_from_page = current;
    settings_current_page = target;

    aroma_node_set_hidden(target, false);
    tgt_rect->x = (int)tgt_start;
    aroma_node_invalidate(target);

    AromaAnimation *cur_anim = aroma_animation_start_custom(
        current, 0.0f, cur_end, SETTINGS_NAV_MS, settings_nav_slide_cb, NULL);
    if (cur_anim)
        aroma_animation_set_easing(cur_anim, AROMA_EASE_OUT_CUBIC);

    AromaAnimation *tgt_anim = aroma_animation_start_custom(
        target, tgt_start, 0.0f, SETTINGS_NAV_MS, settings_nav_slide_cb, NULL);
    if (tgt_anim)
    {
        aroma_animation_set_easing(tgt_anim, AROMA_EASE_OUT_CUBIC);
        aroma_animation_set_on_complete(tgt_anim, settings_nav_complete);
    }
    else
    {
        settings_nav_complete(target, NULL);
    }
}

static void settings_show_page(int page)
{
    AromaNode *target = NULL;
    if (page == 0)
        target = settings_page_general;
    else if (page == 1)
        target = settings_page_display;
    else if (page == 2)
        target = settings_page_updates;
    else if (page == 3)
        target = settings_page_packages;
    if (!target)
        return;
    if (page == 0)
        update_bt_info_card();
    if (page == 2)
    {
        update_swupdate_service_status();
        update_ota_display();
    }
    if (page == 3)
        refresh_installed_list();
    settings_navigate_to(target, false);
}

static void settings_go_back(void *user_data)
{
    (void)user_data;
    if (!settings_main_page)
        return;
    settings_navigate_to(settings_main_page, true);
}

static bool settings_row_matches(const char *title, const char *section)
{
    if (!s_settings_search[0])
        return true;
    char hay[256];
    snprintf(hay, sizeof(hay), "%s %s", title ? title : "", section ? section : "");
    for (const char *h = hay; *h; h++)
    {
        const char *hh = h;
        const char *n = s_settings_search;
        while (*n && tolower((unsigned char)*hh) == tolower((unsigned char)*n))
        {
            hh++;
            n++;
        }
        if (!*n)
            return true;
    }
    return false;
}

static void refresh_settings_list(void)
{
    if (!settings_list)
        return;
    aroma_listview_clear(settings_list);
    s_settings_row_count = 0;

    static const struct {
        const char *section;
        const char *title;
        const char *secondary;
        const char *icon;
        int page;
    } entries[] = {
        {"General", "General", "Bluetooth, About", AROMA_ICON_SETTINGS, 0},
        {"Display & Brightness", "Display", "Dark Mode, 3D", AROMA_ICON_BRIGHTNESS_6, 1},
        {"System", "Software Update", "Aroma OS", AROMA_ICON_REFRESH, 2},
        {"System", "Packages", "Install & manage", AROMA_ICON_FILE_DOWNLOAD, 3},
    };
    const char *last_section = NULL;
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++)
    {
        if (!settings_row_matches(entries[i].title, entries[i].section))
            continue;
        if (!last_section || strcmp(last_section, entries[i].section) != 0)
        {
            aroma_listview_add_header(settings_list, entries[i].section);
            last_section = entries[i].section;
        }
        aroma_listview_add_item_with_icon(settings_list, entries[i].title,
                                          entries[i].secondary, entries[i].icon, NULL);
        if (s_settings_row_count < 8)
            s_settings_row_page[s_settings_row_count++] = entries[i].page;
    }
    if (s_settings_row_count == 0)
    {
        aroma_listview_add_header(settings_list, "No results");
        aroma_listview_add_item_with_icon(settings_list, "No settings found",
                                          "Try another search", AROMA_ICON_SEARCH, NULL);
    }
    int content_h = aroma_listview_get_content_height(settings_list);
    AromaRect *lr = aroma_node_get_rect(settings_list);
    if (lr && content_h > 0)
    {
        lr->height = content_h;
        aroma_node_invalidate(settings_list);
    }
    if (settings_list_scroll)
    {
        aroma_container_update_auto_content_size(settings_list_scroll);
        aroma_node_invalidate(settings_list_scroll);
    }
}

static void on_settings_list_item(int index, void *user_data)
{
    (void)user_data;
    if (index < 0 || index >= s_settings_row_count)
        return;
    settings_show_page(s_settings_row_page[index]);
}

static bool on_settings_search_changed(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    snprintf(s_settings_search, sizeof(s_settings_search), "%s", text ? text : "");
    refresh_settings_list();
    return true;
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

    // NOTE: open is intentionally instant (no slide animation). The card
    // used to slide in vertically, but moving it while detail pages are
    // hidden desyncs their layout caches (per-frame layout skips hidden
    // subtrees), which threw the packages list/rows off-screen on the next
    // show. Detail drill-in/out slides are unaffected: both pages stay
    // visible and tracked for their whole animation.
    AromaRect *card_rect = aroma_node_get_rect(card_node);
    if (card_rect)
    {
        card_rect->x = 0;
        card_rect->y = 0;
        card_rect->width = WIN_W;
        card_rect->height = WIN_H;
        aroma_node_invalidate(card_node);
    }
    aroma_node_set_hidden(card_node, false);
    set_app_open(true);
    if (media_ui.media_card)
        aroma_node_set_hidden(media_ui.media_card, true);
    settings_nav_animating = false;
    settings_nav_from_page = NULL;
    settings_current_page = settings_main_page;
    if (settings_main_page)
    {
        AromaRect *mr = aroma_node_get_rect(settings_main_page);
        if (mr)
            mr->x = 0;
        aroma_node_set_hidden(settings_main_page, false);
    }
    // Reset any stray slide offsets so every detail page restarts from a
    // known-good position; all are hidden until drilled into.
    AromaNode *detail_pages[] = {
        settings_page_general, settings_page_display,
        settings_page_updates, settings_page_packages
    };
    for (size_t i = 0; i < sizeof(detail_pages) / sizeof(detail_pages[0]); i++)
    {
        if (!detail_pages[i])
            continue;
        AromaRect *pr = aroma_node_get_rect(detail_pages[i]);
        if (pr)
            pr->x = 0;
        aroma_node_set_hidden(detail_pages[i], true);
    }
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
        aroma_node_set_hidden(settings_main_page, true);
        aroma_node_set_hidden(settings_page_general, true);
        aroma_node_set_hidden(settings_page_display, true);
        aroma_node_set_hidden(settings_page_updates, true);
        aroma_node_set_hidden(settings_page_packages, true);
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
    // Instant close (see open_settings): hiding everything synchronously
    // keeps rects/caches consistent, since no parent moves while hidden.
    settings_nav_animating = false;
    settings_nav_from_page = NULL;
    settings_current_page = NULL;
    aroma_node_set_hidden(settings_main_page, true);
    aroma_node_set_hidden(settings_page_general, true);
    aroma_node_set_hidden(settings_page_display, true);
    aroma_node_set_hidden(settings_page_updates, true);
    aroma_node_set_hidden(settings_page_packages, true);
    aroma_node_set_z_index(card_node, 1);
    aroma_node_set_hidden(card_node, true);
    set_app_open(false);
    apply_deferred_bottom_bar_position();
    update_media_card_display();
    restore_app_drawer_from_behind();
}

#define DRAWER_LIST_MAX (16 + 32)
#define DRAWER_CAT_ALL 0
#define DRAWER_CAT_BUILTIN 1
#define DRAWER_CAT_APPS 2
#define DRAWER_CAT_GAMES 3
#define DRAWER_CAT_COUNT 4

#define DRAWER_GRID_COLS 4
#define DRAWER_GRID_PITCH 236
#define DRAWER_GRID_TILE_ICON 84
#define DRAWER_GRID_TILE_H 140
#define DRAWER_GRID_GAP_Y 16
#define DRAWER_CONTENT_X 40
#define DRAWER_CONTENT_Y 192
#define DRAWER_CONTENT_W (WIN_W - 80)
#define DRAWER_CONTENT_H (WIN_H - DRAWER_CONTENT_Y - 16)

#define DRAWER_TITLE_X 40
#define DRAWER_TITLE_Y 20
#define DRAWER_CLOSE_X (WIN_W - 56)
#define DRAWER_CLOSE_Y 16
#define DRAWER_VIEW_TOGGLE_X (WIN_W - 104)
#define DRAWER_VIEW_TOGGLE_Y 16
#define DRAWER_SEARCH_X 40
#define DRAWER_SEARCH_Y 76
#define DRAWER_SEARCH_W (WIN_W - 80)
#define DRAWER_SEARCH_H 48
#define DRAWER_TABS_X 40
#define DRAWER_TABS_Y 136
#define DRAWER_TABS_W (WIN_W - 80)
#define DRAWER_TABS_H 48
static int s_drawer_kind[DRAWER_LIST_MAX];
static int s_drawer_app[DRAWER_LIST_MAX];
static int s_drawer_pkg[DRAWER_LIST_MAX];
static int s_drawer_count = 0;
static char s_drawer_search[256] = "";
static int s_drawer_category = DRAWER_CAT_ALL;
static bool s_drawer_grid_mode = true;
static AromaNode *app_drawer_search_box = NULL;
static AromaNode *app_drawer_view_toggle = NULL;
static AromaNode *app_drawer_grid_scroll = NULL;
static AromaNode *app_drawer_cat_tabs = NULL;
static AromaNode *app_drawer_title = NULL;

static bool drawer_str_contains_ci(const char *hay, const char *needle)
{
    if (!needle || !needle[0])
        return true;
    if (!hay)
        return false;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++)
    {
        size_t i = 0;
        while (i < nl && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nl)
            return true;
    }
    return false;
}

static bool drawer_pkg_is_games(int pkg_idx)
{
    InstalledPackage *pkg = package_manager_get(pkg_idx);
    if (!pkg)
        return false;
    const char *c = pkg->manifest.category;
    if (!c || !c[0])
        return false;
    return strcasecmp(c, "Games") == 0;
}

static bool drawer_matches_category(int kind, int pkg_idx)
{
    if (s_drawer_category == DRAWER_CAT_ALL)
        return true;
    if (s_drawer_category == DRAWER_CAT_BUILTIN)
        return kind == 0;
    if (s_drawer_category == DRAWER_CAT_GAMES)
        return kind == 1 && drawer_pkg_is_games(pkg_idx);
    if (s_drawer_category == DRAWER_CAT_APPS)
    {
        if (kind != 1)
            return false;
        return !drawer_pkg_is_games(pkg_idx);
    }
    return true;
}

static bool drawer_matches_search(const char *name, const char *sub, const char *cat)
{
    if (!s_drawer_search[0])
        return true;
    return drawer_str_contains_ci(name, s_drawer_search) ||
           drawer_str_contains_ci(sub, s_drawer_search) ||
           drawer_str_contains_ci(cat, s_drawer_search);
}

static void open_drawer_entry(int index)
{
    if (index < 0 || index >= s_drawer_count)
        return;
    if (s_drawer_kind[index] == 0)
    {
        AromaAppPlugin *app = app_registry_get_app(s_drawer_app[index]);
        if (app && app->show)
            app->show(app, app->app_root);
    }
    else
    {
        InstalledPackage *pkg = package_manager_get(s_drawer_pkg[index]);
        if (pkg)
            vehicle_view_open_package(pkg->manifest.id);
    }
}

static void drawer_shift_subtree(AromaNode *node, int dx, int dy)
{
    if (!node || (!dx && !dy))
        return;
    AromaRect *r = aroma_node_get_rect(node);
    if (r)
    {
        r->x += dx;
        r->y += dy;
    }
    for (uint64_t i = 0; i < node->child_count; i++)
        drawer_shift_subtree(node->child_nodes[i], dx, dy);
}

static void drawer_place_xy(AromaNode *node, int x, int y)
{
    if (!node)
        return;
    AromaRect *r = aroma_node_get_rect(node);
    if (!r)
        return;
    r->x = x;
    r->y = y;
    aroma_node_invalidate(node);
}

static void drawer_reanchor(void)
{
    if (!app_drawer)
        return;
    AromaRect *dr = aroma_node_get_rect(app_drawer);
    if (!dr)
        return;
    int dx = dr->x;
    int dy = dr->y;

    drawer_place_xy(app_drawer_title, dx + DRAWER_TITLE_X, dy + DRAWER_TITLE_Y);
    drawer_place_xy(app_drawer_close_btn, dx + DRAWER_CLOSE_X, dy + DRAWER_CLOSE_Y);
    drawer_place_xy(app_drawer_view_toggle, dx + DRAWER_VIEW_TOGGLE_X, dy + DRAWER_VIEW_TOGGLE_Y);
    drawer_place_xy(app_drawer_search_box, dx + DRAWER_SEARCH_X, dy + DRAWER_SEARCH_Y);
    drawer_place_xy(app_drawer_cat_tabs, dx + DRAWER_TABS_X, dy + DRAWER_TABS_Y);

    if (app_drawer_scroll)
    {
        AromaRect *sr = aroma_node_get_rect(app_drawer_scroll);
        if (sr)
        {
            sr->x = dx + DRAWER_CONTENT_X;
            sr->y = dy + DRAWER_CONTENT_Y;
            sr->width = DRAWER_CONTENT_W;
            sr->height = DRAWER_CONTENT_H;
            aroma_node_invalidate(app_drawer_scroll);
        }
    }
    if (app_drawer_grid_scroll)
    {
        AromaRect *gr = aroma_node_get_rect(app_drawer_grid_scroll);
        if (gr)
        {
            gr->x = dx + DRAWER_CONTENT_X;
            gr->y = dy + DRAWER_CONTENT_Y;
            gr->width = DRAWER_CONTENT_W;
            gr->height = DRAWER_CONTENT_H;
            aroma_node_invalidate(app_drawer_grid_scroll);
        }
    }

    if (app_drawer_list && app_drawer_scroll)
    {
        AromaRect *sr = aroma_node_get_rect(app_drawer_scroll);
        AromaRect *lr = aroma_node_get_rect(app_drawer_list);
        if (sr && lr)
        {
            lr->x = sr->x;
            lr->y = sr->y;
            lr->width = DRAWER_CONTENT_W;
            aroma_node_invalidate(app_drawer_list);
        }
    }
}

static void refresh_drawer_grid(void)
{
    if (!app_drawer_grid_scroll)
        return;
    while (app_drawer_grid_scroll->child_count > 0)
    {
        AromaNode *c = app_drawer_grid_scroll->child_nodes[0];
        if (!c)
            break;
        __destroy_node_tree(c);
    }

    int bake_x = 0;
    int bake_y = 0;
    if (app_drawer)
    {
        AromaRect *dr = aroma_node_get_rect(app_drawer);
        if (dr)
        {
            bake_x = dr->x + DRAWER_CONTENT_X;
            bake_y = dr->y + DRAWER_CONTENT_Y;
        }
    }
    if (s_drawer_count <= 0)
    {
        AromaNode *empty = aroma_ui_label(app_drawer_grid_scroll, "No apps found",
                                          12, 12,
                                          LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
        if (empty)
        {
            if (bake_x || bake_y)
                drawer_shift_subtree(empty, bake_x, bake_y);
            aroma_node_set_z_index(empty, APP_DRAWER_Z_INDEX + 1);
        }
        aroma_container_set_content_size(app_drawer_grid_scroll, WIN_W - 80, 60);
        aroma_node_invalidate(app_drawer_grid_scroll);
        return;
    }
    int placed = 0;
    char name_buf[32];
    for (int i = 0; i < s_drawer_count; i++)
    {
        const char *name = "";
        const char *icon = AROMA_ICON_WIDGETS;
        if (s_drawer_kind[i] == 0)
        {
            AromaAppPlugin *app = app_registry_get_app(s_drawer_app[i]);
            if (!app)
                continue;
            name = app->name ? app->name : "";
            if (app->icon && app->icon[0])
                icon = app->icon;
        }
        else
        {
            InstalledPackage *pkg = package_manager_get(s_drawer_pkg[i]);
            if (!pkg)
                continue;
            name = pkg->manifest.name[0] ? pkg->manifest.name : "";
            const char *ic = aroma_icon_codepoint_from_name(pkg->manifest.icon);
            if (ic && ic[0])
                icon = ic;
        }

        int col = placed % DRAWER_GRID_COLS;
        int row = placed / DRAWER_GRID_COLS;
        int cell_x = 12 + col * DRAWER_GRID_PITCH;
        int cell_y = 12 + row * (DRAWER_GRID_TILE_H + DRAWER_GRID_GAP_Y);
        int icon_x = cell_x + (DRAWER_GRID_PITCH - DRAWER_GRID_TILE_ICON) / 2;
        AromaNode *tile = aroma_ui_iconbutton(
            app_drawer_grid_scroll, icon, icon_x, cell_y + 8,
            DRAWER_GRID_TILE_ICON, ICON_BUTTON_FILLED,
            on_drawer_grid_icon, (void *)(intptr_t)i, state.drawer_icon_font);
        if (!tile)
            continue;
        aroma_node_set_z_index(tile, APP_DRAWER_Z_INDEX + 1);
        aroma_iconbutton_set_long_press_callback(tile, on_drawer_icon_long_press,
                                                 (void *)(intptr_t)i);
        truncate_for_listview(name, name_buf, sizeof(name_buf));
        AromaNode *cap = aroma_ui_label(app_drawer_grid_scroll, name_buf,
                                        cell_x, cell_y + 8 + DRAWER_GRID_TILE_ICON + 8,
                                        LABEL_STYLE_LABEL_SMALL, state.ui_font);
        if (cap)
        {

            AromaRect *lr = aroma_node_get_rect(cap);
            if (lr && lr->width > 0 && lr->width < DRAWER_GRID_PITCH)
                lr->x += (DRAWER_GRID_PITCH - lr->width) / 2;
            aroma_node_set_z_index(cap, APP_DRAWER_Z_INDEX + 1);
        }
        if (bake_x || bake_y)
        {
            drawer_shift_subtree(tile, bake_x, bake_y);
            if (cap)
                drawer_shift_subtree(cap, bake_x, bake_y);
        }
        placed++;
    }
    int rows = (placed + DRAWER_GRID_COLS - 1) / DRAWER_GRID_COLS;
    int content_h = 24 + rows * DRAWER_GRID_TILE_H + (rows > 0 ? (rows - 1) * DRAWER_GRID_GAP_Y : 0);
    aroma_container_set_content_size(app_drawer_grid_scroll, WIN_W - 80, content_h);
    aroma_node_invalidate(app_drawer_grid_scroll);
}

static void on_drawer_list_item(int index, void *user_data)
{
    (void)user_data;
    open_drawer_entry(index);
}

static void on_drawer_grid_icon(void *user_data)
{
    open_drawer_entry((int)(intptr_t)user_data);
}

static AromaNode *s_info_dialog = NULL;
static int s_info_kind = -1;
static int s_info_app = -1;
static char s_info_id[AROMA_PACKAGE_ID_MAX] = "";

static void close_info_dialog(void)
{
    if (s_info_dialog)
    {
        aroma_dialog_destroy(s_info_dialog);
        s_info_dialog = NULL;
    }
}

static void on_info_open_action(void *user_data)
{
    (void)user_data;
    int kind = s_info_kind;
    int app = s_info_app;
    char id[AROMA_PACKAGE_ID_MAX];
    snprintf(id, sizeof(id), "%s", s_info_id);
    close_info_dialog();
    if (kind == 0)
    {
        AromaAppPlugin *a = app_registry_get_app(app);
        if (a && a->show)
            a->show(a, a->app_root);
    }
    else if (kind == 1 && id[0])
    {
        vehicle_view_open_package(id);
    }
}

static void on_info_uninstall_action(void *user_data)
{
    (void)user_data;
    char id[AROMA_PACKAGE_ID_MAX];
    snprintf(id, sizeof(id), "%s", s_info_id);
    close_info_dialog();
    if (!id[0])
        return;
    char err[256] = {0};
    if (!package_manager_uninstall(id, err, sizeof(err)))
        return;
    refresh_drawer_list();
    refresh_installed_list();
}

static void on_info_close_action(void *user_data)
{
    (void)user_data;
    close_info_dialog();
}

static void show_app_info(int index)
{
    if (index < 0 || index >= s_drawer_count)
        return;
    close_info_dialog();
    char title[64];
    char msg[1024];
    bool can_uninstall = false;
    if (s_drawer_kind[index] == 0)
    {
        AromaAppPlugin *app = app_registry_get_app(s_drawer_app[index]);
        if (!app || !app->name)
            return;
        snprintf(title, sizeof(title), "%s", app->name);
        snprintf(msg, sizeof(msg), "Built-in app\n%s", app->id ? app->id : "");
        s_info_kind = 0;
        s_info_app = s_drawer_app[index];
        s_info_id[0] = '\0';
    }
    else
    {
        InstalledPackage *pkg = package_manager_get(s_drawer_pkg[index]);
        if (!pkg)
            return;
        snprintf(title, sizeof(title), "%s", pkg->manifest.name);
        char desc[400];
        snprintf(desc, sizeof(desc), "%s", pkg->manifest.description);
        char byline[200];
        if (pkg->manifest.author[0] && pkg->manifest.category[0])
            snprintf(byline, sizeof(byline), "%s - %s", pkg->manifest.author, pkg->manifest.category);
        else
            snprintf(byline, sizeof(byline), "%s%s", pkg->manifest.author, pkg->manifest.category);
        size_t off = 0;
        off += (size_t)snprintf(msg + off, sizeof(msg) - off, "Version %s\n%s",
                                pkg->manifest.version, pkg->manifest.id);
        if (byline[0] && off < sizeof(msg) - 1)
            off += (size_t)snprintf(msg + off, sizeof(msg) - off, "\n%s", byline);
        if (desc[0] && off < sizeof(msg) - 1)
            snprintf(msg + off, sizeof(msg) - off, "\n%s", desc);
        s_info_kind = 1;
        s_info_app = -1;
        snprintf(s_info_id, sizeof(s_info_id), "%s", pkg->manifest.id);
        can_uninstall = !package_manager_is_system(pkg->manifest.id);
    }
    s_info_dialog = aroma_dialog_create(app_drawer, title, msg, 560, 340, DIALOG_TYPE_BASIC);
    if (!s_info_dialog)
        return;
    aroma_dialog_set_font(s_info_dialog, state.ui_font);
    aroma_node_set_z_index(s_info_dialog, APP_DRAWER_Z_INDEX + 10);
    aroma_dialog_add_action(s_info_dialog, "Open", on_info_open_action, NULL);
    if (can_uninstall)
        aroma_dialog_add_action(s_info_dialog, "Uninstall", on_info_uninstall_action, NULL);
    aroma_dialog_add_action(s_info_dialog, "Close", on_info_close_action, NULL);
    aroma_dialog_show(s_info_dialog);
}

static void on_drawer_icon_long_press(void *user_data)
{
    show_app_info((int)(intptr_t)user_data);
}

static void on_drawer_list_long_press(int index, void *user_data)
{
    (void)user_data;
    show_app_info(index);
}

static bool on_drawer_search_changed(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    snprintf(s_drawer_search, sizeof(s_drawer_search), "%s", text ? text : "");
    refresh_drawer_list();
    return true;
}

static void on_drawer_category_changed(AromaNode *tabs, int tab_index, void *user_data)
{
    (void)tabs;
    (void)user_data;
    if (tab_index < 0 || tab_index >= DRAWER_CAT_COUNT)
        return;
    if (tab_index == s_drawer_category)
        return;
    s_drawer_category = tab_index;
    refresh_drawer_list();
}

static void on_drawer_view_toggle(void *user_data)
{
    (void)user_data;
    s_drawer_grid_mode = !s_drawer_grid_mode;
    if (app_drawer_view_toggle)
        aroma_iconbutton_set_icon(app_drawer_view_toggle,
                                  s_drawer_grid_mode ? AROMA_ICON_VIEW_LIST : AROMA_ICON_VIEW_MODULE);
    if (app_drawer_scroll)
        aroma_node_set_hidden(app_drawer_scroll, s_drawer_grid_mode);
    if (app_drawer_list)
        aroma_node_set_hidden(app_drawer_list, s_drawer_grid_mode);
    if (app_drawer_grid_scroll)
        aroma_node_set_hidden(app_drawer_grid_scroll, !s_drawer_grid_mode);

    drawer_reanchor();
    if (s_drawer_grid_mode)
        refresh_drawer_grid();
}

 void refresh_drawer_list(void)
{
    if (!app_drawer_list || !app_drawer_scroll)
        return;

    drawer_reanchor();
    aroma_listview_clear(app_drawer_list);
    s_drawer_count = 0;
    int builtin_added = 0;
    int rc = app_registry_get_app_count();
    for (int i = 0; i < rc && s_drawer_count < DRAWER_LIST_MAX; i++)
    {
        AromaAppPlugin *app = app_registry_get_app(i);
        if (!app || !app->name)
            continue;
        if (!drawer_matches_category(0, -1))
            continue;
        const char *sub = app->id ? app->id : "";
        if (!drawer_matches_search(app->name, sub, "Built-in"))
            continue;
        if (!builtin_added)
        {
            aroma_listview_add_header(app_drawer_list, "Built-in");
            builtin_added = 1;
        }
        const char *icon = (app->icon && app->icon[0]) ? app->icon
                                                      : AROMA_ICON_WIDGETS;
        aroma_listview_add_item_with_icon(app_drawer_list, app->name, sub, icon, NULL);
        s_drawer_kind[s_drawer_count] = 0;
        s_drawer_app[s_drawer_count] = i;
        s_drawer_pkg[s_drawer_count] = -1;
        s_drawer_count++;
    }

    int installed_added = 0;
    for (int i = 0; i < package_manager_count() &&
                    s_drawer_count < DRAWER_LIST_MAX;
         i++)
    {
        InstalledPackage *pkg = package_manager_get(i);
        if (!pkg)
            continue;
        if (!drawer_matches_category(1, i))
            continue;
        const char *cat = pkg->manifest.category[0] ? pkg->manifest.category : "Apps";
        char sub[128];
        snprintf(sub, sizeof(sub), "v%s - %s",
                 pkg->manifest.version[0] ? pkg->manifest.version : "?",
                 cat);
        if (!drawer_matches_search(pkg->manifest.name, sub, cat))
            continue;
        if (!installed_added)
        {
            aroma_listview_add_header(app_drawer_list, "Installed");
            installed_added = 1;
        }
        const char *icon = aroma_icon_codepoint_from_name(pkg->manifest.icon);
        if (!icon || !icon[0])
            icon = AROMA_ICON_WIDGETS;
        aroma_listview_add_item_with_icon(app_drawer_list,
                                          pkg->manifest.name, sub, icon, NULL);
        s_drawer_kind[s_drawer_count] = 1;
        s_drawer_app[s_drawer_count] = -1;
        s_drawer_pkg[s_drawer_count] = i;
        s_drawer_count++;
    }

    if (s_drawer_count == 0)
    {
        aroma_listview_add_header(app_drawer_list, "No results");
        aroma_listview_add_item_with_icon(app_drawer_list, "No apps found",
                                          "Try another search", AROMA_ICON_SEARCH, NULL);
    }

    int content_h = aroma_listview_get_content_height(app_drawer_list);
    AromaRect *lr = aroma_node_get_rect(app_drawer_list);
    if (lr && content_h > 0)
    {
        lr->height = content_h;
        aroma_node_invalidate(app_drawer_list);
    }
    aroma_container_update_auto_content_size(app_drawer_scroll);
    aroma_node_invalidate(app_drawer_scroll);
    refresh_drawer_grid();
}

static void on_drawer_close_finished(AromaNode *target, void *user_data)
{
    (void)target;
    (void)user_data;
    if (!app_drawer_visible && app_drawer)
    {
        aroma_node_set_hidden(app_drawer, true);
        AromaRect *r = aroma_node_get_rect(app_drawer);
        if (r)
        {
            r->x = 0;
            r->y = WIN_H;
        }
        aroma_node_invalidate(app_drawer);
    }
}

static void sync_subtree_caches(AromaNode *node)
{
    if (!node)
        return;
    AromaRect *r = aroma_node_get_rect(node);
    if (r)
    {
        node->layout._cache_x = r->x;
        node->layout._cache_y = r->y;
    }
    for (uint64_t i = 0; i < node->child_count; i++)
        sync_subtree_caches(node->child_nodes[i]);
}

static void open_app_drawer(void)
{
    if (!app_drawer || app_drawer_visible)
        return;
    aroma_node_set_hidden(app_drawer, false);

    refresh_drawer_list();
    sync_subtree_caches(app_drawer);
    AromaRect *r = aroma_node_get_rect(app_drawer);
    float from_y = r ? (float)r->y : (float)WIN_H;
    AromaAnimation *slide_up = aroma_animation_start(app_drawer, AROMA_ANIM_SLIDE_Y,
                                                     from_y, 0.0f, APP_ANIM_MS);
    if (slide_up)
        aroma_animation_set_easing(slide_up, AROMA_EASE_OUT_CUBIC);
    app_drawer_visible = true;
    aroma_node_set_z_index(app_drawer, APP_DRAWER_Z_INDEX);
}

static void close_app_drawer(void)
{
    if (!app_drawer || !app_drawer_visible)
        return;
    close_info_dialog();
    AromaRect *r = aroma_node_get_rect(app_drawer);
    float from_y = r ? (float)r->y : 0.0f;
    AromaAnimation *slide_down = aroma_animation_start(app_drawer, AROMA_ANIM_SLIDE_Y,
                                                       from_y, (float)WIN_H, APP_ANIM_MS);
    if (slide_down)
    {
        aroma_animation_set_easing(slide_down, AROMA_EASE_OUT_CUBIC);
        aroma_animation_set_on_complete(slide_down, on_drawer_close_finished);
    }
    else
    {
        on_drawer_close_finished(app_drawer, NULL);
    }
    app_drawer_visible = false;
}

static void on_app_drawer_button_click(void *user_data)
{
    (void)user_data;
    if (app_drawer_visible)
        close_app_drawer();
    else
        open_app_drawer();
}

static void on_app_drawer_close_click(void *user_data)
{
    (void)user_data;
    close_app_drawer();
}

static void drawer_list_setup(void)
{
    if (!app_drawer)
        return;

    app_drawer_view_toggle = aroma_ui_iconbutton(
        app_drawer, s_drawer_grid_mode ? AROMA_ICON_VIEW_LIST : AROMA_ICON_VIEW_MODULE,
        WIN_W - 104, 16, 40, ICON_BUTTON_OUTLINED,
        on_drawer_view_toggle, NULL, state.drawer_chrome_icon_font);
    if (app_drawer_view_toggle)
        aroma_node_set_z_index(app_drawer_view_toggle, APP_DRAWER_Z_INDEX + 2);

    app_drawer_search_box = aroma_ui_textbox(
        app_drawer, 40, 76, WIN_W - 80, 48, "Search apps",
        on_drawer_search_changed, NULL, state.ui_font);
    if (app_drawer_search_box)
        aroma_node_set_z_index(app_drawer_search_box, APP_DRAWER_Z_INDEX + 2);

    static const char *cat_labels[DRAWER_CAT_COUNT] = {
        "All", "Built-in", "Apps", "Games"};
    app_drawer_cat_tabs = aroma_tabs_create(app_drawer, 40, 136,
                                            WIN_W - 80, 48,
                                            cat_labels, DRAWER_CAT_COUNT);
    if (app_drawer_cat_tabs)
    {
        aroma_node_set_z_index(app_drawer_cat_tabs, APP_DRAWER_Z_INDEX + 2);
        aroma_tabs_set_font(app_drawer_cat_tabs, state.ui_font);
        aroma_tabs_set_on_change(app_drawer_cat_tabs, on_drawer_category_changed, NULL);
        aroma_tabs_setup_events(app_drawer_cat_tabs, aroma_ui_request_redraw, NULL);
        aroma_tabs_set_selected(app_drawer_cat_tabs, s_drawer_category);
    }

    app_drawer_scroll = aroma_container_create(app_drawer, DRAWER_CONTENT_X, DRAWER_CONTENT_Y,
                                               DRAWER_CONTENT_W, WIN_H - DRAWER_CONTENT_Y - 16);
    if (!app_drawer_scroll)
        return;
    aroma_container_set_scrollable(app_drawer_scroll, true);
    aroma_container_set_scroll_direction(app_drawer_scroll,
                                         AROMA_SCROLL_VERTICAL);
    aroma_node_set_z_index(app_drawer_scroll, APP_DRAWER_Z_INDEX + 1);

    app_drawer_list = aroma_listview_create(app_drawer_scroll, 0, 0,
                                            DRAWER_CONTENT_W, WIN_H - DRAWER_CONTENT_Y - 16);
    if (!app_drawer_list)
        return;
    aroma_listview_set_font(app_drawer_list, state.ui_font);
    aroma_listview_set_secondary_font(app_drawer_list, state.ui_font);
    aroma_listview_set_icon_font(app_drawer_list, state.icon_font);
    aroma_listview_set_item_height(app_drawer_list, 64);
    aroma_listview_set_callback(app_drawer_list, on_drawer_list_item, NULL);
    aroma_listview_set_long_press_callback(app_drawer_list, on_drawer_list_long_press, NULL);
    aroma_node_set_z_index(app_drawer_list, APP_DRAWER_Z_INDEX + 1);

    app_drawer_grid_scroll = aroma_container_create(app_drawer, DRAWER_CONTENT_X, DRAWER_CONTENT_Y,
                                                    DRAWER_CONTENT_W, WIN_H - DRAWER_CONTENT_Y - 16);
    if (!app_drawer_grid_scroll)
        return;
    aroma_container_set_scrollable(app_drawer_grid_scroll, true);
    aroma_container_set_scroll_direction(app_drawer_grid_scroll,
                                         AROMA_SCROLL_VERTICAL);
    aroma_node_set_z_index(app_drawer_grid_scroll, APP_DRAWER_Z_INDEX + 1);
    aroma_node_set_hidden(app_drawer_grid_scroll, !s_drawer_grid_mode);
    if (app_drawer_scroll)
        aroma_node_set_hidden(app_drawer_scroll, s_drawer_grid_mode);
}

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
        AromaRect *rect = aroma_node_get_rect(app_drawer);
        if (rect)
        {
            rect->x = 0;
            rect->y = 0;
        }
        aroma_node_set_hidden(app_drawer, false);
        refresh_drawer_list();
        sync_subtree_caches(app_drawer);
        app_drawer_visible = true;
        aroma_node_set_z_index(app_drawer, APP_DRAWER_Z_INDEX);
        aroma_node_invalidate(app_drawer);
    }
    else if (strcmp(what, "settings") == 0)
    {
        const char *want = "com.aroma.settings";
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
    refresh_drawer_list();
    return true;
}

void vehicle_view_remove_package_card(const char *id)
{
    (void)id;
    refresh_drawer_list();
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

static void pkg_set_status(const char *text)
{
    if (s_pkg_status_label && text)
    {
        aroma_label_set_text(s_pkg_status_label, text);
        aroma_ui_request_redraw(NULL);
    }
}

static bool on_pkg_path_changed(AromaNode *node, const char *text,
                                void *user_data)
{
    (void)node;
    (void)user_data;
    snprintf(s_pkg_apak_path, sizeof(s_pkg_apak_path), "%s", text ? text : "");
    return true;
}

#define PKG_ROW_H 64
#define PKG_LIST_W (WIN_W - 112)
#define PKG_PAGE_X 0
#define PKG_PAGE_Y 0
#define PKG_PAGE_W WIN_W
#define PKG_PAGE_H WIN_H
#define PKG_CARD_X 40
#define PKG_CARD_Y 280
#define PKG_CARD_W (WIN_W - 80)
#define PKG_CARD_H 280
#define PKG_LIST_X 16
#define PKG_LIST_Y 48
#define PKG_LIST_H 220

static void pkg_page_reanchor(void)
{
    if (!s_settings_root || !settings_page_packages)
        return;
    AromaRect *rr = aroma_node_get_rect(s_settings_root);
    AromaRect *pr = aroma_node_get_rect(settings_page_packages);
    if (!rr || !pr)
        return;
    pr->x = rr->x + PKG_PAGE_X;
    pr->y = rr->y + PKG_PAGE_Y;
    pr->width = PKG_PAGE_W;
    pr->height = PKG_PAGE_H;
    if (s_installed_card)
    {
        AromaRect *cr = aroma_node_get_rect(s_installed_card);
        if (cr)
        {
            cr->x = pr->x + PKG_CARD_X;
            cr->y = pr->y + PKG_CARD_Y;
            cr->width = PKG_CARD_W;
            cr->height = PKG_CARD_H;
            aroma_node_invalidate(s_installed_card);
        }
    }
    if (s_installed_list && s_installed_card)
    {
        AromaRect *lr = aroma_node_get_rect(s_installed_list);
        AromaRect *cr = aroma_node_get_rect(s_installed_card);
        if (lr && cr)
        {
            lr->x = cr->x + PKG_LIST_X;
            lr->y = cr->y + PKG_LIST_Y;
            lr->width = PKG_LIST_W;
            lr->height = PKG_LIST_H;
            aroma_node_invalidate(s_installed_list);
        }
    }
    aroma_node_invalidate(settings_page_packages);
}

static void refresh_installed_list(void)
{
    if (!s_installed_list || !settings_page_packages)
        return;
    // Rects are absolute screen coordinates. Freshly created rows use
    // parent-relative coordinates and rely on the first layout pass to
    // convert them (each parent's first delta shifts its children into
    // place). Manually baking absolute positions is only correct once the
    // subtree has been laid out at least once. NOTE: this must be tested
    // on the list itself, not the page: the detail page sits at (0,0),
    // whose caches are indistinguishable from "never laid out", so a
    // page-based check would never pass and the list would stay at its
    // creation position (16,48), spilling its rows over the sideload card.
    bool anchored = (s_installed_list->layout._cache_x != 0 ||
                     s_installed_list->layout._cache_y != 0);
    int bake_x = 0;
    int bake_y = 0;
    if (anchored)
    {
        pkg_page_reanchor();
        AromaRect *lr0 = aroma_node_get_rect(s_installed_list);
        if (lr0)
        {
            bake_x = lr0->x;
            bake_y = lr0->y;
        }
    }
    while (s_installed_list->child_count > 0)
    {
        AromaNode *c = s_installed_list->child_nodes[0];
        if (!c)
            break;
        __destroy_node_tree(c);
    }
    int n = package_manager_count();
    if (n <= 0)
    {
        AromaNode *empty = aroma_ui_label(s_installed_list, "No packages installed.",
                                          16, 12,
                                          LABEL_STYLE_LABEL_SMALL, state.ui_font);
        if (empty)
        {
            drawer_shift_subtree(empty, bake_x, bake_y);
            aroma_node_set_z_index(empty, Z_LAYER_STATUS_BAR + 14);
        }
        aroma_container_set_content_size(s_installed_list, PKG_LIST_W, PKG_ROW_H);
        aroma_node_invalidate(s_installed_list);
        if (anchored)
            sync_subtree_caches(s_installed_list);
        return;
    }
    char name_buf[48];
    char sub_buf[160];
    for (int i = 0; i < n; i++)
    {
        InstalledPackage *pkg = package_manager_get(i);
        if (!pkg)
            continue;
        int y = 8 + i * PKG_ROW_H;
        truncate_for_listview(pkg->manifest.name[0] ? pkg->manifest.name : pkg->manifest.id,
                              name_buf, sizeof(name_buf));
        AromaNode *name = aroma_ui_label(s_installed_list, name_buf, 16, y,
                                         LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
        if (name)
        {
            drawer_shift_subtree(name, bake_x, bake_y);
            aroma_node_set_z_index(name, Z_LAYER_STATUS_BAR + 14);
        }
        snprintf(sub_buf, sizeof(sub_buf), "%s v%s",
                 pkg->manifest.id,
                 pkg->manifest.version[0] ? pkg->manifest.version : "?");
        AromaNode *sub = aroma_ui_label(s_installed_list, sub_buf, 16, y + 26,
                                        LABEL_STYLE_LABEL_SMALL, state.ui_font);
        if (sub)
        {
            aroma_label_set_color(sub, state.theme.colors.text_secondary);
            drawer_shift_subtree(sub, bake_x, bake_y);
            aroma_node_set_z_index(sub, Z_LAYER_STATUS_BAR + 14);
        }
        if (package_manager_is_system(pkg->manifest.id))
        {
            AromaNode *tag = aroma_ui_label(s_installed_list, "System",
                                            PKG_LIST_W - 110, y + 14,
                                            LABEL_STYLE_LABEL_SMALL, state.ui_font);
            if (tag)
            {
                aroma_label_set_color(tag, state.theme.colors.text_secondary);
                drawer_shift_subtree(tag, bake_x, bake_y);
                aroma_node_set_z_index(tag, Z_LAYER_STATUS_BAR + 14);
            }
        }
        else
        {
            AromaNode *btn = aroma_ui_button(s_installed_list, "Uninstall",
                                             PKG_LIST_W - 126, y + 14, 110, 36,
                                             on_pkg_uninstall_click,
                                             (void *)(intptr_t)i, state.ui_font);
            if (btn)
            {
                drawer_shift_subtree(btn, bake_x, bake_y);
                aroma_node_set_z_index(btn, Z_LAYER_STATUS_BAR + 14);
            }
        }
    }
    aroma_container_set_content_size(s_installed_list, PKG_LIST_W, 16 + n * PKG_ROW_H);
    aroma_node_invalidate(s_installed_list);
    if (anchored)
        sync_subtree_caches(s_installed_list);
}

static bool on_pkg_uninstall_click(AromaNode *node, void *user_data)
{
    (void)node;
    int idx = (int)(intptr_t)user_data;
    InstalledPackage *pkg = package_manager_get(idx);
    if (!pkg || !pkg->manifest.id[0])
        return true;
    char id[AROMA_PACKAGE_ID_MAX];
    snprintf(id, sizeof(id), "%s", pkg->manifest.id);
    char err[256] = {0};
    if (!package_manager_uninstall(id, err, sizeof(err)))
    {
        char buf[320];
        snprintf(buf, sizeof(buf), "Remove failed: %s",
                 err[0] ? err : "unknown error");
        pkg_set_status(buf);
        return true;
    }
    refresh_drawer_list();
    refresh_installed_list();
    char buf[320];
    snprintf(buf, sizeof(buf), "Removed %s", id);
    pkg_set_status(buf);
    return true;
}

static bool on_pkg_install_click(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    if (!s_pkg_apak_path[0])
    {
        pkg_set_status("Enter a path to an .apak file first");
        return true;
    }
    char err[256] = {0};
    if (!package_manager_install_apak(s_pkg_apak_path, err, sizeof(err)))
    {
        char buf[320];
        snprintf(buf, sizeof(buf), "Install failed: %s",
                 err[0] ? err : "unknown error");
        pkg_set_status(buf);
        return true;
    }
    const char *id = package_manager_last_installed_id();
    if (id && !vehicle_view_add_package_card(id))
    {
        char buf[320];
        snprintf(buf, sizeof(buf), "Installed %s but UI failed to load", id);
        pkg_set_status(buf);
        return true;
    }
    char buf[320];
    snprintf(buf, sizeof(buf), "Installed %s - find it in the app drawer",
             id ? id : "package");
    pkg_set_status(buf);
    refresh_installed_list();
    return true;
}

static void build_settings_ui(AromaNode *settings_root)
{
    if (!settings_root)
        return;
    s_settings_root = settings_root;
    s_settings_search[0] = '\0';
    s_settings_row_count = 0;
    settings_nav_animating = false;
    settings_nav_from_page = NULL;
    settings_current_page = NULL;

    AromaNode *settings_close_btn = aroma_ui_iconbutton(
        settings_root, AROMA_ICON_CLOSE, WIN_W - 68, 20, 48, ICON_BUTTON_FILLED,
        close_settings, settings_root, state.icon_font);
    aroma_node_set_z_index(settings_close_btn, Z_LAYER_STATUS_BAR + 16);

    // --- Main page: iOS-style grouped list with search --------------------
    settings_main_page = aroma_ui_container(
        settings_root, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(settings_main_page, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(settings_main_page, true);

    AromaNode *settings_main_title = aroma_ui_label(
        settings_main_page, "Settings",
        40, 70, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(settings_main_title, Z_LAYER_STATUS_BAR + 13);

    AromaNode *settings_search_box = aroma_ui_textbox(
        settings_main_page, 40, 120, WIN_W - 80, 48, "Search settings",
        on_settings_search_changed, NULL, state.ui_font);
    aroma_node_set_z_index(settings_search_box, Z_LAYER_STATUS_BAR + 13);

    settings_list_scroll = aroma_container_create(
        settings_main_page, 40, 180, WIN_W - 80, WIN_H - 180 - 16);
    aroma_container_set_scrollable(settings_list_scroll, true);
    aroma_container_set_scroll_direction(settings_list_scroll, AROMA_SCROLL_VERTICAL);
    aroma_node_set_z_index(settings_list_scroll, Z_LAYER_STATUS_BAR + 12);

    settings_list = aroma_listview_create(settings_list_scroll, 0, 0,
                                          WIN_W - 80, WIN_H - 180 - 16);
    aroma_listview_set_font(settings_list, state.ui_font);
    aroma_listview_set_secondary_font(settings_list, state.ui_font);
    aroma_listview_set_icon_font(settings_list, state.icon_font);
    aroma_listview_set_item_height(settings_list, 64);
    aroma_listview_set_callback(settings_list, on_settings_list_item, NULL);
    aroma_node_set_z_index(settings_list, Z_LAYER_STATUS_BAR + 13);
    refresh_settings_list();
    settings_current_page = settings_main_page;

    // --- Detail pages (full-screen, pushed with back button) ---------------
    settings_page_general = aroma_ui_container(
        settings_root, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(settings_page_general, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(settings_page_general, true);

    AromaNode *general_back = aroma_ui_iconbutton(
        settings_page_general, AROMA_ICON_ARROW_BACK, 20, 20, 48, ICON_BUTTON_OUTLINED,
        settings_go_back, NULL, state.icon_font);
    aroma_node_set_z_index(general_back, Z_LAYER_STATUS_BAR + 14);
    AromaNode *general_title = aroma_ui_label(
        settings_page_general, "General",
        80, 28, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(general_title, Z_LAYER_STATUS_BAR + 13);

    AromaNode *general_card = aroma_ui_card(
        settings_page_general, 40, 100, WIN_W - 80, 80, CARD_TYPE_FILLED);
    aroma_node_set_z_index(general_card, Z_LAYER_STATUS_BAR + 13);

    AromaNode *bt_icon = aroma_ui_icon(general_card, AROMA_ICON_BLUETOOTH, 40, 24, 32, IOS_COLOR_BLUE, state.icon_font);
    AromaNode *bt_label = aroma_ui_label(general_card, "Bluetooth", 90, 28, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    settings_bluetooth_switch = aroma_ui_switch(general_card, WIN_W - 172, 18, 72, 44, true, on_settings_bluetooth_changed, NULL);
    aroma_node_set_z_index(bt_icon, Z_LAYER_STATUS_BAR + 14);
    aroma_node_set_z_index(bt_label, Z_LAYER_STATUS_BAR + 14);
    aroma_node_set_z_index(settings_bluetooth_switch, Z_LAYER_STATUS_BAR + 14);

    bt_info_card = aroma_ui_card(
        settings_page_general, 40, 200, WIN_W - 80, 120, CARD_TYPE_FILLED);
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
        settings_root, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(settings_page_display, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(settings_page_display, true);

    AromaNode *display_back = aroma_ui_iconbutton(
        settings_page_display, AROMA_ICON_ARROW_BACK, 20, 20, 48, ICON_BUTTON_OUTLINED,
        settings_go_back, NULL, state.icon_font);
    aroma_node_set_z_index(display_back, Z_LAYER_STATUS_BAR + 14);
    AromaNode *display_title = aroma_ui_label(
        settings_page_display, "Display",
        80, 28, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(display_title, Z_LAYER_STATUS_BAR + 13);

    AromaNode *display_card = aroma_ui_card(
        settings_page_display, 40, 100, WIN_W - 80, 80, CARD_TYPE_FILLED);
    aroma_node_set_z_index(display_card, Z_LAYER_STATUS_BAR + 13);

    AromaNode *dark_mode_icon = aroma_ui_icon(display_card, AROMA_ICON_PALETTE, 40, 24, 32, IOS_COLOR_PURPLE, state.icon_font);
    AromaNode *dark_mode_label = aroma_ui_label(display_card, "Dark Mode", 90, 28, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    settings_dark_mode_switch = aroma_ui_switch(display_card, WIN_W - 172, 18, 72, 44, false, on_dark_mode_switch_changed, NULL);
    aroma_node_set_z_index(dark_mode_icon, Z_LAYER_STATUS_BAR + 14);
    aroma_node_set_z_index(dark_mode_label, Z_LAYER_STATUS_BAR + 14);
    aroma_node_set_z_index(settings_dark_mode_switch, Z_LAYER_STATUS_BAR + 14);

    AromaNode *aa_card = aroma_ui_card(
        settings_page_display, 40, 200, WIN_W - 80, 80, CARD_TYPE_FILLED);
    aroma_node_set_z_index(aa_card, Z_LAYER_STATUS_BAR + 13);

    AromaNode *aa_icon = aroma_ui_icon(aa_card, AROMA_ICON_HD, 40, 24, 32, IOS_COLOR_BLUE, state.icon_font);
    AromaNode *aa_label = aroma_ui_label(aa_card, "3D Antialiasing", 90, 28, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    settings_aa_switch = aroma_ui_switch(aa_card, WIN_W - 172, 18, 72, 44,
                                         aroma_3d_get_antialiasing(),
                                         on_settings_aa_changed, NULL);
    aroma_node_set_z_index(aa_icon, Z_LAYER_STATUS_BAR + 14);
    aroma_node_set_z_index(aa_label, Z_LAYER_STATUS_BAR + 14);
    aroma_node_set_z_index(settings_aa_switch, Z_LAYER_STATUS_BAR + 14);

    settings_page_updates = aroma_ui_container(
        settings_root, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(settings_page_updates, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(settings_page_updates, true);

    AromaNode *updates_back = aroma_ui_iconbutton(
        settings_page_updates, AROMA_ICON_ARROW_BACK, 20, 20, 48, ICON_BUTTON_OUTLINED,
        settings_go_back, NULL, state.icon_font);
    aroma_node_set_z_index(updates_back, Z_LAYER_STATUS_BAR + 14);
    AromaNode *updates_title = aroma_ui_label(
        settings_page_updates, "Software Update",
        80, 28, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(updates_title, Z_LAYER_STATUS_BAR + 13);

    AromaNode *updates_card = aroma_ui_card(
        settings_page_updates, 40, 100, WIN_W - 80, 340, CARD_TYPE_FILLED);
    aroma_node_set_z_index(updates_card, Z_LAYER_STATUS_BAR + 13);

    AromaNode *logo_icon = aroma_ui_icon(updates_card, AROMA_ICON_MEMORY, (WIN_W - 200) / 2 - 50, 20, 60, IOS_COLOR_BLUE, state.huge_icon_font);
    aroma_node_set_z_index(logo_icon, Z_LAYER_STATUS_BAR + 14);

    AromaNode *os_name_label = aroma_ui_label(updates_card, "Aroma Infotainment OS", (WIN_W - 350) / 2 - 50, 100, LABEL_STYLE_LABEL_LARGE, state.settings_font);
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

    settings_page_packages = aroma_ui_container(
        settings_root, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(settings_page_packages, Z_LAYER_STATUS_BAR + 12);
    aroma_node_set_hidden(settings_page_packages, true);

    AromaNode *packages_back = aroma_ui_iconbutton(
        settings_page_packages, AROMA_ICON_ARROW_BACK, 20, 20, 48, ICON_BUTTON_OUTLINED,
        settings_go_back, NULL, state.icon_font);
    aroma_node_set_z_index(packages_back, Z_LAYER_STATUS_BAR + 14);
    AromaNode *packages_title = aroma_ui_label(
        settings_page_packages, "Packages",
        80, 28, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(packages_title, Z_LAYER_STATUS_BAR + 13);

    AromaNode *sideload_card = aroma_ui_card(
        settings_page_packages, 40, 100, WIN_W - 80, 160, CARD_TYPE_FILLED);
    aroma_node_set_z_index(sideload_card, Z_LAYER_STATUS_BAR + 13);

    AromaNode *sideload_icon = aroma_ui_icon(sideload_card, AROMA_ICON_FILE_DOWNLOAD, 40, 20, 32, IOS_COLOR_BLUE, state.icon_font);
    AromaNode *sideload_label = aroma_ui_label(sideload_card, "Install package file (.apak)", 90, 25, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(sideload_icon, Z_LAYER_STATUS_BAR + 14);
    aroma_node_set_z_index(sideload_label, Z_LAYER_STATUS_BAR + 14);

    AromaNode *sideload_entry = aroma_ui_textbox(sideload_card, 40, 62, 440, 36, "/path/to/app.apak",
                                                 on_pkg_path_changed, NULL, state.ui_font);
    aroma_node_set_z_index(sideload_entry, Z_LAYER_STATUS_BAR + 14);
    AromaNode *sideload_btn = aroma_ui_button(sideload_card, "Install", 490, 62, 120, 36,
                                              on_pkg_install_click, NULL, state.ui_font);
    aroma_node_set_z_index(sideload_btn, Z_LAYER_STATUS_BAR + 14);
    s_pkg_status_label = aroma_ui_label(sideload_card, "Pick an .apak file, then Install.",
                                        40, 112, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(s_pkg_status_label, Z_LAYER_STATUS_BAR + 14);

    AromaNode *installed_card = aroma_ui_card(
        settings_page_packages, PKG_CARD_X, PKG_CARD_Y, PKG_CARD_W, PKG_CARD_H, CARD_TYPE_FILLED);
    aroma_node_set_z_index(installed_card, Z_LAYER_STATUS_BAR + 13);
    s_installed_card = installed_card;

    AromaNode *installed_label = aroma_ui_label(installed_card, "Installed", 40, 12,
                                                LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(installed_label, Z_LAYER_STATUS_BAR + 14);

    s_installed_list = aroma_container_create(installed_card, PKG_LIST_X, PKG_LIST_Y, PKG_LIST_W, PKG_LIST_H);
    if (s_installed_list)
    {
        aroma_container_set_scrollable(s_installed_list, true);
        aroma_container_set_scroll_direction(s_installed_list, AROMA_SCROLL_VERTICAL);
        aroma_node_set_z_index(s_installed_list, Z_LAYER_STATUS_BAR + 14);
    }
    refresh_installed_list();

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

    if (!vehicle_view_set_bluetooth_enabled(true))
        fprintf(stderr, "[BT] bluetooth services unavailable at boot\n");

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

    aroma_3d_init();
    state.viewer_3d = aroma_3d_viewer_create(
        state.vehicle_view_root, 0, 0, WIN_W, WIN_H);
    if (state.viewer_3d)
    {
        aroma_node_set_z_index(state.viewer_3d, Z_LAYER_BACKGROUND + 1);
        aroma_3d_viewer_set_interactive(state.viewer_3d, false);

        Aroma3DModel *placeholder = aroma_3d_create_cube();
        if (placeholder)
            apply_vehicle_model_to_viewer(placeholder, 0.75f, 0.85f);

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

    state.vehicle_view_clock_gauge = NULL;
    state.vehicle_view_ampm_label = NULL;

    state.vehicle_view_clock_label = aroma_ui_label(
        state.vehicle_view_root, "--:--",
        WIN_W / 2 - 150, 8, LABEL_STYLE_LABEL_LARGE, state.clock_font);
    aroma_node_set_z_index(state.vehicle_view_clock_label, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_ampm_label = aroma_ui_label(
        state.vehicle_view_root, "AM",
        WIN_W / 2 + 130, 24, LABEL_STYLE_LABEL_MEDIUM, state.clock_pm_am_font);
    aroma_node_set_z_index(state.vehicle_view_ampm_label, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.vehicle_view_clock_date_label = aroma_ui_label(
        state.vehicle_view_root, "",
        WIN_W / 2 - 150, 88, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_clock_date_label, Z_LAYER_VEHICLE_OVERLAYS + 2);
    update_digital_clock_colors();

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

    int media_card_width = WIN_W - 110 - 20;
    media_ui.media_card = aroma_ui_card(
        state.vehicle_view_root, 110, WIN_H - 110, media_card_width, 80, CARD_TYPE_GLASS);
    aroma_node_set_z_index(media_ui.media_card, Z_LAYER_VEHICLE_OVERLAYS + 2);
    aroma_node_set_hidden(media_ui.media_card, true);

    AromaNode *music_icon_btn = aroma_ui_iconbutton(
        media_ui.media_card, AROMA_ICON_MUSIC_NOTE,
        18, 18, 44, ICON_BUTTON_FILLED,
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

    int media_next_x = media_card_width - 49 - 44;
    int media_play_x = media_next_x - 52;
    int media_prev_x = media_play_x - 52;

    media_ui.media_prev_button = aroma_ui_iconbutton(
        media_ui.media_card, AROMA_ICON_SKIP_PREVIOUS,
        media_prev_x, 18, 44, ICON_BUTTON_OUTLINED,
        on_media_prev_click, NULL, state.icon_font);
    aroma_node_set_z_index(media_ui.media_prev_button, Z_LAYER_VEHICLE_OVERLAYS + 3);

    media_ui.media_play_pause_button = aroma_ui_iconbutton(
        media_ui.media_card, AROMA_ICON_PLAY_ARROW,
        media_play_x, 18, 44, ICON_BUTTON_OUTLINED,
        on_media_play_pause_click, NULL, state.icon_font);
    aroma_node_set_z_index(media_ui.media_play_pause_button, Z_LAYER_VEHICLE_OVERLAYS + 3);

    media_ui.media_next_button = aroma_ui_iconbutton(
        media_ui.media_card, AROMA_ICON_SKIP_NEXT,
        media_next_x, 18, 44, ICON_BUTTON_OUTLINED,
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

    AromaNode *ac_minus = aroma_ui_iconbutton(state.bottom_bar, AROMA_ICON_REMOVE, 18, 172, 44, ICON_BUTTON_FILLED, ac_temp_down_callback, NULL, state.icon_font);
    aroma_node_set_z_index(ac_minus, Z_LAYER_VEHICLE_OVERLAYS + 2);
    AromaNode *ac_temp_label = aroma_ui_label(state.bottom_bar, "22C", 22, 222, LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(ac_temp_label, Z_LAYER_VEHICLE_OVERLAYS + 2);
    state.ac_temp_label = ac_temp_label;
    AromaNode *ac_plus = aroma_ui_iconbutton(state.bottom_bar, AROMA_ICON_ADD, 18, 258, 44, ICON_BUTTON_FILLED, ac_temp_up_callback, NULL, state.icon_font);
    aroma_node_set_z_index(ac_plus, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.tire_button = aroma_ui_iconbutton(state.vehicle_view_root, AROMA_ICON_INFO, 393, 393, 44, ICON_BUTTON_FILLED, tire_cycle_callback, NULL, state.icon_font);
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
        180, 8, 36, ICON_BUTTON_OUTLINED,
        tire_exit_check_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.tire_close_btn, Z_LAYER_CARDS_TOP + 1);

    AromaNode *fl_btn = aroma_ui_button(state.tire_card, "FL", 30, 66, 100, 44, tire_select_callback, (void *)1, state.ui_font);
    aroma_node_set_z_index(fl_btn, Z_LAYER_CARDS_TOP + 1);

    AromaNode *fr_btn = aroma_ui_button(state.tire_card, "FR", 125, 66, 100, 44, tire_select_callback, (void *)0, state.ui_font);
    aroma_node_set_z_index(fr_btn, Z_LAYER_CARDS_TOP + 1);

    AromaNode *rl_btn = aroma_ui_button(state.tire_card, "RL", 30, 118, 100, 44, tire_select_callback, (void *)3, state.ui_font);
    aroma_node_set_z_index(rl_btn, Z_LAYER_CARDS_TOP + 1);

    AromaNode *rr_btn = aroma_ui_button(state.tire_card, "RR", 125, 118, 100, 44, tire_select_callback, (void *)2, state.ui_font);
    aroma_node_set_z_index(rr_btn, Z_LAYER_CARDS_TOP + 1);

    state.interior_ac_btn = aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_AC_UNIT,
        446, 276, 44, ICON_BUTTON_FILLED,
        ac_interior_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.interior_ac_btn, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.ac_controls_btn = aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_SETTINGS,
        86, 196, 44, ICON_BUTTON_FILLED,
        toggle_ac_controls_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.ac_controls_btn, Z_LAYER_VEHICLE_OVERLAYS + 2);
    aroma_node_set_hidden(state.ac_controls_btn, true);

    state.ac_controls_card = aroma_ui_card(
        state.vehicle_view_root, 90, 230, 260, 250, CARD_TYPE_GLASS);
    aroma_node_set_z_index(state.ac_controls_card, Z_LAYER_CARDS_TOP);
    aroma_node_set_hidden(state.ac_controls_card, true);

    AromaNode *ac_icon = aroma_ui_icon(
        state.ac_controls_card, AROMA_ICON_AC_UNIT, 20, 20, 28,
        state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(ac_icon, Z_LAYER_CARDS_TOP + 1);

    AromaNode *climate_label = aroma_ui_label(
        state.ac_controls_card, "Climate", 62, 18,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(climate_label, Z_LAYER_CARDS_TOP + 1);

    state.ac_controls_temp_label = aroma_ui_label(
        state.ac_controls_card, "22C", 50, 50,
        LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.ac_controls_temp_label, Z_LAYER_CARDS_TOP + 1);

    state.ac_temp_up_btn = aroma_ui_iconbutton(
        state.ac_controls_card, AROMA_ICON_ADD, 196, 36, 44, ICON_BUTTON_OUTLINED,
        ac_temp_up_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.ac_temp_up_btn, Z_LAYER_CARDS_TOP + 1);

    state.ac_temp_down_btn = aroma_ui_iconbutton(
        state.ac_controls_card, AROMA_ICON_REMOVE, 196, 88, 44, ICON_BUTTON_OUTLINED,
        ac_temp_down_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.ac_temp_down_btn, Z_LAYER_CARDS_TOP + 1);

    AromaNode *fan_speed_label = aroma_ui_label(
        state.ac_controls_card, "Fan Speed", 20, 104,
        LABEL_STYLE_LABEL_SMALL, state.ui_font);
    aroma_node_set_z_index(fan_speed_label, Z_LAYER_CARDS_TOP + 1);

    state.fan_up_btn = aroma_ui_iconbutton(
        state.ac_controls_card, AROMA_ICON_ARROW_UPWARD, 196, 134, 44, ICON_BUTTON_OUTLINED,
        fan_up_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.fan_up_btn, Z_LAYER_CARDS_TOP + 1);

    state.fan_down_btn = aroma_ui_iconbutton(
        state.ac_controls_card, AROMA_ICON_ARROW_DOWNWARD, 196, 186, 44, ICON_BUTTON_OUTLINED,
        fan_down_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.fan_down_btn, Z_LAYER_CARDS_TOP + 1);

    state.ac_mode_btn = aroma_ui_button(
        state.ac_controls_card, "Auto", 20, 134, 90, 44,
        ac_mode_callback, NULL, state.ui_font);
    aroma_node_set_z_index(state.ac_mode_btn, Z_LAYER_CARDS_TOP + 1);

    state.ac_power_btn = aroma_ui_button(
        state.ac_controls_card, "AC", 118, 134, 64, 44,
        ac_power_callback, NULL, state.ui_font);
    aroma_node_set_z_index(state.ac_power_btn, Z_LAYER_CARDS_TOP + 1);

    state.seat_controls_btn = aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_AIRLINE_SEAT_LEGROOM_EXTRA,
        23, WIN_H - 72, 44, ICON_BUTTON_FILLED,
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
        WIN_W - 56, 16, 40, ICON_BUTTON_FILLED,
        on_app_drawer_close_click, NULL, state.drawer_chrome_icon_font);
    aroma_node_set_z_index(app_drawer_close_btn, APP_DRAWER_Z_INDEX + 2);

    app_drawer_title = aroma_ui_label(
        app_drawer, "All Apps",
        40, 20, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(app_drawer_title, APP_DRAWER_Z_INDEX + 1);

    int app_count = app_registry_get_app_count();
    for (int i = 0; i < app_count; i++) {
        AromaAppPlugin *app = app_registry_get_app(i);
        app->drawer_card = NULL;

        app->app_root = aroma_ui_card(state.vehicle_view_root, 0, 0, WIN_W, WIN_H, CARD_TYPE_ELEVATED);
        aroma_node_set_z_index(app->app_root, APP_DRAWER_Z_INDEX + 10);
        aroma_node_set_hidden(app->app_root, true);

        if (app->build_ui) {
            app->build_ui(app, app->app_root);
        }
    }
    drawer_list_setup();
    refresh_drawer_list();

    for (int i = 0; i < package_manager_count(); i++) {
        InstalledPackage *pkg = package_manager_get(i);
        if (pkg && !vehicle_view_add_package_card(pkg->manifest.id))
            fprintf(stderr, "[packages] skipping %s\n", pkg->manifest.id);
    }

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

    if (state.camera_animating && state.viewer_3d)
    {
        Aroma3DCamera cam;
        aroma_3d_viewer_get_camera(state.viewer_3d, &cam);
        float t = 0.08f;
        float dtheta_step = state.anim_target_theta - cam.theta;
        while (dtheta_step > 3.141592653589793f)
            dtheta_step -= 6.283185307179586f;
        while (dtheta_step < -3.141592653589793f)
            dtheta_step += 6.283185307179586f;
        cam.theta += dtheta_step * t;
        cam.phi += (state.anim_target_phi - cam.phi) * t;
        cam.radius += (state.anim_target_radius - cam.radius) * t;
        cam.target[0] += (state.anim_target_x - cam.target[0]) * t;
        cam.target[1] += (state.anim_target_y - cam.target[1]) * t;
        cam.target[2] += (state.anim_target_z - cam.target[2]) * t;

        float dtheta = state.anim_target_theta - cam.theta;
        while (dtheta > 3.141592653589793f)
            dtheta -= 6.283185307179586f;
        while (dtheta < -3.141592653589793f)
            dtheta += 6.283185307179586f;
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
            memcpy(&locked_vehicle_camera, &cam, sizeof(Aroma3DCamera));
            has_locked_vehicle_camera = true;

            if (state.startup_animating)
            {
                state.startup_animating = false;
                aroma_3d_viewer_set_auto_rotate(state.viewer_3d, false);
            }
        }
        aroma_3d_viewer_set_camera(state.viewer_3d, &cam);
    }

    if (state.viewer_3d)
    {
        aroma_3d_viewer_update(state.viewer_3d);
    }
}
