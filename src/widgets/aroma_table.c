#include "widgets/aroma_table.h"
#include "core/aroma_logger.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "aroma_ui.h"
#include "core/aroma_slab_alloc.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#include <stdlib.h>
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define MAX_ROWS 100
#define MAX_COLS 10

typedef struct
{
    AromaRect rect;
    int num_cols;
    int num_rows;
    int col_widths[MAX_COLS];
    char headers[MAX_COLS][64];
    char cells[MAX_ROWS][MAX_COLS][64];
    AromaNode *cell_widgets[MAX_ROWS][MAX_COLS];
    int row_height;
    int header_height;
    int selected_row;
    AromaFont *font;
    void (*callback)(int, void *);
    void *user_data;
    AromaNode *self_node;
} AromaTableInternal;

static AromaTableInternal *get_table(AromaNode *node)
{
    if (!node || !node->node_widget_ptr)
        return NULL;
    return (AromaTableInternal *)node->node_widget_ptr;
}

static bool __table_handle_event(AromaEvent *event, void *user_data)
{
    AromaNode *node = (AromaNode *)user_data;
    AromaTableInternal *t = get_table(node);
    if (!t)
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
    
    if (event->event_type == EVENT_TYPE_MOUSE_CLICK || event->event_type == EVENT_TYPE_TOUCH_DOWN)
    {
        int my;
        if (event->event_type == EVENT_TYPE_MOUSE_CLICK)
            my = adjusted_y - t->rect.y;
        else
            my = adjusted_y - t->rect.y;
        if (my > t->header_height)
        {
            int row = (my - t->header_height) / t->row_height;
            if (row >= 0 && row < t->num_rows)
            {
                t->selected_row = row;
                aroma_node_invalidate(t->self_node);
                if (t->callback)
                    t->callback(row, t->user_data);
                return true;
            }
        }
    }
    return false;
}

AromaNode *aroma_table_create(AromaNode *parent, int x, int y, int width, int height, int num_cols)
{
    if (!parent || num_cols <= 0 || num_cols > MAX_COLS)
        return NULL;

#ifdef __ANDROID__
    x = aroma_android_dp_to_px(x);
    y = aroma_android_dp_to_px(y);
    width = aroma_android_dp_to_px(width);
    height = aroma_android_dp_to_px(height);
#endif

    AromaTableInternal *t = (AromaTableInternal *)aroma_widget_alloc(sizeof(AromaTableInternal));
    if (!t)
        return NULL;

    memset(t, 0, sizeof(*t));
    t->rect.x = x;
    t->rect.y = y;
    t->rect.width = width;
    t->rect.height = height;
    t->num_cols = num_cols;
    t->row_height = 40;
    t->header_height = 40;
    t->selected_row = -1;

    int default_w = width / num_cols;
    for (int i = 0; i < num_cols; i++)
    {
        t->col_widths[i] = default_w;
    }

    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, t);
    if (!node)
    {
        aroma_widget_free(t);
        return NULL;
    }
    t->self_node = node;

    aroma_node_set_draw_cb(node, aroma_table_draw);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __table_handle_event, node, 90);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_DOWN, __table_handle_event, node, 90);

    return node;
}

static void _aroma_table_update_widgets(AromaNode *table_node)
{
    AromaTableInternal *t = get_table(table_node);
    if (!t)
        return;
    int cur_y = t->rect.y + t->header_height;
    for (int r = 0; r < t->num_rows; r++)
    {
        int cur_x = t->rect.x;
        for (int c = 0; c < t->num_cols; c++)
        {
            if (t->cell_widgets[r][c])
            {
                AromaRect *w_rect = aroma_node_get_rect(t->cell_widgets[r][c]);
                if (w_rect)
                {
                    w_rect->x = cur_x + 5;
                    w_rect->y = cur_y + 5;
                    w_rect->width = t->col_widths[c] - 10;
                    w_rect->height = t->row_height - 10;
                    t->cell_widgets[r][c]->layout.type = AROMA_LAYOUT_NONE;
                    aroma_node_invalidate(t->cell_widgets[r][c]);
                }
            }
            cur_x += t->col_widths[c];
        }
        cur_y += t->row_height;
    }
}

void aroma_table_set_col_width(AromaNode *table_node, int col_idx, int width)
{
    AromaTableInternal *t = get_table(table_node);
    if (t && col_idx >= 0 && col_idx < t->num_cols)
    {
        t->col_widths[col_idx] = width;
        _aroma_table_update_widgets(table_node);
        aroma_node_invalidate(table_node);
    }
}

void aroma_table_set_header(AromaNode *table_node, int col_idx, const char *text)
{
    AromaTableInternal *t = get_table(table_node);
    if (t && col_idx >= 0 && col_idx < t->num_cols && text)
    {
        strncpy(t->headers[col_idx], text, 63);
        t->headers[col_idx][63] = '\0';
        aroma_node_invalidate(table_node);
    }
}

int aroma_table_add_row(AromaNode *table_node)
{
    AromaTableInternal *t = get_table(table_node);
    if (!t || t->num_rows >= MAX_ROWS)
        return -1;
    int row = t->num_rows++;
    t->rect.height = t->header_height + (t->num_rows * t->row_height);

    if (table_node->parent_node && aroma_container_is_scrollable(table_node->parent_node))
    {
        aroma_container_set_content_size(table_node->parent_node, t->rect.width, t->rect.height);
    }

    aroma_node_invalidate(table_node);
    return row;
}

void aroma_table_set_cell_text(AromaNode *table_node, int row_idx, int col_idx, const char *text)
{
    AromaTableInternal *t = get_table(table_node);
    if (t && row_idx >= 0 && row_idx < t->num_rows && col_idx >= 0 && col_idx < t->num_cols && text)
    {
        strncpy(t->cells[row_idx][col_idx], text, 63);
        t->cells[row_idx][col_idx][63] = '\0';
        aroma_node_invalidate(table_node);
    }
}

int aroma_table_get_selected_row(AromaNode *table_node)
{
    AromaTableInternal *t = get_table(table_node);
    return t ? t->selected_row : -1;
}

void aroma_table_set_callback(AromaNode *table_node, void (*callback)(int, void *), void *user_data)
{
    AromaTableInternal *t = get_table(table_node);
    if (t)
    {
        t->callback = callback;
        t->user_data = user_data;
    }
}

void aroma_table_draw(AromaNode *table_node, size_t window_id)
{
    AromaTableInternal *t = get_table(table_node);
    if (!t || aroma_node_is_hidden(table_node))
        return;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->fill_rectangle || !gfx->render_text || !t->font)
        return;

    AromaTheme theme = aroma_theme_get_global();
    int cur_y = t->rect.y;
    int total_w = 0;
    for (int i = 0; i < t->num_cols; i++)
        total_w += t->col_widths[i];

    gfx->fill_rectangle(window_id, t->rect.x, cur_y, total_w, t->header_height, theme.colors.surface, false, 0);

    int cur_x = t->rect.x;
    for (int c = 0; c < t->num_cols; c++)
    {

        if (c > 0)
            gfx->fill_rectangle(window_id, cur_x, cur_y, 1, t->rect.height, theme.colors.border, false, 0);

        gfx->render_text(window_id, t->font, t->headers[c], cur_x + 10, cur_y + (t->header_height / 2) - 10, theme.colors.text_primary, 1.0f);
        cur_x += t->col_widths[c];
    }

    gfx->fill_rectangle(window_id, t->rect.x, cur_y + t->header_height - 1, total_w, 2, theme.colors.border, false, 0);
    cur_y += t->header_height;

    for (int r = 0; r < t->num_rows; r++)
    {
        if (r == t->selected_row)
        {
            gfx->fill_rectangle(window_id, t->rect.x, cur_y, total_w, t->row_height, theme.colors.primary, false, 0);
        }
        else if (r % 2 == 1)
        {
            uint32_t bg = theme.colors.surface;
            gfx->fill_rectangle(window_id, t->rect.x, cur_y, total_w, t->row_height, bg, false, 0);
        }

        cur_x = t->rect.x;
        for (int c = 0; c < t->num_cols; c++)
        {
            if (!t->cell_widgets[r][c] || t->cells[r][c][0] != '\0')
            {
                uint32_t text_col = (r == t->selected_row) ? theme.colors.surface : theme.colors.text_primary;
                gfx->render_text(window_id, t->font, t->cells[r][c], cur_x + 10, cur_y + (t->row_height / 2) - 10, text_col, 1.0f);
            }
            cur_x += t->col_widths[c];
        }

        gfx->fill_rectangle(window_id, t->rect.x, cur_y + t->row_height - 1, total_w, 1, theme.colors.border, false, 0);
        cur_y += t->row_height;
    }
}
void aroma_table_set_font(AromaNode *table_node, AromaFont *font)
{
    AromaTableInternal *t = get_table(table_node);
    if (t)
    {
        t->font = font;
        aroma_node_invalidate(table_node);
    }
}

void aroma_table_set_cell_widget(AromaNode *table_node, int row_idx, int col_idx, AromaNode *widget)
{
    AromaTableInternal *t = get_table(table_node);
    if (t && row_idx >= 0 && row_idx < t->num_rows && col_idx >= 0 && col_idx < t->num_cols)
    {
        t->cell_widgets[row_idx][col_idx] = widget;
        if (widget)
        {
            t->cells[row_idx][col_idx][0] = '\0';
        }
        _aroma_table_update_widgets(table_node);
        aroma_node_invalidate(table_node);
    }
}
