#include <aroma.h>
#include <unistd.h>
#include <stdio.h>

/* ===================== WINDOW ===================== */
#define WIN_W 800
#define WIN_H 480
#define SIDEBAR_W 200
#define CONTENT_W (WIN_W - SIDEBAR_W)

/* ===================== CONTENT LAYOUT ===================== */
#define CENTER_X (CONTENT_W / 2) + SIDEBAR_W

/* ===================== GLOBAL ===================== */
static AromaFont *ui_font = NULL;
static AromaSidebar *sidebar = NULL;
static AromaContainer *content_root = NULL;
static AromaContainer *media_root = NULL;
static AromaContainer *climate_root = NULL;
static AromaContainer *settings_root = NULL;
static AromaTheme theme;

/* ===================== SIDEBAR ===================== */
static const char *sidebar_items[] = {
    "Media",
    "Navigation",
    "Climate",
    "Settings"
};

/* ===================== CALLBACKS ===================== */
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
    aroma_ui_request_redraw(NULL);
}

/* ===================== MEDIA ===================== */
static void build_media_ui(AromaContainer *root)
{
    AromaLabel *header =
        aroma_label_create(
            (AromaNode *)root,
            "Now playing: Kendrick Lamar - good kid, m.A.A.d city.",
            CENTER_X - 200, 40,
            LABEL_STYLE_LABEL_LARGE);
    aroma_label_set_font((AromaNode *)header, ui_font);

    aroma_image_create(
        (AromaNode *)root,
        "../album_cover.jpg",
        CENTER_X - 110, 80,
        220, 220);

    AromaButton *prev =
        aroma_button_create(
            (AromaNode *)root,
            "PREV",
            CENTER_X - 160, 330,
            90, 42);
    AromaButton *play =
        aroma_button_create(
            (AromaNode *)root,
            "PLAY",
            CENTER_X - 60, 330,
            120, 42);
    AromaButton *next =
        aroma_button_create(
            (AromaNode *)root,
            "NEXT",
            CENTER_X + 80, 330,
            90, 42);

    aroma_button_set_font((AromaNode *)prev, ui_font);
    aroma_button_set_font((AromaNode *)play, ui_font);
    aroma_button_set_font((AromaNode *)next, ui_font);

    aroma_button_setup_events((AromaNode *)prev, aroma_ui_request_redraw, NULL);
    aroma_button_setup_events((AromaNode *)play, aroma_ui_request_redraw, NULL);
    aroma_button_setup_events((AromaNode *)next, aroma_ui_request_redraw, NULL);

    AromaSlider *vol =
        aroma_slider_create(
            (AromaNode *)root,
            CENTER_X - 160, 390,
            320, 26,
            0, 100, 45);

    aroma_slider_setup_events(
        (AromaNode *)vol,
        aroma_ui_request_redraw,
        NULL);
}

/* ===================== CLIMATE (VW STYLE) ===================== */
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

    /* Bottom row (VW-style) */
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

/* ===================== SETTINGS ===================== */
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

/* ===================== MAIN ===================== */
int main(void)
{
    aroma_ui_init();

    theme =
        aroma_theme_create_material_preset_dark(
            AROMA_THEME_MATERIAL_BLUE);
    aroma_ui_set_theme(&theme);

    ui_font =
        aroma_font_create_from_memory(
            aroma_ubuntu_ttf,
            aroma_ubuntu_ttf_len,
            16);

    AromaWindow *window =
        aroma_ui_create_window(
            "Automotive HMI",
            WIN_W, WIN_H);

    aroma_event_set_root((AromaNode *)window);
    aroma_ui_prepare_font_for_window(0, ui_font);

    content_root =
        aroma_container_create(
            (AromaNode *)window,
            SIDEBAR_W, 0,
            CONTENT_W, WIN_H);

    media_root    = aroma_container_create((AromaNode *)content_root, 0, 0, CONTENT_W, WIN_H);
    climate_root  = aroma_container_create((AromaNode *)content_root, 0, 0, CONTENT_W, WIN_H);
    settings_root = aroma_container_create((AromaNode *)content_root, 0, 0, CONTENT_W, WIN_H);

    build_media_ui(media_root);
    build_climate_ui(climate_root);
    build_settings_ui(settings_root);

    sidebar =
        aroma_sidebar_create(
            (AromaNode *)window,
            0, 0,
            SIDEBAR_W, WIN_H,
            sidebar_items, 4);

    aroma_sidebar_set_font((AromaNode *)sidebar, ui_font);

    AromaNode *media_nodes[]    = {(AromaNode *)media_root};
    AromaNode *climate_nodes[]  = {(AromaNode *)climate_root};
    AromaNode *settings_nodes[] = {(AromaNode *)settings_root};

    aroma_sidebar_set_content((AromaNode *)sidebar, 0, media_nodes, 1);
    aroma_sidebar_set_content((AromaNode *)sidebar, 2, climate_nodes, 1);
    aroma_sidebar_set_content((AromaNode *)sidebar, 3, settings_nodes, 1);

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
        aroma_node_invalidate_tree((AromaNode *)window);
        usleep(16000);
    }

    aroma_ui_destroy_window(window);
    aroma_ui_unload_font(ui_font);
    aroma_ui_shutdown();
    return 0;
}
