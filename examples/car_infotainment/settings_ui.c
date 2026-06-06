#include "settings_ui.h"
#include "app_state.h"
#include "aroma_animation.h"
#include "ui_animation_utils.h"
#include "theme_manager.h"
#include "tabs_manager.h"
#include "voice_handler.h"
#include "bt_speaker_api.h"
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

static BluetoothUI bt_ui = {
    .current_state = BT_STATE_IDLE,
    .initialized = false,
    .init_in_progress = false,
    .monitor_running = false,
    .ui_needs_update = false,
    .current_media = {0},
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .update_cond = PTHREAD_COND_INITIALIZER};

static void action_toggle_theme(void) { toggle_theme(); }
static void action_dev_easter_egg(void) {}

static const ListItemAction item_action_table[] = {
    {1, 1, action_toggle_theme},
};
static const int item_action_table_size =
    (int)(sizeof(item_action_table) / sizeof(item_action_table[0]));

static pthread_mutex_t easter_egg_lock = PTHREAD_MUTEX_INITIALIZER;

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

static void schedule_ui_update(void)
{
    pthread_mutex_lock(&bt_ui.lock);
    bt_ui.ui_needs_update = true;
    pthread_cond_signal(&bt_ui.update_cond);
    pthread_mutex_unlock(&bt_ui.lock);
}
static void *bt_monitor_thread_func(void *arg)
{
    UNUSED(arg);

    printf("[BT MONITOR] Monitor thread started\n");

    while (bt_ui.monitor_running)
    {
        pthread_mutex_lock(&bt_ui.lock);

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += BT_MONITOR_INTERVAL_US * 1000;
        if (ts.tv_nsec >= 1000000000)
        {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }

        pthread_cond_timedwait(&bt_ui.update_cond, &bt_ui.lock, &ts);

        if (bt_ui.ui_needs_update)
        {
            bt_ui.ui_needs_update = false;
            pthread_mutex_unlock(&bt_ui.lock);
            update_bluetooth_ui();
        }
        else if (bt_ui.initialized)
        {
            bt_state_t current = bt_speaker_get_state();
            bt_stats_t stats = bt_speaker_get_stats();
            bt_media_info_t media = bt_speaker_get_media_info();
            printf("[BT MONITOR] Monitor thread exiting. Final state: %d, Media: %s - %s (%s)\n",
                   bt_ui.current_state, media.artist, media.title, media.status);

            bool media_changed = (strcmp(bt_ui.current_media.title, media.title) != 0 ||
                                  strcmp(bt_ui.current_media.artist, media.artist) != 0 ||
                                  strcmp(bt_ui.current_media.status, media.status) != 0);

            // Audio is active if we're playing OR if media info exists AND audio time > 0
            bool audio_actually_active = (current == BT_STATE_PLAYING) ||
                                         (media.title[0] != '\0' && stats.audio_active);

            if (current != bt_ui.current_state)
            {
                bt_ui.current_state = current;
                pthread_mutex_unlock(&bt_ui.lock);
                printf("[BT MONITOR] State changed to: %d\n", current);
                update_bluetooth_ui();
            }
            else if (media_changed)
            {
                bt_ui.current_media = media;
                // If we have media info and we're in CONNECTED state, switch to PLAYING
                if (bt_ui.current_state == BT_STATE_CONNECTED &&
                    (media.title[0] != '\0' || media.artist[0] != '\0'))
                {
                    bt_ui.current_state = BT_STATE_PLAYING;
                    printf("[BT MONITOR] Auto-switching to PLAYING due to media info\n");
                }
                pthread_mutex_unlock(&bt_ui.lock);
                update_bluetooth_ui();
            }
            else if (audio_actually_active && bt_ui.current_state != BT_STATE_PLAYING)
            {
                bt_ui.current_state = BT_STATE_PLAYING;
                pthread_mutex_unlock(&bt_ui.lock);
                printf("[BT MONITOR] Forcing PLAYING state due to active audio\n");
                update_bluetooth_ui();
            }
            else
            {
                pthread_mutex_unlock(&bt_ui.lock);
            }
        }
        else
        {
            pthread_mutex_unlock(&bt_ui.lock);
        }
    }
    printf("[BT MONITOR] Monitor thread stopped\n");
    return NULL;
}
static void *bt_init_thread_func(void *arg)
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

    pthread_mutex_lock(&bt_ui.lock);
    bt_ui.init_in_progress = false;

    if (result == 0)
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

        // Update state based on media status
        if (strcmp(media->status, "playing") == 0)
        {
            bt_ui.current_state = BT_STATE_PLAYING;
            printf("[BT AVRCP] State set to PLAYING\n");
        }
        else if (strcmp(media->status, "paused") == 0)
        {
            bt_ui.current_state = BT_STATE_CONNECTED;
            printf("[BT AVRCP] State set to CONNECTED\n");
        }
        else if (media->title[0] != '\0' || media->artist[0] != '\0')
        {
            // If we have media info but status is unknown, assume playing
            if (bt_ui.current_state == BT_STATE_CONNECTED)
            {
                bt_ui.current_state = BT_STATE_PLAYING;
                printf("[BT AVRCP] State set to PLAYING (by media presence)\n");
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

    // Also check actual state from API to ensure consistency
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

    // Improved audio status detection
    if (bt_ui.audio_status_label)
    {
        if (current_state == BT_STATE_PLAYING)
        {
            aroma_label_set_text(bt_ui.audio_status_label, "Audio: Playing");
            aroma_label_set_color(bt_ui.audio_status_label, 0xFF4CAF50);
        }
        else if (current_state == BT_STATE_CONNECTED)
        {
            // Check if there's media info - might indicate music is playing but state not updated
            if (current_media.title[0] != '\0' || current_media.artist[0] != '\0')
            {
                aroma_label_set_text(bt_ui.audio_status_label, "Audio: Playing (AVRCP active)");
                aroma_label_set_color(bt_ui.audio_status_label, 0xFF4CAF50);
                // Force state to PLAYING if media info is present
                if (current_state == BT_STATE_CONNECTED && (current_media.title[0] != '\0' || current_media.artist[0] != '\0'))
                {
                    pthread_mutex_lock(&bt_ui.lock);
                    bt_ui.current_state = BT_STATE_PLAYING;
                    pthread_mutex_unlock(&bt_ui.lock);
                    bt_speaker_set_state_callback(NULL, NULL); // This will trigger UI update
                }
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

    bt_ui.status_card = aroma_ui_card(parent, 10, y_offset,
                                      card_width, 100, CARD_TYPE_GLASS);
    if (!bt_ui.status_card)
    {
        return NULL;
    }

    AromaNode* small_bg_card_for_status_icon = aroma_ui_card(bt_ui.status_card, 25, 30, 40, 40, CARD_TYPE_FILLED);
  

    bt_ui.status_icon = aroma_ui_icon(small_bg_card_for_status_icon, AROMA_ICON_BLUETOOTH,
                                      28, 5, 32, 0xFF607D8B, state.big_icon_font);

    bt_ui.status_label = aroma_ui_label(bt_ui.status_card, "Ready to Connect",
                                        85, 25, LABEL_STYLE_LABEL_LARGE, state.settings_font);

    bt_ui.stats_label = aroma_ui_label(bt_ui.status_card, "No device connected",
                                       85, 50, LABEL_STYLE_LABEL_SMALL, state.settings_font);

    y_offset += 110;
    bt_ui.device_info_card = aroma_ui_card(parent, 10, y_offset,
                                           card_width, 130, CARD_TYPE_GLASS);
    if (bt_ui.device_info_card)
    {
        aroma_ui_label(bt_ui.device_info_card, "Device Information", 16, 16,
                       LABEL_STYLE_LABEL_MEDIUM, state.settings_font);

        bt_ui.device_name_label = aroma_ui_label(bt_ui.device_info_card, "Name: None",
                                                 16, 45, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        bt_ui.device_address_label = aroma_ui_label(bt_ui.device_info_card, "Address: None",
                                                    16, 70, LABEL_STYLE_LABEL_SMALL, state.settings_font);

        bt_ui.device_manufacturer_label = aroma_ui_label(bt_ui.device_info_card, "Type: None",
                                                         16, 95, LABEL_STYLE_LABEL_SMALL, state.settings_font);

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
    AromaNode *info_label1 = aroma_ui_label(parent, "Car Bluetooth Name: Aroma Speaker" , 10, y_offset + 100,
                                                        LABEL_STYLE_LABEL_SMALL, state.settings_font);
    AromaNode* info_label2 = aroma_ui_label(parent, "PIN Code: 0000 (if asked)" , 10, y_offset + 130,
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
    aroma_listview_add_item_with_icon(listview, "CAN interface", CAN_INTERFACE, AROMA_ICON_SETTINGS_INPUT_COMPONENT, NULL);

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
    const int area_h = panel_h - 120;
    const int panel_x = sidebar_w + 8;
    const int panel_w = area_w - sidebar_w - 8;

    state.settings_panel_node = aroma_ui_container(
        window, WIN_W, 0, SETTINGS_PANEL_W, panel_h,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);

    if (!state.settings_panel_node)
    {
        return;
    }

    aroma_node_set_z_index(state.settings_panel_node, Z_LAYER_SETTINGS_PANEL);
    aroma_node_set_hidden(state.settings_panel_node, true);
    state.settings_panel_open = false;

    state.settings_root = aroma_container_create(
        state.settings_panel_node, 10, 10, area_w, area_h);

    if (!state.settings_root)
    {
        return;
    }

    aroma_node_set_z_index(state.settings_root, Z_LAYER_SETTINGS_PANEL + 1);

    const char *labels[] = {
        "Bluetooth", "Display & Theme", "Sound & Media",
        "Navigation", "Vehicle & Climate", "Behaviors",
        "System & About"};
    const char *icons[] = {
        AROMA_ICON_BLUETOOTH, AROMA_ICON_BRIGHTNESS_HIGH, AROMA_ICON_VOLUME_UP,
        AROMA_ICON_MAP, AROMA_ICON_DIRECTIONS_CAR, AROMA_ICON_SETTINGS,
        AROMA_ICON_INFO};
    const int num_sections = 7;

    state.sidebar = aroma_ui_sidebar_with_icons(
        state.settings_root, 0, 0, sidebar_w, area_h,
        labels, icons, num_sections,
        NULL, NULL, state.settings_font, state.icon_font);

    if (!state.sidebar)
    {
        return;
    }

    aroma_sidebar_set_transition(state.sidebar, AROMA_ANIM_FADE, 200);

    state.listview_containers[0] = build_bluetooth_page(state.settings_root, panel_w, area_h);

    state.listviews[1] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    if (state.listviews[1])
    {
        aroma_listview_add_item_with_icon(state.listviews[1], "Brightness level", "Adaptive", AROMA_ICON_BRIGHTNESS_HIGH, NULL);
        aroma_listview_add_item_with_icon(state.listviews[1], "Dark theme", "Toggle dark/light", AROMA_ICON_INVERT_COLORS, NULL);
        aroma_listview_add_item_with_icon(state.listviews[1], "Auto-rotate screen", "On", AROMA_ICON_SCREEN_ROTATION, NULL);
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

    state.listviews[2] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    if (state.listviews[2])
    {
        aroma_listview_add_item_with_icon(state.listviews[2], "Media volume", "70%", AROMA_ICON_VOLUME_UP, NULL);
        aroma_listview_add_item_with_icon(state.listviews[2], "Navigation volume", "80%", AROMA_ICON_NAVIGATION, NULL);
        aroma_listview_add_item_with_icon(state.listviews[2], "System sounds", "On", AROMA_ICON_NOTIFICATIONS, NULL);
        state.listview_containers[2] = aroma_listview_get_scroll_container(state.listviews[2]);
        if (!state.listview_containers[2])
        {
            state.listview_containers[2] = state.listviews[2];
        }
    }
    else
    {
        state.listview_containers[2] = NULL;
    }

    state.listviews[3] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    if (state.listviews[3])
    {
        aroma_listview_add_item_with_icon(state.listviews[3], "Location services", "High accuracy", AROMA_ICON_GPS_FIXED, NULL);
        aroma_listview_add_item_with_icon(state.listviews[3], "Live traffic", "On", AROMA_ICON_DIRECTIONS_CAR, NULL);
        aroma_listview_add_item_with_icon(state.listviews[3], "Voice guidance", "On", AROMA_ICON_VOLUME_UP, NULL);
        state.listview_containers[3] = aroma_listview_get_scroll_container(state.listviews[3]);
        if (!state.listview_containers[3])
        {
            state.listview_containers[3] = state.listviews[3];
        }
    }
    else
    {
        state.listview_containers[3] = NULL;
    }

    state.listviews[4] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    if (state.listviews[4])
    {
        aroma_listview_add_item_with_icon(state.listviews[4], "Climate settings", "Auto mode", AROMA_ICON_DIRECTIONS_CAR, NULL);
        aroma_listview_add_item_with_icon(state.listviews[4], "Vehicle diagnostics", "All systems normal", AROMA_ICON_INFO, NULL);
        aroma_listview_add_item_with_icon(state.listviews[4], "Drive mode", "Comfort", AROMA_ICON_DIRECTIONS_CAR, NULL);
        state.listview_containers[4] = aroma_listview_get_scroll_container(state.listviews[4]);
        if (!state.listview_containers[4])
        {
            state.listview_containers[4] = state.listviews[4];
        }
    }
    else
    {
        state.listview_containers[4] = NULL;
    }

    state.listviews[5] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    if (state.listviews[5])
    {
        aroma_listview_add_item_with_icon(state.listviews[5], "Tab Transition", "Toggle Fade/Slide", AROMA_ICON_SETTINGS, toggle_tab_animation_cb);
        aroma_listview_add_item_with_icon(state.listviews[5], "Voice Assistant", "Enable/Disable", AROMA_ICON_VOLUME_UP, toggle_voice_assistant_cb);
        state.listview_containers[5] = aroma_listview_get_scroll_container(state.listviews[5]);
        if (!state.listview_containers[5])
        {
            state.listview_containers[5] = state.listviews[5];
        }
    }
    else
    {
        state.listview_containers[5] = NULL;
    }

    state.listviews[6] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    if (state.listviews[6])
    {
        populate_system_info_list(state.listviews[6]);
        state.listview_containers[6] = aroma_listview_get_scroll_container(state.listviews[6]);
        if (!state.listview_containers[6])
        {
            state.listview_containers[6] = state.listviews[6];
        }
    }
    else
    {
        state.listview_containers[6] = NULL;
    }

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