#include "widgets/aroma_dialog.h"
#include "widgets/aroma_container.h"
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

typedef struct
{
    char label[32];
    void (*callback)(void *user_data);
    void *user_data;
} AromaDialogAction;

typedef struct  AromaDialog
{
    AromaRect rect;
    int centered_x;
    int centered_y;

    int action_button_widths[AROMA_DIALOG_ACTION_MAX];
    int action_button_x[AROMA_DIALOG_ACTION_MAX];
    int action_button_text_widths[AROMA_DIALOG_ACTION_MAX];
    int action_button_y;
    int action_button_height;
    int action_button_spacing;

    char title[64];
    char message[256];
    AromaDialogType type;
    bool visible;

    AromaDialogAction actions[AROMA_DIALOG_ACTION_MAX];
    size_t action_count;

    AromaFont *font;

    AromaNode *content_node;
    int content_y_offset;
} AromaDialog;

static void __dialog_request_redraw(void *user_data)
{
    (void)user_data;
    aroma_ui_request_redraw(NULL);
}

static void __dialog_update_rect(AromaDialog *dlg)
{
    dlg->rect.x = dlg->centered_x;
    dlg->rect.y = dlg->centered_y;
}

static void __dialog_recompute_action_layout(AromaDialog *dlg, AromaGraphicsInterface *gfx, size_t window_id)
{
    if (!dlg)
        return;

    const int padding = 16;
    int button_height = 36;

    if (dlg->font)
    {
        int font_h = aroma_font_get_line_height(dlg->font);
        if (font_h > 20)
        {
            button_height = font_h + 24;
        }
    }

    const int spacing = 8;

    dlg->action_button_height = button_height;
    dlg->action_button_spacing = spacing;

    int total_width = 0;

    for (size_t i = 0; i < dlg->action_count; i++)
    {
        int text_w = 48;
        if (gfx && gfx->measure_text && dlg->font)
        {
            text_w = (int)gfx->measure_text(window_id, dlg->font, dlg->actions[i].label, 1.0f);
        }

        int button_w = text_w + 24;
        if (button_w < 64)
            button_w = 64;

        dlg->action_button_text_widths[i] = text_w;
        dlg->action_button_widths[i] = button_w;
        total_width += button_w;
        if (i + 1 < dlg->action_count)
            total_width += spacing;
    }

    int right = dlg->rect.x + dlg->rect.width - padding;
    int x = right - total_width;
    for (size_t i = 0; i < dlg->action_count; i++)
    {
        dlg->action_button_x[i] = x;
        x += dlg->action_button_widths[i] + spacing;
    }

    int btn_y = dlg->rect.y + dlg->rect.height - padding - button_height;
    dlg->action_button_y = btn_y;
}

static bool __dialog_handle_event(AromaEvent *event, void *user_data)
{
    if (!event || !event->target_node)
        return false;

    AromaDialog *dlg = (AromaDialog *)user_data;
    if (!dlg || !dlg->visible)
        return false;
    if (event->event_type != EVENT_TYPE_MOUSE_RELEASE)
        return false;

    __dialog_update_rect(dlg);
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __dialog_recompute_action_layout(dlg, gfx, 0);

    const int mx = event->data.mouse.x;
    const int my = event->data.mouse.y;

    for (size_t i = 0; i < dlg->action_count; i++)
    {
        int x = dlg->action_button_x[i];
        int y = dlg->action_button_y;
        int w = dlg->action_button_widths[i];
        int h = dlg->action_button_height;

        if (mx >= x && mx <= x + w && my >= y && my <= y + h)
        {
            if (dlg->actions[i].callback)
                dlg->actions[i].callback(dlg->actions[i].user_data);
            dlg->visible = false;
            aroma_node_invalidate(event->target_node);
            aroma_ui_request_redraw(NULL);
            return true;
        }
    }

    return (mx >= dlg->rect.x && mx <= dlg->rect.x + dlg->rect.width &&
            my >= dlg->rect.y && my <= dlg->rect.y + dlg->rect.height);
}

AromaNode *aroma_dialog_create(AromaNode *parent, const char *title, const char *message, int width, int height, AromaDialogType type)
{
    if (!parent || width <= 0 || height <= 0)
        return NULL;

    AromaDialog *dlg = (AromaDialog *)aroma_widget_alloc(sizeof(AromaDialog));
    if (!dlg)
        return NULL;
    memset(dlg, 0, sizeof(AromaDialog));

    dlg->rect.width = width;
    dlg->rect.height = height;
    dlg->type = type;
    dlg->visible = false;

    if (title)
        strncpy(dlg->title, title, sizeof(dlg->title) - 1);
    if (title)
        dlg->title[sizeof(dlg->title) - 1] = '\0';
    if (message)
        strncpy(dlg->message, message, sizeof(dlg->message) - 1);
    if (message)
        dlg->message[sizeof(dlg->message) - 1] = '\0';

    AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
    if (!platform)
        return NULL;

    int win_h, win_w;
    platform->get_window_size(0, &win_w, &win_h);

#ifdef __ANDROID__

    width = win_w;
    height = win_h;
    dlg->centered_x = 0;
    dlg->centered_y = 0;
#else
    dlg->centered_x = (win_w - width) / 2;
    dlg->centered_y = (win_h - height) / 2;
#endif

    dlg->rect.width = width;
    dlg->rect.height = height;

    __dialog_update_rect(dlg);

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __dialog_recompute_action_layout(dlg, gfx, 0);

    AromaNode *node = __add_child_node(NODE_TYPE_CONTAINER, parent, dlg);
    if (!node)
    {
        aroma_widget_free(dlg);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_dialog_draw);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, __dialog_handle_event, dlg, 100);

    const int padding = 16;
    dlg->content_y_offset = 72;
    int content_h = height - dlg->content_y_offset - padding - 36 - padding;
    if (content_h < 0)
        content_h = 0;
    AromaNode *content = aroma_container_create(
        node,
        padding,
        dlg->content_y_offset,
        width - padding * 2,
        content_h);
    dlg->content_node = content;

#ifdef ESP32
    aroma_node_invalidate(node);
#endif

    return node;
}

void aroma_dialog_add_action(AromaNode *dialog_node, const char *label, void (*callback)(void *), void *user_data)
{
    if (!dialog_node || !dialog_node->node_widget_ptr || !label)
        return;

    AromaDialog *dlg = (AromaDialog *)dialog_node->node_widget_ptr;
    if (dlg->action_count >= AROMA_DIALOG_ACTION_MAX)
        return;

    AromaDialogAction *act = &dlg->actions[dlg->action_count++];
    strncpy(act->label, label, sizeof(act->label) - 1);
    act->label[sizeof(act->label) - 1] = '\0';
    act->callback = callback;
    act->user_data = user_data;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __dialog_recompute_action_layout(dlg, gfx, 0);
}

void aroma_dialog_set_font(AromaNode *dialog_node, AromaFont *font)
{
    if (!dialog_node || !dialog_node->node_widget_ptr)
        return;

    AromaDialog *dlg = (AromaDialog *)dialog_node->node_widget_ptr;
    dlg->font = font;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __dialog_recompute_action_layout(dlg, gfx, 0);
}

void aroma_dialog_show(AromaNode *dialog_node)
{
    if (!dialog_node || !dialog_node->node_widget_ptr)
        return;

    AromaDialog *dlg = (AromaDialog *)dialog_node->node_widget_ptr;
    dlg->visible = true;
    __dialog_update_rect(dlg);

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    __dialog_recompute_action_layout(dlg, gfx, 0);

    if (dlg->content_node)
        aroma_node_set_hidden(dlg->content_node, false);

    aroma_node_invalidate(dialog_node);
    aroma_ui_request_redraw(NULL);
}

void aroma_dialog_hide(AromaNode *dialog_node)
{
    if (!dialog_node || !dialog_node->node_widget_ptr)
        return;

    AromaDialog *dlg = (AromaDialog *)dialog_node->node_widget_ptr;
    dlg->visible = false;
    if (dlg->content_node)
        aroma_node_set_hidden(dlg->content_node, true);
    aroma_node_set_hidden(dialog_node, true);
    aroma_node_invalidate(dialog_node);
    aroma_ui_request_redraw(NULL);
}

AromaNode *aroma_dialog_get_content_area(AromaNode *dialog_node)
{
    if (!dialog_node || !dialog_node->node_widget_ptr)
        return NULL;
    AromaDialog *dlg = (AromaDialog *)dialog_node->node_widget_ptr;
    return dlg->content_node;
}

void aroma_dialog_draw(AromaNode *dialog_node, size_t window_id)
{
    if (!dialog_node || !dialog_node->node_widget_ptr)
        return;
    AromaDialog *dlg = (AromaDialog *)dialog_node->node_widget_ptr;
    if (!dlg->visible || aroma_node_is_hidden(dialog_node))
        return;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;

    __dialog_update_rect(dlg);
    __dialog_recompute_action_layout(dlg, gfx, window_id);

    if (dlg->content_node)
    {
        const int padding = 16;
        aroma_container_set_rect(dlg->content_node,
                                 dlg->rect.x + padding,
                                 dlg->rect.y + dlg->content_y_offset,
                                 dlg->rect.width - padding * 2,
                                 dlg->action_button_y - (dlg->rect.y + dlg->content_y_offset) - 8);
    }

    AromaTheme theme = aroma_theme_get_global();
    int x = dlg->rect.x, y = dlg->rect.y;

    gfx->fill_rectangle(window_id, x, y, dlg->rect.width, dlg->rect.height, theme.colors.surface, true, 12.0f);
    gfx->draw_hollow_rectangle(window_id, x, y, dlg->rect.width, dlg->rect.height, theme.colors.border, 1, true, 12.0f);

    if (dlg->font && gfx->render_text)
    {
        gfx->render_text(window_id, dlg->font, dlg->title, x + 16, y + 24, theme.colors.text_primary, 1.0f);
        gfx->render_text(window_id, dlg->font, dlg->message, x + 16, y + 52, theme.colors.text_secondary, 1.0f);
    }

    for (size_t i = 0; i < dlg->action_count; i++)
    {
        int bx = dlg->action_button_x[i];
        int by = dlg->action_button_y;
        int bw = dlg->action_button_widths[i];
        int bh = dlg->action_button_height;

        gfx->fill_rectangle(window_id, bx, by, bw, bh, theme.colors.primary_light, true, 8.0f);
        gfx->draw_hollow_rectangle(window_id, bx, by, bw, bh, theme.colors.primary, 1, true, 8.0f);

        if (dlg->font && gfx->render_text)
        {
            int text_w = dlg->action_button_text_widths[i];
            if (text_w <= 0)
                text_w = 48;
            int text_x = bx + (bw - text_w) / 2;
            int line_h = aroma_font_get_line_height(dlg->font);
            int text_y = by + (bh - line_h) / 2;
            gfx->render_text(window_id, dlg->font, dlg->actions[i].label, text_x, text_y, theme.colors.text_primary, 1.0f);
        }
    }
}

void aroma_dialog_destroy(AromaNode *dialog_node)
{
    if (!dialog_node)
        return;
    if (dialog_node->node_widget_ptr)
    {
        aroma_widget_free(dialog_node->node_widget_ptr);
        dialog_node->node_widget_ptr = NULL;
    }
    __destroy_node(dialog_node);
}
