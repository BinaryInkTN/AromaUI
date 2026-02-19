#include <aroma.h>
#include <unistd.h>
#include <stdio.h>


#define WIN_W 1280
#define WIN_H 720
#define SIDEBAR_W 200
#define CONTENT_W (WIN_W - SIDEBAR_W)
AromaFont *icon_font = NULL;

#define CENTER_X (CONTENT_W / 2) + SIDEBAR_W


static AromaFont *ui_font = NULL;
static AromaSidebar *sidebar = NULL;
static AromaContainer *content_root = NULL;
static AromaContainer *general_root = NULL;
static AromaContainer *climate_root = NULL;
static AromaContainer *settings_root = NULL;
static AromaTheme theme;


static const char *sidebar_items[] = {
    "General",
    "Navigation",
    "Climate",
    "Settings"
};


static void on_theme_change(int index, const char *option, void *user_data)
{
    (void)option;
    (void)user_data;

    switch (index)
    {
    case 0:
        theme = aroma_theme_create_material_preset_dark(
            AROMA_THEME_MATERIAL_BLUE);
        break;
    case 1:
        theme = aroma_theme_create_material_black();
        break;
    case 2:
        theme = aroma_theme_create_material_preset_dark(
            AROMA_THEME_MATERIAL_ORANGE);
        break;
    default:
        return;
    }

    aroma_ui_set_theme(&theme);
}


static void build_general_ui(AromaContainer *root)
{

    // Card Backgrounds
    AromaNode* general_info = aroma_ui_card((AromaNode *)root, 0, 0, 400, 100, CARD_TYPE_FILLED);
    aroma_node_set_flex_grow(general_info, 1);
    // flex
    AromaNode* applets_container = aroma_ui_container((AromaNode *)root, 0, 120, 610, 300, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_ui_card((AromaNode *)applets_container, 0, 0, 400, 300, CARD_TYPE_ELEVATED);
    aroma_node_set_gap((AromaNode *)applets_container, 20);
    AromaNode* applets_row = aroma_ui_container((AromaNode *)applets_container, 0, 130, 610, 310, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    AromaNode* applet1 = aroma_ui_card((AromaNode *)applets_row, 0, 0, 280, 70, CARD_TYPE_ELEVATED);
    AromaNode* applet2 = aroma_ui_card((AromaNode *)applets_row, 0, 0, 280, 70, CARD_TYPE_ELEVATED);
    aroma_node_set_gap((AromaNode *)applets_row, 20);
    aroma_node_set_flex_grow(applet1, 1);
    aroma_node_set_flex_grow(applet2, 1);


    // Temporary workaround to add widgets on the card until we have proper support for that
    AromaRect* info_rect = (AromaRect*)general_info->node_widget_ptr;
    



    aroma_ui_image(
        (AromaNode *)general_info,
        "../car.png",
        info_rect->x + 40, info_rect->y + 20,
        264 *1.2,126 *1.2); 

    aroma_ui_label(
        (AromaNode *)general_info,
        "Vehicle Status: All systems normal",
        info_rect->x + 90, info_rect->y + 200,
        LABEL_STYLE_LABEL_MEDIUM, ui_font);

    AromaNode* progress_bar = aroma_ui_progressbar(
        (AromaNode *)general_info,
        info_rect->x + 30, info_rect->y + 260,
        360, 20,
        PROGRESS_TYPE_DETERMINATE, 0.75f);

        aroma_ui_icon((AromaNode*) general_info, AROMA_ICON_BATTERY_CHARGING_FULL, info_rect->x + 40, info_rect->y + 305, 48, theme.colors.primary, icon_font);

        AromaFont* font = aroma_font_create("../Ubuntu-Bold.ttf", 24);

        aroma_ui_label(
            (AromaNode *)general_info,
            "204",
            info_rect->x + 90, info_rect->y + 310,
            LABEL_STYLE_LABEL_MEDIUM, font);

        aroma_ui_label(
            (AromaNode *)general_info,
            "km",
            info_rect->x + 90 + aroma_font_get_line_width(font, "204"), info_rect->y + 320,
            LABEL_STYLE_LABEL_SMALL, ui_font);

         aroma_ui_label(
            (AromaNode *)general_info,
            "Remaining",
            info_rect->x + 90 , info_rect->y + 340,
            LABEL_STYLE_LABEL_SMALL, ui_font);
        aroma_ui_divider(
            (AromaNode *)general_info,
            info_rect->x + 180, info_rect->y + 300,
             80, DIVIDER_ORIENTATION_VERTICAL);


        // Wh/km
      aroma_ui_label(
            (AromaNode *)general_info,
            "128",
            info_rect->x + 200, info_rect->y + 310,
            LABEL_STYLE_LABEL_MEDIUM, font);

        aroma_ui_label(
            (AromaNode *)general_info,
            "Wh",
            info_rect->x + 200 + aroma_font_get_line_width(font, "128"), info_rect->y + 320,
            LABEL_STYLE_LABEL_SMALL, ui_font);

         aroma_ui_label(
            (AromaNode *)general_info,
            "Average",
            info_rect->x + 200 , info_rect->y + 340,
            LABEL_STYLE_LABEL_SMALL, ui_font);

            aroma_ui_divider(
            (AromaNode *)general_info,
            info_rect->x + 290, info_rect->y + 300,
             80, DIVIDER_ORIENTATION_VERTICAL);

            // Full Capacity
                  aroma_ui_label(
            (AromaNode *)general_info,
            "35.5",
            info_rect->x + 310, info_rect->y + 310,
            LABEL_STYLE_LABEL_MEDIUM, font);

        aroma_ui_label(
            (AromaNode *)general_info,
            "kWh",
            info_rect->x + 310 + aroma_font_get_line_width(font, "35.5"), info_rect->y + 320,
            LABEL_STYLE_LABEL_SMALL, ui_font);

         aroma_ui_label(
            (AromaNode *)general_info,
            "Fuel Capacity",
            info_rect->x + 310 , info_rect->y + 340,
            LABEL_STYLE_LABEL_SMALL, ui_font);


        // Speed 
        AromaNode* speed_card = aroma_ui_card(
            (AromaNode *)general_info,
            info_rect->x + 20, info_rect->y + info_rect->height - 220,
            200, 200, CARD_TYPE_FILLED);


        AromaRect* speed_rect = (AromaRect*)speed_card->node_widget_ptr;

        AromaFont* speed_font = aroma_font_create("../Ubuntu-Bold.ttf", 76);

        aroma_ui_label((AromaNode *)speed_card, "Speed", speed_rect->x + 20, speed_rect->y + 20, LABEL_STYLE_LABEL_MEDIUM, ui_font);

        aroma_ui_label(
            (AromaNode *)speed_card,
            "88",
            speed_rect->x + 20, speed_rect->y + 40,
            LABEL_STYLE_LABEL_LARGE, speed_font);



        aroma_ui_label(
            (AromaNode *)speed_card,
            "km/h",
            speed_rect->x + 20 , speed_rect->y + 160,
            LABEL_STYLE_LABEL_MEDIUM, ui_font);


            aroma_ui_divider(
            (AromaNode *)speed_card,
            speed_rect->x + 150, speed_rect->y + 20,
             170, DIVIDER_ORIENTATION_VERTICAL);
        


}


static void build_climate_ui(AromaContainer *root)
{
    AromaLabel *title =
        aroma_label_create(
            (AromaNode *)root,
            "Adjust the climate to your comfort",
            CENTER_X-120, 30,
            LABEL_STYLE_LABEL_LARGE);
    aroma_label_set_font((AromaNode *)title, ui_font);

    AromaLabel *temp =
        aroma_label_create(
            (AromaNode *)root,
            "22.0",
            CENTER_X -20, 110,
            LABEL_STYLE_LABEL_LARGE);
    aroma_label_set_font((AromaNode *)temp, ui_font);

    AromaButton *minus =
        aroma_button_create(
            (AromaNode *)root,
            "-",
            CENTER_X - 140, 95,
            60, 60);
    AromaButton *plus =
        aroma_button_create(
            (AromaNode *)root,
            "+",
            CENTER_X + 80, 95,
            60, 60);

    aroma_button_setup_events((AromaNode *)minus, aroma_ui_request_redraw, NULL);
    aroma_button_setup_events((AromaNode *)plus, aroma_ui_request_redraw, NULL);

    aroma_button_set_font((AromaNode *)minus, ui_font);
    aroma_button_set_font((AromaNode *)plus, ui_font);

    AromaLabel *fan =
        aroma_label_create(
            (AromaNode *)root,
            "Fan Speed",
            CENTER_X-30, 180,
            LABEL_STYLE_LABEL_MEDIUM);
    aroma_label_set_font((AromaNode *)fan, ui_font);

    AromaSlider *fan_slider =
        aroma_slider_create(
            (AromaNode *)root,
            CENTER_X - 160, 220,
            320, 26,
            0, 7, 3);
    aroma_slider_setup_events(
        (AromaNode *)fan_slider,
        aroma_ui_request_redraw,
        NULL);

   
    AromaSwitch *ac =
        aroma_switch_create(
            (AromaNode *)root,
            CENTER_X - 160, 300,
            52, 28,
            true);
    AromaLabel *ac_l =
        aroma_label_create(
            (AromaNode *)root,
            "A/C",
            CENTER_X - 145, 335,
            LABEL_STYLE_LABEL_SMALL);

    AromaSwitch *auto_sw =
        aroma_switch_create(
            (AromaNode *)root,
            CENTER_X - 26, 300,
            52, 28,
            true);
    AromaLabel *auto_l =
        aroma_label_create(
            (AromaNode *)root,
            "AUTO",
            CENTER_X - 20, 335,
            LABEL_STYLE_LABEL_SMALL);

    AromaSwitch *recirc =
        aroma_switch_create(
            (AromaNode *)root,
            CENTER_X + 108, 300,
            52, 28,
            false);
    AromaLabel *recirc_l =
        aroma_label_create(
            (AromaNode *)root,
            "RECIRC",
            CENTER_X + 114, 335,
            LABEL_STYLE_LABEL_SMALL);

    aroma_label_set_font((AromaNode *)ac_l, ui_font);
    aroma_label_set_font((AromaNode *)auto_l, ui_font);
    aroma_label_set_font((AromaNode *)recirc_l, ui_font);

    aroma_switch_setup_events((AromaNode *)ac, aroma_ui_request_redraw, NULL);
    aroma_switch_setup_events((AromaNode *)auto_sw, aroma_ui_request_redraw, NULL);
    aroma_switch_setup_events((AromaNode *)recirc, aroma_ui_request_redraw, NULL);

    aroma_image_create(
        (AromaNode *)root,
        "../air-conditioner.png",
        CENTER_X - 160, 380,
        48, 48);

    aroma_image_create(
        (AromaNode *)root,
        "../both.png",
        CENTER_X - 20, 380,
        48, 48);

    aroma_image_create(
        (AromaNode *)root,
        "../under.png",
        CENTER_X + 120, 380,
        48, 48);
}


static void build_settings_ui(AromaContainer *root)
{
    AromaLabel *title =
        aroma_label_create(
            (AromaNode *)root,
            "Settings",
            CENTER_X, 40,
            LABEL_STYLE_LABEL_LARGE);
    aroma_label_set_font((AromaNode *)title, ui_font);

    AromaLabel *theme_lbl =
        aroma_label_create(
            (AromaNode *)root,
            "Theme",
            CENTER_X - 120, 130,
            LABEL_STYLE_LABEL_MEDIUM);
    aroma_label_set_font((AromaNode *)theme_lbl, ui_font);

    AromaNode *themes =
        aroma_dropdown_create(
            (AromaNode *)root,
            CENTER_X + 20, 115,
            200, 32);

    aroma_dropdown_add_option(themes, "Dark (BLUE)");
    aroma_dropdown_add_option(themes, "OLED (BLACK)");
    aroma_dropdown_add_option(themes, "Dark (ORANGE)");

    aroma_dropdown_set_font(themes, ui_font);
    aroma_dropdown_set_on_change(themes, on_theme_change, NULL);
    aroma_dropdown_setup_events(themes, aroma_ui_request_redraw, NULL);
}


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
    
    // status bar 

    aroma_ui_label(
        (AromaNode *)window,
        "12:45 PM",
        250, 30,
        LABEL_STYLE_LABEL_LARGE, ui_font);

    aroma_ui_label(
        (AromaNode *)window,
        "San Francisco, 68°F",
        400, 30,
        LABEL_STYLE_LABEL_MEDIUM, ui_font);

     aroma_ui_icon((AromaNode*) window, AROMA_ICON_SIGNAL_CELLULAR_4_BAR, WIN_W - 120, 30, 24, theme.colors.primary, icon_font);
     aroma_ui_icon((AromaNode*) window, AROMA_ICON_WIFI, WIN_W - 80, 30, 24, theme.colors.primary, icon_font);
     aroma_ui_icon((AromaNode*) window, AROMA_ICON_BATTERY_FULL, WIN_W - 40, 30, 24, theme.colors.primary, icon_font);
     aroma_ui_icon((AromaNode*) window, AROMA_ICON_GPS_FIXED, WIN_W - 160, 30, 24, theme.colors.primary, icon_font);
     aroma_ui_icon((AromaNode*) window, AROMA_ICON_BLUETOOTH_AUDIO, WIN_W - 200, 30, 24, theme.colors.primary, icon_font);



    content_root =
        aroma_container_create(
            (AromaNode *)window,
            SIDEBAR_W, 0,
            CONTENT_W, WIN_H);

    general_root    = aroma_ui_container((AromaNode *)content_root, 230, 90, CONTENT_W - 30, WIN_H - 90, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_gap((AromaNode *)general_root, 20);
    climate_root  = aroma_container_create((AromaNode *)content_root, 0, 80, CONTENT_W, WIN_H - 80);
    settings_root = aroma_container_create((AromaNode *)content_root, 0, 80, CONTENT_W, WIN_H- 80);
    
    build_general_ui(general_root);
    build_climate_ui(climate_root);
    build_settings_ui(settings_root);

    sidebar =
        aroma_sidebar_create(
            (AromaNode *)window,
            0, 0,
            SIDEBAR_W, WIN_H,
            sidebar_items, 4);

    aroma_sidebar_set_font((AromaNode *)sidebar, ui_font);

    AromaNode *general_nodes[]    = {(AromaNode *)general_root};
    AromaNode *climate_nodes[]  = {(AromaNode *)climate_root};
    AromaNode *settings_nodes[] = {(AromaNode *)settings_root};

    aroma_sidebar_set_content((AromaNode *)sidebar, 0, general_nodes, 1);
    aroma_sidebar_set_content((AromaNode *)sidebar, 2, climate_nodes, 1);
    aroma_sidebar_set_content((AromaNode *)sidebar, 3, settings_nodes, 1);



    aroma_sidebar_set_icon((AromaNode*) sidebar, 0, AROMA_ICON_DASHBOARD, icon_font);
    aroma_sidebar_set_icon((AromaNode*) sidebar, 1, AROMA_ICON_MAP, icon_font);
    aroma_sidebar_set_icon((AromaNode*) sidebar, 2, AROMA_ICON_AC_UNIT, icon_font);
    aroma_sidebar_set_icon((AromaNode*) sidebar, 3, AROMA_ICON_SETTINGS, icon_font);
    aroma_sidebar_setup_events(
        (AromaNode *)sidebar,
        aroma_ui_request_redraw,
        NULL);

    aroma_sidebar_set_selected((AromaNode *)sidebar, 0);
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
