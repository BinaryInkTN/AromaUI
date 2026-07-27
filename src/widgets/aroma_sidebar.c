#include "widgets/aroma_sidebar.h"
#include "core/aroma_common.h"
#include "core/aroma_event.h"
#include "core/aroma_logger.h"
#include "core/aroma_node.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "aroma_ui.h"
#include "aroma_animation.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include <string.h>
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define AROMA_SIDEBAR_CONTENT_MAX 8

struct AromaSidebar
{
    AromaRect rect;

    AromaFont *icon_font;
    int count;
    int selected_index;
    int hovered_index;
    int item_height;

    AromaFont *font;
    uint32_t bg_color;
    uint32_t text_color;
    uint32_t selected_color;
    uint32_t selected_bg_color;
    uint32_t hover_bg_color;
    void (*on_select)(AromaNode *, int, void *);
    void *user_data;

    bool responsive;
    bool is_retracted;
    int full_width;
    int retracted_width;
    int breakpoint;

    bool apple_style;
    int corner_radius;
    int item_spacing;
    int side_margin;
    int top_padding;

    int transition_type;
    uint32_t transition_duration;
    int prev_selected_index;

    AromaNode *content_nodes[AROMA_SIDEBAR_MAX_ITEMS][AROMA_SIDEBAR_CONTENT_MAX];
    int content_counts[AROMA_SIDEBAR_MAX_ITEMS];
    char labels[AROMA_SIDEBAR_MAX_ITEMS][AROMA_SIDEBAR_LABEL_MAX];
    char icons[AROMA_SIDEBAR_MAX_ITEMS][8];
};

static void __sidebar_request_redraw(void *user_data)
{
    if (!user_data)
        return;
    void (*on_redraw)(void *) = (void (*)(void *))user_data;
    on_redraw(NULL);
}

static int __sidebar_index_from_y(AromaSidebar *sidebar, int y)
{
    if (!sidebar || sidebar->count <= 0)
        return -1;
    int local_y = y - sidebar->rect.y - sidebar->top_padding;
    if (local_y < 0)
        return -1;
    int effective_height = sidebar->item_height + sidebar->item_spacing;
    int index = local_y / effective_height;
    if (index < 0 || index >= sidebar->count)
        return -1;

    int item_local_y = local_y - (index * effective_height);
    if (item_local_y >= sidebar->item_height)
        return -1;
    return index;
}

static void __sidebar_set_hidden_recursive(AromaNode *node, bool hidden)
{
    if (!node)
        return;
    aroma_node_set_hidden(node, hidden);
    for (uint64_t i = 0; i < node->child_count; i++)
    {
        if (node->child_nodes[i])
        {
            __sidebar_set_hidden_recursive(node->child_nodes[i], hidden);
        }
    }
}

static void __sidebar_update_content_visibility(AromaSidebar *sidebar)
{
    if (!sidebar)
        return;
    for (int i = 0; i < sidebar->count; i++)
    {
        bool hide = (i != sidebar->selected_index);
        for (int j = 0; j < sidebar->content_counts[i]; j++)
        {
            AromaNode *content = sidebar->content_nodes[i][j];
            if (!content)
                continue;
            aroma_node_set_hidden(content, hide);

            if (!hide)
            {
                AromaRect *content_rect = aroma_node_get_rect(content);
                if (content_rect)
                {
                    aroma_node_update_layout(content,
                                             content_rect->x,
                                             content_rect->y,
                                             content_rect->width,
                                             content_rect->height);

                    if (sidebar->transition_type != 0 && sidebar->transition_duration > 0 && sidebar->prev_selected_index != sidebar->selected_index)
                    {
                        int offset = (sidebar->selected_index > sidebar->prev_selected_index) ? 200 : -200;
                        if (sidebar->transition_type == AROMA_ANIM_SLIDE_X)
                        {
                            aroma_animation_start(content, AROMA_ANIM_SLIDE_X, content_rect->x + offset, content_rect->x, sidebar->transition_duration);
                        }
                        else if (sidebar->transition_type == AROMA_ANIM_SLIDE_Y)
                        {
                            aroma_animation_start(content, AROMA_ANIM_SLIDE_Y, content_rect->y + offset, content_rect->y, sidebar->transition_duration);
                        }
                        else if (sidebar->transition_type == AROMA_ANIM_FADE)
                        {
                            aroma_animation_start(content, AROMA_ANIM_FADE, 0.0f, 1.0f, sidebar->transition_duration);
                        }
                    }
                }
            }

            aroma_node_invalidate(content);
        }
    }
}

static bool __sidebar_handle_event(AromaEvent *event, void *user_data)
{
    if (!event || !event->target_node)
        return false;
    AromaSidebar *sidebar = (AromaSidebar *)event->target_node->node_widget_ptr;
    if (!sidebar)
        return false;

   int adjusted_x = event->data.mouse.x;
    int adjusted_y = event->data.mouse.y;
                          AromaNode *cur = event->target_node->parent_node;
    while (cur) {
        if (aroma_container_is_scrollable(cur)) {
            int scroll_x, scroll_y;
            aroma_container_get_scroll(cur, &scroll_x, &scroll_y);
            adjusted_x += scroll_x;
            adjusted_y += scroll_y;
        }
        cur = cur->parent_node;
    }
    

    bool in_bounds = (adjusted_x >= sidebar->rect.x && adjusted_x <= sidebar->rect.x + sidebar->rect.width &&
                      adjusted_y >= sidebar->rect.y && adjusted_y <= sidebar->rect.y + sidebar->rect.height);
    switch (event->event_type)
    {
    case EVENT_TYPE_MOUSE_MOVE:
    {
        int new_hover = in_bounds ? __sidebar_index_from_y(sidebar, adjusted_y) : -1;
        if (new_hover != sidebar->hovered_index)
        {
            sidebar->hovered_index = new_hover;
            aroma_node_invalidate(event->target_node);
            __sidebar_request_redraw(user_data);
        }
        return in_bounds;
    }
    case EVENT_TYPE_MOUSE_EXIT:
        if (sidebar->hovered_index != -1)
        {
            sidebar->hovered_index = -1;
            aroma_node_invalidate(event->target_node);
            __sidebar_request_redraw(user_data);
        }
        return false;
    case EVENT_TYPE_MOUSE_CLICK:
        if (in_bounds)
        {
            int index = __sidebar_index_from_y(sidebar, adjusted_y);
            if (index >= 0 && index < sidebar->count && index != sidebar->selected_index)
            {
                sidebar->prev_selected_index = sidebar->selected_index;
                sidebar->selected_index = index;
                __sidebar_update_content_visibility(sidebar);
                if (sidebar->on_select)
                {
                    sidebar->on_select(event->target_node, index, sidebar->user_data);
                }
                aroma_node_invalidate(event->target_node);
                __sidebar_request_redraw(user_data);
            }
            return true;
        }
        break;
    default:
        break;
    }

    return false;
}

AromaNode *aroma_sidebar_create(AromaNode *parent, int x, int y, int width, int height,
                                const char **labels, int count)
{
    if (!parent || !labels || count <= 0)
        return NULL;


#ifdef __ANDROID__
x = aroma_android_dp_to_px(x);
y = aroma_android_dp_to_px(y);
width = aroma_android_dp_to_px(width);
height = aroma_android_dp_to_px(height);
#endif

    AromaSidebar *sidebar = (AromaSidebar *)aroma_widget_alloc(sizeof(AromaSidebar));
    if (!sidebar)
        return NULL;

    memset(sidebar, 0, sizeof(AromaSidebar));
    sidebar->rect.x = x;
    sidebar->rect.y = y;
    sidebar->rect.width = width;
    sidebar->rect.height = height;
    sidebar->count = (count > AROMA_SIDEBAR_MAX_ITEMS) ? AROMA_SIDEBAR_MAX_ITEMS : count;
    sidebar->selected_index = 0;
    sidebar->hovered_index = -1;
    sidebar->item_height = 52;

    sidebar->apple_style = true;
    sidebar->corner_radius = 13;
    sidebar->item_spacing = 4;
    sidebar->side_margin = 10;
    sidebar->top_padding = 8;

    sidebar->responsive = false;
    sidebar->is_retracted = false;
    sidebar->full_width = width;
    sidebar->retracted_width = 60;
    sidebar->breakpoint = 600;

    AromaTheme theme = aroma_theme_get_global();
    sidebar->bg_color = theme.colors.surface;
    sidebar->text_color = theme.colors.text_primary;
    sidebar->selected_color = theme.colors.primary;

    sidebar->selected_bg_color = aroma_color_blend(sidebar->selected_color, 0xFFFFFFFF, 0.88f);
    sidebar->hover_bg_color = aroma_color_blend(theme.colors.surface, 0xFF000000, 0.05f);

    for (int i = 0; i < sidebar->count; i++)
    {
        if (labels[i])
        {
            strncpy(sidebar->labels[i], labels[i], AROMA_SIDEBAR_LABEL_MAX - 1);
            sidebar->labels[i][AROMA_SIDEBAR_LABEL_MAX - 1] = '\0';
        }
        else
        {
            sidebar->labels[i][0] = '\0';
        }
        sidebar->content_counts[i] = 0;
        for (int j = 0; j < AROMA_SIDEBAR_CONTENT_MAX; j++)
        {
            sidebar->content_nodes[i][j] = NULL;
        }
    }

    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, sidebar);
    if (!node)
    {
        aroma_widget_free(sidebar);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_sidebar_draw);

    if (!sidebar->font)
    {
        AromaNode *root_node = parent;
        while (root_node && root_node->parent_node)
        {
            root_node = root_node->parent_node;
        }
        if (root_node && root_node->node_widget_ptr)
        {
            struct AromaWindow *window_data = (struct AromaWindow *)root_node->node_widget_ptr;
            for (int i = 0; i < g_window_count; ++i)
            {
                if (g_windows[i].is_active && g_windows[i].window_id == window_data->window_id)
                {
                    if (g_windows[i].default_font)
                    {
                        sidebar->font = g_windows[i].default_font;
                    }
                    break;
                }
            }
        }
    }

    aroma_node_invalidate(node);

    return node;
}

void aroma_sidebar_set_selected(AromaNode *sidebar_node, int index)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr)
        return;
    AromaSidebar *sidebar = (AromaSidebar *)sidebar_node->node_widget_ptr;
    if (index < 0 || index >= sidebar->count)
        return;
    if (sidebar->selected_index != index)
    {
        sidebar->prev_selected_index = sidebar->selected_index;
        sidebar->selected_index = index;
    }
    __sidebar_update_content_visibility(sidebar);
    aroma_node_invalidate(sidebar_node);
}

int aroma_sidebar_get_selected(AromaNode *sidebar_node)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr)
        return -1;
    AromaSidebar *sidebar = (AromaSidebar *)sidebar_node->node_widget_ptr;
    return sidebar->selected_index;
}

void aroma_sidebar_set_on_select(AromaNode *sidebar_node,
                                 void (*callback)(AromaNode *, int, void *),
                                 void *user_data)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr)
        return;
    AromaSidebar *sidebar = (AromaSidebar *)sidebar_node->node_widget_ptr;
    sidebar->on_select = callback;
    sidebar->user_data = user_data;
}

void aroma_sidebar_set_font(AromaNode *sidebar_node, AromaFont *font)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr)
        return;
    AromaSidebar *sidebar = (AromaSidebar *)sidebar_node->node_widget_ptr;
    sidebar->font = font;
    aroma_node_invalidate(sidebar_node);
}

void aroma_sidebar_set_responsive(AromaNode *sidebar_node, bool enable)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr)
        return;
    AromaSidebar *sidebar = (AromaSidebar *)sidebar_node->node_widget_ptr;
    sidebar->responsive = enable;
    if (enable)
    {
        aroma_node_invalidate(sidebar_node);
    }
}

void aroma_sidebar_set_icon(AromaNode *sidebar_node, int index, const char *icon_code, AromaFont *icon_font)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr)
        return;
    AromaSidebar *sidebar = (AromaSidebar *)sidebar_node->node_widget_ptr;

    if (index < 0 || index >= sidebar->count)
        return;

    if (icon_code)
    {
        strncpy(sidebar->icons[index], icon_code, 7);
        sidebar->icons[index][7] = '\0';
    }
    else
    {
        sidebar->icons[index][0] = '\0';
    }

    if (icon_font)
    {
        sidebar->icon_font = icon_font;
    }

    aroma_node_invalidate(sidebar_node);
}

void aroma_sidebar_set_style(AromaNode *sidebar_node, bool enable, int corner_radius, int item_spacing, int side_margin)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr)
        return;
    AromaSidebar *sidebar = (AromaSidebar *)sidebar_node->node_widget_ptr;
    sidebar->apple_style = enable;
    if (corner_radius > 0)
        sidebar->corner_radius = corner_radius;
    if (item_spacing >= 0)
        sidebar->item_spacing = item_spacing;
    if (side_margin >= 0)
        sidebar->side_margin = side_margin;
    aroma_node_invalidate(sidebar_node);
}

void aroma_sidebar_set_retracted(AromaNode *sidebar_node, bool retracted)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr)
        return;
    AromaSidebar *sidebar = (AromaSidebar *)sidebar_node->node_widget_ptr;
    if (sidebar->is_retracted != retracted)
    {
        sidebar->is_retracted = retracted;
        sidebar->rect.width = retracted ? sidebar->retracted_width : sidebar->full_width;
        aroma_node_invalidate(sidebar_node);
    }
}

void aroma_sidebar_toggle(AromaNode *sidebar_node)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr)
        return;
    AromaSidebar *sidebar = (AromaSidebar *)sidebar_node->node_widget_ptr;
    aroma_sidebar_set_retracted(sidebar_node, !sidebar->is_retracted);
}

bool aroma_sidebar_is_retracted(AromaNode *sidebar_node)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr)
        return false;
    AromaSidebar *sidebar = (AromaSidebar *)sidebar_node->node_widget_ptr;
    return sidebar->is_retracted;
}

void aroma_sidebar_set_content(AromaNode *sidebar_node, int index, AromaNode **content_nodes, int content_count)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr)
        return;
    AromaSidebar *sidebar = (AromaSidebar *)sidebar_node->node_widget_ptr;
    if (index < 0 || index >= sidebar->count)
        return;
    int max_count = (content_count > AROMA_SIDEBAR_CONTENT_MAX) ? AROMA_SIDEBAR_CONTENT_MAX : content_count;
    sidebar->content_counts[index] = 0;
    for (int i = 0; i < max_count; i++)
    {
        sidebar->content_nodes[index][i] = content_nodes ? content_nodes[i] : NULL;
        if (sidebar->content_nodes[index][i])
        {
            sidebar->content_counts[index]++;
        }
    }
    __sidebar_update_content_visibility(sidebar);
}

bool aroma_sidebar_setup_events(AromaNode *sidebar_node, void (*on_redraw_callback)(void *), void *user_data)
{
    (void)user_data;
    if (!sidebar_node)
        return false;
    aroma_event_subscribe(sidebar_node->node_id, EVENT_TYPE_MOUSE_MOVE, __sidebar_handle_event, (void *)on_redraw_callback, 80);
    aroma_event_subscribe(sidebar_node->node_id, EVENT_TYPE_MOUSE_EXIT, __sidebar_handle_event, (void *)on_redraw_callback, 80);
    aroma_event_subscribe(sidebar_node->node_id, EVENT_TYPE_MOUSE_CLICK, __sidebar_handle_event, (void *)on_redraw_callback, 90);
    return true;
}

void aroma_sidebar_draw(AromaNode *sidebar_node, size_t window_id)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr)
        return;
    if (aroma_node_is_hidden(sidebar_node))
        return;
    AromaSidebar *sidebar = (AromaSidebar *)sidebar_node->node_widget_ptr;

    if (!sidebar->font)
    {
        for (int i = 0; i < g_window_count; ++i)
        {
            if (g_windows[i].is_active && g_windows[i].window_id == window_id && g_windows[i].default_font)
            {
                sidebar->font = g_windows[i].default_font;
                break;
            }
        }
    }

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;

    if (sidebar->responsive)
    {
        AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
        if (platform && platform->get_window_size)
        {
            int w, h;
            platform->get_window_size(window_id, &w, &h);
            bool should_retract = (w < sidebar->breakpoint);
            if (sidebar->is_retracted != should_retract)
            {
                sidebar->is_retracted = should_retract;
                sidebar->rect.width = should_retract ? sidebar->retracted_width : sidebar->full_width;
            }
        }
    }

    AromaTheme theme = aroma_theme_get_global();
    sidebar->bg_color = theme.colors.surface;
    sidebar->text_color = theme.colors.text_primary;
    sidebar->selected_color = theme.colors.primary;
    sidebar->selected_bg_color = aroma_color_blend(sidebar->selected_color, 0xFFFFFFFF, 0.88f);
    sidebar->hover_bg_color = aroma_color_blend(theme.colors.surface, 0xFF000000, 0.05f);

    float bg_radius = sidebar->apple_style ? (float)sidebar->corner_radius : 12.0f;
    gfx->fill_rectangle(window_id, sidebar->rect.x, sidebar->rect.y,
                        sidebar->rect.width, sidebar->rect.height,
                        sidebar->bg_color, true, bg_radius);

    int effective_item_height = sidebar->item_height;
    int spacing = sidebar->apple_style ? sidebar->item_spacing : 0;
    int margin = sidebar->apple_style ? sidebar->side_margin : 6;
    int top_pad = sidebar->apple_style ? sidebar->top_padding : 0;

    for (int i = 0; i < sidebar->count; i++)
    {
        int item_y = sidebar->rect.y + top_pad + i * (effective_item_height + spacing);
        if (item_y + effective_item_height > sidebar->rect.y + sidebar->rect.height)
            break;

        bool selected = (i == sidebar->selected_index);
        bool hovered = (i == sidebar->hovered_index);

        if (!sidebar->is_retracted)
        {
            if (sidebar->apple_style)
            {

                if (selected)
                {
                    gfx->fill_rectangle(window_id,
                                        sidebar->rect.x + margin,
                                        item_y,
                                        sidebar->rect.width - margin * 2,
                                        effective_item_height,
                                        sidebar->selected_bg_color, true,
                                        (float)sidebar->corner_radius);
                }
                else if (hovered)
                {
                    gfx->fill_rectangle(window_id,
                                        sidebar->rect.x + margin,
                                        item_y,
                                        sidebar->rect.width - margin * 2,
                                        effective_item_height,
                                        sidebar->hover_bg_color, true,
                                        (float)sidebar->corner_radius);
                }

                int item_center_y = item_y + effective_item_height / 2;

                int icon_size = sidebar->icon_font ? aroma_font_get_line_height(sidebar->icon_font) : 24;
                int text_height = sidebar->font ? aroma_font_get_line_height(sidebar->font) : 16;

                int content_x = sidebar->rect.x + margin + 16;

                uint32_t text_color = selected ? sidebar->selected_color : sidebar->text_color;
                uint32_t icon_color = selected ? sidebar->selected_color : aroma_color_blend(sidebar->text_color, 0xFF000000, 0.7f);

                if (sidebar->icons[i][0] != '\0' && sidebar->icon_font && gfx->render_text)
                {
                    int icon_y = item_center_y - icon_size / 2;
                    gfx->render_text(window_id, sidebar->icon_font, sidebar->icons[i],
                                     content_x, icon_y, icon_color, 1.0f);

                    int icon_w = aroma_font_get_px_size(sidebar->icon_font);
                    content_x += icon_w + 12;
                }

                if (sidebar->font && gfx->render_text)
                {
                    int text_y = item_center_y - text_height / 2;

                    float text_opacity = selected ? 1.0f : 0.9f;
                    gfx->render_text(window_id, sidebar->font, sidebar->labels[i],
                                     content_x, text_y, text_color, text_opacity);
                }
            }
            else
            {

                int row_margin = 6;
                uint32_t row_color = sidebar->bg_color;
                float row_radius = 10.0f;

                if (selected)
                {
                    row_color = aroma_color_blend(sidebar->bg_color, sidebar->selected_color, 0.16f);
                }
                else if (hovered)
                {
                    row_color = aroma_color_blend(sidebar->bg_color, sidebar->selected_color, 0.08f);
                }

                gfx->fill_rectangle(window_id,
                                    sidebar->rect.x + row_margin,
                                    item_y + 4,
                                    sidebar->rect.width - row_margin * 2,
                                    effective_item_height - 8,
                                    row_color, true, row_radius);

                int content_x = sidebar->rect.x + 14;
                uint32_t text_color = selected ? sidebar->selected_color : sidebar->text_color;

                if (sidebar->icons[i][0] != '\0' && sidebar->icon_font && gfx->render_text)
                {
                    int icon_line_h = aroma_font_get_line_height(sidebar->icon_font);
                    int icon_y = item_y + (effective_item_height - icon_line_h) / 2;

                    gfx->render_text(window_id, sidebar->icon_font, sidebar->icons[i],
                                     content_x, icon_y, text_color, 1.0f);

                    int icon_w = aroma_font_get_px_size(sidebar->icon_font);
                    content_x += icon_w + 12;
                }

                if (sidebar->font && gfx->render_text)
                {
                    int line_height = aroma_font_get_line_height(sidebar->font);
                    int text_y = item_y + (effective_item_height - line_height) / 2;
                    gfx->render_text(window_id, sidebar->font, sidebar->labels[i],
                                     content_x, text_y, text_color, 1.0f);
                }
            }
        }
        else
        {

            uint32_t text_color = selected ? sidebar->selected_color : sidebar->text_color;
            int item_center_y = item_y + effective_item_height / 2;

            if (sidebar->icons[i][0] != '\0' && sidebar->icon_font && gfx->render_text)
            {
                int icon_size = aroma_font_get_line_height(sidebar->icon_font);
                int w = aroma_font_get_line_width(sidebar->icon_font, sidebar->icons[i]);
                int icon_x = sidebar->rect.x + (sidebar->rect.width - w) / 2;
                int icon_y = item_center_y - icon_size / 2;

                gfx->render_text(window_id, sidebar->icon_font, sidebar->icons[i],
                                 icon_x, icon_y, text_color, 1.0f);
            }
            else if (sidebar->font && gfx->render_text)
            {
                char glyph[2] = {sidebar->labels[i][0], '\0'};
                if (glyph[0] != '\0')
                {
                    int text_height = aroma_font_get_line_height(sidebar->font);
                    float w = gfx->measure_text(window_id, sidebar->font, glyph, 1.0f);
                    int text_x = sidebar->rect.x + (sidebar->rect.width - (int)w) / 2;
                    int text_y = item_center_y - text_height / 2;
                    gfx->render_text(window_id, sidebar->font, glyph, text_x, text_y, text_color, 1.0f);
                }
            }
        }
    }
}

void aroma_sidebar_destroy(AromaNode *sidebar_node)
{
    if (!sidebar_node)
        return;
    if (sidebar_node->node_widget_ptr)
    {
        aroma_widget_free(sidebar_node->node_widget_ptr);
        sidebar_node->node_widget_ptr = NULL;
    }
}

void aroma_sidebar_set_transition(AromaNode *sidebar_node, int type, uint32_t duration_ms)
{
    if (!sidebar_node || !sidebar_node->node_widget_ptr)
        return;
    AromaSidebar *sidebar = (AromaSidebar *)sidebar_node->node_widget_ptr;
    sidebar->transition_type = type;
    sidebar->transition_duration = duration_ms;
}