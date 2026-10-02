#include <aroma.h>
#include <aroma_animation.h>
#include <aroma_incense_loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static AromaWindow *g_window = NULL;
static AromaFont *g_text_font = NULL;
static AromaFont *g_icon_font = NULL;
static IncenseRegistry *g_registry = NULL;

void on_click_callback(void* userdata) {
    printf("Button clicked! (userdata: %p)\n", userdata);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("Initializing UI system...\n");
    aroma_ui_init();
    aroma_animation_manager_init();


    g_text_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);

    IncenseRegisterCallback("on_click_clbk", INCENSE_CALLBACK_VOID_PTR, on_click_callback, NULL);

    printf("Loading UI from file...\n");
    g_window = IncenseLoadFileEx("../ui.aroma", g_text_font, g_icon_font, &g_registry);
    if (!g_window) {
        printf("Failed to load test.aroma\n");
        aroma_font_destroy(g_text_font);
        aroma_font_destroy(g_icon_font);
        aroma_ui_shutdown();
        return 1;
    }


    AromaNode *root = (AromaNode *)g_window;
    aroma_event_set_root(root);

    printf("Starting main loop...\n");
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop([]() {
        if (!aroma_ui_is_running()) return;
        aroma_ui_process_events();
        aroma_ui_render(g_window);
    }, 60, 1);
#else
    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        if (aroma_ui_consume_redraw())
        {
            aroma_ui_render(g_window);
        }
#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }
#endif

    printf("Shutting down...\n");
    aroma_font_destroy(g_text_font);
    aroma_font_destroy(g_icon_font);
    aroma_ui_destroy_window(g_window);
    IncenseFreeRegistry(g_registry);
    aroma_ui_shutdown();

    printf("Done!\n");
    return 0;
}
