    #include <aroma.h>
    #include <unistd.h>
    #include <aroma_incense_loader.h>
    #ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #endif

    #include <stdio.h>
    #include <stdlib.h>

    void on_click_callback(void* userdata) {
        printf("Button clicked! (userdata: %p)\n", userdata);
    }

    int main()
    {

        printf("Initializing UI system...\n");
        aroma_ui_init();
        
        AromaFont *text_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
        AromaFont *icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
            IncenseRegisterCallback("on_click_clbk", INCENSE_CALLBACK_VOID_PTR, on_click_callback, NULL);
        IncenseRegistry *registry = NULL;
    
        printf("Loading UI from test.aroma...\n");
        AromaWindow *window = IncenseLoadFileEx("../test.aroma", text_font, icon_font, &registry);
        if (!window) {
            printf("Failed to load test.aroma\n");
            aroma_font_destroy(text_font);
            aroma_font_destroy(icon_font);
            aroma_ui_shutdown();
            return 1;
        }
        AromaNode* label_settings = IncenseFindWidget(registry, "label_settings");
        if(label_settings) {
            aroma_label_set_text(label_settings, "Settings (updated from C callback)");
        } else {
            printf("label_settings widget not found\n");
        }
        
        printf("Starting main loop...\n");
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

        printf("Shutting down...\n");
        aroma_font_destroy(text_font);
        aroma_font_destroy(icon_font);
        aroma_ui_destroy_window(window);
        aroma_ui_shutdown();
                IncenseFreeRegistry(registry);

        printf("Done!\n");
        return 0;
    }