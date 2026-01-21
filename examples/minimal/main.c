#include <aroma.h>
#include <unistd.h>

static AromaFont* font = NULL;
static AromaLabel* title_label = NULL;
static AromaProgressBar* progress_bar = NULL;
static AromaNode* image_widget = NULL;
static AromaButton* action_button = NULL;




static void window_update_callback(size_t window_id, void* data) {
    if (!aroma_ui_consume_redraw()) return;
    
    aroma_ui_begin_frame(window_id);
    AromaTheme theme = aroma_theme_get_global();
    aroma_ui_render_dirty_window(window_id, theme.colors.background);
    aroma_ui_end_frame(window_id);
    aroma_graphics_swap_buffers(window_id);
}

int main(void) {
    if (!aroma_ui_init()) return 1;
    
    AromaTheme preset = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_GREEN);
    aroma_ui_set_theme(&preset);
    
    font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 18);
    if (!font) {
        aroma_ui_shutdown();
        return 1;
    }
    
    AromaWindow* window = aroma_ui_create_window("Hello Aroma!", 800, 400);
    if (!window) return 1;
    
    aroma_event_set_root((AromaNode*)window);
    if (font) aroma_ui_prepare_font_for_window(0, font);
    
    AromaContainer* main_container = (AromaContainer*)aroma_container_create((AromaNode*)window, 0, 0, 800, 400);
    AromaNode* container_node = main_container ? (AromaNode*)main_container : (AromaNode*)window;
    
    title_label = (AromaLabel*)aroma_label_create(container_node, "Hello Aroma!", 300, 50, LABEL_STYLE_TITLE_LARGE);
    if (title_label) {
        aroma_label_set_font((AromaNode*)title_label, font);
        aroma_label_set_color((AromaNode*)title_label, 0x4CAF50);
    }
    
    progress_bar = (AromaProgressBar*)aroma_progressbar_create(container_node, 200, 120, 400, 20, PROGRESS_TYPE_DETERMINATE);
    if (progress_bar) {
        aroma_progressbar_set_progress((AromaNode*)progress_bar, 0.5f);
    }
    
    image_widget = aroma_image_create(container_node, "hero.png", 50, 50, 100, 100);
    aroma_image_set_source(image_widget, "hero.png");
    aroma_platform_set_window_update_callback(window_update_callback, NULL);
    
    aroma_ui_request_redraw(NULL);
    
    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(16000);
    }
    
    if (image_widget) aroma_image_destroy(image_widget);
    aroma_ui_destroy_window(window);
    if (font) aroma_ui_unload_font(font);
    aroma_ui_shutdown();
    
    return 0;
}