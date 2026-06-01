#include <aroma.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif


int main()
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue();
    aroma_ui_set_theme(&theme);
    AromaWindow *window = aroma_ui_create_window("Map Example", 700, 400);
    AromaFont *text_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 14);
    AromaNode *map = aroma_ui_map((AromaNode *)window, 0, 0, 700, 400);
    // aroma_map_set_zoom(map, 12);
    if (map)
    {
        aroma_map_set_center(map, 33.8869f, 9.5375f);
        aroma_map_add_icon_marker(map, 33.8869f, 9.5375f, 0xFF0000, AROMA_ICON_HOME);                                   // Marker at Tunisia
        aroma_map_add_popup_marker(map, 37.7749f, -122.4194f, 0x0000FF, "San Francisco is a city in California, USA."); // Popup marker at San Francisco
        aroma_map_set_route(map, 48.8566, 2.3522, 48.8049, 2.1204, 0xFF35A8FE);
    }
      AromaNode *bottom_card = aroma_ui_card((AromaNode *)window, 10, 340, 440, 40, CARD_TYPE_ELEVATED);
    AromaNode *card_label = aroma_ui_label(bottom_card, "This is a map of the world with markers and a route. Built in C with AromaUI.", 10, 10, LABEL_STYLE_LABEL_MEDIUM, text_font);

        aroma_node_set_z_index(bottom_card, 99999);
        aroma_node_set_z_index(card_label, 99999);

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