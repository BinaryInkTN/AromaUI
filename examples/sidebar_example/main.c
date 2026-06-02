#include <aroma.h>
#include <aroma_animation.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static bool g_sidebar_open = false;

void open_sidebar(void *user_data)
{
    AromaNode *sidebar = (AromaNode *)user_data;
    if (!sidebar)
        return;

    if (!g_sidebar_open)
    {
        aroma_animation_start(sidebar, AROMA_ANIM_SLIDE_X, -150.0f, 10.0f, 1200);
        g_sidebar_open = true;
    }
    else
    {
        aroma_animation_start(sidebar, AROMA_ANIM_SLIDE_X, 10.0f, -150.0f, 1200);
        g_sidebar_open = false;
    }
}

int main()
{
    aroma_ui_init();
    aroma_animation_manager_init();
    AromaTheme theme = aroma_theme_create_material_black();
    aroma_ui_set_theme(&theme);
    AromaWindow *window = aroma_ui_create_window("Map Example", 700, 400);
    AromaFont *text_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    AromaFont *icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);

    AromaNode *container1 = aroma_ui_container((AromaNode *)window, 0, 0, 700, 400, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    AromaNode *container2 = aroma_ui_container((AromaNode *)window, 0, 0, 700, 400, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    AromaNode *container3 = aroma_ui_container((AromaNode *)window, 0, 0, 700, 400, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);

    AromaNode *sidebar = aroma_ui_sidebar(
        (AromaNode *)window,
        -150, 60, 150, 300,
        (const char *[]){"Page 1", "Page 2", "Page 3"}, 3,
        NULL, NULL,
        text_font);

    AromaNode *sidebar_open_button = aroma_ui_iconbutton(
        (AromaNode *)window,
        AROMA_ICON_MENU,
        10, 10, 34,
        ICON_BUTTON_STANDARD,
        open_sidebar, sidebar, icon_font);

    AromaNode *label1 = aroma_ui_label((AromaNode *)container1, "Content for Page 1", 0, 0, LABEL_STYLE_LABEL_LARGE, text_font);
    AromaNode *label2 = aroma_ui_label((AromaNode *)container2, "Content for Page 2", 0, 0, LABEL_STYLE_LABEL_LARGE, text_font);
    AromaNode *label3 = aroma_ui_label((AromaNode *)container3, "Content for Page 3", 0, 0, LABEL_STYLE_LABEL_LARGE, text_font);

    AromaNode *tab1_nodes[] = {container1};
    AromaNode *tab2_nodes[] = {container2};
    AromaNode *tab3_nodes[] = {container3};

    aroma_sidebar_set_content(sidebar, 0, tab1_nodes, 1);
    aroma_sidebar_set_content(sidebar, 1, tab2_nodes, 1);
    aroma_sidebar_set_content(sidebar, 2, tab3_nodes, 1);

    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        aroma_ui_render(window);
#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }

    aroma_font_destroy(text_font);
    aroma_font_destroy(icon_font);
    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();
}