#define AROMA_USE_EGL_SURFACELESS // Enable surfaceless mode for GLFW backend if supported

#include <aroma.h>
#include <aroma_animation.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define SHM_NAME "/aroma_frame_shm"
#define WIDTH 1280
#define HEIGHT 720
#define BPP 4

#define Z_BG 0
#define Z_ROAD 1
#define Z_LANE 2
#define Z_CAR 3
#define Z_SPEED 15
#define Z_SPEED_UNIT 16
#define Z_GEAR 17
#define Z_MODE 18
#define Z_TIME 20
#define Z_TEMP 21
#define Z_ODO 22
#define Z_TRIP 23
#define Z_BATTERY_BG 25
#define Z_BATTERY_FILL 26
#define Z_BATTERY_TEXT 27
#define Z_RANGE 28
#define Z_POWER_BG 30
#define Z_POWER_FILL 31
#define Z_STATUS_BG 35
#define Z_STATUS_ICONS 36
#define Z_TIRE 37
#define Z_NAV 40
#define Z_NOTIFICATION 45

typedef struct {
    int mouse_x;
    int mouse_y;
    int click;
} SharedEvents;

typedef struct {
    AromaFont *speed_font;
    AromaFont *info_font;
    AromaFont *small_font;
    AromaFont *icon_font;
    AromaFont *tiny_font;

    AromaWindow *window;

    AromaNode *speed_label;
    AromaNode *speed_unit;
    AromaNode *battery_fill;
    AromaNode *battery_pct;
    AromaNode *range_label;
    AromaNode *power_fill;
    AromaNode *gear_label;
    AromaNode *mode_label;
    AromaNode *time_label;
    AromaNode *temp_label;
    AromaNode *odo_label;
    AromaNode *trip_label;
    AromaNode *nav_text;
    AromaNode *nav_dist;
    AromaNode *nav_arrow;
    AromaNode *status_drl;
    AromaNode *status_dipped;
    AromaNode *status_fog;
    AromaNode *tire_pressure;
    AromaNode *notif_panel;
    AromaNode *notif_text;

    int speed;
    int power_kw;
    int battery_percent;
    int range_km;
    int gear;
    int drive_mode;
    float outside_temp;
    int odo;
    float trip;
    bool drl_on;
    bool dipped_on;
    bool fog_on;
    int tire_fl;
    int tire_fr;
    int tire_rl;
    int tire_rr;
    bool nav_active;
    char nav_instruction[64];
    char nav_distance[32];
    bool notif_active;
    char notif_text_buf[128];
} TeslaCluster;

static TeslaCluster tc = {0};

void build_tesla_cluster(AromaNode* window) {
    AromaNode* bg = aroma_ui_card(window, 0, 0, WIDTH, HEIGHT, CARD_TYPE_FILLED);
    aroma_card_set_colors(bg, 0xFF000000, 0xFF000000);
    aroma_node_set_z_index(bg, Z_BG);

    AromaNode* road = aroma_ui_card(window, 0, HEIGHT - 260, WIDTH, 260, CARD_TYPE_FILLED);
    aroma_card_set_colors(road, 0xFF030303, 0xFF030303);
    aroma_node_set_z_index(road, Z_ROAD);

    AromaNode* road_edge = aroma_ui_card(window, 0, HEIGHT - 260, WIDTH, 1, CARD_TYPE_FILLED);
    aroma_card_set_colors(road_edge, 0xFF111111, 0xFF111111);
    aroma_node_set_z_index(road_edge, Z_LANE);

    AromaNode* lane_l = aroma_ui_card(window, WIDTH/2 - 250, HEIGHT - 250, 2, 240, CARD_TYPE_FILLED);
    aroma_card_set_colors(lane_l, 0xFF111111, 0xFF111111);
    aroma_node_set_z_index(lane_l, Z_LANE);

    AromaNode* lane_r = aroma_ui_card(window, WIDTH/2 + 248, HEIGHT - 250, 2, 240, CARD_TYPE_FILLED);
    aroma_card_set_colors(lane_r, 0xFF111111, 0xFF111111);
    aroma_node_set_z_index(lane_r, Z_LANE);

    for (int i = 0; i < 5; i++) {
        AromaNode* dash = aroma_ui_card(window, WIDTH/2 - 1, HEIGHT - 240 + (i * 50), 2, 25, CARD_TYPE_FILLED);
        aroma_card_set_colors(dash, 0xFF0A0A0A, 0xFF0A0A0A);
        aroma_node_set_z_index(dash, Z_LANE);
    }

    AromaNode* car_body = aroma_ui_card(window, WIDTH/2 - 55, HEIGHT - 180, 110, 55, CARD_TYPE_FILLED);
    aroma_card_set_colors(car_body, 0xFF080808, 0xFF080808);
    aroma_node_set_z_index(car_body, Z_CAR);

    AromaNode* car_roof = aroma_ui_card(window, WIDTH/2 - 25, HEIGHT - 188, 50, 18, CARD_TYPE_FILLED);
    aroma_card_set_colors(car_roof, 0xFF0F0F0F, 0xFF0F0F0F);
    aroma_node_set_z_index(car_roof, Z_CAR + 1);

    AromaNode* car_glass = aroma_ui_card(window, WIDTH/2 - 20, HEIGHT - 185, 40, 12, CARD_TYPE_FILLED);
    aroma_card_set_colors(car_glass, 0xFF060606, 0xFF060606);
    aroma_node_set_z_index(car_glass, Z_CAR + 2);

    tc.speed_label = aroma_ui_label(window, "0", WIDTH/2 - 110, HEIGHT/2 - 180,
                                    LABEL_STYLE_LABEL_LARGE, tc.speed_font);
    aroma_node_set_z_index(tc.speed_label, Z_SPEED);
    aroma_label_set_color(tc.speed_label, 0xFFFFFFFF);

    tc.speed_unit = aroma_ui_label(window, "km/h", WIDTH/2 + 40, HEIGHT/2 - 80,
                                   LABEL_STYLE_LABEL_MEDIUM, tc.info_font);
    aroma_node_set_z_index(tc.speed_unit, Z_SPEED_UNIT);
    aroma_label_set_color(tc.speed_unit, 0xFF888888);

    tc.gear_label = aroma_ui_label(window, "P", WIDTH/2 + 40, HEIGHT/2 - 30,
                                   LABEL_STYLE_LABEL_LARGE, tc.info_font);
    aroma_node_set_z_index(tc.gear_label, Z_GEAR);
    aroma_label_set_color(tc.gear_label, 0xFFFFFFFF);

    tc.mode_label = aroma_ui_label(window, "STANDARD", WIDTH/2 + 40, HEIGHT/2 + 15,
                                   LABEL_STYLE_LABEL_SMALL, tc.small_font);
    aroma_node_set_z_index(tc.mode_label, Z_MODE);
    aroma_label_set_color(tc.mode_label, 0xFF666666);

    tc.time_label = aroma_ui_label(window, "12:45", 50, 35,
                                   LABEL_STYLE_LABEL_MEDIUM, tc.small_font);
    aroma_node_set_z_index(tc.time_label, Z_TIME);
    aroma_label_set_color(tc.time_label, 0xFFFFFFFF);

    tc.temp_label = aroma_ui_label(window, "22°C", 50, 70,
                                   LABEL_STYLE_LABEL_MEDIUM, tc.small_font);
    aroma_node_set_z_index(tc.temp_label, Z_TEMP);
    aroma_label_set_color(tc.temp_label, 0xFF888888);

    AromaNode* batt_bg = aroma_ui_card(window, 50, HEIGHT - 55, 200, 3, CARD_TYPE_FILLED);
    aroma_card_set_colors(batt_bg, 0xFF151515, 0xFF151515);
    aroma_node_set_z_index(batt_bg, Z_BATTERY_BG);

    tc.battery_fill = aroma_ui_card(window, 50, HEIGHT - 55, 170, 3, CARD_TYPE_FILLED);
    aroma_card_set_colors(tc.battery_fill, 0xFFFFFFFF, 0xFFFFFFFF);
    aroma_node_set_z_index(tc.battery_fill, Z_BATTERY_FILL);

    tc.battery_pct = aroma_ui_label(window, "85%", 260, HEIGHT - 63,
                                    LABEL_STYLE_LABEL_SMALL, tc.small_font);
    aroma_node_set_z_index(tc.battery_pct, Z_BATTERY_TEXT);
    aroma_label_set_color(tc.battery_pct, 0xFFFFFFFF);

    tc.range_label = aroma_ui_label(window, "420 km", 340, HEIGHT - 63,
                                    LABEL_STYLE_LABEL_SMALL, tc.small_font);
    aroma_node_set_z_index(tc.range_label, Z_RANGE);
    aroma_label_set_color(tc.range_label, 0xFF888888);

    AromaNode* pow_bg = aroma_ui_card(window, WIDTH - 250, HEIGHT - 55, 200, 3, CARD_TYPE_FILLED);
    aroma_card_set_colors(pow_bg, 0xFF151515, 0xFF151515);
    aroma_node_set_z_index(pow_bg, Z_POWER_BG);

    tc.power_fill = aroma_ui_card(window, WIDTH - 250, HEIGHT - 55, 100, 3, CARD_TYPE_FILLED);
    aroma_card_set_colors(tc.power_fill, 0xFFFFFFFF, 0xFFFFFFFF);
    aroma_node_set_z_index(tc.power_fill, Z_POWER_FILL);

    tc.odo_label = aroma_ui_label(window, "12,450 km", WIDTH - 170, 35,
                                  LABEL_STYLE_LABEL_MEDIUM, tc.small_font);
    aroma_node_set_z_index(tc.odo_label, Z_ODO);
    aroma_label_set_color(tc.odo_label, 0xFF888888);

    tc.trip_label = aroma_ui_label(window, "Trip 45.2 km", WIDTH - 170, 70,
                                   LABEL_STYLE_LABEL_SMALL, tc.small_font);
    aroma_node_set_z_index(tc.trip_label, Z_TRIP);
    aroma_label_set_color(tc.trip_label, 0xFF666666);

    AromaNode* status_bg = aroma_ui_card(window, WIDTH - 450, 100, 420, 30, CARD_TYPE_FILLED);
    aroma_card_set_colors(status_bg, 0xFF050505, 0xFF050505);
    aroma_node_set_z_index(status_bg, Z_STATUS_BG);

    tc.status_drl = aroma_ui_icon(window, AROMA_ICON_WB_INCANDESCENT,
                                  WIDTH - 430, 104, 18, 0xFF222222, tc.icon_font);
    aroma_node_set_z_index(tc.status_drl, Z_STATUS_ICONS);

    tc.status_dipped = aroma_ui_icon(window, AROMA_ICON_WB_INCANDESCENT,
                                     WIDTH - 390, 104, 18, 0xFF222222, tc.icon_font);
    aroma_node_set_z_index(tc.status_dipped, Z_STATUS_ICONS);

    tc.status_fog = aroma_ui_icon(window, AROMA_ICON_CLOUD,
                                  WIDTH - 350, 104, 18, 0xFF222222, tc.icon_font);
    aroma_node_set_z_index(tc.status_fog, Z_STATUS_ICONS);

    AromaNode* tire_label = aroma_ui_label(window, "TIRE", WIDTH - 310, 104,
                                           LABEL_STYLE_LABEL_SMALL, tc.tiny_font);
    aroma_node_set_z_index(tire_label, Z_TIRE);
    aroma_label_set_color(tire_label, 0xFF444444);

    tc.tire_pressure = aroma_ui_label(window, "42 42 42 42 psi", WIDTH - 270, 104,
                                      LABEL_STYLE_LABEL_SMALL, tc.tiny_font);
    aroma_node_set_z_index(tc.tire_pressure, Z_TIRE);
    aroma_label_set_color(tc.tire_pressure, 0xFF666666);

    tc.nav_arrow = aroma_ui_icon(window, AROMA_ICON_NAVIGATION,
                                 50, HEIGHT - 135, 30, 0xFFFFFFFF, tc.icon_font);
    aroma_node_set_z_index(tc.nav_arrow, Z_NAV);
    aroma_node_set_hidden(tc.nav_arrow, true);

    tc.nav_text = aroma_ui_label(window, "", 95, HEIGHT - 130,
                                 LABEL_STYLE_LABEL_LARGE, tc.info_font);
    aroma_node_set_z_index(tc.nav_text, Z_NAV);
    aroma_label_set_color(tc.nav_text, 0xFFFFFFFF);
    aroma_node_set_hidden(tc.nav_text, true);

    tc.nav_dist = aroma_ui_label(window, "", 95, HEIGHT - 160,
                                 LABEL_STYLE_LABEL_MEDIUM, tc.small_font);
    aroma_node_set_z_index(tc.nav_dist, Z_NAV);
    aroma_label_set_color(tc.nav_dist, 0xFF888888);
    aroma_node_set_hidden(tc.nav_dist, true);

    tc.notif_panel = aroma_ui_card(window, WIDTH/2 - 280, 10, 560, 45, CARD_TYPE_FILLED);
    aroma_card_set_colors(tc.notif_panel, 0xFF0A0A0A, 0xFF0A0A0A);
    aroma_node_set_z_index(tc.notif_panel, Z_NOTIFICATION);
    aroma_node_set_hidden(tc.notif_panel, true);

    tc.notif_text = aroma_ui_label(window, "", WIDTH/2 - 260, 20,
                                   LABEL_STYLE_LABEL_MEDIUM, tc.small_font);
    aroma_node_set_z_index(tc.notif_text, Z_NOTIFICATION + 1);
    aroma_label_set_color(tc.notif_text, 0xFFFFFFFF);
}

void update_cluster() {
    char buf[64];

    snprintf(buf, sizeof(buf), "%d", tc.speed);
    aroma_label_set_text(tc.speed_label, buf);

    const char* gears[] = {"P", "R", "N", "D"};
    aroma_label_set_text(tc.gear_label, gears[tc.gear]);

    const char* modes[] = {"CHILL", "STANDARD", "SPORT"};
    aroma_label_set_text(tc.mode_label, modes[tc.drive_mode]);
    
    uint32_t mode_colors[] = {0xFF888888, 0xFFFFFFFF, 0xFFFF5555};
    aroma_label_set_color(tc.mode_label, mode_colors[tc.drive_mode]);
    aroma_label_set_color(tc.gear_label, mode_colors[tc.drive_mode]);

    snprintf(buf, sizeof(buf), "%d%%", tc.battery_percent);
    aroma_label_set_text(tc.battery_pct, buf);

    int batt_width = (int)(200.0f * tc.battery_percent / 100.0f);
    if (batt_width < 2) batt_width = 2;

    uint32_t batt_color;
    if (tc.battery_percent > 50) batt_color = 0xFFFFFFFF;
    else if (tc.battery_percent > 20) batt_color = 0xFFFFAA00;
    else batt_color = 0xFFFF3333;

    aroma_card_set_colors(tc.battery_fill, batt_color, batt_color);
    aroma_label_set_color(tc.battery_pct, batt_color);

    snprintf(buf, sizeof(buf), "%d km", tc.range_km);
    aroma_label_set_text(tc.range_label, buf);

    int power_width = (int)(200.0f * fminf(fabsf((float)tc.power_kw) / 300.0f, 1.0f));
    if (power_width < 2) power_width = 2;

    uint32_t power_color;
    if (tc.power_kw >= 0) {
        power_color = (tc.power_kw > 100) ? 0xFFFF5555 : 0xFFFFFFFF;
    } else {
        power_color = 0xFF44AA44;
    }

    aroma_card_set_colors(tc.power_fill, power_color, power_color);

    time_t rawtime = time(NULL);
    struct tm *ti = localtime(&rawtime);
    strftime(buf, sizeof(buf), "%H:%M", ti);
    aroma_label_set_text(tc.time_label, buf);

    snprintf(buf, sizeof(buf), "%.0f°C", tc.outside_temp);
    aroma_label_set_text(tc.temp_label, buf);

    snprintf(buf, sizeof(buf), "%d km", tc.odo);
    aroma_label_set_text(tc.odo_label, buf);

    snprintf(buf, sizeof(buf), "Trip %.1f km", tc.trip);
    aroma_label_set_text(tc.trip_label, buf);

    aroma_icon_set_color(tc.status_drl, tc.drl_on ? 0xFF44FF44 : 0xFF222222);
    aroma_icon_set_color(tc.status_dipped, tc.dipped_on ? 0xFF44CCFF : 0xFF222222);
    aroma_icon_set_color(tc.status_fog, tc.fog_on ? 0xFFFFAA44 : 0xFF222222);

    snprintf(buf, sizeof(buf), "%d %d %d %d psi", tc.tire_fl, tc.tire_fr, tc.tire_rl, tc.tire_rr);
    aroma_label_set_text(tc.tire_pressure, buf);

    if (tc.nav_active) {
        aroma_node_set_hidden(tc.nav_arrow, false);
        aroma_node_set_hidden(tc.nav_text, false);
        aroma_node_set_hidden(tc.nav_dist, false);
        aroma_label_set_text(tc.nav_text, tc.nav_instruction);
        aroma_label_set_text(tc.nav_dist, tc.nav_distance);
    } else {
        aroma_node_set_hidden(tc.nav_arrow, true);
        aroma_node_set_hidden(tc.nav_text, true);
        aroma_node_set_hidden(tc.nav_dist, true);
    }

    if (tc.notif_active) {
        aroma_node_set_hidden(tc.notif_panel, false);
        aroma_label_set_text(tc.notif_text, tc.notif_text_buf);
    } else {
        aroma_node_set_hidden(tc.notif_panel, true);
    }
}

int main() {
   
       aroma_ui_set_offscreen_mode(true);
    aroma_ui_set_use_surfaceless(true);
    aroma_ui_init();
    aroma_animation_manager_init();


    tc.speed_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 140);
    tc.info_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 32);
    tc.small_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 18);
    tc.icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 18);
    tc.tiny_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 12);

    tc.window = aroma_ui_create_window("Tesla Cluster", WIDTH, HEIGHT);
    if (!tc.window) return 1;

    AromaNode* root = (AromaNode*)tc.window;
    aroma_event_set_root(root);
    build_tesla_cluster(root);

    tc.speed = 0;
    tc.power_kw = 0;
    tc.battery_percent = 85;
    tc.range_km = 420;
    tc.gear = 0;
    tc.drive_mode = 1;
    tc.outside_temp = 22.0f;
    tc.odo = 12450;
    tc.trip = 45.2f;
    tc.drl_on = true;
    tc.dipped_on = false;
    tc.fog_on = false;
    tc.tire_fl = 42;
    tc.tire_fr = 42;
    tc.tire_rl = 42;
    tc.tire_rr = 42;
    tc.nav_active = false;
    tc.notif_active = false;

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, WIDTH * HEIGHT * BPP);
    void* shm_pixels = mmap(0, WIDTH * HEIGHT * BPP, PROT_WRITE, MAP_SHARED, shm_fd, 0);

    int ev_shm_fd = shm_open("/aroma_events_shm", O_CREAT | O_RDWR, 0666);
    ftruncate(ev_shm_fd, sizeof(SharedEvents));
    SharedEvents* shm_events = mmap(0, sizeof(SharedEvents), PROT_READ | PROT_WRITE, MAP_SHARED, ev_shm_fd, 0);

    int last_x = -1, last_y = -1;

    while (aroma_ui_is_running()) {

        int current_x = shm_events->mouse_x;
        int current_y = shm_events->mouse_y;
        int current_click = shm_events->click;

        if (current_click || current_x != last_x || current_y != last_y) {
            AromaNode *target = aroma_event_hit_test(root, current_x, current_y);
            if (target) {
                AromaEvent *ev = NULL;
                if (current_click == 1) {
                    ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_CLICK, target->node_id, current_x, current_y, 0);
                } else if (current_click == 2) {
                    ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_RELEASE, target->node_id, current_x, current_y, 0);
                } else {
                    aroma_event_handle_pointer_move(current_x, current_y, false);
                    ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_MOVE, target->node_id, current_x, current_y, 0);
                }
                if (ev) aroma_event_queue(ev);
            }
            last_x = current_x;
            last_y = current_y;
            shm_events->click = 0;
        }


        aroma_ui_process_events();
        aroma_ui_render(tc.window);
        aroma_ui_read_pixels(tc.window, shm_pixels, WIDTH, HEIGHT);
        usleep(25000);
    }

    munmap(shm_pixels, WIDTH * HEIGHT * BPP);
    close(shm_fd);
    shm_unlink(SHM_NAME);
    munmap(shm_events, sizeof(SharedEvents));
    close(ev_shm_fd);
    shm_unlink("/aroma_events_shm");

    aroma_ui_destroy_window(tc.window);
    aroma_ui_shutdown();
    return 0;
}