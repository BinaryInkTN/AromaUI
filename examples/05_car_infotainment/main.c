#include <aroma.h>

#include <unistd.h>
#include <stdio.h>

#define WIN_W 1280
#define WIN_H 800
AromaFont *icon_font = NULL;

static AromaFont *ui_font = NULL;
static AromaContainer *general_root = NULL;
static AromaTheme theme;

int main(void)
{
    aroma_ui_init();
    aroma_splash(false);
    theme =
        aroma_theme_create_material_preset_dark(
            AROMA_THEME_MATERIAL_BLUE);
    aroma_ui_set_theme(&theme);

    ui_font =
        aroma_font_create_from_memory(
            aroma_ubuntu_ttf,
            aroma_ubuntu_ttf_len,
            16);
    icon_font =
        aroma_font_create_from_memory(
            icon_ttf,
            icon_ttf_len,
            24);
    AromaWindow *window =
        aroma_ui_create_window(
            "Automotive HMI",
            WIN_W, WIN_H);

    aroma_event_set_root((AromaNode *)window);
    aroma_ui_prepare_font_for_window(0, ui_font);

    aroma_ui_label(
        (AromaNode *)window,
        "12:45 PM",
        50, 30,
        LABEL_STYLE_LABEL_LARGE, ui_font);

    aroma_ui_label(
        (AromaNode *)window,
        "San Francisco, 68°F",
        150, 30,
        LABEL_STYLE_LABEL_MEDIUM, ui_font);

    aroma_ui_card((AromaNode *)window, WIN_W - 235, 18, 200, 50, CARD_TYPE_FILLED);
    aroma_ui_icon((AromaNode *)window, AROMA_ICON_SIGNAL_CELLULAR_4_BAR, WIN_W - 120, 30, 24, theme.colors.primary, icon_font);
    aroma_ui_icon((AromaNode *)window, AROMA_ICON_WIFI, WIN_W - 80, 30, 24, theme.colors.primary, icon_font);
    aroma_ui_icon((AromaNode *)window, AROMA_ICON_BATTERY_FULL, WIN_W - 40, 30, 24, theme.colors.primary, icon_font);
    aroma_ui_icon((AromaNode *)window, AROMA_ICON_GPS_FIXED, WIN_W - 160, 30, 24, theme.colors.primary, icon_font);
    aroma_ui_icon((AromaNode *)window, AROMA_ICON_BLUETOOTH_AUDIO, WIN_W - 200, 30, 24, theme.colors.primary, icon_font);

    general_root = aroma_ui_container((AromaNode *)window, 125, 90, WIN_W - 250, WIN_H - 210, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_gap((AromaNode *)general_root, 20);
    
    build_general_ui(general_root);
    AromaFont *tab_font = aroma_font_create_from_memory(
        icon_ttf, icon_ttf_len, 128);
    AromaNode* tabs = aroma_ui_tabs_with_icons((AromaNode *)window, 0, WIN_H - 80, WIN_W, 80, 
                                                (const char*[]){"Main Screen", "Navigation", "Phone", "Settings"},
                                                (const char*[]){AROMA_ICON_DASHBOARD, AROMA_ICON_MAP, AROMA_ICON_PHONE, AROMA_ICON_SETTINGS},
                                                4, NULL, NULL, ui_font, tab_font);


    aroma_tabs_set_content(tabs, 0, (AromaNode**)&general_root, 1);
    aroma_ui_request_redraw(NULL);

    while (aroma_ui_is_running())
    {

        
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(16000);
    }

    aroma_ui_destroy_window(window);
    aroma_ui_unload_font(ui_font);
    aroma_ui_shutdown();
    return 0;
}

void build_general_ui(AromaContainer *root)
{
    AromaNode *general_info = aroma_ui_card((AromaNode *)root, 0, 0, 400, 100, CARD_TYPE_FILLED);
    aroma_node_set_flex_grow(general_info, 1);
    
    AromaNode *applets_container = aroma_ui_container((AromaNode *)root, 0, 120, 610, 300, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    AromaNode *ac_applet = aroma_ui_card((AromaNode *)applets_container, 0, 0, 400, 300, CARD_TYPE_ELEVATED);
    aroma_node_set_gap((AromaNode *)applets_container, 20);
    AromaNode *applets_row = aroma_ui_container((AromaNode *)applets_container, 0, 130, 610, 270, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    AromaNode *applet1 = aroma_ui_card((AromaNode *)applets_row, 0, 0, 280, 70, CARD_TYPE_ELEVATED);
    AromaNode *applet2 = aroma_ui_image((AromaNode *)applets_row, "../test.png", 0, 0, 280, 70);
    aroma_node_set_gap((AromaNode *)applets_row, 20);
    aroma_node_set_flex_grow(applet1, 1);
    aroma_node_set_flex_grow(applet2, 1);

    aroma_ui_image(
        (AromaNode *)general_info,
        "../car.png",
        40, 20,
        264 * 1.2, 126 * 1.2);

    aroma_ui_label(
        (AromaNode *)general_info,
        "Vehicle Status: All systems normal",
        90, 200,
        LABEL_STYLE_LABEL_MEDIUM, ui_font);

    AromaNode *progress_bar = aroma_ui_progressbar(
        (AromaNode *)general_info,
        30, 260,
        360, 20,
        PROGRESS_TYPE_DETERMINATE, 0.75f);

    aroma_ui_icon((AromaNode *)general_info, AROMA_ICON_BATTERY_CHARGING_FULL, 40, 305, 48, theme.colors.primary, icon_font);

    AromaFont *font = aroma_font_create("../Ubuntu-Bold.ttf", 24);

    aroma_ui_label(
        (AromaNode *)general_info,
        "204",
        90, 310,
        LABEL_STYLE_LABEL_MEDIUM, font);

    aroma_ui_label(
        (AromaNode *)general_info,
        "km",
        90 + aroma_font_get_line_width(font, "204"), 320,
        LABEL_STYLE_LABEL_SMALL, ui_font);

    aroma_ui_label(
        (AromaNode *)general_info,
        "Remaining",
        90, 340,
        LABEL_STYLE_LABEL_SMALL, ui_font);
    aroma_ui_divider(
        (AromaNode *)general_info,
        180, 300,
        80, DIVIDER_ORIENTATION_VERTICAL);

    aroma_ui_label(
        (AromaNode *)general_info,
        "128",
        200, 310,
        LABEL_STYLE_LABEL_MEDIUM, font);

    aroma_ui_label(
        (AromaNode *)general_info,
        "Wh",
        200 + aroma_font_get_line_width(font, "128"), 320,
        LABEL_STYLE_LABEL_SMALL, ui_font);

    aroma_ui_label(
        (AromaNode *)general_info,
        "Average",
        200, 340,
        LABEL_STYLE_LABEL_SMALL, ui_font);

    aroma_ui_divider(
        (AromaNode *)general_info,
        290, 300,
        80, DIVIDER_ORIENTATION_VERTICAL);

    aroma_ui_label(
        (AromaNode *)general_info,
        "35.5",
        310, 310,
        LABEL_STYLE_LABEL_MEDIUM, font);

    aroma_ui_label(
        (AromaNode *)general_info,
        "kWh",
        310 + aroma_font_get_line_width(font, "35.5"), 320,
        LABEL_STYLE_LABEL_SMALL, ui_font);

    aroma_ui_label(
        (AromaNode *)general_info,
        "Fuel Capacity",
        310, 340,
        LABEL_STYLE_LABEL_SMALL, ui_font);

    AromaNode *speed_card = aroma_ui_card(
        (AromaNode *)general_info,
        20, 380,
        200, 200, CARD_TYPE_FILLED);


    AromaFont *speed_font = aroma_font_create("../Ubuntu-Bold.ttf", 76);

    aroma_ui_label((AromaNode *)speed_card, "Speed", 20,  20, LABEL_STYLE_LABEL_MEDIUM, ui_font);

    aroma_ui_label(
        (AromaNode *)speed_card,
        "88",
        20,  40,
        LABEL_STYLE_LABEL_LARGE, speed_font);

    aroma_ui_label(
        (AromaNode *)speed_card,
        "km/h",
        20,  160,
        LABEL_STYLE_LABEL_MEDIUM, ui_font);

    aroma_ui_divider(
        (AromaNode *)speed_card,
        150,  20,
        170, DIVIDER_ORIENTATION_VERTICAL);

    aroma_ui_label(
        (AromaNode *)speed_card,
        "Gear: Drive",
        170,  40,
        LABEL_STYLE_LABEL_MEDIUM, ui_font);

    AromaNode *gear_card = aroma_ui_card(
        (AromaNode *)general_info,
        190, 450,
        200, 50, CARD_TYPE_ELEVATED);

    static const char *gears[4] = {"P", "R", "N", "D"};

    for (int i = 0; i < 4; ++i)
    {
        if (i == 3)
        {
            aroma_ui_card(
                (AromaNode *)gear_card,
                (i * 40) + 20,  10,
                40, 30, CARD_TYPE_FILLED);
        }
        AromaNode *label = aroma_ui_label(
            (AromaNode *)gear_card,
            gears[i],
            (i * 41) + 30, 8,
            LABEL_STYLE_LABEL_MEDIUM, font);
        aroma_node_set_z_index(label, 99999999);
    }



    AromaFont* ac_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 76);

    aroma_ui_label((AromaNode *)ac_applet, "Adjust AC to your comfort level", 20,  20, LABEL_STYLE_LABEL_LARGE, ui_font);
    aroma_ui_label((AromaNode *)ac_applet, "23°F", 225,  60, LABEL_STYLE_LABEL_LARGE, ac_font);

    aroma_ui_iconbutton((AromaNode *)ac_applet, AROMA_ICON_REMOVE, 145,  100, 48, ICON_BUTTON_FILLED, NULL, NULL, icon_font);
    aroma_ui_iconbutton((AromaNode *)ac_applet, AROMA_ICON_ADD, 400,  100, 48, ICON_BUTTON_FILLED, NULL, NULL, icon_font);

    aroma_ui_card((AromaNode *)ac_applet, 130,  200, 330, 80, CARD_TYPE_FILLED);

    aroma_ui_image(
        (AromaNode *)ac_applet,
        "../air-conditioner.png",
        170,  215,
        48, 48);

    aroma_ui_image(
        (AromaNode *)ac_applet,
        "../under.png",
        270,  215,
        48, 48);

    aroma_ui_image(
        (AromaNode *)ac_applet,
        "../both.png",
        370,  215,
        48, 48);

    // System status
   
    aroma_ui_label((AromaNode *)applet1, "System Status",  20, 20, LABEL_STYLE_LABEL_MEDIUM, ui_font);
    
    aroma_ui_image(
        (AromaNode *)applet1,
        "../brake_indicator.png",
         20, 60,
        32, 32);
    
    aroma_ui_image(
        (AromaNode *)applet1,
        "../abs_indicator.png",
         70, 60,
        32, 32);

        
    aroma_ui_card((AromaNode *)applet1,  115, 55, 42, 42, CARD_TYPE_FILLED);
    
        aroma_ui_image(
        (AromaNode *)applet1,
        "../high_beams.png",
         120, 60,
        32, 32);

    

    aroma_ui_image(
        (AromaNode *)applet1,
        "../low_beams.png",
         170, 60,
        32, 32);
    

    aroma_ui_image(
        (AromaNode *)applet1,
        "../seatbelt.png",
         220, 60,
        32, 32);

    aroma_ui_image(
        (AromaNode *)applet1,
        "../battery_indicator.png",
         20, 120,
        32, 32);
    
    aroma_ui_image(
        (AromaNode *)applet1,
        "../temperature.png",
         70, 120,
        32, 32);
}
