#include <aroma.h>
#include <unistd.h>

static AromaFont* font = NULL;
static AromaLabel* title_label = NULL;
static AromaNode* image_widget = NULL;


static bool on_button_click(AromaNode* node, void *user_data) {
    (void)node;
    (void)user_data;
    if (title_label) {
        aroma_label_set_text((AromaNode*)title_label, "You clicked the button!");
    }
    return true;
}


int main(void) {
    if (!aroma_ui_init()) return 1;
    
    AromaTheme preset = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_PURPLE);
    aroma_ui_set_theme(&preset);
    
    font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    if (!font) {
        aroma_ui_shutdown();
        return 1;
    }
    
    AromaWindow* window = aroma_ui_create_window("Hello Aroma!", 400, 400);
    if (!window) return 1;
    
    aroma_event_set_root((AromaNode*)window);
    if (font) aroma_ui_prepare_font_for_window(0, font);
    
    AromaContainer* main_container = (AromaContainer*)aroma_container_create((AromaNode*)window, 0, 0, 800, 400);
    AromaNode* container_node = main_container ? (AromaNode*)main_container : (AromaNode*)window;
    
    title_label = (AromaLabel*)aroma_label_create(container_node, "Welcome to AromaUI!", 120, 250, LABEL_STYLE_LABEL_LARGE);
    if (title_label) {
        aroma_label_set_font((AromaNode*)title_label, font);
    }
    
    AromaButton* get_started_button = (AromaButton*)aroma_ui_create_button((AromaWindow*)container_node, "Get Started", 150, 300, 100, 40);
    if (get_started_button) {
        aroma_button_set_font((AromaNode*)get_started_button, font);
    }
 
    
    image_widget = aroma_image_create(container_node, "../assets/leaf.png", 130, 50, 128, 128);
    aroma_node_set_z_index(image_widget, 0);
    aroma_button_set_on_click((AromaNode*)get_started_button, on_button_click, NULL);
    
    aroma_ui_request_redraw(NULL);
    
    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(16000);
        // Can't do rendering here.
    }
    
    if (image_widget) aroma_image_destroy(image_widget);
    aroma_ui_destroy_window(window);
    if (font) aroma_ui_unload_font(font);
    aroma_ui_shutdown();
    
    return 0;
}