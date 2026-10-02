#include <aroma.h>
#include <unistd.h>

int main(void)
{
    aroma_ui_init();

    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);

    AromaWindow *window = aroma_ui_create_window("Hello World", 400, 600);

    AromaFont *font = aroma_font_create_from_memory(
        aroma_ubuntu_ttf,
        aroma_ubuntu_ttf_len,
        24);

    AromaNode *root = aroma_ui_container(
        (AromaNode *)window,
        0, 0, 400, 600,
        AROMA_LAYOUT_MODE_FLEX,
        AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_CENTER,
        AROMA_ALIGN_CENTER);

    aroma_ui_label(
        root,
        "Hello, World!",
        0, 0,
        LABEL_STYLE_LABEL_LARGE,
        font);

    int counter = 0;
    char counter_text[64];

    AromaNode *counter_label = aroma_ui_label(
        root,
        "Counter: 0",
        0, 20,
        LABEL_STYLE_LABEL_LARGE,
        font);

    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();

        snprintf(counter_text, sizeof(counter_text),
                 "Counter: %d", counter++);

        aroma_label_set_text(counter_label, counter_text);

        aroma_ui_render(window);
        usleep(1000000); // Update once per second
    }

    aroma_font_destroy(font);
    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();

    return 0;
}

#ifdef __ANDROID__
#include <android_native_app_glue.h>

void android_main(struct android_app *state)
{
    aroma_android_set_app(state);
    main();
}
#endif