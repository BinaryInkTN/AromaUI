#include <aroma.h>
#include "aroma_incense_loader.h"
  #include <unistd.h>
int main() {
        set_minimum_log_level(DEBUG_LEVEL_WARNING);

    aroma_ui_init();
    // Load fonts
    AromaFont *font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 18);
    AromaFont *icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 18);
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    IncenseRegistry *registry = NULL;
       AromaFont *big_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 80);
    IncenseRegisterFont("big_font", big_font);
    // Load UI from file
    AromaWindow *window = IncenseLoadFileEx("../ui.aroma", font, icon_font, &registry);
    if (!window) return 1;
    aroma_ui_set_theme(&theme);
    // Make root for events
    aroma_event_set_root((AromaNode *)window);

    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(25000);
    }
    aroma_font_destroy(font);
    aroma_font_destroy(icon_font);
    aroma_font_destroy(big_font);
    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();
    return 0;
}