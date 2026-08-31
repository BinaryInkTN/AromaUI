#include "lock_screen.h"
#include "vehicle_view.h"
#include "app_state.h"
#include <time.h>
#include <unistd.h>

static bool lock_screen_active = true;
static AromaNode *lock_screen_root = NULL;
static AromaNode *lock_screen_time = NULL;
static AromaNode *lock_screen_date = NULL;
static AromaNode *lock_screen_unlock_btn = NULL;

void unlock_screen(void)
{
    if (!lock_screen_active)
        return;
    lock_screen_active = false;
    if (lock_screen_root)
    {
        aroma_node_set_hidden(lock_screen_root, true);
    }
    if (state.vehicle_view_root)
    {
        aroma_node_set_hidden(state.vehicle_view_root, false);
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

void build_lock_screen(AromaNode *window)
{
    lock_screen_root = aroma_ui_container(
        window, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_z_index(lock_screen_root, 9990);

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

bool lock_screen_is_active(void)
{
    return lock_screen_active;
}
