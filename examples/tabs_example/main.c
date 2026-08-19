#include <aroma.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

int main()
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    AromaWindow *window = aroma_ui_create_window("Map Example", 700, 400);
    AromaFont *text_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    AromaNode *tabs = aroma_ui_tabs(
        (AromaNode *)window,
        10, 10, 680, 60,
        (const char *[]){"Tab 1", "Tab 2", "Tab 3"}, 3,
        NULL, NULL,
        text_font);
    AromaNode *container1 = aroma_ui_container((AromaNode *)window, 10, 80, 680, 310, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    AromaNode *container2 = aroma_ui_container((AromaNode *)window, 10, 80, 680, 310, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);

    AromaNode *container3 = aroma_ui_container((AromaNode *)window, 10, 80, 680, 310, AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);

    AromaNode *label1 = aroma_ui_label((AromaNode *)container1, "Content for Tab 1", 0, 0, LABEL_STYLE_LABEL_LARGE, text_font);
    AromaNode *label2 = aroma_ui_label((AromaNode *)container2, "Content for Tab 2", 0, 0, LABEL_STYLE_LABEL_LARGE, text_font);
    AromaNode *label3 = aroma_ui_label((AromaNode *)container3, "Content for Tab 3", 0, 0, LABEL_STYLE_LABEL_LARGE, text_font);
    aroma_tabs_set_content(tabs, 0, (AromaNode *[]){container1}, 1);
    aroma_tabs_set_content(tabs, 1, (AromaNode *[]){container2}, 1);
    aroma_tabs_set_content(tabs, 2, (AromaNode *[]){container3}, 1);
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
    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();
}