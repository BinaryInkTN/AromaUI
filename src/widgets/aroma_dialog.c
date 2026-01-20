#include "widgets/aroma_dialog.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "aroma_ui.h"
#include "core/aroma_event.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>

#define AROMA_DIALOG_ACTION_MAX 3

typedef struct {
    char label[32];
    void (*callback)(void* user_data);
    void* user_data;
} AromaDialogAction;

typedef struct AromaDialog {
    AromaRect rect;
    char title[64];
    char message[256];
    AromaDialogType type;
    bool visible;
    AromaDialogAction actions[AROMA_DIALOG_ACTION_MAX];
    size_t action_count;
    AromaFont* font;
} AromaDialog;

static void __dialog_request_redraw(void* user_data)
{
    if (!user_data) return;
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

static void __dialog_layout_actions(AromaDialog* dlg, AromaGraphicsInterface* gfx, size_t window_id,
                                    int* out_start_x, int* out_y, int* out_height, int* out_spacing)
{
    int padding = 16;
    int button_height = 32;
    int spacing = 8;
    int right = dlg->rect.x + dlg->rect.width - padding;
    int y = dlg->rect.y + dlg->rect.height - padding - button_height;

    int total_width = 0;
    for (size_t i = 0; i < dlg->action_count; i++) {
        int text_w = 48;
        if (gfx && gfx->measure_text && dlg->font) {
            text_w = (int)gfx->measure_text(window_id, dlg->font, dlg->actions[i].label);
        }
        int button_w = text_w + 24;
        if (button_w < 64) button_w = 64;
        total_width += button_w;
        if (i + 1 < dlg->action_count) total_width += spacing;
    }

    int start_x = right - total_width;
    if (out_start_x) *out_start_x = start_x;
    if (out_y) *out_y = y;
    if (out_height) *out_height = button_height;
    if (out_spacing) *out_spacing = spacing;
}

static bool __dialog_handle_event(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;
    AromaDialog* dlg = (AromaDialog*)event->target_node->node_widget_ptr;
    if (!dlg || !dlg->visible) return false;

    if (event->event_type != EVENT_TYPE_MOUSE_RELEASE) return false;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    int start_x = 0, y = 0, height = 0, spacing = 8;
    __dialog_layout_actions(dlg, gfx, 0, &start_x, &y, &height, &spacing);

    int x = start_x;
    for (size_t i = 0; i < dlg->action_count; i++) {
        int text_w = 48;
        if (gfx && gfx->measure_text && dlg->font) {
            text_w = (int)gfx->measure_text(0, dlg->font, dlg->actions[i].label);
        }
        int button_w = text_w + 24;
        if (button_w < 64) button_w = 64;

        bool hit = (event->data.mouse.x >= x && event->data.mouse.x <= x + button_w &&
                    event->data.mouse.y >= y && event->data.mouse.y <= y + height);
        if (hit) {
            if (dlg->actions[i].callback) {
                dlg->actions[i].callback(dlg->actions[i].user_data);
            }
            dlg->visible = false;
            aroma_node_invalidate(event->target_node);
            __dialog_request_redraw(user_data);
            return true;
        }
        x += button_w + spacing;
    }

    bool in_bounds = (event->data.mouse.x >= dlg->rect.x && event->data.mouse.x <= dlg->rect.x + dlg->rect.width &&
                      event->data.mouse.y >= dlg->rect.y && event->data.mouse.y <= dlg->rect.y + dlg->rect.height);
    return in_bounds;
}

AromaNode* aroma_dialog_create(AromaNode* parent, const char* title, const char* message, int width, int height, AromaDialogType type)
{
    if (!parent || width <= 0 || height <= 0) return NULL;
    AromaDialog* dlg = (AromaDialog*)aroma_widget_alloc(sizeof(AromaDialog));
    if (!dlg) return NULL;

    memset(dlg, 0, sizeof(AromaDialog));
    dlg->rect.x = 0;
    dlg->rect.y = 0;
    dlg->rect.width = width;
    dlg->rect.height = height;
    dlg->type = type;
    dlg->visible = false;
    dlg->action_count = 0;
    dlg->font = NULL;
    if (title) strncpy(dlg->title, title, sizeof(dlg->title) - 1);
    if (message) strncpy(dlg->message, message, sizeof(dlg->message) - 1);

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, dlg);
    if (!node) {
        aroma_widget_free(dlg);
        return NULL;
    }

    aroma_node_set_draw_cb(node, aroma_dialog_draw);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, __dialog_handle_event, aroma_ui_request_redraw, 100);
    return node;
}

void aroma_dialog_add_action(AromaNode* dialog_node, const char* label, void (*callback)(void*), void* user_data)
{
    if (!dialog_node || !dialog_node->node_widget_ptr || !label) return;
    AromaDialog* dlg = (AromaDialog*)dialog_node->node_widget_ptr;
    if (dlg->action_count >= AROMA_DIALOG_ACTION_MAX) return;
    AromaDialogAction* act = &dlg->actions[dlg->action_count++];
    strncpy(act->label, label, sizeof(act->label) - 1);
    act->callback = callback;
    act->user_data = user_data;
}

void aroma_dialog_show(AromaNode* dialog_node)
{
    if (!dialog_node || !dialog_node->node_widget_ptr) return;
    AromaDialog* dlg = (AromaDialog*)dialog_node->node_widget_ptr;
    dlg->visible = true;
    aroma_node_invalidate(dialog_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_dialog_hide(AromaNode* dialog_node)
{
    if (!dialog_node || !dialog_node->node_widget_ptr) return;
    AromaDialog* dlg = (AromaDialog*)dialog_node->node_widget_ptr;
    dlg->visible = false;
    aroma_node_invalidate(dialog_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_dialog_set_font(AromaNode* dialog_node, AromaFont* font)
{
    if (!dialog_node || !dialog_node->node_widget_ptr) return;
    AromaDialog* dlg = (AromaDialog*)dialog_node->node_widget_ptr;
    dlg->font = font;
}

void aroma_dialog_draw(AromaNode* dialog_node, size_t window_id)
{
    if (!dialog_node || !dialog_node->node_widget_ptr) return;
    AromaDialog* dlg = (AromaDialog*)dialog_node->node_widget_ptr;
    if (!dlg->visible) return;

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;
    if (aroma_node_is_hidden(dialog_node)) return;
    AromaTheme theme = aroma_theme_get_global();

    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    if (platform && platform->get_window_size) {
        int win_w = 0, win_h = 0;
        platform->get_window_size(window_id, &win_w, &win_h);
        if (win_w > 0 && win_h > 0) {
            dlg->rect.x = (win_w - dlg->rect.width) / 2;
            dlg->rect.y = (win_h - dlg->rect.height) / 2;
        }
    }

    gfx->fill_rectangle(window_id, dlg->rect.x, dlg->rect.y, dlg->rect.width, dlg->rect.height,
                        theme.colors.surface, true, 12.0f);
    gfx->draw_hollow_rectangle(window_id, dlg->rect.x, dlg->rect.y, dlg->rect.width, dlg->rect.height,
                               theme.colors.border, 1, true, 12.0f);

    if (dlg->font && gfx->render_text) {
        gfx->render_text(window_id, dlg->font, dlg->title, dlg->rect.x + 16, dlg->rect.y + 24, theme.colors.text_primary);
        gfx->render_text(window_id, dlg->font, dlg->message, dlg->rect.x + 16, dlg->rect.y + 52, theme.colors.text_secondary);
    }

    if (dlg->action_count > 0) {
        int start_x = 0, y = 0, height = 0, spacing = 8;
        __dialog_layout_actions(dlg, gfx, window_id, &start_x, &y, &height, &spacing);
        int x = start_x;
        for (size_t i = 0; i < dlg->action_count; i++) {
            int text_w = 48;
            if (gfx->measure_text && dlg->font) {
                text_w = (int)gfx->measure_text(window_id, dlg->font, dlg->actions[i].label);
            }
            int button_w = text_w + 24;
            if (button_w < 64) button_w = 64;

            gfx->fill_rectangle(window_id, x, y, button_w, height,
                                theme.colors.primary_light, true, 8.0f);
            gfx->draw_hollow_rectangle(window_id, x, y, button_w, height,
                                       theme.colors.primary, 1, true, 8.0f);

            if (dlg->font && gfx->render_text) {
                int text_x = x + (button_w - text_w) / 2;
                int text_y = y + height / 2 + aroma_font_get_ascender(dlg->font) / 2;
                gfx->render_text(window_id, dlg->font, dlg->actions[i].label, text_x, text_y, theme.colors.primary_dark);
            }

            x += button_w + spacing;
        }
    }
}

void aroma_dialog_destroy(AromaNode* dialog_node)
{
    if (!dialog_node) return;
    if (dialog_node->node_widget_ptr) {
        aroma_widget_free(dialog_node->node_widget_ptr);
        dialog_node->node_widget_ptr = NULL;
    }
    __destroy_node(dialog_node);
}