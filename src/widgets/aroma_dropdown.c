#include "widgets/aroma_dropdown.h"
#include "aroma_node.h"
#include "aroma_logger.h"
#include "aroma_slab_alloc.h"
#include "aroma_event.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <stdlib.h>
#include <string.h>

#define AROMA_MAX_DROPDOWN_OVERLAYS 32

static void __dropdown_request_redraw(void* user_data)
{
    if (!user_data) {
        return;
    }
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

typedef struct {
    AromaNode* node;
    size_t window_id;
    int x;
    int y;
    int width;
    int height;
} DropdownOverlayEntry;

static DropdownOverlayEntry g_dropdown_overlays[AROMA_MAX_DROPDOWN_OVERLAYS];
static size_t g_dropdown_overlay_count = 0;

static void __dropdown_register_overlay(AromaNode* node, size_t window_id,
                                        int x, int y, int width, int height) {
    if (!node) return;

    for (size_t i = 0; i < g_dropdown_overlay_count; ++i) {
        if (g_dropdown_overlays[i].node == node) {
            g_dropdown_overlays[i].window_id = window_id;
            g_dropdown_overlays[i].x = x;
            g_dropdown_overlays[i].y = y;
            g_dropdown_overlays[i].width = width;
            g_dropdown_overlays[i].height = height;
            return;
        }
    }

    if (g_dropdown_overlay_count >= AROMA_MAX_DROPDOWN_OVERLAYS) {
        LOG_WARNING("Maximum dropdown overlays reached; additional overlays will be skipped");
        return;
    }

    g_dropdown_overlays[g_dropdown_overlay_count].node = node;
    g_dropdown_overlays[g_dropdown_overlay_count].window_id = window_id;
    g_dropdown_overlays[g_dropdown_overlay_count].x = x;
    g_dropdown_overlays[g_dropdown_overlay_count].y = y;
    g_dropdown_overlays[g_dropdown_overlay_count].width = width;
    g_dropdown_overlays[g_dropdown_overlay_count].height = height;
    g_dropdown_overlay_count++;
}

static void __dropdown_unregister_overlay(AromaNode* node) {
    if (!node) return;

    for (size_t i = 0; i < g_dropdown_overlay_count; ++i) {
        if (g_dropdown_overlays[i].node == node) {
            g_dropdown_overlays[i] = g_dropdown_overlays[g_dropdown_overlay_count - 1];
            g_dropdown_overlay_count--;
            return;
        }
    }
}

AromaNode* aroma_dropdown_create(AromaNode* parent, int x, int y, int width, int height) {
    if (!parent || width <= 0 || height <= 0) {
        LOG_ERROR("Invalid dropdown parameters");
        return NULL;
    }

    AromaDropdown* dd = (AromaDropdown*)aroma_widget_alloc(sizeof(AromaDropdown));
    if (!dd) {
        LOG_ERROR("Failed to allocate dropdown");
        return NULL;
    }

    dd->rect.x = x;
    dd->rect.y = y;
    dd->rect.width = width;
    dd->rect.height = height;

    dd->options = (char**)malloc(sizeof(char*) * AROMA_DROPDOWN_MAX_OPTIONS);
    if (!dd->options) {
        aroma_widget_free(dd);
        return NULL;
    }

    dd->option_count = 0;
    dd->selected_index = -1;
    dd->hover_index = -1;
    dd->is_expanded = false;
    dd->is_hovered = false;
    dd->on_selection_changed = NULL;
    dd->user_data = NULL;
    dd->bridge.node = NULL;
    dd->bridge.handler = NULL;
    dd->bridge.user_data = NULL;
    dd->font = NULL;
    dd->text_color = 0x333333;
    dd->list_bg_color = 0xFFFFFF;
    dd->hover_bg_color = 0xEFF6FF;
    dd->selected_bg_color = 0xD0E7FF;

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, dd);
    if (!node) {
        free(dd->options);
        aroma_widget_free(dd);
        LOG_ERROR("Failed to create dropdown node");
        return NULL;
    }

    LOG_INFO("Dropdown created at (%d, %d) size %dx%d", x, y, width, height);
    return node;
}

void aroma_dropdown_add_option(AromaNode* dropdown_node, const char* option) {
    if (!dropdown_node || !option) return;

    AromaDropdown* dd = (AromaDropdown*)dropdown_node->node_widget_ptr;
    if (!dd || dd->option_count >= AROMA_DROPDOWN_MAX_OPTIONS) return;

    dd->options[dd->option_count] = (char*)malloc(AROMA_DROPDOWN_OPTION_MAX);
    if (!dd->options[dd->option_count]) return;

    strncpy(dd->options[dd->option_count], option, AROMA_DROPDOWN_OPTION_MAX - 1);
    dd->options[dd->option_count][AROMA_DROPDOWN_OPTION_MAX - 1] = '\0';

    if (dd->option_count == 0) {
        dd->selected_index = 0;
    }

    dd->option_count++;
}

void aroma_dropdown_set_on_change(AromaNode* dropdown_node, 
    void (*on_change)(int index, const char* option, void* user_data), 
    void* user_data) {
    if (!dropdown_node) return;

    AromaDropdown* dd = (AromaDropdown*)dropdown_node->node_widget_ptr;
    if (dd) {
        dd->on_selection_changed = on_change;
        dd->user_data = user_data;
    }
}

static bool __dropdown_default_mouse_handler(AromaEvent* event, void* user_data) {
    if (!event || !event->target_node) return false;
    AromaDropdown* dd = (AromaDropdown*)event->target_node->node_widget_ptr;
    if (!dd) return false;

    int option_height = dd->rect.height;
    bool in_main = (event->data.mouse.x >= dd->rect.x && event->data.mouse.x <= dd->rect.x + dd->rect.width &&
                    event->data.mouse.y >= dd->rect.y && event->data.mouse.y <= dd->rect.y + dd->rect.height);
    bool in_list = false;
    int clicked_index = -1;
    if (dd->is_expanded && dd->option_count > 0) {
        int list_top = dd->rect.y + dd->rect.height;
        int list_bottom = list_top + option_height * dd->option_count;
        in_list = (event->data.mouse.x >= dd->rect.x && event->data.mouse.x <= dd->rect.x + dd->rect.width &&
                   event->data.mouse.y >= list_top && event->data.mouse.y <= list_bottom);
        if (in_list) {
            clicked_index = (event->data.mouse.y - list_top) / option_height;
            if (clicked_index < 0 || clicked_index >= dd->option_count) clicked_index = -1;
        }
    }

    if (event->event_type == EVENT_TYPE_MOUSE_MOVE) {
        int previous_hover_index = dd->hover_index;
        if (in_list && clicked_index >= 0) {
            dd->hover_index = clicked_index;
        } else {
            dd->hover_index = -1;
        }

        bool hover = in_main || in_list;
        bool state_changed = (hover != dd->is_hovered) || (previous_hover_index != dd->hover_index);
        if (hover != dd->is_hovered) {
            dd->is_hovered = hover;
        }

        if (state_changed && user_data) {
            aroma_node_invalidate(event->target_node);
            __dropdown_request_redraw(user_data);
        }

        return hover;
    }

    if (event->event_type == EVENT_TYPE_MOUSE_CLICK) {
        bool consumed = false;
        if (in_main) {
            dd->is_expanded = !dd->is_expanded;
            if (!dd->is_expanded) {
                dd->hover_index = -1;
                __dropdown_unregister_overlay(event->target_node);
            }
            consumed = true;
        } else if (in_list && clicked_index >= 0) {
            dd->selected_index = clicked_index;
            dd->is_expanded = false;
            dd->hover_index = -1;
            consumed = true;
            if (dd->on_selection_changed) {
                dd->on_selection_changed(clicked_index, dd->options[clicked_index], dd->user_data);
            }
            __dropdown_unregister_overlay(event->target_node);
        }
        if (consumed && user_data) {
            aroma_node_invalidate(event->target_node);
            __dropdown_request_redraw(user_data);
        }
        return consumed;
    }

    if (event->event_type == EVENT_TYPE_MOUSE_EXIT) {
        bool changed = dd->is_hovered || dd->hover_index != -1;
        dd->is_hovered = false;
        dd->hover_index = -1;
        if (changed && user_data) {
            aroma_node_invalidate(event->target_node);
            __dropdown_request_redraw(user_data);
        }
        return false;
    }

    if (event->event_type == EVENT_TYPE_MOUSE_ENTER) {
        if (!dd->is_hovered) {
            dd->is_hovered = true;
            aroma_node_invalidate(event->target_node);
        }
        __dropdown_request_redraw(user_data);
        return true;
    }

    return false;
}

void aroma_dropdown_setup_events(AromaNode* dropdown_node, void (*on_redraw_callback)(void*), void* user_data) {
    if (!dropdown_node) return;
    aroma_event_subscribe(dropdown_node->node_id, EVENT_TYPE_MOUSE_MOVE, __dropdown_default_mouse_handler, (void*)on_redraw_callback, 50);
    aroma_event_subscribe(dropdown_node->node_id, EVENT_TYPE_MOUSE_CLICK, __dropdown_default_mouse_handler, (void*)on_redraw_callback, 50);
    aroma_event_subscribe(dropdown_node->node_id, EVENT_TYPE_MOUSE_EXIT, __dropdown_default_mouse_handler, (void*)on_redraw_callback, 50);
    aroma_event_subscribe(dropdown_node->node_id, EVENT_TYPE_MOUSE_ENTER, __dropdown_default_mouse_handler, (void*)on_redraw_callback, 50);
}

void aroma_dropdown_draw(AromaNode* dropdown_node, size_t window_id) {
    if (!dropdown_node || !dropdown_node->node_widget_ptr) {
        return;
    }

    AromaDropdown* dd = (AromaDropdown*)dropdown_node->node_widget_ptr;
    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    uint32_t base_bg_color = dd->is_hovered ? dd->hover_bg_color : dd->list_bg_color;
    uint32_t border_color = 0x222222;

    gfx->fill_rectangle(window_id, dd->rect.x, dd->rect.y, dd->rect.width, dd->rect.height, base_bg_color, true, 3.0f);
    gfx->draw_hollow_rectangle(window_id, dd->rect.x, dd->rect.y, dd->rect.width, dd->rect.height, border_color, 1.0f, true, 3.0f);

    if (dd->font && dd->selected_index >= 0 && dd->selected_index < dd->option_count && dd->options[dd->selected_index]) {
        int text_x = dd->rect.x + 10;
        int baseline = dd->rect.y + (dd->rect.height / 2) + (aroma_font_get_ascender(dd->font) / 2);
        gfx->render_text(window_id, dd->font, dd->options[dd->selected_index], text_x, baseline, dd->text_color);
    }

    if (dd->is_expanded && dd->option_count > 0) {
        int option_height = dd->rect.height;
        int list_x = dd->rect.x;
        int list_y = dd->rect.y + dd->rect.height;
        int list_height = option_height * dd->option_count;
        __dropdown_register_overlay(dropdown_node, window_id, list_x, list_y, dd->rect.width, list_height);
    } else {
        __dropdown_unregister_overlay(dropdown_node);
    }
}

void aroma_dropdown_render_overlays(size_t window_id) {
    if (g_dropdown_overlay_count == 0) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;

    for (size_t i = 0; i < g_dropdown_overlay_count;) {
        DropdownOverlayEntry entry = g_dropdown_overlays[i];
        AromaNode* node = entry.node;
        if (!node) {
            g_dropdown_overlays[i] = g_dropdown_overlays[g_dropdown_overlay_count - 1];
            g_dropdown_overlay_count--;
            continue;
        }

        if (entry.window_id != window_id) {
            ++i;
            continue;
        }

        AromaDropdown* dd = (AromaDropdown*)node->node_widget_ptr;
        if (!dd || !dd->is_expanded || dd->option_count <= 0) {
            g_dropdown_overlays[i] = g_dropdown_overlays[g_dropdown_overlay_count - 1];
            g_dropdown_overlay_count--;
            continue;
        }

        int option_height = dd->rect.height;
        int list_x = entry.x;
        int list_y = entry.y;
        int list_width = entry.width;
        int list_height = entry.height;

        gfx->fill_rectangle(window_id, list_x, list_y, list_width, list_height, dd->list_bg_color, true, 0.0f);
        gfx->draw_hollow_rectangle(window_id, list_x, list_y, list_width, list_height, 0x222222, 1.0f, true, 0.0f);

        for (int opt = 0; opt < dd->option_count; ++opt) {
            int y = list_y + opt * option_height;
            uint32_t row_color = dd->list_bg_color;
            if (opt == dd->selected_index) {
                row_color = dd->selected_bg_color;
            } else if (opt == dd->hover_index) {
                row_color = dd->hover_bg_color;
            }

            if (row_color != dd->list_bg_color) {
                gfx->fill_rectangle(window_id, list_x, y, list_width, option_height, row_color, true, 0.0f);
            }

            gfx->draw_hollow_rectangle(window_id, list_x, y, list_width, option_height, 0xDDDDDD, 1.0f, true, 0.0f);

            if (dd->font && dd->options[opt]) {
                int text_x = list_x + 10;
                int baseline = y + (option_height / 2) + (aroma_font_get_ascender(dd->font) / 2);
                gfx->render_text(window_id, dd->font, dd->options[opt], text_x, baseline, dd->text_color);
            }
        }

        ++i;
    }
}

bool aroma_dropdown_overlay_hit_test(int x, int y, AromaNode** out_node) {
    for (size_t i = 0; i < g_dropdown_overlay_count; ++i) {
        DropdownOverlayEntry* entry = &g_dropdown_overlays[i];
        if (!entry->node) {
            continue;
        }
        if (x >= entry->x && x < (entry->x + entry->width) &&
            y >= entry->y && y < (entry->y + entry->height)) {
            if (out_node) {
                *out_node = entry->node;
            }
            return true;
        }
    }
    return false;
}

void aroma_dropdown_set_font(AromaNode* dropdown_node, AromaFont* font) {
    if (!dropdown_node) return;
    AromaDropdown* dd = (AromaDropdown*)dropdown_node->node_widget_ptr;
    if (!dd) return;
    dd->font = font;
    aroma_node_invalidate(dropdown_node);
}

void aroma_dropdown_set_text_color(AromaNode* dropdown_node, uint32_t text_color) {
    if (!dropdown_node) return;
    AromaDropdown* dd = (AromaDropdown*)dropdown_node->node_widget_ptr;
    if (!dd) return;
    dd->text_color = text_color;
    aroma_node_invalidate(dropdown_node);
}

void aroma_dropdown_destroy(AromaNode* dropdown_node) {
    if (!dropdown_node) return;

    AromaDropdown* dd = (AromaDropdown*)dropdown_node->node_widget_ptr;
    if (dd) {
        __dropdown_unregister_overlay(dropdown_node);
        if (dd->options) {
            for (int i = 0; i < dd->option_count; i++) {
                if (dd->options[i]) {
                    free(dd->options[i]);
                }
            }
            free(dd->options);
        }
        aroma_widget_free(dd);
    }

    __destroy_node(dropdown_node);
}

