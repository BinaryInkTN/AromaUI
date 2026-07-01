#include <unistd.h>

#include <aroma.h>
#include "aroma_incense_loader.h"

#define UI_FILE "../ui.aroma"
#define FRAME_SLEEP_US 16667

#define FONT_SIZE_BODY 18
#define FONT_SIZE_HERO 80

static void set_gear_park(void *userdata) {
    (void)userdata;
    IncenseStateSetString("current_gear", "P");
    IncenseStateSetInt("gear_x_position", 3);
    IncenseStateSetInt("vehicle_speed", 0);
}

static void set_gear_reverse(void *userdata) {
    (void)userdata;
    IncenseStateSetString("current_gear", "R");
    IncenseStateSetInt("gear_x_position", 63);
    IncenseStateSetInt("vehicle_speed", 0);
}

static void set_gear_neutral(void *userdata) {
    (void)userdata;
    IncenseStateSetString("current_gear", "N");
    IncenseStateSetInt("gear_x_position", 123);
    IncenseStateSetInt("vehicle_speed", 0);
}

static void set_gear_drive(void *userdata) {
    (void)userdata;
    IncenseStateSetString("current_gear", "D");
    IncenseStateSetInt("gear_x_position", 183);
}

int main(void)
{
    set_minimum_log_level(DEBUG_LEVEL_WARNING);

    aroma_ui_init();

    AromaFont *font = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                                    aroma_ubuntu_ttf_len,
                                                    FONT_SIZE_BODY);
    AromaFont *icon_font = aroma_font_create_from_memory(icon_ttf,
                                                         icon_ttf_len,
                                                         FONT_SIZE_BODY);
    AromaFont *big_font = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                                        aroma_ubuntu_ttf_len,
                                                        FONT_SIZE_HERO);
    AromaFont *medium_font = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                                        aroma_ubuntu_ttf_len,
                                                        50);
    if (!font || !icon_font || !big_font || !medium_font)
    {
        aroma_ui_shutdown();
        return 1;
    }

    IncenseRegisterFont("big_font", big_font);
    IncenseRegisterFont("medium_font", medium_font);

    IncenseRegisterCallback("set_gear_park", INCENSE_CALLBACK_VOID_PTR, set_gear_park, NULL);
    IncenseRegisterCallback("set_gear_reverse", INCENSE_CALLBACK_VOID_PTR, set_gear_reverse, NULL);
    IncenseRegisterCallback("set_gear_neutral", INCENSE_CALLBACK_VOID_PTR, set_gear_neutral, NULL);
    IncenseRegisterCallback("set_gear_drive", INCENSE_CALLBACK_VOID_PTR, set_gear_drive, NULL);

    IncenseStateSetString("current_time", "14:23");
    IncenseStateSetString("outside_temp", "18°C");
    IncenseStateSetString("greeting", "Welcome back, Yassine!");
    IncenseStateSetInt("vehicle_speed", 82);
    IncenseStateSetFloat("battery_percent", 76.0f);
    IncenseStateSetString("range_remaining", "412km");
    IncenseStateSetString("charging_stations_nearby", "8 stations nearby");
    IncenseStateSetString("current_gear", "P");
    IncenseStateSetInt("gear_x_position", 43);
    IncenseStateSetBool("eco_mode", true);

    AromaTheme theme = aroma_theme_create_material_black();
    theme.colors.primary = 0xFF2196F3;
    aroma_ui_set_theme(&theme);
    IncenseRegistry *registry = NULL;
    int watcher = IncenseHotReloadStart(UI_FILE, font, icon_font, &registry);

    if (watcher < 0)
    {
        aroma_font_destroy(font);
        aroma_font_destroy(icon_font);
        aroma_font_destroy(big_font);
        aroma_ui_shutdown();
        return 1;
    }

    IncenseHotReloadSetCallback(watcher, NULL);

    AromaWindow *window = IncenseHotReloadGetWindow(watcher);

    if (!window)
    {
        aroma_font_destroy(font);
        aroma_font_destroy(icon_font);
        aroma_font_destroy(big_font);
        aroma_ui_shutdown();
        return 1;
    }

    aroma_event_set_root((AromaNode *)window);

    while (aroma_ui_is_running())
    {
        int reloaded = IncenseHotReloadCheck();

        if (reloaded > 0)
        {
            AromaWindow *new_window = IncenseHotReloadGetWindow(watcher);
            if (new_window && new_window != window)
            {
                window = new_window;
                aroma_event_set_root((AromaNode *)window);
            }
        }

        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(FRAME_SLEEP_US);
    }

    IncenseHotReloadStopAll();
    aroma_ui_destroy_window(window);

    aroma_font_destroy(big_font);
    aroma_font_destroy(icon_font);
    aroma_font_destroy(font);
    aroma_font_destroy(medium_font);

    aroma_ui_shutdown();
    return 0;
}