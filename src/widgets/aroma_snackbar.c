#include "widgets/aroma_snackbar.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "glps_timer.h"
#include <string.h>
#include <limits.h>

#define AROMA_SNACKBAR_TEXT_MAX 128

typedef struct AromaSnackbar {
    AromaRect rect;
    char message[AROMA_SNACKBAR_TEXT_MAX];
    char action_label[32];
    void (*action_callback)(void* user_data);
    void* user_data;
    int action_hit_width;
    int duration_ms;
    bool visible;
    bool pending_show;
    AromaFont* font;
    glps_timer* timer;
} AromaSnackbar;

static void __snackbar_request_redraw(void* user_data)
{
    if (!user_data) {
        return;
    }
    void (*on_redraw)(void*) = (void (*)(void*))user_data;
    on_redraw(NULL);
}

static bool __snackbar_handle_event(AromaEvent* event, void* user_data)
{
    if (!event || !event->target_node) return false;
    AromaSnackbar* bar = (AromaSnackbar*)event->target_node->node_widget_ptr;
    if (!bar || !bar->visible) return false;

    if (event->event_type != EVENT_TYPE_MOUSE_RELEASE) return false;

    AromaRect* r = &bar->rect;
    bool in_bounds = (event->data.mouse.x >= r->x && event->data.mouse.x <= r->x + r->width &&
                      event->data.mouse.y >= r->y && event->data.mouse.y <= r->y + r->height);
    if (!in_bounds) return false;

    if (bar->action_label[0] && bar->action_callback) {
        int action_width = (bar->action_hit_width > 0) ? bar->action_hit_width : 72;
        if (event->data.mouse.x >= r->x + r->width - action_width) {
            bar->action_callback(bar->user_data);
            if (bar->timer) {
                glps_timer_stop(bar->timer);
            }
            bar->visible = false;
            __snackbar_request_redraw(user_data);
            return true;
        }
    }

    return false;
}

static void __snackbar_auto_dismiss(void* arg)
{
    AromaNode* snackbar_node = (AromaNode*)arg;
    if (!snackbar_node || !snackbar_node->node_widget_ptr) return;
    AromaSnackbar* bar = (AromaSnackbar*)snackbar_node->node_widget_ptr;
    if (!bar->visible) return;
    bar->visible = false;
    if (bar->timer) {
        glps_timer_stop(bar->timer);
    }
    aroma_node_invalidate(snackbar_node);
    aroma_ui_request_redraw(NULL);
}

AromaNode* aroma_snackbar_create(AromaNode* parent, const char* message, int duration_ms)
{
    if (!parent || !message) return NULL;
    AromaSnackbar* bar = (AromaSnackbar*)aroma_widget_alloc(sizeof(AromaSnackbar));
    if (!bar) return NULL;

    memset(bar, 0, sizeof(AromaSnackbar));
    bar->rect.x = 16;
    bar->rect.y = 16;
    bar->rect.width = 360;
    bar->rect.height = 48;
    bar->duration_ms = duration_ms;
    bar->visible = false;
    bar->pending_show = false;
    bar->timer = glps_timer_init();
    strncpy(bar->message, message, AROMA_SNACKBAR_TEXT_MAX - 1);

    AromaNode* node = __add_child_node(NODE_TYPE_WIDGET, parent, bar);
    if (!node) {
        aroma_widget_free(bar);
        return NULL;
    }

    aroma_node_set_z_index(node, INT_MAX);

    aroma_node_set_draw_cb(node, aroma_snackbar_draw);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, __snackbar_handle_event, aroma_ui_request_redraw, 80);
    return node;
}

void aroma_snackbar_set_action(AromaNode* snackbar_node, const char* action_text,
                               void (*callback)(void* user_data), void* user_data)
{
    if (!snackbar_node || !snackbar_node->node_widget_ptr || !action_text) return;
    AromaSnackbar* bar = (AromaSnackbar*)snackbar_node->node_widget_ptr;
    strncpy(bar->action_label, action_text, sizeof(bar->action_label) - 1);
    bar->action_callback = callback;
    bar->user_data = user_data;
}

void aroma_snackbar_set_font(AromaNode* snackbar_node, AromaFont* font)
{
    if (!snackbar_node || !snackbar_node->node_widget_ptr) return;
    AromaSnackbar* bar = (AromaSnackbar*)snackbar_node->node_widget_ptr;
    bar->font = font;
}

void aroma_snackbar_show(AromaNode* snackbar_node)
{
    if (!snackbar_node || !snackbar_node->node_widget_ptr) return;
    AromaSnackbar* bar = (AromaSnackbar*)snackbar_node->node_widget_ptr;
    bar->visible = true;
    bar->pending_show = true;
    aroma_node_invalidate(snackbar_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_snackbar_draw(AromaNode* snackbar_node, size_t window_id)
{
    if (!snackbar_node || !snackbar_node->node_widget_ptr) return;
    AromaSnackbar* bar = (AromaSnackbar*)snackbar_node->node_widget_ptr;
    if (aroma_node_is_hidden(snackbar_node)) return;
    if (!bar->visible) return;

    if (bar->pending_show) {
        bar->pending_show = false;
        if (bar->timer && bar->duration_ms > 0) {
            glps_timer_start(bar->timer, (uint64_t)bar->duration_ms, __snackbar_auto_dismiss, snackbar_node);
        }
    }

    if (bar->timer && bar->duration_ms > 0) {
        glps_timer_check_and_call(bar->timer);
        if (!bar->visible) return;
    }

    AromaGraphicsInterface* gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx) return;
    AromaPlatformInterface* platform = aroma_backend_abi.get_platform_interface();
    AromaTheme theme = aroma_theme_get_global();

    if (platform && platform->get_window_size) {
        int win_w = 0;
        int win_h = 0;
        platform->get_window_size(window_id, &win_w, &win_h);
        if (win_w > 0 && win_h > 0) {
            int margin = 24;
            int max_width = win_w - (margin * 2);
            if (max_width > 0 && bar->rect.width > max_width) {
                bar->rect.width = max_width;
            }
            bar->rect.x = (win_w - bar->rect.width) / 2;
            bar->rect.y = win_h - bar->rect.height - margin;
        }
    }

    uint32_t bg = aroma_color_adjust(theme.colors.text_primary, -0.7f);
    gfx->fill_rectangle(window_id, bar->rect.x, bar->rect.y, bar->rect.width, bar->rect.height,
                        bg, true, 8.0f);

    if (bar->font && gfx->render_text) {
        gfx->render_text(window_id, bar->font, bar->message, bar->rect.x + 12, bar->rect.y + 28, theme.colors.surface);
        if (bar->action_label[0]) {
            int padding = 12;
            int action_width = 72;
            if (gfx->measure_text) {
                float measured = gfx->measure_text(window_id, bar->font, bar->action_label);
                action_width = (int)measured + (padding * 2);
                if (action_width < 48) action_width = 48;
            }
            bar->action_hit_width = action_width;
            gfx->render_text(window_id, bar->font, bar->action_label,
                             bar->rect.x + bar->rect.width - action_width + padding,
                             bar->rect.y + 28, theme.colors.primary_light);
        } else {
            bar->action_hit_width = 0;
        }
    }
}

void aroma_snackbar_destroy(AromaNode* snackbar_node)
{
    if (!snackbar_node) return;
    if (snackbar_node->node_widget_ptr) {
        AromaSnackbar* bar = (AromaSnackbar*)snackbar_node->node_widget_ptr;
        if (bar->timer) {
            glps_timer_destroy(bar->timer);
            bar->timer = NULL;
        }
        aroma_widget_free(snackbar_node->node_widget_ptr);
        snackbar_node->node_widget_ptr = NULL;
    }
    __destroy_node(snackbar_node);
}