

#include "widgets/aroma_tabs.h"
#include "core/aroma_common.h"
#include "core/aroma_event.h"
#include "core/aroma_logger.h"
#include "core/aroma_node.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#include "aroma_animation.h"

#define AROMA_TABS_CONTENT_MAX 8
#define AROMA_TABS_GAP 8

struct AromaTabs {
    AromaRect rect;
    char labels[AROMA_TABS_MAX][AROMA_TAB_LABEL_MAX];
    char icons[AROMA_TABS_MAX][8];
    AromaFont* icon_font;
    int count;
    int selected_index;
    int hovered_index;
    AromaNode* content_nodes[AROMA_TABS_MAX][AROMA_TABS_CONTENT_MAX];
    int content_counts[AROMA_TABS_MAX];
    uint32_t bg_color;
    uint32_t selected_color;
    uint32_t text_color;
    uint32_t text_selected_color;
    bool use_theme_color;
    AromaFont* font;
    uint32_t hover_overlay_color;
    int indicator_height;
    int indicator_padding;
    float corner_radius;
    float text_scale;
    void (*on_change)(AromaNode*, int, void*);
    void* user_data;
    bool visibility_dirty;  
    int transition_type;
    uint32_t transition_duration;
    int prev_selected_index;
};

static void __tabs_request_redraw(void* user_data)
{
    if (!user_data) return;
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

static int __tabs_index_from_x(AromaTabs* tabs, int x)
{
    if (!tabs || tabs->count <= 0) return -1;
    if (x < tabs->rect.x || x >= (tabs->rect.x + tabs->rect.width)) return -1;

    int base_width = tabs->rect.width / tabs->count;
    if (base_width <= 0) base_width = 1;  

    int start_x = tabs->rect.x;
    for (int i = 0; i < tabs->count; i++) {
        int w = (i == tabs->count - 1)
            ? (tabs->rect.x + tabs->rect.width - start_x)
            : base_width;
        
        
        if (w <= 0) w = 1;
        
        if (x < start_x + w) {
            return i;
        }
        start_x += w;
    }

    return -1;  
}

static void __tabs_set_hidden_recursive(AromaNode* node, bool hidden)
{
    if (!node) return;
    aroma_node_set_hidden(node, hidden);
    for (uint64_t i = 0; i < node->child_count; i++) {
        if (node->child_nodes[i]) {
            __tabs_set_hidden_recursive(node->child_nodes[i], hidden);
        }
    }
}

static void __tabs_update_content_visibility(AromaTabs* tabs)
{
    if (!tabs) return;
    
    tabs->visibility_dirty = false;  
    
    for (int i = 0; i < tabs->count; i++) {
        bool hide = (i != tabs->selected_index);
        for (int j = 0; j < tabs->content_counts[i]; j++) {
            AromaNode* content = tabs->content_nodes[i][j];
            if (!content) continue;
            
            /* Only toggle the root node — hiding a parent is enough
             * to prevent collect_draw_tasks from traversing it.
             * Do NOT recurse, because children may have their own
             * visibility managed by other widgets (e.g. sidebar). */
            aroma_node_set_hidden(content, hide);
            
            
            if (!hide) {
                
                if (content->node_widget_ptr) {
                    AromaRect* content_rect = (AromaRect*)content->node_widget_ptr;
                    
                    
                    
                    aroma_node_update_layout(content, 
                                           content_rect->x, 
                                           content_rect->y, 
                                           content_rect->width, 
                                           content_rect->height);
                                           
                    if (tabs->transition_type != 0 && tabs->transition_duration > 0 && tabs->prev_selected_index != tabs->selected_index) {
                        int offset = (tabs->selected_index > tabs->prev_selected_index) ? 200 : -200;
                        if (tabs->transition_type == AROMA_ANIM_SLIDE_X) {
                            aroma_animation_start(content, AROMA_ANIM_SLIDE_X, content_rect->x + offset, content_rect->x, tabs->transition_duration);
                        } else if (tabs->transition_type == AROMA_ANIM_SLIDE_Y) {
                            aroma_animation_start(content, AROMA_ANIM_SLIDE_Y, content_rect->y + offset, content_rect->y, tabs->transition_duration);
                        } else if (tabs->transition_type == AROMA_ANIM_FADE) {
                            aroma_animation_start(content, AROMA_ANIM_FADE, 0.0f, 1.0f, tabs->transition_duration);
                        }
                    }
                }
            }
            
            aroma_node_invalidate(content);
        }
    }
}
static bool __tabs_handle_event(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;
    AromaTabs* tabs = (AromaTabs*)event->target_node->node_widget_ptr;
    if (!tabs) return false;

    bool in_bounds = (event->data.mouse.x >= tabs->rect.x && 
                      event->data.mouse.x <= tabs->rect.x + tabs->rect.width &&
                      event->data.mouse.y >= tabs->rect.y && 
                      event->data.mouse.y <= tabs->rect.y + tabs->rect.height);

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_MOVE: {
            return in_bounds;
        }
        case EVENT_TYPE_MOUSE_EXIT:
            return false;
        case EVENT_TYPE_MOUSE_CLICK:
            if (in_bounds) {
                int index = __tabs_index_from_x(tabs, event->data.mouse.x);
                if (index >= 0 && index < tabs->count && index != tabs->selected_index) {
                    tabs->prev_selected_index = tabs->selected_index;
                    tabs->selected_index = index;
                    tabs->visibility_dirty = true;  
                    __tabs_update_content_visibility(tabs);
                    if (tabs->on_change) {
                        tabs->on_change(event->target_node, index, tabs->user_data);
                    }
                    aroma_node_invalidate(event->target_node);
                    __tabs_request_redraw(user_data);
                }
                return true;
            }
            break;
        default:
            break;
    }

    return false;
}

AromaNode* aroma_tabs_create(AromaNode* parent, int x, int y, int width, int height,
                             const char** labels, int count)
{
    if (!parent || !labels || count <= 0) return NULL;

    AromaTabs* tabs = (AromaTabs*)aroma_widget_alloc(sizeof(AromaTabs));
    if (!tabs) return NULL;

    memset(tabs, 0, sizeof(AromaTabs));
    tabs->rect.x = x;
    tabs->rect.y = y;
    tabs->rect.width = width;
    tabs->rect.height = height;
    tabs->count = (count > AROMA_TABS_MAX) ? AROMA_TABS_MAX : count;
    tabs->selected_index = 0;
    tabs->hovered_index = -1;
    tabs->visibility_dirty = true;

    AromaTheme theme = aroma_theme_get_global();
    tabs->bg_color = theme.colors.surface;
    tabs->selected_color = theme.colors.primary;
    tabs->text_color = theme.colors.text_primary;
    tabs->text_selected_color = theme.colors.text_primary;
    tabs->hover_overlay_color = aroma_color_blend(tabs->bg_color, tabs->selected_color, 0.12f);
    tabs->use_theme_color = true;
    tabs->indicator_height = 3;
    tabs->indicator_padding = 8;
    tabs->corner_radius = 0.0f;
    tabs->text_scale = 1.0f;
    
    
    tabs->rect.x = x;
    tabs->rect.y = y;
    tabs->rect.width = width;
    tabs->rect.height = height;
    
    for (int i = 0; i < tabs->count; i++) {
        if (labels[i]) {
            strncpy(tabs->labels[i], labels[i], AROMA_TAB_LABEL_MAX - 1);
            tabs->labels[i][AROMA_TAB_LABEL_MAX - 1] = '\0';
        } else {
            tabs->labels[i][0] = '\0';
        }
        
        
        tabs->icons[i][0] = '\0';
        
        
        tabs->content_counts[i] = 0;
        for (int j = 0; j < AROMA_TABS_CONTENT_MAX; j++) {
            tabs->content_nodes[i][j] = NULL;
        }
    }

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, tabs);
    if (!node) {
        aroma_widget_free(tabs);
        return NULL;
    }


    aroma_node_set_draw_cb(node, aroma_tabs_draw);

    
    if (!tabs->font) {
        AromaNode* root_node = parent;
        while (root_node && root_node->parent_node) {
            root_node = root_node->parent_node;
        }
        if (root_node && root_node->node_widget_ptr) {
            struct AromaWindow* window_data = (struct AromaWindow*)root_node->node_widget_ptr;
            for (int i = 0; i < g_window_count; ++i) {
                if (g_windows[i].is_active && g_windows[i].window_id == window_data->window_id) {
                    if (g_windows[i].default_font) {
                        tabs->font = g_windows[i].default_font;
                    }
                    break;
                }
            }
        }
    }

    
    __tabs_update_content_visibility(tabs);
    aroma_node_invalidate(node);

    return node;
}

void aroma_tabs_set_selected(AromaNode* tabs_node, int index)
{
    if (!tabs_node || !tabs_node->node_widget_ptr) return;
    AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;
    
    
    if (index < 0 || index >= tabs->count || index >= AROMA_TABS_MAX) return;
    
    if (tabs->selected_index != index) {
        tabs->prev_selected_index = tabs->selected_index;
        tabs->selected_index = index;
        tabs->visibility_dirty = true;
        __tabs_update_content_visibility(tabs);
        aroma_node_invalidate(tabs_node);
    }
}

int aroma_tabs_get_selected(AromaNode* tabs_node)
{
    if (!tabs_node || !tabs_node->node_widget_ptr) return -1;
    AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;
    return tabs->selected_index;
}

void aroma_tabs_set_on_change(AromaNode* tabs_node,
                              void (*callback)(AromaNode*, int, void*),
                              void* user_data)
{
    if (!tabs_node || !tabs_node->node_widget_ptr) return;
    AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;
    tabs->on_change = callback;
    tabs->user_data = user_data;
}

void aroma_tabs_set_font(AromaNode* tabs_node, AromaFont* font)
{
    if (!tabs_node || !tabs_node->node_widget_ptr) return;
    AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;
    tabs->font = font;
    aroma_node_invalidate(tabs_node);
}
void aroma_tabs_set_content(AromaNode* tabs_node, int index, AromaNode** content_nodes, int content_count)
{
    if (!tabs_node || !tabs_node->node_widget_ptr) return;
    AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;
    
    if (index < 0 || index >= tabs->count || index >= AROMA_TABS_MAX) return;
    
    
    for (int j = 0; j < tabs->content_counts[index]; j++) {
        AromaNode* old_content = tabs->content_nodes[index][j];
        if (old_content) {
            aroma_node_set_hidden(old_content, true);
        }
    }
    
    int max_count = (content_count > AROMA_TABS_CONTENT_MAX) ? AROMA_TABS_CONTENT_MAX : content_count;
    tabs->content_counts[index] = 0;
    
    for (int i = 0; i < max_count; i++) {
        tabs->content_nodes[index][i] = content_nodes ? content_nodes[i] : NULL;
        if (tabs->content_nodes[index][i]) {
            tabs->content_counts[index]++;
        }
    }
    
    for (int i = max_count; i < AROMA_TABS_CONTENT_MAX; i++) {
        tabs->content_nodes[index][i] = NULL;
    }
    
    tabs->visibility_dirty = true;
    
    
    for (int i = 0; i < tabs->count; i++) {
        bool hide = (i != tabs->selected_index);
        for (int j = 0; j < tabs->content_counts[i]; j++) {
            AromaNode* content = tabs->content_nodes[i][j];
            if (!content) continue;
            
            aroma_node_set_hidden(content, hide);
            aroma_node_invalidate(content);
        }
    }
    
    aroma_node_invalidate(tabs_node);
}
void aroma_tabs_set_icon(AromaNode* tabs_node, int index, const char* icon_code, AromaFont* icon_font) 
{
    if (!tabs_node || !tabs_node->node_widget_ptr) return;
    AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;
    
    
    if (index < 0 || index >= tabs->count || index >= AROMA_TABS_MAX) return;
    
    if (icon_code) {
        strncpy(tabs->icons[index], icon_code, 7);
        tabs->icons[index][7] = '\0';
    } else {
        tabs->icons[index][0] = '\0';
    }
    
    if (icon_font) {
        tabs->icon_font = icon_font;
    }
    
    aroma_node_invalidate(tabs_node);
}

bool aroma_tabs_setup_events(AromaNode* tabs_node, void (*on_redraw_callback)(void*), void* user_data)
{
    (void)user_data;
    if (!tabs_node) return false;
    
    
    
    
    aroma_event_subscribe(tabs_node->node_id, EVENT_TYPE_MOUSE_MOVE, 
                         __tabs_handle_event, (void*)on_redraw_callback, 80);
    aroma_event_subscribe(tabs_node->node_id, EVENT_TYPE_MOUSE_EXIT, 
                         __tabs_handle_event, (void*)on_redraw_callback, 80);
    aroma_event_subscribe(tabs_node->node_id, EVENT_TYPE_MOUSE_CLICK, 
                         __tabs_handle_event, (void*)on_redraw_callback, 90);
    return true;
}

void aroma_tabs_draw(AromaNode* tabs_node, size_t window_id)
{
    if (!tabs_node || !tabs_node->node_widget_ptr) return;
    if (aroma_node_is_hidden(tabs_node)) return;
    
    AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;
    
    
    if (tabs->count <= 0 || tabs->count > AROMA_TABS_MAX) return;
    
    #ifndef ESP32
    
    if (!tabs->font) {
        for (int i = 0; i < g_window_count; ++i) {
            if (i >= AROMA_MAX_WINDOWS) break;  
            if (g_windows[i].is_active && g_windows[i].window_id == window_id && g_windows[i].default_font) {
                tabs->font = g_windows[i].default_font;
                break;
            }
        }
    }
    #endif

    
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle || !gfx->render_text) return;

    
    if (tabs->use_theme_color) {
        AromaTheme theme = aroma_theme_get_global();
        tabs->bg_color = theme.colors.surface;
        tabs->selected_color = theme.colors.primary;
        tabs->text_color = theme.colors.text_primary;
        tabs->text_selected_color = theme.colors.text_primary;
        tabs->hover_overlay_color = aroma_color_blend(tabs->bg_color, tabs->selected_color, 0.12f);
    }

    
    gfx->fill_rectangle(window_id, tabs->rect.x, tabs->rect.y, tabs->rect.width,
                        tabs->rect.height, tabs->bg_color, false, tabs->corner_radius);

    
    int tab_width = tabs->rect.width / tabs->count;
    if (tab_width <= 0) tab_width = 1;  
    
    int indicator_height = tabs->indicator_height;
    int indicator_y = tabs->rect.y + tabs->rect.height - indicator_height;

    
    for (int i = 0; i < tabs->count; i++) {
        int x = tabs->rect.x + i * tab_width;
        int w = (i == tabs->count - 1) ? (tabs->rect.x + tabs->rect.width - x) : tab_width;
        
        
        if (w <= 0) w = 1;
        
        bool selected = (i == tabs->selected_index);
        bool hovered = (i == tabs->hovered_index);

        
        if (hovered && !selected) {
            gfx->fill_rectangle(window_id, x, tabs->rect.y, w, tabs->rect.height, 
                               tabs->hover_overlay_color, false, tabs->corner_radius);
        }

        
        if (selected) {
            int indicator_x = x + tabs->indicator_padding;
            int indicator_width = w - (tabs->indicator_padding * 2);
            if (indicator_width > 0) {
                gfx->fill_rectangle(window_id, indicator_x, indicator_y, indicator_width, 
                                   indicator_height, tabs->selected_color, false, tabs->corner_radius);
            }
        }

        
        if (tabs->font && gfx->render_text) {
            uint32_t text_color = selected ? tabs->text_selected_color : tabs->text_color;
            
            int text_w = 0;
            int icon_w = 0;
            float icon_scale = 1.0f;
            
            
            if (tabs->labels[i][0] != '\0') {
                text_w = aroma_font_get_line_width(tabs->font, tabs->labels[i]) * tabs->text_scale;
            }

            
            if (tabs->icons[i][0] != '\0' && tabs->icon_font) {
                int default_icon_h = aroma_font_get_line_height(tabs->icon_font);
                if (default_icon_h > 0) {
                    
                    int target_h = tabs->rect.height * 0.5f;
                    
                    if (target_h > w * 0.8f) target_h = w * 0.8f;
                    
                    icon_scale = (float)target_h / (float)default_icon_h;
                    icon_w = aroma_font_get_px_size(tabs->icon_font) * icon_scale;
                }
            }

            
            int gap = (icon_w > 0 && text_w > 0) ? AROMA_TABS_GAP : 0;
            int total_w = icon_w + gap + text_w;
            int start_x = x + (w - total_w) / 2;
            int center_y = tabs->rect.y + (tabs->rect.height / 2);

            
            if (icon_w > 0 && tabs->icon_font) {
                int icon_line_h = aroma_font_get_line_height(tabs->icon_font) * icon_scale;
                int icon_y = center_y - (icon_line_h / 2); 
                
                gfx->render_text(window_id, tabs->icon_font, tabs->icons[i], 
                               start_x, icon_y, text_color, icon_scale);
                
                start_x += icon_w + gap;
            }
            
            
            if (text_w > 0) {
                int line_height = aroma_font_get_line_height(tabs->font) * tabs->text_scale;
                int text_y = center_y - (line_height / 2);
                
                gfx->render_text(window_id, tabs->font, tabs->labels[i], 
                               start_x, text_y, text_color, tabs->text_scale);
            }
        }
    }
}

void aroma_tabs_destroy(AromaNode* tabs_node)
{
    if (!tabs_node) return;
    
    if (tabs_node->node_widget_ptr) {
        AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;
        
        
        for (int i = 0; i < tabs->count && i < AROMA_TABS_MAX; i++) {
            for (int j = 0; j < tabs->content_counts[i] && j < AROMA_TABS_CONTENT_MAX; j++) {
                tabs->content_nodes[i][j] = NULL;
            }
            tabs->content_counts[i] = 0;
        }
        
        
        
        
        aroma_widget_free(tabs);
        tabs_node->node_widget_ptr = NULL;
    }
}

void aroma_tabs_set_transition(AromaNode* tabs_node, int type, uint32_t duration_ms)
{
    if (!tabs_node || !tabs_node->node_widget_ptr) return;
    AromaTabs* tabs = (AromaTabs*)tabs_node->node_widget_ptr;
    tabs->transition_type = type;
    tabs->transition_duration = duration_ms;
}
