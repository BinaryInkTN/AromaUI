#include <unistd.h>

#include <aroma.h>
#include "aroma_incense_loader.h"

#define UI_FILE "../ui.aroma"
#define FRAME_SLEEP_US 16667

#define FONT_SIZE_BODY 18
#define FONT_SIZE_HERO 80

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

    if (!font || !icon_font || !big_font)
    {
        aroma_ui_shutdown();
        return 1;
    }

    IncenseRegisterFont("big_font", big_font);

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
        IncenseHotReloadCheck();
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(FRAME_SLEEP_US);
    }

    IncenseHotReloadStopAll();
    IncenseFreeRegistry(registry);
    aroma_ui_destroy_window(window);

    aroma_font_destroy(big_font);
    aroma_font_destroy(icon_font);
    aroma_font_destroy(font);

    aroma_ui_shutdown();
    return 0;
}