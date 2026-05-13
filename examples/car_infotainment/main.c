#include <aroma.h>
#include <aroma_animation.h>
#include <aroma_native_utils.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <fcntl.h>

#ifdef AROMA_USE_VOICE_CONTROL
#include "voice_control.h"
#else
static inline void start_voice_control_thread(void) {}
static inline void trigger_manual_wake(void)         {}
static inline void aroma_voice_speak(const char *s)  { (void)s; }
#endif

#define CAN_INTERFACE "vcan0"

typedef struct {
    double   speed;
    int      rpm;
    int      gear;
    double   soc;
    double   voltage;
    double   current;
    double   cabin_temp;
    double   target_temp;
    int      hvac_on;
    int      fan_speed;
    int      seat_heaters;
    int      doors;
    uint32_t fault_code;
    double   range;
} EVState;

static EVState         vehicle_state = {0};
static pthread_mutex_t can_mtx       = PTHREAD_MUTEX_INITIALIZER;

#define LOG_MAX_LINES 1024
#define LOG_LINE_LEN  256

static char            log_lines[LOG_MAX_LINES][LOG_LINE_LEN];
static int             log_head  = 0;
static int             log_count = 0;
static pthread_mutex_t log_mtx   = PTHREAD_MUTEX_INITIALIZER;
static int             log_dirty = 0;

static int log_pipe_fd[2];
static int original_stdout = -1;
static int original_stderr = -1;
static pthread_t log_capture_thread;
static volatile int log_capture_running = 0;

static void *log_capture_thread_func(void *arg)
{
    char buffer[LOG_LINE_LEN];
    char line[LOG_LINE_LEN];
    int line_pos = 0;

    while (log_capture_running) {
        ssize_t n = read(log_pipe_fd[0], buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            if (n == 0) break;
            usleep(1000);
            continue;
        }

        for (ssize_t i = 0; i < n; i++) {
            char c = buffer[i];

            if (c == '\n' || c == '\r' || line_pos >= LOG_LINE_LEN - 1) {
                line[line_pos] = '\0';
                if (line_pos > 0) {
                    time_t t = time(NULL);
                    struct tm *tm_info = localtime(&t);
                    char entry[LOG_LINE_LEN];
                    snprintf(entry, sizeof(entry), "%s", line);

                    pthread_mutex_lock(&log_mtx);
                    strncpy(log_lines[log_head], entry, LOG_LINE_LEN - 1);
                    log_lines[log_head][LOG_LINE_LEN - 1] = '\0';
                    log_head = (log_head + 1) % LOG_MAX_LINES;
                    log_count++;
                    log_dirty = 1;
                    pthread_mutex_unlock(&log_mtx);
                }
                line_pos = 0;
            } else {
                line[line_pos++] = c;
            }
        }
    }
    return NULL;
}

static void init_log_capture(void)
{
    if (pipe(log_pipe_fd) == -1) {
        perror("Failed to create log capture pipe");
        return;
    }

    original_stdout = dup(STDOUT_FILENO);
    original_stderr = dup(STDERR_FILENO);

    int flags = fcntl(log_pipe_fd[0], F_GETFL, 0);
    fcntl(log_pipe_fd[0], F_SETFL, flags | O_NONBLOCK);

    dup2(log_pipe_fd[1], STDOUT_FILENO);
    dup2(log_pipe_fd[1], STDERR_FILENO);

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    log_capture_running = 1;
    pthread_create(&log_capture_thread, NULL, log_capture_thread_func, NULL);
}

static void cleanup_log_capture(void)
{
    log_capture_running = 0;
    close(log_pipe_fd[1]);
    pthread_join(log_capture_thread, NULL);
    close(log_pipe_fd[0]);

    if (original_stdout != -1) {
        dup2(original_stdout, STDOUT_FILENO);
        close(original_stdout);
    }
    if (original_stderr != -1) {
        dup2(original_stderr, STDERR_FILENO);
        close(original_stderr);
    }
}

static void app_log(const char *fmt, ...)
{
    char msg[LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char entry[LOG_LINE_LEN];
    snprintf(entry, sizeof(entry), "%s",msg);

    if (original_stdout != -1) {
        dprintf(original_stdout, "%s\n", entry);
    }

    pthread_mutex_lock(&log_mtx);
    strncpy(log_lines[log_head], entry, LOG_LINE_LEN - 1);
    log_lines[log_head][LOG_LINE_LEN - 1] = '\0';
    log_head  = (log_head + 1) % LOG_MAX_LINES;
    log_count++;
    log_dirty = 1;
    pthread_mutex_unlock(&log_mtx);
}

#define WIN_W 1280
#define WIN_H 800

#define Z_LAYER_BACKGROUND       1
#define Z_LAYER_VEHICLE_IMAGE    2
#define Z_LAYER_VEHICLE_OVERLAYS 3
#define Z_LAYER_CARDS_BOTTOM     10
#define Z_LAYER_MAP_PANEL        20
#define Z_LAYER_MAP_BUTTON       15
#define Z_LAYER_MAP_CONTROLS     21
#define Z_LAYER_MAP_CLOSE        22
#define Z_LAYER_STATUS_BAR       100
#define Z_LAYER_SETTINGS_PANEL   150
#define Z_LAYER_VOICE_CARD       999998
#define Z_LAYER_VOICE_CONTENT    999999

#define MAP_PANEL_WIDTH  WIN_W
#define MAP_PANEL_OFFSET 0

#define SETTINGS_PANEL_W 800
#define SETTINGS_ANIM_MS 350

typedef struct
{
    AromaFont *icon_font;
    AromaFont *ui_font;
    AromaFont *tab_font;
    AromaFont *settings_font;
    AromaFont *clock_font;
    AromaFont *clock_pm_am_font;
    AromaFont *log_font;

    AromaWindow *window;
    AromaNode   *settings_root;
    AromaNode   *vehicle_view_root;
    AromaNode   *tabs;
    AromaNode   *sidebar;

    AromaNode *time_label;
    AromaNode *location_label;
    AromaNode *status_card;
    AromaNode *signal_icon;
    AromaNode *wifi_icon;
    AromaNode *battery_icon;
    AromaNode *gps_icon;
    AromaNode *bluetooth_icon;

    AromaNode *vehicle_view_lock_icon;
    AromaNode *vehicle_view_lock_divider;
    AromaNode *vehicle_view_charge_port_divider;
    AromaNode *vehicle_view_charge_port_icon;
    AromaNode *vehicle_view_frunk_header;
    AromaNode *vehicle_view_frunk_desc;
    AromaNode *vehicle_view_frunk_divider;
    AromaNode *vehicle_view_trunk_divider;
    AromaNode *vehicle_view_trunk_header;
    AromaNode *vehicle_view_trunk_desc;
    AromaNode *vehicle_view_warning_message_card;
    AromaNode *vehicle_view_warning_message_label;
    AromaNode *vehicle_view_warning_warning_icon;
    AromaNode *vehicle_view_warning_message_action;
    AromaNode *vehicle_view_large_clock;
    AromaNode *vehicle_view_large_clock_pm_am;
    AromaNode *vehicle_view_hints;
    AromaNode *vehicle_view_side_arrow_icon_button;
    AromaNode *overlay;
    AromaNode *recent_lv;

    AromaNode *speed_label;
    AromaNode *speed_gauge;
    AromaNode *range_label;
    AromaNode *climate_label;
    AromaNode *gear_bg_card;
    AromaNode *gear_fg_card;

    AromaNode *ac_card;
    AromaNode *music_card;
    AromaNode *nav_card;

    AromaNode *vehicle_view_battery_divider;
    AromaNode *vehicle_view_battery_percentage;

    AromaNode *battery_image;
    AromaNode *battery_health;
    AromaNode *battery_percentage;

    AromaNode *map_node;
    AromaNode *map_panel;
    AromaNode *map_overlay_background;
    bool       map_panel_open;

    AromaNode *settings_panel_node;
    bool       settings_panel_open;

    AromaNode *ac_temp_label;

    AromaNode *voice_button;
    AromaNode *settings_icon;
    AromaNode *voice_status_label;
    AromaNode *voice_status_card;
    AromaNode *loading_spinner;

    AromaNode *listviews[8];
    AromaNode *listview_containers[8];

    AromaNode *easter_egg_overlay;
    AromaNode *easter_egg_icon;

    AromaNode *setup_overlay;

    AromaNode *battery_button;

    AromaTheme theme;
    bool       dark_theme_enabled;
} AppState;

static AppState state = {0};

void build_settings_ui(AromaNode *window);
void build_vehicle_view(AromaNode *window);
void build_setup_ui(AromaNode *window);
void build_easter_egg_ui(AromaNode *window);
void listview_callback(int index, void *user_data);
void navigate_to_tab(int index);
bool global_keyboard_event_handler(AromaEvent *event, void *user_data);
void open_map_panel(void *user_data);
void close_map_panel(void *user_data);
void close_settings_panel(void *user_data);

static bool            voice_is_visible      = false;
static pthread_mutex_t voice_mutex           = PTHREAD_MUTEX_INITIALIZER;
static int             voice_target_tab      = -1;
static char            voice_status_text[256]  = "";
static char            voice_partial_text[512] = "";
static char            voice_nav_dest[128]   = {0};
static bool            voice_nav_trigger     = false;
static int             voice_partial_timeout = 0;
static int             voice_theme_change    = -1;
static int             voice_ac_change       = 0;
static int             voice_info_request    = 0;
static int             current_ac_temp       = 23;

bool g_voice_assistant_enabled = true;

void parse_can(struct can_frame *frame)
{
    pthread_mutex_lock(&can_mtx);
    if (frame->can_id == 0x100) {
        uint16_t soc = (frame->data[0] << 8) | frame->data[1];
        int16_t  cur = (frame->data[2] << 8) | frame->data[3];
        uint16_t vol = (frame->data[4] << 8) | frame->data[5];
        vehicle_state.soc     = soc / 100.0;
        vehicle_state.current = cur / 10.0;
        vehicle_state.voltage = vol / 10.0;
        pthread_mutex_unlock(&can_mtx);
        printf("CAN 0x100: SoC=%.1f%% V=%.1fV I=%.1fA\n",
                vehicle_state.soc, vehicle_state.voltage, vehicle_state.current);
        fflush(stdout);
    } else if (frame->can_id == 0x101) {
        uint16_t spd = (frame->data[0] << 8) | frame->data[1];
        int32_t  rpm = (frame->data[2] << 24) | (frame->data[3] << 16)
                     | (frame->data[4] <<  8) |  frame->data[5];
        vehicle_state.speed = spd / 10.0;
        vehicle_state.rpm   = rpm;
        vehicle_state.gear  = frame->data[6];
        pthread_mutex_unlock(&can_mtx);
        printf("CAN 0x101: Speed=%.1f km/h RPM=%d Gear=%d\n",
                vehicle_state.speed, vehicle_state.rpm, vehicle_state.gear);
        fflush(stdout);
    } else if (frame->can_id == 0x102) {
        int16_t temp = (frame->data[0] << 8) | frame->data[1];
        vehicle_state.cabin_temp   = temp / 10.0;
        vehicle_state.hvac_on      = frame->data[2];
        vehicle_state.doors        = frame->data[3];
        vehicle_state.fan_speed    = frame->data[4];
        vehicle_state.target_temp  = frame->data[5];
        vehicle_state.seat_heaters = frame->data[6];
        pthread_mutex_unlock(&can_mtx);
        printf("CAN 0x102: Cabin=%.1f°C HVAC=%s Fan=%d Doors=0x%02X\n",
                vehicle_state.cabin_temp,
                vehicle_state.hvac_on ? "On" : "Off",
                vehicle_state.fan_speed,
                vehicle_state.doors);
        fflush(stdout);
    } else if (frame->can_id == 0x103) {
        uint16_t rng = (frame->data[0] << 8) | frame->data[1];
        vehicle_state.range = rng;
        pthread_mutex_unlock(&can_mtx);
        printf("CAN 0x103: Range=%d km\n", (int)vehicle_state.range);
        fflush(stdout);
    } else if (frame->can_id == 0x104) {
        vehicle_state.fault_code =
            (frame->data[0] << 24) | (frame->data[1] << 16)
          | (frame->data[2] <<  8) |  frame->data[3];
        uint32_t fc = vehicle_state.fault_code;
        pthread_mutex_unlock(&can_mtx);
        if (fc) {
            printf("CAN 0x104: FAULT 0x%04X\n", fc);
            fflush(stdout);
        } else {
            printf("CAN 0x104: Fault cleared\n");
            fflush(stdout);
        }
    } else if (frame->can_id == 0x01) {
        pthread_mutex_unlock(&can_mtx);
        printf("CAN 0x01: Map-open trigger\n");
        fflush(stdout);
        open_map_panel(NULL);
    } else {
        int dlc = frame->can_dlc;
        unsigned int cid = frame->can_id;
        pthread_mutex_unlock(&can_mtx);
        printf("CAN 0x%03X: %d bytes (unhandled)\n", cid, dlc);
        fflush(stdout);
    }
}

void *can_thread(void *arg)
{
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        printf("CAN: socket() failed – running without CAN\n");
        fflush(stdout);
        return NULL;
    }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, CAN_INTERFACE, IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        printf("CAN: interface '%s' not found – running without CAN\n", CAN_INTERFACE);
        fflush(stdout);
        close(s);
        return NULL;
    }

    struct sockaddr_can addr = {0};
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("CAN: bind() failed – running without CAN\n");
        fflush(stdout);
        close(s);
        return NULL;
    }

    printf("CAN: listening on %s\n", CAN_INTERFACE);
    fflush(stdout);
    struct can_frame frame;
    while (aroma_ui_is_running()) {
        if (read(s, &frame, sizeof(struct can_frame)) > 0)
            parse_can(&frame);
    }
    close(s);
    return NULL;
}

void set_voice_status(const char *status)
{
    if (state.voice_status_label)
        aroma_label_set_text(state.voice_status_label, status);

    if (!state.voice_status_card)
        return;

    bool hide = (!status || strlen(status) == 0);
    if (!hide && !voice_is_visible) {
        aroma_node_set_hidden(state.voice_status_card, false);
        aroma_node_set_hidden(state.loading_spinner, false);
        aroma_animation_start((AromaNode *)state.voice_status_card,
                              AROMA_ANIM_SLIDE_Y, -100, -20, 300);
        voice_is_visible = true;
    } else if (hide && voice_is_visible) {
        aroma_animation_start((AromaNode *)state.voice_status_card,
                              AROMA_ANIM_SLIDE_Y, -20, -100, 300);
        voice_is_visible = false;
        aroma_node_set_hidden(state.loading_spinner, true);
    }
}

void queue_voice_navigation(const char *dest)
{
    pthread_mutex_lock(&voice_mutex);
    strncpy(voice_nav_dest, dest, sizeof(voice_nav_dest) - 1);
    voice_nav_trigger = true;
    pthread_mutex_unlock(&voice_mutex);
}

void queue_voice_partial(const char *partial_text)
{
    pthread_mutex_lock(&voice_mutex);
    if (partial_text && strlen(partial_text) > 0) {
        strncpy(voice_partial_text, partial_text, sizeof(voice_partial_text) - 1);
        voice_partial_text[sizeof(voice_partial_text) - 1] = '\0';
        voice_partial_timeout = 180;
    }
    pthread_mutex_unlock(&voice_mutex);
}

void queue_voice_theme(int dark_mode)
{
    pthread_mutex_lock(&voice_mutex);
    voice_theme_change = dark_mode;
    pthread_mutex_unlock(&voice_mutex);
}

void queue_voice_ac_action(int temp_delta)
{
    pthread_mutex_lock(&voice_mutex);
    voice_ac_change = temp_delta;
    pthread_mutex_unlock(&voice_mutex);
}

void queue_voice_info_request(int info_type)
{
    pthread_mutex_lock(&voice_mutex);
    voice_info_request = info_type;
    pthread_mutex_unlock(&voice_mutex);
}

void queue_voice_action(int tab_index, bool call, bool end_call, const char *status)
{
    pthread_mutex_lock(&voice_mutex);
    if (tab_index >= 0)
        voice_target_tab = tab_index;
    if (status)
        strncpy(voice_status_text, status, sizeof(voice_status_text) - 1);
    (void)call; (void)end_call;
    pthread_mutex_unlock(&voice_mutex);
}

void voice_button_callback(AromaNode *node, void *user_data)
{
#ifdef AROMA_USE_VOICE_CONTROL
    trigger_manual_wake();
    system("(speaker-test -t sine -f 800 -l 1 >/dev/null 2>&1 & pid=$!; sleep 0.1; kill -9 $pid >/dev/null 2>&1) &");
#endif
    printf("Voice: manual wake triggered\n");
    fflush(stdout);
    queue_voice_partial("Listening...");
}

void navigate_to_tab(int index)
{
    if (state.tabs)
        aroma_tabs_set_selected(state.tabs, index);
}

static void animate_node_x(AromaNode *node, int from, int to)
{
    if (!node) return;
    AromaAnimation *anim = aroma_animation_start(
        node, AROMA_ANIM_SLIDE_X, from, to, SETTINGS_ANIM_MS);
    if (anim)
        aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
}

static int get_node_x(AromaNode *node)
{
    if (!node || !node->node_widget_ptr) return 0;
    return ((AromaRect *)node->node_widget_ptr)->x;
}

static void shift_node(AromaNode *node, int delta)
{
    if (!node) return;
    int x = get_node_x(node);
    animate_node_x(node, x, x + delta);
}

void open_map_panel(void *user_data)
{
    if (!state.map_panel || state.map_panel_open)
        return;

    if (state.settings_panel_open)
        close_settings_panel(NULL);

    if (state.map_overlay_background)
        aroma_node_set_hidden(state.map_overlay_background, false);

    aroma_node_set_hidden(state.map_panel, false);
    AromaAnimation *map_anim = aroma_animation_start(
        state.map_panel, AROMA_ANIM_SLIDE_X, WIN_W, MAP_PANEL_OFFSET, 450);
    aroma_animation_start(state.recent_lv, AROMA_ANIM_SLIDE_X, WIN_W, MAP_PANEL_OFFSET, 350);
    aroma_animation_set_easing(map_anim, AROMA_EASE_OUT_CUBIC);
    state.map_panel_open = true;
    printf("UI: map panel opened\n");
    fflush(stdout);
}

void close_map_panel(void *user_data)
{
    if (!state.map_panel || !state.map_panel_open)
        return;

    if (state.map_overlay_background)
        aroma_node_set_hidden(state.map_overlay_background, true);

    AromaAnimation *map_anim = aroma_animation_start(
        state.map_panel, AROMA_ANIM_SLIDE_X, MAP_PANEL_OFFSET, WIN_W, 450);
    aroma_animation_set_easing(map_anim, AROMA_EASE_OUT_CUBIC);
    state.map_panel_open = false;
    printf("UI: map panel closed\n");
    fflush(stdout);
}

static void open_settings_panel(void *user_data)
{
    if (!state.settings_panel_node || state.settings_panel_open)
        return;

    if (state.map_panel_open)
        close_map_panel(NULL);

    aroma_node_set_hidden(state.settings_panel_node, false);

    animate_node_x(state.settings_panel_node, WIN_W, WIN_W - SETTINGS_PANEL_W);
    animate_node_x(state.vehicle_view_root, 0, -SETTINGS_PANEL_W);

    shift_node(state.overlay,            -SETTINGS_PANEL_W);
    shift_node(state.status_card,        -SETTINGS_PANEL_W);
    shift_node(state.battery_icon,       -SETTINGS_PANEL_W);
    shift_node(state.signal_icon,        -SETTINGS_PANEL_W);
    shift_node(state.wifi_icon,          -SETTINGS_PANEL_W);
    shift_node(state.gps_icon,           -SETTINGS_PANEL_W);
    shift_node(state.bluetooth_icon,     -SETTINGS_PANEL_W);
    shift_node(state.voice_button,       -SETTINGS_PANEL_W);
    shift_node(state.settings_icon,      -SETTINGS_PANEL_W);

    animate_node_x(state.tabs, 0, -(SETTINGS_PANEL_W / 3));

    state.settings_panel_open = true;
    printf("UI: settings panel opened\n");
    fflush(stdout);
}

void close_settings_panel(void *user_data)
{
    if (!state.settings_panel_node || !state.settings_panel_open)
        return;

    animate_node_x(state.settings_panel_node, WIN_W - SETTINGS_PANEL_W, WIN_W);
    animate_node_x(state.vehicle_view_root, -SETTINGS_PANEL_W, 0);

    shift_node(state.overlay,            SETTINGS_PANEL_W);
    shift_node(state.status_card,        SETTINGS_PANEL_W);
    shift_node(state.battery_icon,       SETTINGS_PANEL_W);
    shift_node(state.signal_icon,        SETTINGS_PANEL_W);
    shift_node(state.wifi_icon,          SETTINGS_PANEL_W);
    shift_node(state.gps_icon,           SETTINGS_PANEL_W);
    shift_node(state.bluetooth_icon,     SETTINGS_PANEL_W);
    shift_node(state.voice_button,       SETTINGS_PANEL_W);
    shift_node(state.settings_icon,      SETTINGS_PANEL_W);

    animate_node_x(state.tabs, -(SETTINGS_PANEL_W / 3), 0);

    state.settings_panel_open = false;
    printf("UI: settings panel closed\n");
    fflush(stdout);
}

static void battery_diagnostics(AromaNode *node, void *user_data)
{
    aroma_image_set_source(state.overlay, "../assets/car_battery.png");
    AromaAnimation *anim = aroma_animation_start(
        state.overlay, AROMA_ANIM_SLIDE_Y, 900, 250, 400);
    aroma_animation_set_easing(anim, AROMA_EASE_OUT_ELASTIC);

    aroma_node_set_hidden(state.vehicle_view_lock_divider,           true);
    aroma_node_set_hidden(state.vehicle_view_charge_port_divider,    true);
    aroma_node_set_hidden(state.vehicle_view_charge_port_icon,       true);
    aroma_node_set_hidden(state.vehicle_view_frunk_header,           true);
    aroma_node_set_hidden(state.vehicle_view_frunk_desc,             true);
    aroma_node_set_hidden(state.vehicle_view_frunk_divider,          true);
    aroma_node_set_hidden(state.vehicle_view_trunk_divider,          true);
    aroma_node_set_hidden(state.vehicle_view_trunk_header,           true);
    aroma_node_set_hidden(state.vehicle_view_trunk_desc,             true);
    aroma_node_set_hidden(state.vehicle_view_lock_icon,              true);
    aroma_node_set_hidden(state.vehicle_view_warning_message_card,   true);
    aroma_node_set_hidden(state.vehicle_view_warning_message_label,  true);
    aroma_node_set_hidden(state.vehicle_view_warning_warning_icon,   true);
    aroma_node_set_hidden(state.vehicle_view_warning_message_action, true);
    aroma_node_set_hidden(state.battery_image,      false);
    aroma_node_set_hidden(state.battery_health,     false);
    aroma_node_set_hidden(state.battery_percentage, false);
    aroma_animation_start(state.battery_image,      AROMA_ANIM_FADE, 0, 1, 1000);
    aroma_animation_start(state.battery_health,     AROMA_ANIM_FADE, 0, 1, 1000);
    aroma_animation_start(state.battery_percentage, AROMA_ANIM_FADE, 0, 1, 1000);
    printf("UI: battery diagnostics view shown\n");
    fflush(stdout);
}

static void map_zoom_in_cb(void *user_data)
{
    if (user_data) aroma_map_zoom_in((AromaNode *)user_data);
}

static void map_zoom_out_cb(void *user_data)
{
    if (user_data) aroma_map_zoom_out((AromaNode *)user_data);
}

static void toggle_recent_card_cb(void *user_data) { (void)user_data; }

static void navigate_map(int index, void *user_data)
{
    AromaNode *map = (AromaNode *)user_data;
    if (!map) return;
    aroma_map_clear_route(map);

    typedef struct {
        double lat, lon;
        const char *start_label, *end_label;
        double end_lat, end_lon;
        int zoom;
    } Route;
    static const Route routes[] = {
        { 48.8566,  2.3522, "Start: Paris",       "Home: Versailles",  48.8049,  2.1204, 12 },
        { 51.5074, -0.1278, "Start: London",      "Work: Heathrow",    51.4700, -0.4543, 11 },
        { 52.5200, 13.4050, "Start: Berlin",      "Gym: BER Airport",  52.3667, 13.5033, 11 },
        { 41.9028, 12.4964, "Start: Colosseum",   "Supermarket: FCO",  41.7999, 12.2462, 12 },
        { 48.1351, 11.5820, "Start: Marienplatz", "Cafe: MUC Airport", 48.3537, 11.7861, 11 },
    };
    if (index < 0 || index >= (int)(sizeof(routes) / sizeof(routes[0])))
        return;

    const Route *r = &routes[index];
    aroma_map_pan_to(map, r->lat, r->lon);
    aroma_map_set_zoom(map, r->zoom);
    aroma_map_set_route(map, r->lat, r->lon, r->end_lat, r->end_lon, 0xFF35A8FE);
    aroma_map_add_popup_marker(map, r->lat,     r->lon,     0xFF00C853, r->start_label);
    aroma_map_add_popup_marker(map, r->end_lat, r->end_lon, 0xFFD50000, r->end_label);
    printf("Map: route %d selected (%s)\n", index, r->start_label);
    fflush(stdout);
}

static void ac_temp_up_callback(AromaNode *node, void *user_data)
{
    if (current_ac_temp < 30) current_ac_temp++;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d°C", current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
    printf("AC: temperature set to %d°C\n", current_ac_temp);
    fflush(stdout);
}

static void ac_temp_down_callback(AromaNode *node, void *user_data)
{
    if (current_ac_temp > 16) current_ac_temp--;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d°C", current_ac_temp);
    aroma_label_set_text(state.ac_temp_label, buf);
    printf("AC: temperature set to %d°C\n", current_ac_temp);
    fflush(stdout);
}

static float bounce_start_x, bounce_start_y, bounce_end_x, bounce_end_y;

static void bounce_anim_cb(AromaNode *target, float t, void *user_data)
{
    if (!target) return;
    AromaRect *r = (AromaRect *)target->node_widget_ptr;
    if (r) {
        r->x = bounce_start_x + (bounce_end_x - bounce_start_x) * t;
        r->y = bounce_start_y + (bounce_end_y - bounce_start_y) * t;
    }
}

static void interact_easter_egg_cb(void *user_data)
{
    if (!state.easter_egg_icon) return;
    AromaRect *r = (AromaRect *)state.easter_egg_icon->node_widget_ptr;
    if (!r) return;
    bounce_start_x = r->x;
    bounce_start_y = r->y;
    bounce_end_x   = 50 + (rand() % (WIN_W - 200));
    bounce_end_y   = 50 + (rand() % (WIN_H - 200));
    AromaAnimation *anim = aroma_animation_start_custom(
        state.easter_egg_icon, 0.0f, 1.0f, 800, bounce_anim_cb, NULL);
    if (anim)
        aroma_animation_set_easing(anim, AROMA_EASE_OUT_ELASTIC);
}

static void close_easter_egg_cb(void *user_data)
{
    if (state.easter_egg_overlay)
        aroma_node_set_hidden(state.easter_egg_overlay, true);
}

void build_easter_egg_ui(AromaNode *window)
{
    state.easter_egg_overlay = aroma_ui_card(window, 0, 0, WIN_W, WIN_H, CARD_TYPE_GLASS);
    if (!state.easter_egg_overlay) return;
    aroma_node_set_z_index(state.easter_egg_overlay, INT_MAX);
    aroma_node_set_hidden(state.easter_egg_overlay, true);

    state.easter_egg_icon = aroma_ui_iconbutton(
        state.easter_egg_overlay, AROMA_ICON_BUG_REPORT,
        WIN_W / 2 - 50, WIN_H / 2 - 50, 100,
        ICON_BUTTON_FILLED, interact_easter_egg_cb, NULL, state.icon_font);
    aroma_node_set_z_index(state.easter_egg_icon, INT_MAX);

    AromaNode *close_btn = aroma_ui_iconbutton(
        state.easter_egg_overlay, AROMA_ICON_CLOSE,
        WIN_W - 80, 30, 50,
        ICON_BUTTON_OUTLINED, close_easter_egg_cb, NULL, state.icon_font);
    aroma_node_set_z_index(close_btn, INT_MAX);
}

bool global_keyboard_event_handler(AromaEvent *event, void *user_data)
{
    if (event->event_type == EVENT_TYPE_KEY_PRESS) {
        if ((event->data.key.key_code == 'i' || event->data.key.key_code == 'I') &&
            (event->data.key.modifiers & AROMA_KEY_MOD_CTRL))
            return true;
    }
    return false;
}

static void settings_button_callback(AromaNode *node, void *user_data)
{
    if (state.map_panel_open)
        close_map_panel(NULL);

    if (state.settings_panel_open)
        close_settings_panel(NULL);
    else
        open_settings_panel(NULL);
}

void toggle_tab_animation_cb(AromaNode *sender, void *user_data)
{
    static bool is_slide = true;
    is_slide = !is_slide;
    aroma_tabs_set_transition(state.tabs,
        is_slide ? AROMA_ANIM_SLIDE_X : AROMA_ANIM_FADE, 300);
    printf("Settings: tab transition set to %s\n", is_slide ? "slide" : "fade");
    fflush(stdout);
}

void toggle_voice_assistant_cb(AromaNode *sender, void *user_data)
{
    g_voice_assistant_enabled = !g_voice_assistant_enabled;
    printf("Settings: voice assistant %s\n",
            g_voice_assistant_enabled ? "enabled" : "disabled");
    fflush(stdout);
}

void listview_callback(int index, void *user_data)
{
    int selected = aroma_sidebar_get_selected(state.sidebar);

    if (selected == 1 && index == 1) {
        if (state.dark_theme_enabled) {
            state.theme = aroma_theme_create_high_contrast();
            state.theme.colors.primary       = 0xFF2196F3;
            state.theme.colors.primary_dark  = 0xFF1976D2;
            state.theme.colors.primary_light = 0xFFBBDEFB;
        } else {
            state.theme = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_BLUE);
        }
        aroma_ui_set_theme(&state.theme);
        state.dark_theme_enabled = !state.dark_theme_enabled;
        printf("Settings: theme switched to %s\n",
                state.dark_theme_enabled ? "dark" : "light");
        fflush(stdout);
    } else if (selected == 6) {
        static int build_clicks = 0;
        if (index == 4) {
            build_clicks++;
            if (build_clicks > 2 && build_clicks < 7) {
                char msg[64];
                snprintf(msg, sizeof(msg),
                         "You are now %d steps away from being a developer.",
                         7 - build_clicks);
                queue_voice_partial(msg);
                printf("Easter-egg: %d/7 clicks on build date\n", build_clicks);
                fflush(stdout);
            } else if (build_clicks >= 7) {
                if (build_clicks == 7) {
                    queue_voice_action(-1, false, false, "You are now a developer!");
#ifdef AROMA_USE_VOICE_CONTROL
                    system("(speaker-test -t sine -f 1200 -l 1 >/dev/null 2>&1 "
                           "& pid=$!; sleep 0.15; kill -9 $pid >/dev/null 2>&1) &");
#endif
                    printf("Easter-egg: developer mode unlocked!\n");
                    fflush(stdout);
                }
                aroma_node_set_hidden(state.easter_egg_overlay, false);
            }
        } else {
            build_clicks = 0;
        }
    }
}

static void refresh_log_panel(void)
{
}

static AromaNode *settings_listview(AromaNode *parent, int x, int y, int w, int h)
{
    AromaNode *lv = aroma_ui_listview(parent, x, y, w, h,
                                      listview_callback, NULL, state.settings_font);
    if (lv)
        aroma_listview_set_icon_font(lv, state.icon_font);
    return lv;
}

void build_settings_ui(AromaNode *window)
{
    int panel_h   = WIN_H - 80;
    int sidebar_w = 220;

    state.settings_panel_node = aroma_ui_container(
        window, WIN_W, 0, SETTINGS_PANEL_W, panel_h,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.settings_panel_node, Z_LAYER_SETTINGS_PANEL);
    aroma_node_set_hidden(state.settings_panel_node, true);
    state.settings_panel_open = false;

    int area_w  = SETTINGS_PANEL_W - 20;
    int area_h  = panel_h - 120;
    int panel_x = sidebar_w + 8;
    int panel_w = area_w - sidebar_w - 8;

    state.settings_font = aroma_font_create_from_memory(
        aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 18);
    state.log_font = aroma_font_create_from_memory(
        aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 14);

    state.settings_root = aroma_container_create(
        state.settings_panel_node, 10, 10, area_w, area_h);
    aroma_node_set_z_index(state.settings_root, Z_LAYER_SETTINGS_PANEL + 1);

    const char *labels[] = {
        "Connectivity", "Display & Theme", "Sound & Media",
        "Navigation",   "Vehicle & Climate", "Behaviors",
        "System & About"
    };
    const char *icons[] = {
        AROMA_ICON_WIFI,          AROMA_ICON_BRIGHTNESS_HIGH, AROMA_ICON_VOLUME_UP,
        AROMA_ICON_MAP,           AROMA_ICON_DIRECTIONS_CAR,  AROMA_ICON_SETTINGS,
        AROMA_ICON_INFO
    };
    int num_sections = 7;

    state.sidebar = aroma_ui_sidebar_with_icons(
        state.settings_root, 0, 0, sidebar_w, area_h,
        labels, icons, num_sections,
        NULL, NULL, state.settings_font, state.icon_font);
    aroma_sidebar_set_transition(state.sidebar, AROMA_ANIM_FADE, 200);

    state.listviews[0] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[0], "Wi-Fi",       "Connected - AutoNet",  AROMA_ICON_WIFI,         NULL);
    aroma_listview_add_item_with_icon(state.listviews[0], "Bluetooth",   "1 Device Paired",      AROMA_ICON_BLUETOOTH,    NULL);
    aroma_listview_add_item_with_icon(state.listviews[0], "Mobile data", "5G connection active", AROMA_ICON_NETWORK_CELL, NULL);
    state.listview_containers[0] = aroma_listview_get_scroll_container(state.listviews[0]);

    state.listviews[1] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[1], "Brightness level",   "Adaptive",          AROMA_ICON_BRIGHTNESS_HIGH, NULL);
    aroma_listview_add_item_with_icon(state.listviews[1], "Dark theme",         "Toggle dark/light", AROMA_ICON_INVERT_COLORS,   NULL);
    aroma_listview_add_item_with_icon(state.listviews[1], "Auto-rotate screen", "On",                AROMA_ICON_SCREEN_ROTATION, NULL);
    state.listview_containers[1] = aroma_listview_get_scroll_container(state.listviews[1]);

    state.listviews[2] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[2], "Media volume",      "70%", AROMA_ICON_VOLUME_UP,     NULL);
    aroma_listview_add_item_with_icon(state.listviews[2], "Navigation volume", "80%", AROMA_ICON_NAVIGATION,    NULL);
    aroma_listview_add_item_with_icon(state.listviews[2], "System sounds",     "On",  AROMA_ICON_NOTIFICATIONS, NULL);
    state.listview_containers[2] = aroma_listview_get_scroll_container(state.listviews[2]);

    state.listviews[3] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[3], "Location services", "High accuracy", AROMA_ICON_GPS_FIXED,      NULL);
    aroma_listview_add_item_with_icon(state.listviews[3], "Live traffic",      "On",            AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(state.listviews[3], "Voice guidance",    "On",            AROMA_ICON_VOLUME_UP,      NULL);
    state.listview_containers[3] = aroma_listview_get_scroll_container(state.listviews[3]);

    state.listviews[4] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[4], "Climate settings",    "Auto mode",          AROMA_ICON_DIRECTIONS_CAR, NULL);
    aroma_listview_add_item_with_icon(state.listviews[4], "Vehicle diagnostics", "All systems normal", AROMA_ICON_INFO,           NULL);
    aroma_listview_add_item_with_icon(state.listviews[4], "Drive mode",          "Comfort",            AROMA_ICON_DIRECTIONS_CAR, NULL);
    state.listview_containers[4] = aroma_listview_get_scroll_container(state.listviews[4]);

    state.listviews[5] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    aroma_listview_add_item_with_icon(state.listviews[5], "Tab Transition",  "Toggle Fade/Slide", AROMA_ICON_SETTINGS,  toggle_tab_animation_cb);
    aroma_listview_add_item_with_icon(state.listviews[5], "Voice Assistant", "Enable/Disable",    AROMA_ICON_VOLUME_UP, toggle_voice_assistant_cb);
    state.listview_containers[5] = aroma_listview_get_scroll_container(state.listviews[5]);

    state.listviews[6] = settings_listview(state.settings_root, panel_x, 0, panel_w, area_h);
    {
        char processor_name[256] = "Unknown";
        FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
        if (cpuinfo) {
            char line[256];
            while (fgets(line, sizeof(line), cpuinfo)) {
                if (strncmp(line, "model name", 10) == 0) {
                    char *colon = strchr(line, ':');
                    if (colon) {
                        strcpy(processor_name, colon + 2);
                        char *nl = strchr(processor_name, '\n');
                        if (nl) *nl = '\0';
                    }
                    break;
                }
            }
            fclose(cpuinfo);
        }

        long total_ram_mb = 0;
        FILE *meminfo = fopen("/proc/meminfo", "r");
        if (meminfo) {
            char line[256];
            while (fgets(line, sizeof(line), meminfo)) {
                if (strncmp(line, "MemTotal", 8) == 0) {
                    long kb;
                    sscanf(line, "MemTotal: %ld kB", &kb);
                    total_ram_mb = kb / 1024;
                    break;
                }
            }
            fclose(meminfo);
        }
        char ram_str[64];
        if      (total_ram_mb > 1024) snprintf(ram_str, sizeof(ram_str), "%.1f GB", total_ram_mb / 1024.0);
        else if (total_ram_mb > 0)    snprintf(ram_str, sizeof(ram_str), "%ld MB",  total_ram_mb);
        else                          strcpy(ram_str, "NaN");

        char uptime_str[64] = "Unknown";
        FILE *uptime_file = fopen("/proc/uptime", "r");
        if (uptime_file) {
            double s;
            if (fscanf(uptime_file, "%lf", &s) == 1) {
                int d = (int)(s / 86400),
                    h = (int)((s - d * 86400) / 3600),
                    m = (int)((s - d * 86400 - h * 3600) / 60);
                if      (d > 0) snprintf(uptime_str, sizeof(uptime_str), "%d days, %d hrs", d, h);
                else if (h > 0) snprintf(uptime_str, sizeof(uptime_str), "%d hrs, %d min",  h, m);
                else            snprintf(uptime_str, sizeof(uptime_str), "%d min", m);
            }
            fclose(uptime_file);
        }

        char load_str[64] = "Unknown";
        FILE *loadavg = fopen("/proc/loadavg", "r");
        if (loadavg) {
            double l1, l5, l15;
            if (fscanf(loadavg, "%lf %lf %lf", &l1, &l5, &l15) == 3)
                snprintf(load_str, sizeof(load_str), "%.2f, %.2f, %.2f", l1, l5, l15);
            fclose(loadavg);
        }

        char time_str[64];
        time_t rawtime; time(&rawtime);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&rawtime));

        aroma_listview_add_item_with_icon(state.listviews[6], "Processor",        processor_name,                AROMA_ICON_MEMORY,         NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "RAM",              ram_str,                       AROMA_ICON_STORAGE,        NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Vehicle name",     "Aroma Automotive",            AROMA_ICON_DIRECTIONS_CAR, NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Software",         "AromaHMI v0.0.1 / AromaSDK",  AROMA_ICON_INFO,           NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Build date",       __DATE__ " " __TIME__,         AROMA_ICON_BUILD,          NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Security patch",   "March 1, 2026",               AROMA_ICON_SECURITY,       NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Uptime",           uptime_str,                    AROMA_ICON_ACCESS_TIME,    NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Load average",     load_str,                      AROMA_ICON_COMPUTER,       NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Current time",     time_str,                      AROMA_ICON_ACCESS_TIME,    NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Platform Backend", "GLPS (X11)",                  AROMA_ICON_VERIFIED_USER,  NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Graphics backend", "Vulkan",                      AROMA_ICON_MEMORY,         NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "CAN interface",    CAN_INTERFACE,                 AROMA_ICON_SETTINGS_INPUT_COMPONENT, NULL);
        aroma_listview_add_item_with_icon(state.listviews[6], "Voice control",
#ifdef AROMA_USE_VOICE_CONTROL
                                          "Enabled (compiled)",
#else
                                          "Disabled (stub)",
#endif
                                          AROMA_ICON_MIC, NULL);
    }
    state.listview_containers[6] = aroma_listview_get_scroll_container(state.listviews[6]);

    for (int i = 0; i < num_sections; i++)
        aroma_sidebar_set_content(state.sidebar, i, &state.listview_containers[i], 1);

    aroma_sidebar_set_selected(state.sidebar, 0);

    printf("Settings UI built — %d sections\n", num_sections);
    fflush(stdout);
}

void build_vehicle_view(AromaNode *window)
{
    state.vehicle_view_root = aroma_ui_container(
        window, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);

    AromaNode *backroad = aroma_ui_image(
        state.vehicle_view_root, "../assets/backroad_blur.png", 0, 0, WIN_W, WIN_H);
    aroma_node_set_z_index(backroad, Z_LAYER_BACKGROUND);

    AromaNode *car_img = aroma_ui_image(
        state.vehicle_view_root, "../assets/car.png", 250, 250, 700, 405);
    aroma_node_set_z_index(car_img, Z_LAYER_VEHICLE_IMAGE);

    state.overlay = aroma_ui_image(state.vehicle_view_root, NULL, 250, 250, 700, 405);
    aroma_node_set_z_index(state.overlay, Z_LAYER_VEHICLE_OVERLAYS);

    state.battery_button = aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_BATTERY_FULL,
        WIN_W - 395, 22, 40, ICON_BUTTON_OUTLINED,
        battery_diagnostics, NULL, state.icon_font);
    aroma_node_set_z_index(state.battery_button, Z_LAYER_VEHICLE_OVERLAYS);

    state.clock_font       = aroma_font_create("../assets/Ubuntu-Light.ttf", 68);
    state.clock_pm_am_font = aroma_font_create("../assets/Ubuntu-Light.ttf", 24);

    state.vehicle_view_large_clock = aroma_ui_label(
        state.vehicle_view_root, "12:45",
        WIN_W / 2 - 90, 35, LABEL_STYLE_LABEL_LARGE, state.clock_font);
    aroma_node_set_z_index(state.vehicle_view_large_clock, Z_LAYER_VEHICLE_OVERLAYS);

    state.vehicle_view_large_clock_pm_am = aroma_ui_label(
        state.vehicle_view_root, "PM",
        WIN_W / 2 + 90, 60, LABEL_STYLE_LABEL_MEDIUM, state.clock_pm_am_font);
    aroma_node_set_z_index(state.vehicle_view_large_clock_pm_am, Z_LAYER_VEHICLE_OVERLAYS);

    AromaNode *location_temp_label = aroma_ui_label(
        state.vehicle_view_root, "68°F, San Francisco",
        WIN_W / 2 - 100, 130, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(location_temp_label, Z_LAYER_VEHICLE_OVERLAYS);

   
    state.gear_bg_card = aroma_ui_card(
        state.vehicle_view_root, 25, 18, 225, 50, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.gear_bg_card, Z_LAYER_VEHICLE_OVERLAYS);

    state.gear_fg_card = aroma_ui_card(state.gear_bg_card, 25, 5, 50, 40, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.gear_fg_card, Z_LAYER_VEHICLE_OVERLAYS + 1);
    aroma_card_set_colors(state.gear_fg_card,
                          state.theme.colors.primary, state.theme.colors.primary);

    AromaNode *lbl_p = aroma_ui_label(state.gear_bg_card, "P",  22, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    AromaNode *lbl_r = aroma_ui_label(state.gear_bg_card, "R",  77, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    AromaNode *lbl_n = aroma_ui_label(state.gear_bg_card, "N", 132, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    AromaNode *lbl_d = aroma_ui_label(state.gear_bg_card, "D", 187, 8, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(lbl_p, Z_LAYER_VEHICLE_OVERLAYS + 2);
    aroma_node_set_z_index(lbl_r, Z_LAYER_VEHICLE_OVERLAYS + 2);
    aroma_node_set_z_index(lbl_n, Z_LAYER_VEHICLE_OVERLAYS + 2);
    aroma_node_set_z_index(lbl_d, Z_LAYER_VEHICLE_OVERLAYS + 2);

    state.speed_label = aroma_ui_label(
        state.vehicle_view_root, "0",
        140, 215, LABEL_STYLE_LABEL_LARGE, state.clock_font);
    aroma_node_set_z_index(state.speed_label, Z_LAYER_VEHICLE_OVERLAYS);

    AromaNode *kmh_lbl = aroma_ui_label(
        state.vehicle_view_root, "km/h",
        155, 305, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(kmh_lbl, Z_LAYER_VEHICLE_OVERLAYS);

    state.range_label = aroma_ui_label(
        state.vehicle_view_root, "Range: 0 km",
        WIN_W - 250, 100, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.range_label, Z_LAYER_VEHICLE_OVERLAYS);

    state.climate_label = aroma_ui_label(
        state.vehicle_view_root, "Climate: Off",
        WIN_W - 250, 140, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.climate_label, Z_LAYER_VEHICLE_OVERLAYS);

    state.vehicle_view_frunk_divider = aroma_ui_divider(
        state.vehicle_view_root, 400, 340, 80, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(state.vehicle_view_frunk_divider, Z_LAYER_VEHICLE_OVERLAYS);

    state.vehicle_view_lock_divider = aroma_ui_divider(
        state.vehicle_view_root, 700, 260, 80, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(state.vehicle_view_lock_divider, Z_LAYER_VEHICLE_OVERLAYS);

    state.vehicle_view_lock_icon = aroma_ui_icon(
        state.vehicle_view_root, AROMA_ICON_LOCK, 712, 220, 24,
        state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(state.vehicle_view_lock_icon, Z_LAYER_VEHICLE_OVERLAYS);

    state.vehicle_view_frunk_header = aroma_ui_label(
        state.vehicle_view_root, "Frunk", 410, 320, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_frunk_header, Z_LAYER_VEHICLE_OVERLAYS);

    state.vehicle_view_frunk_desc = aroma_ui_label(
        state.vehicle_view_root, "Open", 410, 345, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_frunk_desc, Z_LAYER_VEHICLE_OVERLAYS);

    state.vehicle_view_trunk_divider = aroma_ui_divider(
        state.vehicle_view_root, 880, 310, 80, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(state.vehicle_view_trunk_divider, Z_LAYER_VEHICLE_OVERLAYS);

    state.vehicle_view_trunk_header = aroma_ui_label(
        state.vehicle_view_root, "Trunk", 890, 290, LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_trunk_header, Z_LAYER_VEHICLE_OVERLAYS);

    state.vehicle_view_trunk_desc = aroma_ui_label(
        state.vehicle_view_root, "Closed", 890, 315, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.vehicle_view_trunk_desc, Z_LAYER_VEHICLE_OVERLAYS);

    state.vehicle_view_charge_port_divider = aroma_ui_divider(
        state.vehicle_view_root, 900, 430, 40, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_z_index(state.vehicle_view_charge_port_divider, Z_LAYER_VEHICLE_OVERLAYS);

    state.vehicle_view_charge_port_icon = aroma_ui_icon(
        state.vehicle_view_root, AROMA_ICON_POWER, 970, 415, 24,
        state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(state.vehicle_view_charge_port_icon, Z_LAYER_VEHICLE_OVERLAYS);

    state.vehicle_view_warning_message_card = aroma_ui_card(
        state.vehicle_view_root, 330, WIN_H + 100, 600, 70, CARD_TYPE_FILLED);
    state.vehicle_view_warning_warning_icon = aroma_ui_icon(
        state.vehicle_view_warning_message_card, AROMA_ICON_WARNING,
        65, 22, 24, 0xFFFFD600, state.icon_font);
    state.vehicle_view_warning_message_label = aroma_ui_label(
        state.vehicle_view_warning_message_card,
        "Warning: The Frunk is Open. Close it before driving.",
        110, 15, LABEL_STYLE_LABEL_LARGE, state.ui_font);

    aroma_node_set_z_index(state.vehicle_view_warning_message_card,  Z_LAYER_CARDS_BOTTOM + 50);
    aroma_node_set_z_index(state.vehicle_view_warning_warning_icon,  Z_LAYER_MAP_PANEL + 52);
    aroma_node_set_z_index(state.vehicle_view_warning_message_label, Z_LAYER_MAP_PANEL + 52);
    aroma_node_set_hidden(state.vehicle_view_warning_message_card, true);

    state.battery_image = aroma_ui_image(
        state.vehicle_view_root, "../assets/charging.png",
        WIN_W / 2 - 180, 200, 128, 128);
    state.battery_health = aroma_ui_label(
        state.vehicle_view_root, "Battery Health: Good",
        WIN_W / 2 - 20, 220, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.battery_health, Z_LAYER_VEHICLE_OVERLAYS);
    aroma_label_set_color(state.battery_health, 0xFF00C853);
    state.battery_percentage = aroma_ui_label(
        state.vehicle_view_root, "85%",
        WIN_W / 2 - 20, 260, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.battery_percentage, Z_LAYER_VEHICLE_OVERLAYS);
    aroma_node_set_hidden(state.battery_image,      true);
    aroma_node_set_hidden(state.battery_health,     true);
    aroma_node_set_hidden(state.battery_percentage, true);
    aroma_node_set_z_index(state.battery_image, Z_LAYER_VEHICLE_OVERLAYS);

    AromaNode *icons_col = aroma_ui_container(
        state.vehicle_view_root, 50, 100, 28, 300,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_gap(icons_col, 20);

    AromaNode *high_beams = aroma_ui_image(icons_col, "../assets/high_beams.png",      0, 0, 28, 28);
    AromaNode *low_beams  = aroma_ui_image(icons_col, "../assets/low_beams.png",       0, 0, 28, 28);
    AromaNode *abs_icon   = aroma_ui_image(icons_col, "../assets/abs_indicator.png",   0, 0, 28, 28);
    AromaNode *brake_icon = aroma_ui_image(icons_col, "../assets/brake_indicator.png", 0, 0, 28, 28);
    aroma_node_set_z_index(high_beams, Z_LAYER_VEHICLE_OVERLAYS);
    aroma_node_set_z_index(low_beams,  Z_LAYER_VEHICLE_OVERLAYS);
    aroma_node_set_z_index(abs_icon,   Z_LAYER_VEHICLE_OVERLAYS);
    aroma_node_set_z_index(brake_icon, Z_LAYER_VEHICLE_OVERLAYS);

    state.ac_card = aroma_ui_card(state.window, 30, WIN_H - 200, 220, 120, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.ac_card, Z_LAYER_MAP_PANEL + 1);

    AromaFont *ac_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 36);
    AromaNode *ac_label = aroma_ui_label(state.ac_card, "Climate", 15, 12, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(ac_label, Z_LAYER_MAP_PANEL + 2);

    state.ac_temp_label = aroma_ui_label(state.ac_card, "23°C", 75, 45, LABEL_STYLE_LABEL_LARGE, ac_font);
    aroma_node_set_z_index(state.ac_temp_label, Z_LAYER_MAP_PANEL + 2);

    AromaNode *ac_down = aroma_ui_iconbutton(state.ac_card, AROMA_ICON_REMOVE,  15, 55, 40, ICON_BUTTON_FILLED, ac_temp_down_callback, NULL, state.icon_font);
    AromaNode *ac_up   = aroma_ui_iconbutton(state.ac_card, AROMA_ICON_ADD,    165, 55, 40, ICON_BUTTON_FILLED, ac_temp_up_callback,   NULL, state.icon_font);
    aroma_node_set_z_index(ac_down, Z_LAYER_MAP_PANEL + 2);
    aroma_node_set_z_index(ac_up,   Z_LAYER_MAP_PANEL + 2);

    state.music_card = aroma_ui_card(state.window, WIN_W / 2 - 225, WIN_H - 200, 450, 120, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.music_card, Z_LAYER_MAP_PANEL + 1);

    AromaNode *music_divider = aroma_ui_divider(state.music_card, 0, 60, 450, DIVIDER_ORIENTATION_HORIZONTAL);
    aroma_node_set_z_index(music_divider, Z_LAYER_MAP_PANEL + 2);

    AromaNode *music_label = aroma_ui_label(state.music_card, "Kendrick Lamar - HUMBLE.", 20, 18, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(music_label, Z_LAYER_MAP_PANEL + 2);

    AromaNode *music_row = aroma_ui_container(
        state.music_card, 110, 70, 410, 40,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_CENTER);
    aroma_node_set_gap(music_row, 80);

    AromaNode *m_prev = aroma_ui_icon(music_row, AROMA_ICON_SKIP_PREVIOUS, 0, 0, 40, aroma_color_blend(state.theme.colors.primary, state.theme.colors.surface, 0.5), state.icon_font);
    AromaNode *m_play = aroma_ui_icon(music_row, AROMA_ICON_PLAY_ARROW,    0, 0, 40, state.theme.colors.primary, state.icon_font);
    AromaNode *m_next = aroma_ui_icon(music_row, AROMA_ICON_SKIP_NEXT,     0, 0, 40, aroma_color_blend(state.theme.colors.primary, state.theme.colors.surface, 0.5), state.icon_font);
    aroma_node_set_z_index(m_prev, Z_LAYER_MAP_PANEL + 2);
    aroma_node_set_z_index(m_play, Z_LAYER_MAP_PANEL + 2);
    aroma_node_set_z_index(m_next, Z_LAYER_MAP_PANEL + 2);

    state.nav_card = aroma_ui_card(state.window, WIN_W / 2 + 250, WIN_H - 200, 300, 120, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.nav_card, Z_LAYER_MAP_PANEL + 1);

    AromaNode *nav_divider_h = aroma_ui_divider(state.nav_card,   0, 60, 300, DIVIDER_ORIENTATION_HORIZONTAL);
    AromaNode *nav_divider_v = aroma_ui_divider(state.nav_card, 150, 60,  60, DIVIDER_ORIENTATION_VERTICAL);
    aroma_node_set_z_index(nav_divider_h, Z_LAYER_MAP_PANEL + 2);
    aroma_node_set_z_index(nav_divider_v, Z_LAYER_MAP_PANEL + 2);

    AromaNode *nav_label     = aroma_ui_label(state.nav_card, "Navigate",  20, 15, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    AromaNode *nav_map_icon  = aroma_ui_icon(state.nav_card, AROMA_ICON_MAP, 260, 20, 24, state.theme.colors.primary, state.icon_font);
    AromaNode *nav_home_label = aroma_ui_label(state.nav_card, "Home",  20, 75, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    AromaNode *nav_work_label = aroma_ui_label(state.nav_card, "Work", 170, 75, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    AromaNode *nav_home_icon  = aroma_ui_icon(state.nav_card, AROMA_ICON_HOME, 120, 80, 24, state.theme.colors.primary, state.icon_font);
    AromaNode *nav_work_icon  = aroma_ui_icon(state.nav_card, AROMA_ICON_WORK, 270, 80, 24, state.theme.colors.primary, state.icon_font);
    aroma_node_set_z_index(nav_label,      Z_LAYER_MAP_PANEL + 2);
    aroma_node_set_z_index(nav_map_icon,   Z_LAYER_MAP_PANEL + 2);
    aroma_node_set_z_index(nav_home_label, Z_LAYER_MAP_PANEL + 2);
    aroma_node_set_z_index(nav_work_label, Z_LAYER_MAP_PANEL + 2);
    aroma_node_set_z_index(nav_home_icon,  Z_LAYER_MAP_PANEL + 2);
    aroma_node_set_z_index(nav_work_icon,  Z_LAYER_MAP_PANEL + 2);

    AromaAnimation *a1 = aroma_animation_start(state.music_card, AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 2000);
    AromaAnimation *a2 = aroma_animation_start(state.nav_card,   AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 2000);
    AromaAnimation *a3 = aroma_animation_start(state.ac_card,    AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 2000);
    aroma_animation_set_easing(a1, AROMA_EASE_OUT_ELASTIC);
    aroma_animation_set_easing(a2, AROMA_EASE_OUT_ELASTIC);
    aroma_animation_set_easing(a3, AROMA_EASE_OUT_ELASTIC);

    state.vehicle_view_side_arrow_icon_button = aroma_ui_iconbutton(
        state.vehicle_view_root, AROMA_ICON_MAP,
        WIN_W - 80, WIN_H / 2 - 25, 50,
        ICON_BUTTON_FILLED, open_map_panel, NULL, state.icon_font);
    aroma_node_set_z_index(state.vehicle_view_side_arrow_icon_button, Z_LAYER_MAP_BUTTON);

    aroma_animation_start(state.vehicle_view_frunk_divider,       AROMA_ANIM_SCALE_Y, 0, 90, 1200);
    aroma_animation_start(state.vehicle_view_trunk_divider,       AROMA_ANIM_SCALE_Y, 0, 90, 1200);
    aroma_animation_start(state.vehicle_view_lock_divider,        AROMA_ANIM_SCALE_Y, 0, 90, 1200);
    aroma_animation_start(state.vehicle_view_charge_port_divider, AROMA_ANIM_SCALE_X, 0, 40, 1200);

    state.map_overlay_background = aroma_ui_container(
        window, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.map_overlay_background, Z_LAYER_MAP_PANEL - 1);
    aroma_node_set_hidden(state.map_overlay_background, true);

    state.map_panel = aroma_ui_container(
        window, MAP_PANEL_OFFSET, 0, MAP_PANEL_WIDTH, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.map_panel, Z_LAYER_MAP_PANEL);
    aroma_node_set_hidden(state.map_panel, true);
    state.map_panel_open = false;

    state.map_node = aroma_ui_map(state.map_panel, 0, 0, MAP_PANEL_WIDTH, WIN_H);
    aroma_node_set_z_index(state.map_node, Z_LAYER_MAP_PANEL);
    aroma_map_set_show_attribution(state.map_node, false);
    aroma_map_set_center(state.map_node, 48.8566, 2.3522);
    aroma_map_set_zoom(state.map_node, 12);
    aroma_map_set_route(state.map_node, 48.8566, 2.3522, 48.8049, 2.1204, 0xFF35A8FE);
    aroma_map_add_popup_marker(state.map_node, 48.8566, 2.3522, 0xFF00C853, "Start: Paris");
    aroma_map_add_popup_marker(state.map_node, 48.8049, 2.1204, 0xFFD50000, "Home: Versailles");
    aroma_map_add_icon_marker(state.map_node, 48.8606, 2.3376, 0xFFFFD600, AROMA_ICON_STAR);

    AromaNode *zoom_in  = aroma_ui_iconbutton(state.map_panel, AROMA_ICON_ADD,    MAP_PANEL_WIDTH - 70, 120, 50, ICON_BUTTON_FILLED, map_zoom_in_cb,  (void *)state.map_node, state.icon_font);
    AromaNode *zoom_out = aroma_ui_iconbutton(state.map_panel, AROMA_ICON_REMOVE, MAP_PANEL_WIDTH - 70, 180, 50, ICON_BUTTON_FILLED, map_zoom_out_cb, (void *)state.map_node, state.icon_font);
    aroma_button_set_colors(zoom_in,  state.theme.colors.primary, state.theme.colors.primary, state.theme.colors.secondary, state.theme.colors.text_primary);
    aroma_button_set_colors(zoom_out, state.theme.colors.primary, state.theme.colors.primary, state.theme.colors.secondary, state.theme.colors.text_primary);
    aroma_node_set_z_index(zoom_in,  Z_LAYER_MAP_CONTROLS);
    aroma_node_set_z_index(zoom_out, Z_LAYER_MAP_CONTROLS);

    AromaNode *recent_card = aroma_ui_card(state.map_panel, WIN_W - 390, WIN_H - 450, 300, 280, CARD_TYPE_FILLED);
    aroma_node_set_z_index(recent_card, Z_LAYER_MAP_CONTROLS);

    AromaNode *recent_label = aroma_ui_label(recent_card, "Recently Visited", 20, 20, LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(recent_label, Z_LAYER_MAP_CONTROLS + 1);

    AromaNode *recent_close = aroma_ui_iconbutton(recent_card, AROMA_ICON_CLOSE, 240, 10, 40, ICON_BUTTON_OUTLINED, toggle_recent_card_cb, (void *)recent_card, state.icon_font);
    aroma_node_set_z_index(recent_close, Z_LAYER_MAP_CONTROLS + 1);
    aroma_node_set_hidden(recent_close, true);

    state.recent_lv = aroma_ui_listview(recent_card, 0, 60, 300, 200, navigate_map, state.map_node, state.ui_font);
    aroma_listview_add_item(state.recent_lv, "Home", "123 Main St",     NULL);
    aroma_listview_add_item(state.recent_lv, "Work", "456 Business Rd", NULL);
    aroma_listview_add_item(state.recent_lv, "Gym",  "789 Fitness Ave", NULL);

    AromaNode *scroll_container = aroma_listview_get_scroll_container(state.recent_lv);
    aroma_node_set_z_index(scroll_container, Z_LAYER_MAP_CONTROLS + 1);

    AromaNode *close_map = aroma_ui_iconbutton(
        state.map_panel, AROMA_ICON_CLOSE, 20, 20, 50,
        ICON_BUTTON_FILLED, close_map_panel, NULL, state.icon_font);
    aroma_node_set_z_index(close_map, Z_LAYER_MAP_CLOSE);
}

int main(int argc, char **argv)
{
    init_log_capture();

    aroma_animation_manager_init();

    char build_info[256];
    snprintf(build_info, sizeof(build_info),
             "AromaOS v0.0.1 - Build: %s %s", __DATE__, __TIME__);
    aroma_splash(true, "AromaOS", build_info);
    aroma_ui_init();

    state.theme = aroma_theme_create_material_blue();
    state.theme.enable_shadows = false;
    state.theme.colors.background = aroma_color_blend(
        state.theme.colors.primary, state.theme.colors.background, 0.96f);
    aroma_ui_set_theme(&state.theme);

    state.ui_font   = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 24);
    state.icon_font = aroma_font_create_from_memory(icon_ttf,         icon_ttf_len,         24);

    state.window = aroma_ui_create_window("Automotive HMI", WIN_W, WIN_H);
    aroma_event_set_root((AromaNode *)state.window);
    aroma_event_subscribe(((AromaNode *)state.window)->node_id,
                          EVENT_TYPE_KEY_PRESS, global_keyboard_event_handler, NULL, 0);
    aroma_ui_prepare_font_for_window(0, state.ui_font);

    state.time_label = aroma_ui_label(
        (AromaNode *)state.window, "12:45 PM", 50, 30,
        LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(state.time_label, Z_LAYER_STATUS_BAR);

    state.location_label = aroma_ui_label(
        (AromaNode *)state.window, "San Francisco, 68°F", 150, 30,
        LABEL_STYLE_LABEL_MEDIUM, state.ui_font);
    aroma_node_set_z_index(state.location_label, Z_LAYER_STATUS_BAR);

    aroma_node_set_hidden(state.location_label, true);
    aroma_node_set_hidden(state.time_label,     true);

    state.status_card = aroma_ui_card(
        (AromaNode *)state.window, WIN_W - 235, 18, 200, 50, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.status_card, Z_LAYER_STATUS_BAR);

    state.signal_icon    = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_SIGNAL_CELLULAR_4_BAR, WIN_W - 120, 30, 24, state.theme.colors.text_primary, state.icon_font);
    state.wifi_icon      = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_WIFI,                  WIN_W -  80, 30, 24, state.theme.colors.text_primary, state.icon_font);
    state.battery_icon   = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_BATTERY_FULL,           WIN_W -  40, 30, 24, state.theme.colors.text_primary, state.icon_font);
    state.gps_icon       = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_GPS_FIXED,              WIN_W - 160, 30, 24, state.theme.colors.text_primary, state.icon_font);
    state.bluetooth_icon = aroma_ui_icon((AromaNode *)state.window, AROMA_ICON_BLUETOOTH_AUDIO,        WIN_W - 200, 30, 24, state.theme.colors.text_primary, state.icon_font);

    AromaNode *status_icons[] = {
        state.signal_icon, state.wifi_icon, state.battery_icon,
        state.gps_icon,    state.bluetooth_icon
    };
    for (int i = 0; i < 5; i++)
        aroma_node_set_z_index(status_icons[i], Z_LAYER_STATUS_BAR);

    state.voice_button = aroma_ui_iconbutton(
        (AromaNode *)state.window, AROMA_ICON_MIC,
        WIN_W - 290, 22, 40, ICON_BUTTON_FILLED,
        voice_button_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.voice_button, Z_LAYER_STATUS_BAR);

    state.settings_icon = aroma_ui_iconbutton(
        state.window, AROMA_ICON_SETTINGS,
        WIN_W - 345, 22, 40, ICON_BUTTON_OUTLINED,
        settings_button_callback, NULL, state.icon_font);
    aroma_node_set_z_index(state.settings_icon, Z_LAYER_STATUS_BAR);

    AromaNode *status_nodes[] = {
        state.time_label,    state.status_card,    state.location_label,
        state.signal_icon,   state.wifi_icon,      state.battery_icon,
        state.gps_icon,      state.bluetooth_icon, state.voice_button
    };
    int status_y[] = { 30, 18, 30, 30, 30, 30, 30, 30, 22 };
    for (int i = 0; i < 9; i++)
        aroma_animation_start(status_nodes[i], AROMA_ANIM_SLIDE_Y, -40, status_y[i], 800);

    state.voice_status_card = aroma_ui_card(
        (AromaNode *)state.window, WIN_W / 2 - 300, -100, 600, 80, CARD_TYPE_FILLED);
    aroma_node_set_z_index(state.voice_status_card, Z_LAYER_VOICE_CARD);
    state.voice_status_label = aroma_ui_label(
        state.voice_status_card, "  ", 20, 40,
        LABEL_STYLE_LABEL_LARGE, state.ui_font);
    state.loading_spinner = aroma_ui_loading(
        state.voice_status_card, 530, 28, 22, 5, state.theme.colors.primary);
    aroma_node_set_z_index(state.loading_spinner,    Z_LAYER_VOICE_CONTENT);
    aroma_node_set_z_index(state.voice_status_label, Z_LAYER_VOICE_CONTENT);
    aroma_node_set_hidden(state.loading_spinner, true);

    build_vehicle_view((AromaNode *)state.window);
    build_settings_ui((AromaNode *)state.window);
    build_easter_egg_ui((AromaNode *)state.window);

    state.tab_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 128);
    state.tabs = aroma_ui_tabs_with_icons(
        (AromaNode *)state.window, 0, WIN_H - 80, WIN_W, 80,
        (const char *[]){"Vehicle View", "Settings"},
        (const char *[]){AROMA_ICON_VISIBILITY, AROMA_ICON_SETTINGS},
        2, NULL, NULL, state.ui_font, state.tab_font);
    aroma_node_set_z_index(state.tabs, Z_LAYER_MAP_BUTTON);

    aroma_tabs_set_content(state.tabs, 0, (AromaNode **)&state.vehicle_view_root, 1);
    aroma_tabs_set_content(state.tabs, 1, &state.settings_panel_node, 1);

    start_voice_control_thread();

    pthread_t can_t;
    pthread_create(&can_t, NULL, can_thread, NULL);

    aroma_node_set_hidden(state.time_label,     true);
    aroma_node_set_hidden(state.location_label, true);
    aroma_node_set_hidden(state.tabs,           true);

    printf("AromaHMI started — %s %s\n", __DATE__, __TIME__);
    fflush(stdout);
#ifdef AROMA_USE_VOICE_CONTROL
    printf("Voice control: enabled\n");
    fflush(stdout);
#else
    printf("Voice control: disabled (stub)\n");
    fflush(stdout);
#endif

    uint64_t last_time_update = aroma_time_now_ms();
    bool     battery_shown    = false;

    while (aroma_ui_is_running())
    {
        uint64_t now = aroma_time_now_ms();

        if (now - last_time_update > 60000) {
            time_t rawtime; time(&rawtime);
            struct tm *timeinfo = localtime(&rawtime);
            char clock_str[16];
            strftime(clock_str, sizeof(clock_str), "%H:%M", timeinfo);
            if (state.vehicle_view_large_clock)
                aroma_label_set_text(state.vehicle_view_large_clock, clock_str);
            last_time_update = now;
        }

        pthread_mutex_lock(&voice_mutex);

        if (voice_nav_trigger) {
            static const struct { const char *key; double lat, lon; } cities[] = {
                {"paris",    48.8566,  2.3522},
                {"london",   51.5074, -0.1278},
                {"new york", 40.7128,-74.0060},
                {"tokyo",    35.6762,139.6503},
                {"berlin",   52.5200, 13.4050},
            };
            double lat = 37.7749, lon = -122.4194;
            for (int i = 0; i < 5; i++) {
                if (strstr(voice_nav_dest, cities[i].key)) {
                    lat = cities[i].lat;
                    lon = cities[i].lon;
                    break;
                }
            }
            if (state.map_node) {
                aroma_map_set_zoom(state.map_node, 10);
                aroma_map_pan_to(state.map_node, lat, lon);
            }
            open_map_panel(NULL);
            printf("Voice: navigate to \"%s\"\n", voice_nav_dest);
            fflush(stdout);
            voice_nav_trigger = false;
        }

        if (voice_target_tab != -1) {
            navigate_to_tab(voice_target_tab);
            voice_target_tab = -1;
        }

        if (strlen(voice_status_text) > 0) {
            set_voice_status(voice_status_text);
            voice_partial_timeout = 180;
            voice_status_text[0]  = '\0';
        } else if (voice_partial_timeout > 0) {
            if (strlen(voice_partial_text) > 0)
                set_voice_status(voice_partial_text);
            if (--voice_partial_timeout == 0) {
                set_voice_status("");
                voice_partial_text[0] = '\0';
            }
        }

        if (voice_theme_change != -1) {
            bool want_dark = (voice_theme_change == 1);
            if (state.dark_theme_enabled != want_dark) {
                state.dark_theme_enabled = want_dark;
                if (want_dark)
                    state.theme = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_BLUE);
                else {
                    state.theme = aroma_theme_create_high_contrast();
                    state.theme.colors.primary       = 0xFF2196F3;
                    state.theme.colors.primary_dark  = 0xFF1976D2;
                    state.theme.colors.primary_light = 0xFFBBDEFB;
                }
                aroma_ui_set_theme(&state.theme);
                printf("Voice: theme set to %s\n", want_dark ? "dark" : "light");
                fflush(stdout);
            }
            voice_theme_change = -1;
        }

        if (voice_ac_change != 0) {
            current_ac_temp += voice_ac_change;
            if (current_ac_temp < 16) current_ac_temp = 16;
            if (current_ac_temp > 30) current_ac_temp = 30;
            char buf[16];
            snprintf(buf, sizeof(buf), "%d°C", current_ac_temp);
            aroma_label_set_text(state.ac_temp_label, buf);
            char speak[64];
            snprintf(speak, sizeof(speak), "Setting AC to %d degrees", current_ac_temp);
            aroma_voice_speak(speak);
            printf("Voice: AC set to %d°C\n", current_ac_temp);
            fflush(stdout);
            voice_ac_change = 0;
        }

        if (voice_info_request != 0) {
            if      (voice_info_request == 1) aroma_voice_speak("Battery is at 75 percent charge.");
            else if (voice_info_request == 2) aroma_voice_speak("Estimated range is 204 kilometers.");
            else if (voice_info_request == 3) aroma_voice_speak("Battery is at 75 percent. Estimated range is 204 kilometers.");
            printf("Voice: info request %d answered\n", voice_info_request);
            fflush(stdout);
            voice_info_request = 0;
        }

        pthread_mutex_unlock(&voice_mutex);

        if (!battery_shown && aroma_time_now_ms() - last_time_update > 6000)
            battery_shown = true;

        pthread_mutex_lock(&can_mtx);
        double   spd         = vehicle_state.speed;
        int      gear_idx    = vehicle_state.gear;
        double   rng         = vehicle_state.range;
        double   soc         = vehicle_state.soc;
        double   cab_temp    = vehicle_state.cabin_temp;
        double   tgt_temp    = vehicle_state.target_temp;
        int      fan_spd     = vehicle_state.fan_speed;
        int      hvac_active = vehicle_state.hvac_on;
        uint32_t fault       = vehicle_state.fault_code;
        pthread_mutex_unlock(&can_mtx);

        if (state.speed_label) {
            char spd_str[16];
            snprintf(spd_str, sizeof(spd_str), "%.0f", spd);
            aroma_label_set_text(state.speed_label, spd_str);
        }
       
        if (state.gear_fg_card) {
            static int last_gear_idx = -1;
            if (gear_idx >= 0 && gear_idx <= 3 && gear_idx != last_gear_idx) {
                int target_x = 30 + gear_idx * 55;
                int start_x  = (last_gear_idx == -1) ? target_x : (30 + last_gear_idx * 55);
                aroma_animation_start(state.gear_fg_card, AROMA_ANIM_SLIDE_X,
                                      start_x, target_x,
                                      (last_gear_idx == -1) ? 1 : 300);
                last_gear_idx = gear_idx;
            }
            static const uint32_t gear_colors[] = {
                0xFF00C853, 0xFFD50000, 0xFFFFD600, 0xFF2196F3
            };
            if (gear_idx >= 0 && gear_idx <= 3)
                aroma_card_set_colors(state.gear_fg_card,
                                      gear_colors[gear_idx], gear_colors[gear_idx]);
        }

        if (state.range_label) {
            char rng_str[32];
            snprintf(rng_str, sizeof(rng_str), "Range: %.0f km", rng);
            aroma_label_set_text(state.range_label, rng_str);
        }

        if (state.battery_percentage) {
            char bat_str[16];
            snprintf(bat_str, sizeof(bat_str), "%.0f%%", soc);
            aroma_label_set_text(state.battery_percentage, bat_str);
        }

        if (state.ac_temp_label) {
            char ac_str[32];
            if (hvac_active) snprintf(ac_str, sizeof(ac_str), "%.1f°C (Fan %d)", tgt_temp, fan_spd);
            else             strcpy(ac_str, "Off");
            aroma_label_set_text(state.ac_temp_label, ac_str);
        }

        if (state.climate_label) {
            char clim_str[64];
            if (hvac_active)
                snprintf(clim_str, sizeof(clim_str), "Inside: %.1f°C | AC: %.1f°C (Auto)", cab_temp, tgt_temp);
            else
                snprintf(clim_str, sizeof(clim_str), "Inside: %.1f°C | AC Off", cab_temp);
            aroma_label_set_text(state.climate_label, clim_str);
        }

        if (state.location_label) {
            char loc_str[64];
            snprintf(loc_str, sizeof(loc_str), "%.1f°C, San Francisco", cab_temp);
            aroma_label_set_text(state.location_label, loc_str);
        }

        if (state.vehicle_view_warning_message_card) {
            static uint32_t last_fault = 0;
            if (fault != 0) {
                const char *fault_msg = "Unknown Error";
                switch (fault) {
                    case 0xB101: fault_msg = "BMS: Cell Overvoltage";        break;
                    case 0xB102: fault_msg = "BMS: Cell Undervoltage";       break;
                    case 0xB100: fault_msg = "BMS: Isolation Fault";         break;
                    case 0xC201: fault_msg = "Motor: Inverter Overtemp";     break;
                    case 0xC200: fault_msg = "Motor: Drive Inverter Fault";  break;
                    case 0xA100: fault_msg = "Battery: Critical Low";        break;
                    case 0xD300: fault_msg = "HVAC: Compressor Fault";       break;
                    case 0xD301: fault_msg = "HVAC: Coolant Pump Fault";     break;
                    case 0xE400: fault_msg = "Autopilot: Camera Blinded";    break;
                    case 0xE401: fault_msg = "Autopilot: Radar Fault";       break;
                }
                char flt_str[128];
                snprintf(flt_str, sizeof(flt_str), "FAULT 0x%04X: %s", fault, fault_msg);
                aroma_label_set_text(state.vehicle_view_warning_message_label, flt_str);

                if (last_fault == 0) {
                    aroma_node_set_hidden(state.vehicle_view_warning_message_card, false);
                    aroma_animation_start(state.vehicle_view_warning_message_card,
                                         AROMA_ANIM_SLIDE_Y, WIN_H + 100, WIN_H - 120, 400);
                    if (state.ac_card)    aroma_animation_start(state.ac_card,    AROMA_ANIM_SLIDE_Y, WIN_H - 200, WIN_H + 120, 400);
                    if (state.music_card) aroma_animation_start(state.music_card, AROMA_ANIM_SLIDE_Y, WIN_H - 200, WIN_H + 120, 400);
                    if (state.nav_card)   aroma_animation_start(state.nav_card,   AROMA_ANIM_SLIDE_Y, WIN_H - 200, WIN_H + 120, 400);
                    printf("FAULT active: 0x%04X – %s\n", fault, fault_msg);
                    fflush(stdout);
                }
            } else {
                if (last_fault != 0) {
                    aroma_animation_start(state.vehicle_view_warning_message_card,
                                         AROMA_ANIM_SLIDE_Y, WIN_H - 120, WIN_H + 100, 400);
                    if (state.ac_card)    aroma_animation_start(state.ac_card,    AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 400);
                    if (state.music_card) aroma_animation_start(state.music_card, AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 400);
                    if (state.nav_card)   aroma_animation_start(state.nav_card,   AROMA_ANIM_SLIDE_Y, WIN_H + 120, WIN_H - 200, 400);
                    printf("FAULT cleared (was 0x%04X)\n", last_fault);
                    fflush(stdout);
                }
            }
            last_fault = fault;
        }

        aroma_ui_process_events();
        aroma_ui_render(state.window);
        usleep(16000);
    }

    cleanup_log_capture();

    aroma_ui_destroy_window(state.window);
    aroma_ui_unload_font(state.ui_font);
    aroma_ui_unload_font(state.icon_font);
    aroma_ui_unload_font(state.tab_font);
    aroma_ui_unload_font(state.clock_font);
    aroma_ui_unload_font(state.clock_pm_am_font);
    aroma_ui_unload_font(state.settings_font);
    aroma_ui_unload_font(state.log_font);
    aroma_ui_shutdown();
    return 0;
}