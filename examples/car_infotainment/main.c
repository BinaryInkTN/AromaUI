#include <aroma.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

int main()
{
    aroma_ui_init();
    AromaWindow *window = aroma_ui_create_window("Map Example", 700, 400);
    AromaFont *font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
        AromaNode* button = aroma_ui_button((AromaNode*) window, "Click Me", 50, 50, 100, 40, NULL, NULL, font);
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

    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();
}