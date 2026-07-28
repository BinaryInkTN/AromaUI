#include <aroma.h>
#include <aroma_animation.h>
#include <unistd.h>

static bool g_sidebar_open = false;

void open_sidebar(void *user_data)
{
    AromaNode *sidebar = (AromaNode *)user_data;
    if (!sidebar)
        return;

    if (!g_sidebar_open)
    {
        AromaAnimation *anim = aroma_animation_start(sidebar, AROMA_ANIM_SLIDE_X, -150.0f, 10.0f, 400);
        aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
        g_sidebar_open = true;
    }
    else
    {
        AromaAnimation *anim = aroma_animation_start(sidebar, AROMA_ANIM_SLIDE_X, 10.0f, -150.0f, 400);
        aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
        g_sidebar_open = false;
    }
}

int main()
{
    aroma_ui_init();
    aroma_animation_manager_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    
    // Get screen size
    int screen_width, screen_height;
    AromaWindow *window = aroma_ui_create_window("Map Example", 400, 600);
    aroma_window_get_size(window, &screen_width, &screen_height);
    
    // Calculate scaling factors based on screen size
    float scale_x = screen_width / 400.0f;
    float scale_y = screen_height / 600.0f;
    float scale = (scale_x < scale_y) ? scale_x : scale_y; // Use the smaller scale to fit
    
    // Scale font sizes
    int text_font_size = (int)(16 * scale);
    int icon_font_size = (int)(24 * scale);
    
    // Ensure minimum font sizes
    if (text_font_size < 12) text_font_size = 12;
    if (icon_font_size < 16) icon_font_size = 16;
    
    AromaFont *text_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, text_font_size);
    AromaFont *icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, icon_font_size);
    
    // Scale sidebar dimensions
    int sidebar_width = (int)(150 * scale);
    int sidebar_height = (int)(screen_height * 0.7f); // 70% of screen height
    int sidebar_x = -sidebar_width; // Start off screen
    int sidebar_y = (int)(60 * scale);
    
    // Scale button size
    int button_size = (int)(34 * scale);
    int button_x = (int)(10 * scale);
    int button_y = (int)(40 * scale);
    
    // Create containers that fill the screen
    AromaNode *container1 = aroma_ui_container(
        (AromaNode *)window, 0, 0, screen_width, screen_height, 
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, 
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    
    AromaNode *container2 = aroma_ui_container(
        (AromaNode *)window, 0, 0, screen_width, screen_height, 
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, 
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);
    
    AromaNode *container3 = aroma_ui_container(
        (AromaNode *)window, 0, 0, screen_width, screen_height, 
        AROMA_LAYOUT_MODE_FLEX, AROMA_FLEX_COLUMN, 
        AROMA_JUSTIFY_CENTER, AROMA_ALIGN_CENTER);

    const char *labels[] = {"Page 1", "Page 2", "Page 3"};
    AromaNode *sidebar = aroma_ui_sidebar(
        (AromaNode *)window,
        sidebar_x, sidebar_y, sidebar_width, sidebar_height,
        labels, 3,
        NULL, NULL,
        text_font);

    AromaNode *sidebar_open_button = aroma_ui_iconbutton(
        (AromaNode *)window,
        AROMA_ICON_MENU,
        button_x, button_y, button_size,
        ICON_BUTTON_STANDARD,
        open_sidebar, sidebar, icon_font);
    
    // Open sidebar initially
    open_sidebar(sidebar);
    
    // Create labels with scaled fonts
    AromaNode *label1 = aroma_ui_label(
        (AromaNode *)container1, "Content for Page 1", 0, 0, 
        LABEL_STYLE_LABEL_LARGE, text_font);
    
    AromaNode *label2 = aroma_ui_label(
        (AromaNode *)container2, "Content for Page 2", 0, 0, 
        LABEL_STYLE_LABEL_LARGE, text_font);
    
    AromaNode *label3 = aroma_ui_label(
        (AromaNode *)container3, "Content for Page 3", 0, 0, 
        LABEL_STYLE_LABEL_LARGE, text_font);

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
        usleep(16000);
    }

    aroma_font_destroy(text_font);
    aroma_font_destroy(icon_font);
    aroma_ui_destroy_window(window);
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