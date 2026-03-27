#define AROMA_HAS_VULKAN
#include "aroma.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    AromaNode *greeting_label;
    AromaNode *root_container;
    AromaNode *btn_click;
    char greeting_text[64];
    int click_count;
    AromaFont *title_font;
    AromaFont *button_font;
    AromaWindow *window;
} AppState;

static bool on_click(AromaNode *btn, void *data)
{
    (void)btn;
    AppState *state = (AppState *)data;
    state->click_count++;
    snprintf(state->greeting_text, sizeof(state->greeting_text),
             "Hello, World! (%d)", state->click_count);
    aroma_label_set_text(state->greeting_label, state->greeting_text);
    return true;
}

int main(int argc, char **argv)
{
    if (!aroma_ui_init())
    {
        printf("Failed to initialise AromaUI\n");
        return 1;
    }

    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);

    AppState state = {0};
    state.window = aroma_ui_create_window("AromaUI Hello World",
                                          aroma_android_dp_to_px(400),
                                          aroma_android_dp_to_px(600));
    if (!state.window)
    {
        printf("Failed to create window\n");
        aroma_ui_shutdown();
        return 1;
    }

    aroma_window_set_fullscreen((AromaNode *)state.window, true);

    state.title_font = aroma_font_create_from_memory(
        aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, aroma_android_sp_to_px(36));
    state.button_font = aroma_font_create_from_memory(
        aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, aroma_android_sp_to_px(20));

    if (!state.title_font || !state.button_font)
    {
        printf("Failed to load fonts\n");
        aroma_ui_destroy_window(state.window);
        aroma_ui_shutdown();
        return 1;
    }

    int w, h;
    aroma_window_get_size(state.window, &w, &h);

    state.root_container = aroma_ui_container(
        (AromaNode *)state.window, 0, 0, w, h,
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    aroma_node_set_gap(state.root_container, aroma_android_dp_to_px(32));

    state.click_count = 0;
    snprintf(state.greeting_text, sizeof(state.greeting_text), "Hello, World! (0)");
    state.greeting_label = aroma_ui_label(
        state.root_container,
        state.greeting_text, 0, 0,
        LABEL_STYLE_LABEL_LARGE, state.title_font);

    int btn_width = aroma_android_dp_to_px(160);
    int btn_height = aroma_android_dp_to_px(48);
    state.btn_click = aroma_ui_button(
        state.root_container,
        "Click me!", 0, 0, btn_width, btn_height,
        on_click, &state, state.button_font);

    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        aroma_ui_render(state.window);
    }

    if (state.btn_click)
        aroma_button_destroy(state.btn_click);
    if (state.greeting_label)
        aroma_label_destroy(state.greeting_label);
    if (state.root_container)
        aroma_container_destroy(state.root_container);

    if (state.title_font)
        aroma_font_destroy(state.title_font);
    if (state.button_font)
        aroma_font_destroy(state.button_font);

    aroma_ui_destroy_window(state.window);
    aroma_ui_shutdown();

    return 0;
}

#ifdef __ANDROID__
#include <android_native_app_glue.h>
void android_main(struct android_app *state)
{
    aroma_android_set_app(state);
    main(0, NULL);
}
#endif