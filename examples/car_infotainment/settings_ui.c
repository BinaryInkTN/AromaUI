#include "settings_ui.h"
#include "app_state.h"
#include "aroma_animation.h"
#include "ui_animation_utils.h"
#include "theme_manager.h"
#include "tabs_manager.h"
#include "voice_handler.h"
#include "bt_speaker_api.h"
#include "bt_speaker_hfp.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

#ifndef safe_strncpy
#define safe_strncpy(dest, src, n)           \
    do                                       \
    {                                        \
        if ((dest) && (n) > 0)               \
        {                                    \
            strncpy((dest), (src), (n) - 1); \
            (dest)[(n) - 1] = '\0';          \
        }                                    \
    } while (0)
#endif

#define SETTINGS_TAB_SHIFT_DENOM 3
#define MAX_PROCESSOR_NAME_LEN 256
#define MAX_RAM_STR_LEN 64
#define MAX_UPTIME_STR_LEN 64
#define MAX_LOAD_STR_LEN 64
#define MAX_TIME_STR_LEN 64
#define MAX_ERROR_MSG_LEN 256
#define MAX_INFO_STR_LEN 256
#define BEEP_DURATION_NS 150000000L
#define BEEP_FREQ_HZ 1200
#define EASTER_EGG_CLICKS 7
#define EASTER_EGG_VOICE_THRESHOLD 2
#define BT_MONITOR_INTERVAL_US 200000
#define MAX_SERVICE_NAME_LEN 128
#define MAX_SERVICE_STATUS_LEN 64
#define MAX_SERVICE_DESC_LEN 256
#define SERVICES_REFRESH_INTERVAL_US 2000000
#define SEAT_REFRESH_INTERVAL_US 500000

extern pthread_mutex_t contact_list_lock;

typedef void (*item_action_fn)(void);

typedef struct
{
    int section;
    int item;
    item_action_fn action;
} ListItemAction;

typedef struct
{
    AromaNode *status_card;
    AromaNode *status_icon;
    AromaNode *status_label;
    AromaNode *device_info_card;
    AromaNode *device_name_label;
    AromaNode *device_address_label;
    AromaNode *device_manufacturer_label;
    AromaNode *stats_label;
    AromaNode *audio_status_label;
    AromaNode *media_info_card;
    AromaNode *media_title_label;
    AromaNode *media_artist_label;
    AromaNode *media_album_label;
    AromaNode *disconnect_button;
    bt_state_t current_state;
    bt_media_info_t current_media;
    bool initialized;
    bool init_in_progress;
    bool monitor_running;
    bool ui_needs_update;
    pthread_mutex_t lock;
    pthread_t init_thread;
    pthread_t monitor_thread;
    pthread_cond_t update_cond;
} BluetoothUI;

typedef struct
{
    char name[MAX_SERVICE_NAME_LEN];
    char load_state[MAX_SERVICE_STATUS_LEN];
    char active_state[MAX_SERVICE_STATUS_LEN];
    char sub_state[MAX_SERVICE_STATUS_LEN];
    uint32_t main_pid;
    uint64_t memory_current;
    uint64_t cpu_usage;
    uint64_t active_enter_timestamp;
    bool is_active;
    bool is_running;
} ServiceStatus;

typedef struct
{
    AromaNode *status_card;
    AromaNode *status_icon;
    AromaNode *status_label;
    AromaNode *detail_card;
    AromaNode *pid_label;
    AromaNode *memory_label;
    AromaNode *cpu_label;
    AromaNode *load_state_label;
    AromaNode *active_state_label;
    AromaNode *sub_state_label;
    AromaNode *uptime_label;
    AromaNode *start_button;
    AromaNode *stop_button;
    AromaNode *restart_button;
    AromaNode *enable_button;
    AromaNode *disable_button;
    ServiceStatus current_status;
    bool initialized;
    bool monitor_running;
    bool ui_needs_update;
    pthread_mutex_t lock;
    pthread_t monitor_thread;
    pthread_cond_t update_cond;
} ServicesUI;

typedef struct
{
    int16_t position;
    uint8_t profile;
    uint8_t occupied;
} SeatStatus;

typedef struct
{
    AromaNode *status_card;
    AromaNode *status_icon;
    AromaNode *status_label;
    AromaNode *driver_position_label;
    AromaNode *driver_profile_label;
    AromaNode *driver_occupied_label;
    AromaNode *passenger_position_label;
    AromaNode *passenger_profile_label;
    AromaNode *passenger_occupied_label;
    AromaNode *driver_slider;
    AromaNode *passenger_slider;
    AromaNode *profile1_btn;
    AromaNode *profile2_btn;
    AromaNode *profile3_btn;
    AromaNode *manual_btn;
    AromaNode *driver_forward_btn;
    AromaNode *driver_backward_btn;
    AromaNode *driver_up_btn;
    AromaNode *driver_down_btn;
    AromaNode *passenger_forward_btn;
    AromaNode *passenger_backward_btn;
    AromaNode *passenger_up_btn;
    AromaNode *passenger_down_btn;
    SeatStatus driver_status;
    SeatStatus passenger_status;
    bool initialized;
    bool monitor_running;
    bool ui_needs_update;
    pthread_mutex_t lock;
    pthread_t monitor_thread;
    pthread_cond_t update_cond;
} SeatUI;

static BluetoothUI bt_ui = {
    .current_state = BT_STATE_IDLE,
    .initialized = false,
    .init_in_progress = false,
    .monitor_running = false,
    .ui_needs_update = false,
    .current_media = {0},
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .update_cond = PTHREAD_COND_INITIALIZER};

static ServicesUI svc_ui = {
    .initialized = false,
    .monitor_running = false,
    .ui_needs_update = false,
    .current_status = {0},
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .update_cond = PTHREAD_COND_INITIALIZER};

static SeatUI seat_ui = {
    .initialized = false,
    .monitor_running = false,
    .ui_needs_update = false,
    .driver_status = {85, 1, 1},
    .passenger_status = {95, 0, 0},
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .update_cond = PTHREAD_COND_INITIALIZER};

static void action_toggle_theme(void) { toggle_theme(); }
static void action_dev_easter_egg(void) {}

static const ListItemAction item_action_table[] = {
    {1, 1, action_toggle_theme},
};
static const int item_action_table_size =
    (int)(sizeof(item_action_table) / sizeof(item_action_table[0]));

 pthread_mutex_t easter_egg_lock = PTHREAD_MUTEX_INITIALIZER;

static void log_bluetooth_error(const char *fmt, ...)
{
    char msg[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    printf("[BT SPEAKER UI] ERROR: %s\n", msg);

    if (state.settings_root)
    {
        aroma_ui_snackbar(state.settings_root, msg, 5000, state.settings_font);
    }
}

static void init_bluetooth_async(void);
static void update_bluetooth_ui(void);
static void bt_state_changed_handler(bt_state_t old_state, bt_state_t new_state, void *user_data);
static void bt_device_callback(const bt_device_info_t *device, bool connected, void *user_data);
static void bt_error_handler(bt_error_t error, const char *message, void *user_data);
static void bt_avrcp_callback(const bt_media_info_t *media, void *user_data);
static bool on_bt_disconnect_click(AromaNode *node, void *user_data);
static AromaNode *build_bluetooth_page(AromaNode *parent, int panel_w, int area_h);
static void *bt_init_thread_func(void *arg);
static void *bt_monitor_thread_func(void *arg);
static void schedule_ui_update(void);
static void init_services_async(void);
static void update_services_ui(void);
static void *services_monitor_thread_func(void *arg);
static void schedule_services_ui_update(void);
static AromaNode *build_services_page(AromaNode *parent, int panel_w, int area_h);
static bool on_service_start_click(AromaNode *node, void *user_data);
static bool on_service_stop_click(AromaNode *node, void *user_data);
static bool on_service_restart_click(AromaNode *node, void *user_data);
static bool on_service_enable_click(AromaNode *node, void *user_data);
static bool on_service_disable_click(AromaNode *node, void *user_data);
static void execute_service_command(const char *command);
static void refresh_service_status(void);
static void init_seat_async(void);
static void update_seat_ui(void);
static void *seat_monitor_thread_func(void *arg);
static void schedule_seat_ui_update(void);
static AromaNode *build_seat_page(AromaNode *parent, int panel_w, int area_h);
static bool on_driver_position_change(AromaNode *node, void *user_data);
static bool on_passenger_position_change(AromaNode *node, void *user_data);
static bool on_seat_profile_click(AromaNode *node, void *user_data);
static void on_seat_nudge_click(void *user_data);

static void schedule_ui_update(void)
{
    pthread_mutex_lock(&bt_ui.lock);
    bt_ui.ui_needs_update = true;
    pthread_cond_signal(&bt_ui.update_cond);
    pthread_mutex_unlock(&bt_ui.lock);
}

static void schedule_services_ui_update(void)
{
    pthread_mutex_lock(&svc_ui.lock);
    svc_ui.ui_needs_update = true;
    pthread_cond_signal(&svc_ui.update_cond);
    pthread_mutex_unlock(&svc_ui.lock);
}

static void schedule_seat_ui_update(void)
{
    pthread_mutex_lock(&seat_ui.lock);
    seat_ui.ui_needs_update = true;
    pthread_cond_signal(&seat_ui.update_cond);
    pthread_mutex_unlock(&seat_ui.lock);
}

static void execute_service_command(const char *command)
{
    char cmd[MAX_ERROR_MSG_LEN];
    snprintf(cmd, sizeof(cmd), "systemctl %s swupdate.service 2>&1", command);

    FILE *fp = popen(cmd, "r");
    if (fp)
    {
        char output[MAX_ERROR_MSG_LEN];
        while (fgets(output, sizeof(output), fp))
        {
            printf("[SERVICE] %s: %s", command, output);
        }
        pclose(fp);
    }

    schedule_services_ui_update();
}

static bool on_service_start_click(AromaNode *node, void *user_data)
{
    UNUSED(node);
    UNUSED(user_data);
    execute_service_command("start");
    return true;
}

static bool on_service_stop_click(AromaNode *node, void *user_data)
{
    UNUSED(node);
    UNUSED(user_data);
    execute_service_command("stop");
    return true;
}

static bool on_service_restart_click(AromaNode *node, void *user_data)
{
    UNUSED(node);
    UNUSED(user_data);
    execute_service_command("restart");
    return true;
}

static bool on_service_enable_click(AromaNode *node, void *user_data)
{
    UNUSED(node);
    UNUSED(user_data);
    execute_service_command("enable");
    return true;
}

static bool on_service_disable_click(AromaNode *node, void *user_data)
{
    UNUSED(node);
    UNUSED(user_data);
    execute_service_command("disable");
    return true;
}

static void refresh_service_status(void)
{
    ServiceStatus status;
    memset(&status, 0, sizeof(status));
    safe_strncpy(status.name, "swupdate.service", MAX_SERVICE_NAME_LEN);

    FILE *fp = popen("systemctl show -p LoadState -p ActiveState -p SubState -p MainPID -p MemoryCurrent -p ActiveEnterTimestamp swupdate.service 2>/dev/null", "r");
    if (fp)
    {
        char line[MAX_ERROR_MSG_LEN];
        while (fgets(line, sizeof(line), fp))
        {
            if (strncmp(line, "LoadState=", 10) == 0)
            {
                safe_strncpy(status.load_state, line + 10, MAX_SERVICE_STATUS_LEN);
                char *nl = strchr(status.load_state, '\n');
                if (nl)
                    *nl = '\0';
            }
            else if (strncmp(line, "ActiveState=", 12) == 0)
            {
                safe_strncpy(status.active_state, line + 12, MAX_SERVICE_STATUS_LEN);
                char *nl = strchr(status.active_state, '\n');
                if (nl)
                    *nl = '\0';
                status.is_active = (strcmp(status.active_state, "active") == 0);
                status.is_running = status.is_active;
            }
            else if (strncmp(line, "SubState=", 9) == 0)
            {
                safe_strncpy(status.sub_state, line + 9, MAX_SERVICE_STATUS_LEN);
                char *nl = strchr(status.sub_state, '\n');
                if (nl)
                    *nl = '\0';
            }
            else if (strncmp(line, "MainPID=", 8) == 0)
            {
                status.main_pid = (uint32_t)strtoul(line + 8, NULL, 10);
            }
            else if (strncmp(line, "MemoryCurrent=", 14) == 0)
            {
                status.memory_current = strtoull(line + 14, NULL, 10);
            }
            else if (strncmp(line, "ActiveEnterTimestamp=", 21) == 0)
            {
                status.active_enter_timestamp = strtoull(line + 21, NULL, 10);
            }
        }
        pclose(fp);
    }
    else
    {
        safe_strncpy(status.load_state, "unknown", MAX_SERVICE_STATUS_LEN);
        safe_strncpy(status.active_state, "unknown", MAX_SERVICE_STATUS_LEN);
        safe_strncpy(status.sub_state, "unknown", MAX_SERVICE_STATUS_LEN);
    }

    pthread_mutex_lock(&svc_ui.lock);
    svc_ui.current_status = status;
    pthread_mutex_unlock(&svc_ui.lock);
}

static void update_services_ui(void)
{
    if (!svc_ui.status_label || !svc_ui.status_icon || !svc_ui.status_card)
    {
        return;
    }

    refresh_service_status();

    pthread_mutex_lock(&svc_ui.lock);
    ServiceStatus status = svc_ui.current_status;
    pthread_mutex_unlock(&svc_ui.lock);

    if (status.is_running)
    {
        aroma_label_set_text(svc_ui.status_label, "Running");
        aroma_icon_set_text(svc_ui.status_icon, AROMA_ICON_CHECK_CIRCLE, state.icon_font);
        aroma_icon_set_color(svc_ui.status_icon, 0xFF4CAF50);
    }
    else
    {
        aroma_label_set_text(svc_ui.status_label, "Stopped");
        aroma_icon_set_text(svc_ui.status_icon, AROMA_ICON_ERROR, state.icon_font);
        aroma_icon_set_color(svc_ui.status_icon, 0xFFF44336);
    }

    if (svc_ui.pid_label)
    {
        char pid_text[MAX_INFO_STR_LEN];
        if (status.main_pid > 0)
        {
            snprintf(pid_text, sizeof(pid_text), "PID: %u", status.main_pid);
        }
        else
        {
            snprintf(pid_text, sizeof(pid_text), "PID: N/A");
        }
        aroma_label_set_text(svc_ui.pid_label, pid_text);
    }

    if (svc_ui.memory_label)
    {
        char mem_text[MAX_INFO_STR_LEN];
        if (status.memory_current > 0)
        {
            snprintf(mem_text, sizeof(mem_text), "Memory: %.2f MB",
                     (double)status.memory_current / (1024.0 * 1024.0));
        }
        else
        {
            snprintf(mem_text, sizeof(mem_text), "Memory: N/A");
        }
        aroma_label_set_text(svc_ui.memory_label, mem_text);
    }

    if (svc_ui.cpu_label)
    {
        char cpu_text[MAX_INFO_STR_LEN];
        snprintf(cpu_text, sizeof(cpu_text), "CPU: Monitoring...");
        aroma_label_set_text(svc_ui.cpu_label, cpu_text);
    }

    if (svc_ui.load_state_label)
    {
        char load_text[MAX_INFO_STR_LEN];
        snprintf(load_text, sizeof(load_text), "Load: %s", status.load_state);
        aroma_label_set_text(svc_ui.load_state_label, load_text);
    }

    if (svc_ui.active_state_label)
    {
        char active_text[MAX_INFO_STR_LEN];
        snprintf(active_text, sizeof(active_text), "Active: %s", status.active_state);
        aroma_label_set_text(svc_ui.active_state_label, active_text);
    }

    if (svc_ui.sub_state_label)
    {
        char sub_text[MAX_INFO_STR_LEN];
        snprintf(sub_text, sizeof(sub_text), "Sub: %s", status.sub_state);
        aroma_label_set_text(svc_ui.sub_state_label, sub_text);
    }

    if (svc_ui.uptime_label)
    {
        char uptime_text[MAX_INFO_STR_LEN];
        if (status.active_enter_timestamp > 0 && status.is_active)
        {
            uint64_t now = (uint64_t)time(NULL) * 1000000;
            uint64_t diff = now - status.active_enter_timestamp;
            unsigned long minutes = diff / 60000000;
            unsigned long hours = minutes / 60;
            minutes = minutes % 60;
            if (hours > 0)
            {
                snprintf(uptime_text, sizeof(uptime_text), "Uptime: %luh %lumin", hours, minutes);
            }
            else
            {
                snprintf(uptime_text, sizeof(uptime_text), "Uptime: %lumin", minutes);
            }
        }
        else
        {
            snprintf(uptime_text, sizeof(uptime_text), "Uptime: N/A");
        }
        aroma_label_set_text(svc_ui.uptime_label, uptime_text);
    }

    if (svc_ui.start_button)
    {
        aroma_node_set_hidden(svc_ui.start_button, status.is_running);
    }
    if (svc_ui.stop_button)
    {
        aroma_node_set_hidden(svc_ui.stop_button, !status.is_running);
    }
    if (svc_ui.restart_button)
    {
        aroma_node_set_hidden(svc_ui.restart_button, !status.is_running);
    }
}

static void *services_monitor_thread_func(void *arg)
{
    UNUSED(arg);

    printf("[SERVICES MONITOR] Monitor thread started\n");

    while (svc_ui.monitor_running)
    {
        pthread_mutex_lock(&svc_ui.lock);

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += SERVICES_REFRESH_INTERVAL_US * 1000;
        if (ts.tv_nsec >= 1000000000)
        {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }

        pthread_cond_timedwait(&svc_ui.update_cond, &svc_ui.lock, &ts);

        if (svc_ui.ui_needs_update)
        {
            svc_ui.ui_needs_update = false;
            pthread_mutex_unlock(&svc_ui.lock);
            update_services_ui();
        }
        else
        {
            pthread_mutex_unlock(&svc_ui.lock);
        }
    }

    printf("[SERVICES MONITOR] Monitor thread stopped\n");
    return NULL;
}

static void init_services_async(void)
{
    pthread_mutex_lock(&svc_ui.lock);

    if (svc_ui.monitor_running)
    {
        pthread_mutex_unlock(&svc_ui.lock);
        return;
    }

    svc_ui.monitor_running = true;
    pthread_mutex_unlock(&svc_ui.lock);

    refresh_service_status();
    schedule_services_ui_update();

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&svc_ui.monitor_thread, &attr, services_monitor_thread_func, NULL);
    pthread_attr_destroy(&attr);
}

static AromaNode *create_services_page_content(AromaNode *parent, int panel_w)
{
    if (!parent)
    {
        return NULL;
    }

    int card_width = panel_w - 20;
    int y_offset = 10;
    aroma_ui_image(parent,
#ifdef __EMSCRIPTEN__
                   "/assets/card_background_services.png"
#elif defined(__arm__) || defined(__aarch64__)
                   "/usr/share/infotainment/assets/card_background_services.png"
#else
                   "../assets/card_background_services.png"
#endif
                   ,
                   10, y_offset, card_width, 100);
    y_offset += 110;
    svc_ui.status_card = aroma_ui_card(parent, 10, y_offset,
                                       card_width, 100, CARD_TYPE_GLASS);
    if (!svc_ui.status_card)
    {
        return NULL;
    }

    AromaNode *small_bg_card_for_status_icon = aroma_ui_card(svc_ui.status_card, 25, 30, 40, 40, CARD_TYPE_FILLED);

    svc_ui.status_icon = aroma_ui_icon(small_bg_card_for_status_icon, AROMA_ICON_INFO,
                                       28, 5, 32, 0xFF607D8B, state.icon_font);

    svc_ui.status_label = aroma_ui_label(svc_ui.status_card, "Checking...",
                                         85, 25, LABEL_STYLE_LABEL_LARGE, state.settings_font);

    svc_ui.pid_label = aroma_ui_label(svc_ui.status_card, "PID: N/A",
                                      85, 50, LABEL_STYLE_LABEL_SMALL, state.settings_font);

    aroma_ui_label(svc_ui.status_card, "swupdate.service",
                   85, 72, LABEL_STYLE_LABEL_SMALL, state.settings_font);

    y_offset += 110;

    svc_ui.detail_card = aroma_ui_card(parent, 10, y_offset,
                                       card_width, 180, CARD_TYPE_GLASS);
    if (svc_ui.detail_card)
    {
        aroma_ui_label(svc_ui.detail_card, "Service Details", 16, 10,
                       LABEL_STYLE_LABEL_MEDIUM, state.settings_font);

        svc_ui.load_state_label = aroma_ui_label(svc_ui.detail_card, "Load: N/A",
                                                 16, 40, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        svc_ui.active_state_label = aroma_ui_label(svc_ui.detail_card, "Active: N/A",
                                                   16, 65, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        svc_ui.sub_state_label = aroma_ui_label(svc_ui.detail_card, "Sub: N/A",
                                                16, 90, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        svc_ui.memory_label = aroma_ui_label(svc_ui.detail_card, "Memory: N/A",
                                             200, 40, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        svc_ui.cpu_label = aroma_ui_label(svc_ui.detail_card, "CPU: Monitoring...",
                                          200, 65, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        svc_ui.uptime_label = aroma_ui_label(svc_ui.detail_card, "Uptime: N/A",
                                             200, 90, LABEL_STYLE_LABEL_SMALL, state.settings_font);
    }

    y_offset += 190;

    AromaNode *control_card = aroma_ui_card(parent, 10, y_offset,
                                            card_width, 100, CARD_TYPE_GLASS);
    if (control_card)
    {
        aroma_ui_label(control_card, "Service Control", 16, 16,
                       LABEL_STYLE_LABEL_MEDIUM, state.settings_font);

        svc_ui.start_button = aroma_ui_button(
            control_card, "Start", 16, 50, 80, 40,
            on_service_start_click, NULL, state.settings_font);
        aroma_button_set_colors(svc_ui.start_button, 0xFF4CAF50, 0xFFFFFFFF, 0xFF388E3C, 0xFFFFFFFF);

        svc_ui.stop_button = aroma_ui_button(
            control_card, "Stop", 106, 50, 80, 40,
            on_service_stop_click, NULL, state.settings_font);
        aroma_button_set_colors(svc_ui.stop_button, 0xFFF44336, 0xFFFFFFFF, 0xFFD32F2F, 0xFFFFFFFF);
        aroma_node_set_hidden(svc_ui.stop_button, true);

        svc_ui.restart_button = aroma_ui_button(
            control_card, "Restart", 196, 50, 80, 40,
            on_service_restart_click, NULL, state.settings_font);
        aroma_button_set_colors(svc_ui.restart_button, 0xFFFFC107, 0xFF000000, 0xFFFFA000, 0xFF000000);
        aroma_node_set_hidden(svc_ui.restart_button, true);

        svc_ui.enable_button = aroma_ui_button(
            control_card, "Enable", 286, 50, 80, 40,
            on_service_enable_click, NULL, state.settings_font);
        aroma_button_set_colors(svc_ui.enable_button, 0xFF2196F3, 0xFFFFFFFF, 0xFF1976D2, 0xFFFFFFFF);

        svc_ui.disable_button = aroma_ui_button(
            control_card, "Disable", 376, 50, 80, 40,
            on_service_disable_click, NULL, state.settings_font);
        aroma_button_set_colors(svc_ui.disable_button, 0xFF9E9E9E, 0xFFFFFFFF, 0xFF757575, 0xFFFFFFFF);
    }

    y_offset += 110;

    aroma_ui_label(parent, "SWUpdate manages over-the-air (OTA)", 10, y_offset + 10,
                   LABEL_STYLE_LABEL_SMALL, state.settings_font);
    aroma_ui_label(parent, "software updates for the infotainment system.", 10, y_offset + 30,
                   LABEL_STYLE_LABEL_SMALL, state.settings_font);
    aroma_ui_label(parent, "Keep this service running for automatic updates.", 10, y_offset + 50,
                   LABEL_STYLE_LABEL_SMALL, state.settings_font);

    return svc_ui.status_card;
}

static AromaNode *build_services_page(AromaNode *parent, int panel_w, int area_h)
{
    if (!parent)
    {
        return NULL;
    }

    AromaNode *scroll_container = aroma_container_create(
        parent, 230, 0, panel_w, area_h);

    if (!scroll_container)
    {
        return NULL;
    }

    aroma_container_set_scrollable(scroll_container, true);

    AromaNode *content = create_services_page_content(scroll_container, panel_w);
    if (!content)
    {
        return scroll_container;
    }

    init_services_async();

    return scroll_container;
}
static void *bt_monitor_thread_func(void *arg)
{
    UNUSED(arg);

    printf("[BT MONITOR] Monitor thread started\n");

    bool contacts_fetched = false;
    bool hfp_ready = false;

bool call_history_fetched = false;

    while (bt_ui.monitor_running)
    {
        pthread_mutex_lock(&bt_ui.lock);

        if (bt_ui.initialized && !hfp_ready)
        {
            hfp_ready = true;
        }

        if (hfp_ready)
        {
            pthread_mutex_unlock(&bt_ui.lock);
            bt_hfp_poll();
            pthread_mutex_lock(&bt_ui.lock);
        }
/*

if (!call_history_fetched && bt_ui.initialized && hfp_ready)
{
    bt_device_info_t device = bt_speaker_get_device_info();
    if (device.connected && device.path[0])
    {
        pthread_mutex_unlock(&bt_ui.lock);
        
        printf("[BT MONITOR] Fetching call history...\n");
        bt_call_info_t phone_history[100];
        int hist_count = bt_hfp_fetch_call_history(device.path, phone_history, 100);
        if (hist_count > 0)
        {
            printf("[BT MONITOR] Fetched %d call history entries\n", hist_count);
            
            state.call_history_count = hist_count;
            for (int i = 0; i < hist_count && i < 100; i++)
            {
                safe_str_copy(state.call_history[i].number, phone_history[i].line_id, 64);
                safe_str_copy(state.call_history[i].name, phone_history[i].name, 128);
            }
            state.call_history_fetched = true;
        }
        
        pthread_mutex_lock(&bt_ui.lock);
        call_history_fetched = true;
    }
}*/
        if (!contacts_fetched && bt_ui.initialized && hfp_ready)
        {
            bt_device_info_t device = bt_speaker_get_device_info();
            if (device.connected && device.path[0])
            {
                pthread_mutex_unlock(&bt_ui.lock);
                usleep(3000000);  
                pthread_mutex_lock(&bt_ui.lock);
                
                printf("[BT MONITOR] Fetching contacts...\n");
                bt_contact_t contacts[100];
                int count = bt_hfp_fetch_contacts(device.path, contacts, 100);

                if (count > 0)
                {
                    printf("[BT MONITOR] Fetched %d contacts\n", count);

                    pthread_mutex_lock(&contact_list_lock);
                    state.contact_count = (count < MAX_CONTACTS) ? count : MAX_CONTACTS;
                    for (int i = 0; i < state.contact_count; i++)
                    {
                        safe_str_copy(state.contacts[i].name, contacts[i].name, sizeof(state.contacts[i].name));
                        safe_str_copy(state.contacts[i].number, contacts[i].number, sizeof(state.contacts[i].number));
                    }
                    state.contacts_fetched = true;
                    pthread_mutex_unlock(&contact_list_lock);

                    printf("[BT MONITOR] Stored %d contacts in app state\n", state.contact_count);
                }
                else if (count == 0)
                {
                    pthread_mutex_lock(&contact_list_lock);
                    state.contact_count = 0;
                    state.contacts_fetched = true;
                    pthread_mutex_unlock(&contact_list_lock);
                    printf("[BT MONITOR] No contacts found\n");
                }
                else
                {
                    printf("[BT MONITOR] Fetch error: %s\n", bt_hfp_get_last_error_message());
                }

                contacts_fetched = true;
            }
        }

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += BT_MONITOR_INTERVAL_US * 1000;
        if (ts.tv_nsec >= 1000000000)
        {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }

        int wait_result = pthread_cond_timedwait(&bt_ui.update_cond, &bt_ui.lock, &ts);

        if (bt_ui.ui_needs_update)
        {
            bt_ui.ui_needs_update = false;
            pthread_mutex_unlock(&bt_ui.lock);
            update_bluetooth_ui();
        }
        else
        {
            pthread_mutex_unlock(&bt_ui.lock);
        }
    }
    printf("[BT MONITOR] Monitor thread stopped\n");
    return NULL;
}static void *bt_init_thread_func(void *arg)
{
    UNUSED(arg);

    bt_config_t config;
    memset(&config, 0, sizeof(config));
    config.device_name = "Aroma Speaker";
    config.pin_code = "0000";
    config.verbose = true;
    config.state_cb = bt_state_changed_handler;
    config.state_cb_data = NULL;
    config.device_cb = bt_device_callback;
    config.device_cb_data = NULL;
    config.error_cb = bt_error_handler;
    config.error_cb_data = NULL;
    config.avrcp_cb = bt_avrcp_callback;
    config.avrcp_cb_data = NULL;

    int result = bt_speaker_init(&config);
    usleep(500000);
    int result2 = bt_hfp_init();
    
    printf("[BT INIT] Speaker init: %d, HFP init: %d\n", result, result2);

    pthread_mutex_lock(&bt_ui.lock);
    bt_ui.init_in_progress = false;

    if (result == 0 && result2 == 0)
    {
        bt_ui.initialized = true;
        bt_speaker_set_discoverable(true);
        bt_speaker_set_pairable(true);
        bt_ui.current_state = bt_speaker_get_state();

        if (bt_speaker_start() != 0)
        {
            bt_ui.current_state = BT_STATE_ERROR;
            bt_ui.initialized = false;
        }
        else
        {
            bt_ui.monitor_running = true;
            pthread_create(&bt_ui.monitor_thread, NULL, bt_monitor_thread_func, NULL);
        }
    }
    else
    {
        bt_ui.initialized = false;
        bt_ui.current_state = BT_STATE_ERROR;
        log_bluetooth_error("Failed to initialize Bluetooth (error %d)", result);
    }
    pthread_mutex_unlock(&bt_ui.lock);

    schedule_ui_update();
    return NULL;
}
static void init_bluetooth_async(void)
{
    pthread_mutex_lock(&bt_ui.lock);

    if (bt_ui.initialized || bt_ui.init_in_progress)
    {
        pthread_mutex_unlock(&bt_ui.lock);
        return;
    }

    bt_ui.init_in_progress = true;
    bt_ui.current_state = BT_STATE_INITIALIZING;
    pthread_mutex_unlock(&bt_ui.lock);

    schedule_ui_update();

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    int ret = pthread_create(&bt_ui.init_thread, &attr, bt_init_thread_func, NULL);
    pthread_attr_destroy(&attr);

    if (ret != 0)
    {
        pthread_mutex_lock(&bt_ui.lock);
        bt_ui.init_in_progress = false;
        bt_ui.current_state = BT_STATE_ERROR;
        pthread_mutex_unlock(&bt_ui.lock);

        schedule_ui_update();
        bt_error_handler(BT_ERROR_CONFIG_FAILED, "Failed to create initialization thread", NULL);
    }
}

static void bt_state_changed_handler(bt_state_t old_state, bt_state_t new_state, void *user_data)
{
    UNUSED(old_state);
    UNUSED(user_data);

    printf("[BT STATE] State changed from %d to %d\n", old_state, new_state);

    pthread_mutex_lock(&bt_ui.lock);
    bt_ui.current_state = new_state;
    pthread_mutex_unlock(&bt_ui.lock);

    schedule_ui_update();
}

static void bt_device_callback(const bt_device_info_t *device, bool connected, void *user_data)
{
    UNUSED(user_data);

    if (device)
    {
        printf("[BT DEVICE] Device %s: %s (Address: %s)\n",
               connected ? "connected" : "disconnected",
               device->name, device->address);
    }

    schedule_ui_update();
}
static void bt_avrcp_callback(const bt_media_info_t *media, void *user_data)
{
    UNUSED(user_data);

    if (media)
    {
        printf("[BT AVRCP] Media: %s - %s (%s)\n",
               media->artist, media->title, media->status);

        pthread_mutex_lock(&bt_ui.lock);
        bt_ui.current_media = *media;

        if (strcmp(media->status, "playing") == 0)
        {
            if (bt_ui.current_state != BT_STATE_PLAYING)
            {
                bt_ui.current_state = BT_STATE_PLAYING;
                printf("[BT AVRCP] State set to PLAYING\n");
            }
        }
        else if (strcmp(media->status, "paused") == 0)
        {
            if (bt_ui.current_state != BT_STATE_CONNECTED)
            {
                bt_ui.current_state = BT_STATE_CONNECTED;
                printf("[BT AVRCP] State set to CONNECTED\n");
            }
        }

        pthread_mutex_unlock(&bt_ui.lock);

        schedule_ui_update();
    }
}
static void bt_error_handler(bt_error_t error, const char *message, void *user_data)
{
    UNUSED(error);
    UNUSED(user_data);

    char error_msg[MAX_ERROR_MSG_LEN];
    const char *safe_message = message ? message : "An unexpected error occurred";

    printf("[BT ERROR] Error %d: %s\n", error, safe_message);

    int written = snprintf(error_msg, sizeof(error_msg),
                           "Bluetooth: %s", safe_message);

    if (written < 0 || (size_t)written >= sizeof(error_msg))
    {
        strncpy(error_msg, "Bluetooth Error", sizeof(error_msg) - 1);
        error_msg[sizeof(error_msg) - 1] = '\0';
    }

    if (state.settings_root)
    {
        aroma_ui_snackbar(state.settings_root, error_msg, 3000, state.settings_font);
    }
}

static const char *get_status_text_for_state(bt_state_t state)
{
    switch (state)
    {
    case BT_STATE_IDLE:
        return "Ready to Connect";
    case BT_STATE_INITIALIZING:
        return "Initializing Bluetooth...";
    case BT_STATE_ADVERTISING:
        return "Waiting for phone";
    case BT_STATE_PAIRED:
        return "Device Paired";
    case BT_STATE_CONNECTED:
        return "Device Connected";
    case BT_STATE_PLAYING:
        return "Playing Music";
    case BT_STATE_ERROR:
        return "Bluetooth Error";
    default:
        return "Unknown";
    }
}

static uint32_t get_icon_color_for_state(bt_state_t state)
{
    switch (state)
    {
    case BT_STATE_IDLE:
        return 0xFF607D8B;
    case BT_STATE_INITIALIZING:
        return 0xFFFFC107;
    case BT_STATE_ADVERTISING:
        return 0xFF2196F3;
    case BT_STATE_PAIRED:
        return 0xFFFF9800;
    case BT_STATE_CONNECTED:
        return 0xFF4CAF50;
    case BT_STATE_PLAYING:
        return 0xFF00BCD4;
    case BT_STATE_ERROR:
        return 0xFFF44336;
    default:
        return 0xFF9E9E9E;
    }
}

static const char *get_icon_for_state(bt_state_t state)
{
    switch (state)
    {
    case BT_STATE_IDLE:
        return AROMA_ICON_BLUETOOTH;
    case BT_STATE_INITIALIZING:
        return AROMA_ICON_SETTINGS;
    case BT_STATE_ADVERTISING:
        return AROMA_ICON_BLUETOOTH_SEARCHING;
    case BT_STATE_PAIRED:
        return AROMA_ICON_BLUETOOTH;
    case BT_STATE_CONNECTED:
        return AROMA_ICON_BLUETOOTH_CONNECTED;
    case BT_STATE_PLAYING:
        return AROMA_ICON_MUSIC_NOTE;
    case BT_STATE_ERROR:
        return AROMA_ICON_ERROR;
    default:
        return AROMA_ICON_BLUETOOTH;
    }
}

static bool is_connected_state(bt_state_t state)
{
    return (state == BT_STATE_CONNECTED || state == BT_STATE_PLAYING);
}

static bool is_playback_active(bt_state_t state)
{
    return (state == BT_STATE_PLAYING);
}

static void update_bluetooth_ui(void)
{
    if (!bt_ui.status_label || !bt_ui.status_icon || !bt_ui.status_card)
    {
        return;
    }

    pthread_mutex_lock(&bt_ui.lock);
    bt_state_t current_state = bt_ui.current_state;
    bool is_initialized = bt_ui.initialized;
    bool init_in_progress = bt_ui.init_in_progress;
    bt_media_info_t current_media = bt_ui.current_media;
    pthread_mutex_unlock(&bt_ui.lock);

    bt_state_t actual_state = bt_speaker_get_state();
    if (actual_state != current_state && actual_state != BT_STATE_ERROR)
    {
        current_state = actual_state;
        pthread_mutex_lock(&bt_ui.lock);
        bt_ui.current_state = actual_state;
        pthread_mutex_unlock(&bt_ui.lock);
    }

    if (init_in_progress)
    {
        aroma_label_set_text(bt_ui.status_label, "Initializing Bluetooth...");
        aroma_icon_set_text(bt_ui.status_icon, AROMA_ICON_SETTINGS, state.icon_font);
        aroma_icon_set_color(bt_ui.status_icon, 0xFFFFC107);
        return;
    }

    if (!is_initialized)
    {
        aroma_label_set_text(bt_ui.status_label, "Bluetooth Unavailable");
        aroma_icon_set_text(bt_ui.status_icon, AROMA_ICON_ERROR, state.icon_font);
        aroma_icon_set_color(bt_ui.status_icon, 0xFFF44336);
        return;
    }

    const char *status_text = get_status_text_for_state(current_state);
    uint32_t icon_color = get_icon_color_for_state(current_state);
    const char *icon_code = get_icon_for_state(current_state);

    aroma_label_set_text(bt_ui.status_label, status_text);
    aroma_icon_set_text(bt_ui.status_icon, icon_code, state.icon_font);
    aroma_icon_set_color(bt_ui.status_icon, icon_color);

    bt_device_info_t device = bt_speaker_get_device_info();

    if (bt_ui.device_info_card)
    {
        if (device.connected && device.name[0])
        {
            char name_buf[MAX_INFO_STR_LEN];
            char address_buf[MAX_INFO_STR_LEN];
            char manufacturer_buf[MAX_INFO_STR_LEN];

            snprintf(name_buf, sizeof(name_buf), "Name: %s", device.name);
            snprintf(address_buf, sizeof(address_buf), "Address: %s", device.address[0] ? device.address : "Unknown");
            snprintf(manufacturer_buf, sizeof(manufacturer_buf), "Device Type: A2DP Speaker");

            if (bt_ui.device_name_label)
            {
                aroma_label_set_text(bt_ui.device_name_label, name_buf);
            }
            if (bt_ui.device_address_label)
            {
                aroma_label_set_text(bt_ui.device_address_label, address_buf);
            }
            if (bt_ui.device_manufacturer_label)
            {
                aroma_label_set_text(bt_ui.device_manufacturer_label, manufacturer_buf);
            }

            aroma_node_set_hidden(bt_ui.device_info_card, false);
        }
        else
        {
            aroma_node_set_hidden(bt_ui.device_info_card, true);
        }
    }

    if (bt_ui.stats_label)
    {
        if (current_state == BT_STATE_CONNECTED || current_state == BT_STATE_PLAYING)
        {
            bt_stats_t stats = bt_speaker_get_stats();
            char stats_text[MAX_INFO_STR_LEN];
            unsigned long minutes = stats.connected_time_sec / 60;
            unsigned long seconds = stats.connected_time_sec % 60;

            if (stats.audio_active && stats.audio_time_sec > 0)
            {
                unsigned long audio_min = stats.audio_time_sec / 60;
                unsigned long audio_sec = stats.audio_time_sec % 60;
                snprintf(stats_text, sizeof(stats_text),
                         "Connected: %lumin %lus | Audio: %lumin %lus",
                         minutes, seconds, audio_min, audio_sec);
            }
            else
            {
                snprintf(stats_text, sizeof(stats_text),
                         "Connected: %lumin %lus", minutes, seconds);
            }
            aroma_label_set_text(bt_ui.stats_label, stats_text);
        }
        else
        {
            aroma_label_set_text(bt_ui.stats_label, "No device connected");
        }
    }

    if (bt_ui.audio_status_label)
    {
        if (current_state == BT_STATE_PLAYING)
        {
            aroma_label_set_text(bt_ui.audio_status_label, "Audio: Playing");
            aroma_label_set_color(bt_ui.audio_status_label, 0xFF4CAF50);
        }
        else if (current_state == BT_STATE_CONNECTED)
        {
            if (current_media.title[0] != '\0' || current_media.artist[0] != '\0')
            {
                aroma_label_set_text(bt_ui.audio_status_label, "Audio: Playing (AVRCP active)");
                aroma_label_set_color(bt_ui.audio_status_label, 0xFF4CAF50);
            }
            else
            {
                aroma_label_set_text(bt_ui.audio_status_label, "Audio: Idle");
                aroma_label_set_color(bt_ui.audio_status_label, 0xFFFF9800);
            }
        }
        else
        {
            aroma_label_set_text(bt_ui.audio_status_label, "Audio: Inactive");
            aroma_label_set_color(bt_ui.audio_status_label, 0xFF9E9E9E);
        }
    }

    if (bt_ui.media_info_card)
    {
        bool has_media = (current_media.title[0] != '\0' || current_media.artist[0] != '\0');

        if (has_media && (current_state == BT_STATE_CONNECTED || current_state == BT_STATE_PLAYING))
        {
            char title_buf[MAX_INFO_STR_LEN];
            char artist_buf[MAX_INFO_STR_LEN];
            char album_buf[MAX_INFO_STR_LEN];

            snprintf(title_buf, sizeof(title_buf), "%s",
                     current_media.title[0] ? current_media.title : "Unknown Track");
            snprintf(artist_buf, sizeof(artist_buf), "Artist: %s",
                     current_media.artist[0] ? current_media.artist : "Unknown Artist");
            snprintf(album_buf, sizeof(album_buf), "Album: %s",
                     current_media.album[0] ? current_media.album : "Unknown Album");

            if (bt_ui.media_title_label)
            {
                aroma_label_set_text(bt_ui.media_title_label, title_buf);
                aroma_label_set_color(bt_ui.media_title_label, 0xFF00BCD4);
            }
            if (bt_ui.media_artist_label)
            {
                aroma_label_set_text(bt_ui.media_artist_label, artist_buf);
            }
            if (bt_ui.media_album_label)
            {
                aroma_label_set_text(bt_ui.media_album_label, album_buf);
            }

            aroma_node_set_hidden(bt_ui.media_info_card, false);
        }
        else
        {
            aroma_node_set_hidden(bt_ui.media_info_card, true);
        }
    }

    if (bt_ui.disconnect_button)
    {
        bool show_button = (current_state == BT_STATE_CONNECTED || current_state == BT_STATE_PLAYING);
        aroma_node_set_hidden(bt_ui.disconnect_button, !show_button);
    }
}

static bool on_bt_disconnect_click(AromaNode *node, void *user_data)
{
    UNUSED(node);
    UNUSED(user_data);

    bt_ui.monitor_running = false;
    usleep(100000);

    bt_speaker_stop();
    bt_speaker_cleanup();

    pthread_mutex_lock(&bt_ui.lock);
    bt_ui.initialized = false;
    bt_ui.current_state = BT_STATE_IDLE;
    memset(&bt_ui.current_media, 0, sizeof(bt_ui.current_media));
    pthread_mutex_unlock(&bt_ui.lock);

    update_bluetooth_ui();

    usleep(500000);
    init_bluetooth_async();

    return true;
}

static AromaNode *create_bluetooth_page_content(AromaNode *parent, int panel_w)
{
    if (!parent)
    {
        return NULL;
    }

    int card_width = panel_w - 20;
    int y_offset = 10;

    AromaNode *card_header = aroma_ui_image(parent,

#ifdef __EMSCRIPTEN__
                                            "/assets/card_background_bluetooth.png"
#elif defined(__arm__) || defined(__aarch64__)
                                            "/usr/share/infotainment/assets/card_background_bluetooth.png"
#else
                                            "../assets/card_background_bluetooth.png"
#endif

                                            ,
                                            10, y_offset, card_width, 100);

    y_offset += 110;

    bt_ui.status_card = aroma_ui_card(parent, 10, y_offset,
                                      card_width, 100, CARD_TYPE_GLASS);
    if (!bt_ui.status_card)
    {
        return NULL;
    }

    AromaNode *small_bg_card_for_status_icon = aroma_ui_card(bt_ui.status_card, 25, 30, 40, 40, CARD_TYPE_FILLED);

    bt_ui.status_icon = aroma_ui_icon(small_bg_card_for_status_icon, AROMA_ICON_BLUETOOTH,
                                      28, 5, 32, 0xFF607D8B, state.icon_font);

    bt_ui.status_label = aroma_ui_label(bt_ui.status_card, "Ready to Connect",
                                        85, 25, LABEL_STYLE_LABEL_LARGE, state.settings_font);

    bt_ui.stats_label = aroma_ui_label(bt_ui.status_card, "No device connected",
                                       85, 50, LABEL_STYLE_LABEL_SMALL, state.settings_font);

    y_offset += 110;
    bt_ui.device_info_card = aroma_ui_card(parent, 10, y_offset,
                                           card_width, 130, CARD_TYPE_GLASS);
    if (bt_ui.device_info_card)
    {
        AromaNode *device_image = aroma_ui_image(bt_ui.device_info_card,

#ifdef __EMSCRIPTEN__
                                                 "/assets/smartphone.png"
#elif defined(__arm__) || defined(__aarch64__)
                                                 "/usr/share/infotainment/assets/smartphone.png"
#else
                                                 "../assets/smartphone.png"
#endif

                                                 ,
                                                 10, 15, 96, 96);
        aroma_ui_label(bt_ui.device_info_card, "Device Information", 120, 16,
                       LABEL_STYLE_LABEL_MEDIUM, state.settings_font);

        bt_ui.device_name_label = aroma_ui_label(bt_ui.device_info_card, "Name: None",
                                                 120, 45, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        bt_ui.device_address_label = aroma_ui_label(bt_ui.device_info_card, "Address: None",
                                                    120, 70, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        bt_ui.device_manufacturer_label = aroma_ui_label(bt_ui.device_info_card, "Type: None",
                                                         120, 95, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        aroma_node_set_hidden(bt_ui.device_info_card, true);
    }

    y_offset += 140;
    AromaNode *control_card = aroma_ui_card(parent, 10, y_offset,
                                            card_width, 80, CARD_TYPE_GLASS);
    if (control_card)
    {
        aroma_ui_label(control_card, "Audio Control", 16, 16,
                       LABEL_STYLE_LABEL_MEDIUM, state.settings_font);

        bt_ui.audio_status_label = aroma_ui_label(control_card, "Audio: Idle",
                                                  16, 50, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        bt_ui.disconnect_button = aroma_ui_button(
            control_card, "Disconnect", card_width - 120, 10, 100, 40,
            on_bt_disconnect_click, NULL, state.settings_font);
        aroma_button_set_colors(bt_ui.disconnect_button, 0xFFF44336, 0xFFFFFFFF, 0xFFB71C1C, 0xFFFFFFFF);
        aroma_node_set_hidden(bt_ui.disconnect_button, true);
    }
    AromaNode *info_label1 = aroma_ui_label(parent, "Car Bluetooth Name: Aroma Speaker", 10, y_offset + 100,
                                            LABEL_STYLE_LABEL_SMALL, state.settings_font);
    AromaNode *info_label2 = aroma_ui_label(parent, "PIN Code: 0000 (if asked)", 10, y_offset + 130,
                                            LABEL_STYLE_LABEL_SMALL, state.settings_font);

    return bt_ui.status_card;
}

static AromaNode *build_bluetooth_page(AromaNode *parent, int panel_w, int area_h)
{
    if (!parent)
    {
        return NULL;
    }

    AromaNode *scroll_container = aroma_container_create(
        parent, 230, 0, panel_w, area_h);

    if (!scroll_container)
    {
        return NULL;
    }

    aroma_container_set_scrollable(scroll_container, true);

    AromaNode *content = create_bluetooth_page_content(scroll_container, panel_w);
    if (!content)
    {
        return scroll_container;
    }

    init_bluetooth_async();

    return scroll_container;
}

static bool on_driver_position_change(AromaNode *node, void *user_data)
{
    UNUSED(user_data);
    if (!node)
        return false;

    int value = aroma_slider_get_value(node);

    pthread_mutex_lock(&seat_ui.lock);
    seat_ui.driver_status.position = (int16_t)value;
    pthread_mutex_unlock(&seat_ui.lock);

    schedule_seat_ui_update();
    return true;
}

static bool on_passenger_position_change(AromaNode *node, void *user_data)
{
    UNUSED(user_data);
    if (!node)
        return false;

    int value = aroma_slider_get_value(node);

    pthread_mutex_lock(&seat_ui.lock);
    seat_ui.passenger_status.position = (int16_t)value;
    pthread_mutex_unlock(&seat_ui.lock);

    schedule_seat_ui_update();
    return true;
}

static void on_seat_nudge_click(void *user_data)
{
    if (!user_data)
        return;

    intptr_t direction = (intptr_t)user_data;

    pthread_mutex_lock(&seat_ui.lock);

    if (direction == 0)
    {
        if (seat_ui.driver_status.position < 120)
            seat_ui.driver_status.position += 1;
    }
    else if (direction == 1)
    {
        if (seat_ui.driver_status.position > 60)
            seat_ui.driver_status.position -= 1;
    }
    else if (direction == 2)
    {
        if (seat_ui.passenger_status.position < 120)
            seat_ui.passenger_status.position += 1;
    }
    else if (direction == 3)
    {
        if (seat_ui.passenger_status.position > 60)
            seat_ui.passenger_status.position -= 1;
    }

    pthread_mutex_unlock(&seat_ui.lock);

    if (seat_ui.driver_slider)
    {
        aroma_slider_set_value(seat_ui.driver_slider, seat_ui.driver_status.position);
    }
    if (seat_ui.passenger_slider)
    {
        aroma_slider_set_value(seat_ui.passenger_slider, seat_ui.passenger_status.position);
    }

    schedule_seat_ui_update();
}

static bool on_seat_profile_click(AromaNode *node, void *user_data)
{
    UNUSED(user_data);
    if (!node)
        return false;

    pthread_mutex_lock(&seat_ui.lock);

    if (node == seat_ui.profile1_btn)
    {
        seat_ui.driver_status.position = 80;
        seat_ui.driver_status.profile = 1;
        seat_ui.passenger_status.position = 90;
        seat_ui.passenger_status.profile = 1;
    }
    else if (node == seat_ui.profile2_btn)
    {
        seat_ui.driver_status.position = 95;
        seat_ui.driver_status.profile = 2;
        seat_ui.passenger_status.position = 100;
        seat_ui.passenger_status.profile = 2;
    }
    else if (node == seat_ui.profile3_btn)
    {
        seat_ui.driver_status.position = 110;
        seat_ui.driver_status.profile = 3;
        seat_ui.passenger_status.position = 105;
        seat_ui.passenger_status.profile = 3;
    }
    else if (node == seat_ui.manual_btn)
    {
        seat_ui.driver_status.profile = 0;
        seat_ui.passenger_status.profile = 0;
    }

    pthread_mutex_unlock(&seat_ui.lock);

    if (seat_ui.driver_slider)
    {
        aroma_slider_set_value(seat_ui.driver_slider, seat_ui.driver_status.position);
    }
    if (seat_ui.passenger_slider)
    {
        aroma_slider_set_value(seat_ui.passenger_slider, seat_ui.passenger_status.position);
    }

    schedule_seat_ui_update();
    return true;
}

static void update_seat_ui(void)
{
    if (!seat_ui.status_label || !seat_ui.status_icon || !seat_ui.status_card)
    {
        return;
    }

    pthread_mutex_lock(&seat_ui.lock);
    SeatStatus driver = seat_ui.driver_status;
    SeatStatus passenger = seat_ui.passenger_status;
    pthread_mutex_unlock(&seat_ui.lock);

    if (seat_ui.driver_occupied_label)
    {
        char occ_text[MAX_INFO_STR_LEN];
        snprintf(occ_text, sizeof(occ_text), "Driver: %s", driver.occupied ? "Occupied" : "Empty");
        aroma_label_set_text(seat_ui.driver_occupied_label, occ_text);
    }

    if (seat_ui.passenger_occupied_label)
    {
        char occ_text[MAX_INFO_STR_LEN];
        snprintf(occ_text, sizeof(occ_text), "Passenger: %s", passenger.occupied ? "Occupied" : "Empty");
        aroma_label_set_text(seat_ui.passenger_occupied_label, occ_text);
    }

    if (seat_ui.driver_position_label)
    {
        char pos_text[MAX_INFO_STR_LEN];
        snprintf(pos_text, sizeof(pos_text), "Position: %d deg", driver.position);
        aroma_label_set_text(seat_ui.driver_position_label, pos_text);
    }

    if (seat_ui.passenger_position_label)
    {
        char pos_text[MAX_INFO_STR_LEN];
        snprintf(pos_text, sizeof(pos_text), "Position: %d deg", passenger.position);
        aroma_label_set_text(seat_ui.passenger_position_label, pos_text);
    }

    if (seat_ui.driver_profile_label)
    {
        char profile_text[MAX_INFO_STR_LEN];
        if (driver.profile == 0)
        {
            snprintf(profile_text, sizeof(profile_text), "Profile: Manual");
        }
        else
        {
            snprintf(profile_text, sizeof(profile_text), "Profile: %d", driver.profile);
        }
        aroma_label_set_text(seat_ui.driver_profile_label, profile_text);
    }

    if (seat_ui.passenger_profile_label)
    {
        char profile_text[MAX_INFO_STR_LEN];
        if (passenger.profile == 0)
        {
            snprintf(profile_text, sizeof(profile_text), "Profile: Manual");
        }
        else
        {
            snprintf(profile_text, sizeof(profile_text), "Profile: %d", passenger.profile);
        }
        aroma_label_set_text(seat_ui.passenger_profile_label, profile_text);
    }

    if (seat_ui.status_label)
    {
        aroma_label_set_text(seat_ui.status_label, driver.occupied ? "Driver Present" : "Driver Away");
    }

    if (seat_ui.status_icon)
    {
        aroma_icon_set_text(seat_ui.status_icon, driver.occupied ? AROMA_ICON_PERSON : AROMA_ICON_EVENT_SEAT, state.icon_font);
        aroma_icon_set_color(seat_ui.status_icon, driver.occupied ? 0xFF4CAF50 : 0xFF9E9E9E);
    }
}

static void *seat_monitor_thread_func(void *arg)
{
    UNUSED(arg);

    printf("[SEAT MONITOR] Monitor thread started\n");

    while (seat_ui.monitor_running)
    {
        pthread_mutex_lock(&seat_ui.lock);

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += SEAT_REFRESH_INTERVAL_US * 1000;
        if (ts.tv_nsec >= 1000000000)
        {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }

        pthread_cond_timedwait(&seat_ui.update_cond, &seat_ui.lock, &ts);

        if (seat_ui.ui_needs_update)
        {
            seat_ui.ui_needs_update = false;
            pthread_mutex_unlock(&seat_ui.lock);
            update_seat_ui();
        }
        else
        {
            pthread_mutex_unlock(&seat_ui.lock);
        }
    }

    printf("[SEAT MONITOR] Monitor thread stopped\n");
    return NULL;
}

static void init_seat_async(void)
{
    pthread_mutex_lock(&seat_ui.lock);

    if (seat_ui.monitor_running)
    {
        pthread_mutex_unlock(&seat_ui.lock);
        return;
    }

    seat_ui.monitor_running = true;
    pthread_mutex_unlock(&seat_ui.lock);

    schedule_seat_ui_update();

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&seat_ui.monitor_thread, &attr, seat_monitor_thread_func, NULL);
    pthread_attr_destroy(&attr);
}

static AromaNode *create_seat_page_content(AromaNode *parent, int panel_w)
{
    if (!parent)
    {
        return NULL;
    }

    int card_width = panel_w - 20;
    int y_offset = 10;
    aroma_ui_image(parent,
#ifdef __EMSCRIPTEN__
                   "/assets/card_background_seats.png"
#elif defined(__arm__) || defined(__aarch64__)
                   "/usr/share/infotainment/assets/card_background_seats.png"
#else
                   "../assets/card_background_seats.png"
#endif
                   ,
                   10, y_offset, card_width, 100);
    y_offset += 120;

    AromaNode *profile_card = aroma_ui_card(parent, 10, y_offset,
                                            card_width, 100, CARD_TYPE_GLASS);
    
    aroma_card_set_colors(profile_card, 0xFFFFFF, 0xFF2196F3);
    if (profile_card)
    {
        aroma_ui_label(profile_card, "Seat Profiles", 16, 10,
                       LABEL_STYLE_LABEL_MEDIUM, state.settings_font);

        seat_ui.profile1_btn = aroma_ui_iconbutton(
            profile_card, AROMA_ICON_LOOKS_ONE, 16, 42, 42, ICON_BUTTON_FILLED,
            (void (*)(void *))on_seat_profile_click, NULL, state.icon_font);
        aroma_iconbutton_set_colors(seat_ui.profile1_btn, 0xFF2196F3, 0xFFFFFFFF);

        seat_ui.profile2_btn = aroma_ui_iconbutton(
            profile_card, AROMA_ICON_LOOKS_TWO, 74, 42, 42, ICON_BUTTON_FILLED,
            (void (*)(void *))on_seat_profile_click, NULL, state.icon_font);
        aroma_iconbutton_set_colors(seat_ui.profile2_btn, 0xFF2196F3, 0xFFFFFFFF);

        seat_ui.profile3_btn = aroma_ui_iconbutton(
            profile_card, AROMA_ICON_LOOKS_3, 132, 42, 42, ICON_BUTTON_FILLED,
            (void (*)(void *))on_seat_profile_click, NULL, state.icon_font);
        aroma_iconbutton_set_colors(seat_ui.profile3_btn, 0xFF2196F3, 0xFFFFFFFF);

        seat_ui.manual_btn = aroma_ui_iconbutton(
            profile_card, AROMA_ICON_SETTINGS, 190, 42, 42, ICON_BUTTON_OUTLINED,
            (void (*)(void *))on_seat_profile_click, NULL, state.icon_font);
        aroma_iconbutton_set_colors(seat_ui.manual_btn, 0xFF9E9E9E, 0xFFFFFFFF);
    }

    y_offset += 110;

    AromaNode *driver_card = aroma_ui_card(parent, 10, y_offset,
                                           card_width, 170, CARD_TYPE_GLASS);
    if (driver_card)
    {
        aroma_ui_label(driver_card, "Driver Seat", 16, 10,
                       LABEL_STYLE_LABEL_MEDIUM, state.settings_font);

        seat_ui.driver_position_label = aroma_ui_label(driver_card, "Position: 85 deg",
                                                       16, 40, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        seat_ui.driver_profile_label = aroma_ui_label(driver_card, "Profile: 1",
                                                      200, 40, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        seat_ui.driver_slider = aroma_ui_slider(
            driver_card, 16, 65, card_width - 66, 30,
            60, 120, 85,
            on_driver_position_change, NULL);

        aroma_ui_label(driver_card, "60 deg", 16, 105, LABEL_STYLE_LABEL_SMALL, state.settings_font);
        aroma_ui_label(driver_card, "120 deg", card_width - 100, 105, LABEL_STYLE_LABEL_SMALL, state.settings_font);
    }

    y_offset += 180;

    AromaNode *passenger_card = aroma_ui_card(parent, 10, y_offset,
                                              card_width, 170, CARD_TYPE_GLASS);
    if (passenger_card)
    {
        aroma_ui_label(passenger_card, "Passenger Seat", 16, 10,
                       LABEL_STYLE_LABEL_MEDIUM, state.settings_font);

        seat_ui.passenger_position_label = aroma_ui_label(passenger_card, "Position: 95 deg",
                                                          16, 40, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        seat_ui.passenger_profile_label = aroma_ui_label(passenger_card, "Profile: Manual",
                                                         200, 40, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        seat_ui.passenger_slider = aroma_ui_slider(
            passenger_card, 16, 65, card_width - 66, 30,
            60, 120, 95,
            on_passenger_position_change, NULL);

        aroma_ui_label(passenger_card, "60 deg", 16, 105, LABEL_STYLE_LABEL_SMALL, state.settings_font);
        aroma_ui_label(passenger_card, "120 deg", card_width - 100, 105, LABEL_STYLE_LABEL_SMALL, state.settings_font);
    }

    y_offset += 180;

    aroma_ui_label(parent, "Seat position range: 60-120 degrees", 10, y_offset + 30,
                   LABEL_STYLE_LABEL_SMALL, state.settings_font);
    aroma_ui_label(parent, "Profiles 1-3 store memory positions, Manual allows free adjustment.", 10, y_offset + 50,
                   LABEL_STYLE_LABEL_SMALL, state.settings_font);

    return seat_ui.status_card;
}

static AromaNode *build_seat_page(AromaNode *parent, int panel_w, int area_h)
{
    if (!parent)
    {
        return NULL;
    }

    AromaNode *scroll_container = aroma_container_create(
        parent, 230, 0, panel_w, area_h);

    if (!scroll_container)
    {
        return NULL;
    }

    aroma_container_set_scrollable(scroll_container, true);

    AromaNode *content = create_seat_page_content(scroll_container, panel_w);
    if (!content)
    {
        return scroll_container;
    }

    init_seat_async();

    return scroll_container;
}

static void shift_ui_chrome(int dx)
{
    shift_node(state.overlay, dx);
    shift_node(state.status_card, dx);
    shift_node(state.battery_icon, dx);
    shift_node(state.signal_icon, dx);
    shift_node(state.wifi_icon, dx);
    shift_node(state.gps_icon, dx);
    shift_node(state.bluetooth_icon, dx);
    shift_node(state.voice_button, dx);
    shift_node(state.settings_icon, dx);
}

void open_settings_panel(void *user_data)
{
    UNUSED(user_data);

    if (!state.settings_panel_node || state.settings_panel_open)
    {
        return;
    }

    aroma_node_set_hidden(state.settings_panel_node, false);
    animate_node_x(state.settings_panel_node, WIN_W, WIN_W - SETTINGS_PANEL_W);
    animate_node_x(state.vehicle_view_root, 0, -SETTINGS_PANEL_W);
    shift_ui_chrome(-SETTINGS_PANEL_W);
    animate_node_x(state.tabs, 0, -(SETTINGS_PANEL_W / SETTINGS_TAB_SHIFT_DENOM));
    state.settings_panel_open = true;
}

void close_settings_panel(void *user_data)
{
    UNUSED(user_data);

    if (!state.settings_panel_node || !state.settings_panel_open)
    {
        return;
    }

    animate_node_x(state.settings_panel_node, WIN_W - SETTINGS_PANEL_W, WIN_W);
    animate_node_x(state.vehicle_view_root, -SETTINGS_PANEL_W, 0);
    shift_ui_chrome(SETTINGS_PANEL_W);
    animate_node_x(state.tabs, -(SETTINGS_PANEL_W / SETTINGS_TAB_SHIFT_DENOM), 0);
    state.settings_panel_open = false;
}

void settings_button_callback(void *user_data)
{
    UNUSED(user_data);

    if (state.settings_panel_open)
    {
        close_settings_panel(NULL);
    }
    else
    {
        open_settings_panel(NULL);
    }
}

static void toggle_tab_animation_cb(AromaNode *sender, void *user_data)
{
    UNUSED(sender);
    UNUSED(user_data);

    static bool is_slide = true;
    is_slide = !is_slide;
    aroma_tabs_set_transition(state.tabs,
                              is_slide ? AROMA_ANIM_SLIDE_X : AROMA_ANIM_FADE, 300);
}

static void toggle_voice_assistant_cb(AromaNode *sender, void *user_data)
{
    UNUSED(sender);
    UNUSED(user_data);

    state.g_voice_assistant_enabled = !state.g_voice_assistant_enabled;
}

static void *dev_beep_thread_func(void *arg)
{
    UNUSED(arg);

#ifdef AROMA_USE_VOICE_CONTROL
    pid_t pid = fork();
    if (pid == -1)
    {
        return NULL;
    }

    if (pid == 0)
    {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0)
        {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        char freq_str[16];
        snprintf(freq_str, sizeof(freq_str), "%d", BEEP_FREQ_HZ);

        char *const argv[] = {
            "speaker-test", "-t", "sine", "-f", freq_str, "-l", "1", NULL};
        execvp("speaker-test", argv);
        _exit(1);
    }

    struct timespec ts = {.tv_sec = 0, .tv_nsec = BEEP_DURATION_NS};
    nanosleep(&ts, NULL);
    kill(pid, SIGTERM);

    int status;
    waitpid(pid, &status, 0);
#endif

    return NULL;
}

static void spawn_dev_beep(void)
{
    pthread_t t;
    pthread_attr_t attr;

    if (pthread_attr_init(&attr) != 0)
    {
        return;
    }

    if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0)
    {
        pthread_attr_destroy(&attr);
        return;
    }

    if (pthread_create(&t, &attr, dev_beep_thread_func, NULL) != 0)
    {
        pthread_attr_destroy(&attr);
        return;
    }

    pthread_attr_destroy(&attr);
}

static void handle_easter_egg(int index)
{
    static int build_clicks = 0;

    if (index != 4)
    {
        pthread_mutex_lock(&easter_egg_lock);
        build_clicks = 0;
        pthread_mutex_unlock(&easter_egg_lock);
        return;
    }

    pthread_mutex_lock(&easter_egg_lock);
    build_clicks++;
    int current_clicks = build_clicks;
    pthread_mutex_unlock(&easter_egg_lock);

    if (current_clicks > EASTER_EGG_VOICE_THRESHOLD &&
        current_clicks < EASTER_EGG_CLICKS)
    {
        char msg[64];
        int remaining = EASTER_EGG_CLICKS - current_clicks;
        int written = snprintf(msg, sizeof(msg),
                               "You are now %d steps away from being a developer.",
                               remaining);
        if (written > 0 && (size_t)written < sizeof(msg))
        {
            queue_voice_partial(msg);
        }
    }
    else if (current_clicks >= EASTER_EGG_CLICKS)
    {
        if (current_clicks == EASTER_EGG_CLICKS)
        {
            queue_voice_action(-1, false, false, "You are now a developer!");
            spawn_dev_beep();
        }
        aroma_node_set_hidden(state.easter_egg_overlay, false);
    }
}

void listview_callback(int index, void *user_data)
{
    UNUSED(user_data);

    int selected = aroma_sidebar_get_selected(state.sidebar);

    if (selected == 6)
    {
        handle_easter_egg(index);
        return;
    }

    for (int i = 0; i < item_action_table_size; i++)
    {
        if (item_action_table[i].section == selected &&
            item_action_table[i].item == index)
        {
            if (item_action_table[i].action)
            {
                item_action_table[i].action();
            }
            return;
        }
    }
}

static AromaNode *settings_listview(AromaNode *parent, int x, int y, int w, int h)
{
    if (!parent)
    {
        return NULL;
    }

    AromaNode *lv = aroma_ui_listview(parent, x, y, w, h,
                                      listview_callback, NULL,
                                      state.settings_font);
    if (lv)
    {
        aroma_listview_set_icon_font(lv, state.icon_font);
    }
    return lv;
}

static void read_processor_name(char *buf, size_t bufsz)
{
    strncpy(buf, "Unknown", bufsz - 1);
    buf[bufsz - 1] = '\0';

    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f)
    {
        return;
    }

    char line[MAX_PROCESSOR_NAME_LEN];
    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "model name", 10) == 0)
        {
            char *colon = strchr(line, ':');
            if (colon)
            {
                strncpy(buf, colon + 2, bufsz - 1);
                buf[bufsz - 1] = '\0';
                char *nl = strchr(buf, '\n');
                if (nl)
                {
                    *nl = '\0';
                }
            }
            break;
        }
    }
    fclose(f);
}

static void read_ram_str(char *buf, size_t bufsz)
{
    long total_ram_mb = 0;

    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
    {
        strncpy(buf, "Unknown", bufsz - 1);
        buf[bufsz - 1] = '\0';
        return;
    }

    char line[MAX_ERROR_MSG_LEN];
    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "MemTotal", 8) == 0)
        {
            long kb = 0;
            if (sscanf(line, "MemTotal: %ld kB", &kb) == 1)
            {
                total_ram_mb = kb / 1024;
            }
            break;
        }
    }
    fclose(f);

    if (total_ram_mb > 1024)
    {
        snprintf(buf, bufsz, "%.1f GB", total_ram_mb / 1024.0);
    }
    else if (total_ram_mb > 0)
    {
        snprintf(buf, bufsz, "%ld MB", total_ram_mb);
    }
    else
    {
        strncpy(buf, "Unknown", bufsz - 1);
        buf[bufsz - 1] = '\0';
    }
}

static void read_uptime_str(char *buf, size_t bufsz)
{
    strncpy(buf, "Unknown", bufsz - 1);
    buf[bufsz - 1] = '\0';

    FILE *f = fopen("/proc/uptime", "r");
    if (!f)
    {
        return;
    }

    double s = 0.0;
    if (fscanf(f, "%lf", &s) == 1)
    {
        int d = (int)(s / 86400);
        int h = (int)((s - d * 86400) / 3600);
        int m = (int)((s - d * 86400 - h * 3600) / 60);

        if (d > 0)
        {
            snprintf(buf, bufsz, "%d days, %d hrs", d, h);
        }
        else if (h > 0)
        {
            snprintf(buf, bufsz, "%d hrs, %d min", h, m);
        }
        else
        {
            snprintf(buf, bufsz, "%d min", m);
        }
    }
    fclose(f);
}

static void read_load_str(char *buf, size_t bufsz)
{
    strncpy(buf, "Unknown", bufsz - 1);
    buf[bufsz - 1] = '\0';

    FILE *f = fopen("/proc/loadavg", "r");
    if (!f)
    {
        return;
    }

    double l1, l5, l15;
    if (fscanf(f, "%lf %lf %lf", &l1, &l5, &l15) == 3)
    {
        snprintf(buf, bufsz, "%.2f, %.2f, %.2f", l1, l5, l15);
    }

    fclose(f);
}

static void read_time_str(char *buf, size_t bufsz)
{
    time_t rawtime;
    struct tm timeinfo;

    if (time(&rawtime) == (time_t)-1)
    {
        strncpy(buf, "Unknown", bufsz - 1);
        buf[bufsz - 1] = '\0';
        return;
    }

    if (!localtime_r(&rawtime, &timeinfo))
    {
        strncpy(buf, "Unknown", bufsz - 1);
        buf[bufsz - 1] = '\0';
        return;
    }

    strftime(buf, bufsz, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

static void populate_system_info_list(AromaNode *listview)
{
    if (!listview)
    {
        return;
    }

    char processor_name[MAX_PROCESSOR_NAME_LEN];
    char ram_str[MAX_RAM_STR_LEN];
    char uptime_str[MAX_UPTIME_STR_LEN];
    char load_str[MAX_LOAD_STR_LEN];
    char time_str[MAX_TIME_STR_LEN];

    read_processor_name(processor_name, sizeof(processor_name));
    read_ram_str(ram_str, sizeof(ram_str));
    read_uptime_str(uptime_str, sizeof(uptime_str));
    read_load_str(load_str, sizeof(load_str));
    read_time_str(time_str, sizeof(time_str));

    aroma_listview_add_item_with_icon(listview, "Processor", processor_name, AROMA_ICON_MEMORY, NULL);
    aroma_listview_add_item_with_icon(listview, "RAM", ram_str, AROMA_ICON_STORAGE, NULL);
    aroma_listview_add_item_with_icon(listview, "Vehicle name", "Aroma Automotive", AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(listview, "Software", "AromaHMI v0.0.1 / AromaSDK", AROMA_ICON_INFO, NULL);
    aroma_listview_add_item_with_icon(listview, "Build date", __DATE__ " " __TIME__, AROMA_ICON_BUILD, NULL);
    aroma_listview_add_item_with_icon(listview, "Security patch", "March 1, 2026", AROMA_ICON_SECURITY, NULL);
    aroma_listview_add_item_with_icon(listview, "Uptime", uptime_str, AROMA_ICON_ACCESS_TIME, NULL);
    aroma_listview_add_item_with_icon(listview, "Load average", load_str, AROMA_ICON_COMPUTER, NULL);
    aroma_listview_add_item_with_icon(listview, "Current time", time_str, AROMA_ICON_ACCESS_TIME, NULL);
    aroma_listview_add_item_with_icon(listview, "Platform Backend", "GLPS (X11)", AROMA_ICON_VERIFIED_USER, NULL);
    aroma_listview_add_item_with_icon(listview, "Graphics backend", "Vulkan", AROMA_ICON_MEMORY, NULL);

#ifdef AROMA_USE_VOICE_CONTROL
    aroma_listview_add_item_with_icon(listview, "Voice control", "Enabled (compiled)", AROMA_ICON_MIC, NULL);
#else
    aroma_listview_add_item_with_icon(listview, "Voice control", "Disabled (stub)", AROMA_ICON_MIC, NULL);
#endif
}

void build_settings_ui(AromaNode *window)
{
    if (!window)
    {
        return;
    }

    const int panel_h = WIN_H - 80;
    const int sidebar_w = 220;
    const int area_w = SETTINGS_PANEL_W - 20;
    const int area_h = panel_h - 60;
    const int panel_x = sidebar_w + 8;
    const int panel_w = area_w - sidebar_w - 8;

    state.settings_panel_node = aroma_ui_container(
        window, WIN_W, 0, SETTINGS_PANEL_W, panel_h,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);

    AromaNode *collapse_button = aroma_ui_iconbutton(
        state.settings_panel_node, AROMA_ICON_CLOSE, 10, 15, 30, ICON_BUTTON_OUTLINED,
        settings_button_callback, NULL, state.icon_font);
    aroma_node_set_z_index(collapse_button, Z_LAYER_SETTINGS_PANEL + 2);
    AromaNode *label = aroma_ui_label(state.settings_panel_node, "Settings", 50, 15, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    AromaNode *debug_build_number = aroma_ui_label(state.settings_panel_node, "Debug Build: " __DATE__ " " __TIME__, 500, 15, LABEL_STYLE_LABEL_LARGE, state.settings_font);
    aroma_node_set_z_index(debug_build_number, Z_LAYER_SETTINGS_PANEL + 2);
    aroma_label_set_color(debug_build_number, 0xFF9E9E9E);
    AromaNode *divider = aroma_ui_divider(state.settings_panel_node, 0, 50, SETTINGS_PANEL_W, DIVIDER_ORIENTATION_HORIZONTAL);
    AromaNode *canvas = aroma_canvas_create(state.settings_panel_node, 0, 0, SETTINGS_PANEL_W, 50);
    aroma_canvas_draw_rect(canvas, 0, 0, SETTINGS_PANEL_W, 50, false, true, 0xFFFFFF);
    aroma_node_set_z_index(label, Z_LAYER_SETTINGS_PANEL + 2);

    if (!state.settings_panel_node)
    {
        return;
    }

    aroma_node_set_z_index(state.settings_panel_node, Z_LAYER_SETTINGS_PANEL);
    aroma_node_set_hidden(state.settings_panel_node, true);
    state.settings_panel_open = false;

    state.settings_root = aroma_container_create(
        state.settings_panel_node, 10, 60, area_w, area_h);

    if (!state.settings_root)
    {
        return;
    }

    aroma_node_set_z_index(state.settings_root, Z_LAYER_SETTINGS_PANEL + 1);

    const char *labels[] = {
        "Bluetooth",
        "System & About", "OS Services", "Seats"};
    const char *icons[] = {
        AROMA_ICON_BLUETOOTH,
        AROMA_ICON_INFO, AROMA_ICON_SETTINGS, AROMA_ICON_EVENT_SEAT};
    const int num_sections = 4;

    state.sidebar = aroma_ui_sidebar_with_icons(
        state.settings_root, 0, 10, sidebar_w, 240,
        labels, icons, num_sections,
        NULL, NULL, state.settings_font, state.icon_font);

    if (!state.sidebar)
    {
        return;
    }
    aroma_sidebar_set_style(state.sidebar, true, 13, 4, 10);

    aroma_sidebar_set_transition(state.sidebar, AROMA_ANIM_FADE, 200);

    state.listview_containers[0] = build_bluetooth_page(state.settings_root, panel_w, area_h);

    state.listviews[1] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    if (state.listviews[1])
    {
        populate_system_info_list(state.listviews[1]);
        state.listview_containers[1] = aroma_listview_get_scroll_container(state.listviews[1]);
        if (!state.listview_containers[1])
        {
            state.listview_containers[1] = state.listviews[1];
        }
    }
    else
    {
        state.listview_containers[1] = NULL;
    }

    state.listview_containers[2] = build_services_page(state.settings_root, panel_w, area_h);
    state.listview_containers[3] = build_seat_page(state.settings_root, panel_w, area_h);

    for (int i = 0; i < num_sections; i++)
    {
        if (state.listview_containers[i])
        {
            AromaNode *node = state.listview_containers[i];
            aroma_sidebar_set_content(state.sidebar, i, &node, 1);
        }
    }

    if (state.listview_containers[0])
    {
        aroma_sidebar_set_selected(state.sidebar, 0);
    }
}