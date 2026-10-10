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
#include "aroma_dp.h"
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define MAX_ROWS 100
#define MAX_COLS 10
#define AROMA_TABLE_ROW_HEIGHT_DP 40
#define AROMA_TABLE_HEADER_HEIGHT_DP 40
#define AROMA_TABLE_CELL_PAD_DP 5
#define AROMA_TABLE_CELL_INSET_DP 10
#define AROMA_TABLE_TEXT_PAD_X_DP 10
#define AROMA_TABLE_TEXT_Y_OFFSET_DP 10
#define AROMA_TABLE_SEPARATOR_WIDTH_DP 1
#define AROMA_TABLE_HEADER_UNDERLINE_H_DP 2
#define AROMA_TABLE_ROW_SEPARATOR_H_DP 1
#define AROMA_TABLE_MIN_ROW_H_DP 8
#define AROMA_TABLE_MAX_ROW_H_DP 400

#ifdef __ANDROID__
static inline int table_dp(int dp) { return aroma_android_dp_to_px(dp); }
static inline float table_dp_f(float dp) { return aroma_android_dp_to_px_f(dp); }
#else
static inline int table_dp(int dp) { return dp; }
static inline float table_dp_f(float dp) { return dp; }
#endif

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
    bool header_visible;
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

static int table_header_h(const AromaTableInternal *t)
{
    return (t && t->header_visible) ? t->header_height : 0;
}

static void table_sync_scroll_size(AromaNode *table_node,
                                   const AromaTableInternal *t)
{
    if (!table_node || !t)
        return;
    if (table_node->parent_node &&
        aroma_container_is_scrollable(table_node->parent_node))
    {
        aroma_container_set_content_size(
            table_node->parent_node, t->rect.width,
            table_header_h(t) + (t->num_rows * t->row_height));
    }
}

static bool __table_handle_event(AromaEvent *event, void *user_data)
{
    AromaNode *node = (AromaNode *)user_data;
    AromaTableInternal *t = get_table(node);
    if (!t || !event)
        return false;


    int adjusted_x = event->data.mouse.x;
    int adjusted_y = event->data.mouse.y;
    if (event->target_node)
    {
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
    }

    if (event->event_type == EVENT_TYPE_MOUSE_CLICK || event->event_type == EVENT_TYPE_TOUCH_DOWN)
    {
        int header_h = table_header_h(t);
        int my = adjusted_y - t->rect.y;
        if (my > header_h)
        {
            int row = (my - header_h) / t->row_height;
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
    t->row_height = table_dp(AROMA_TABLE_ROW_HEIGHT_DP);
    t->header_height = table_dp(AROMA_TABLE_HEADER_HEIGHT_DP);
    t->header_visible = true;
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
    int cur_y = t->rect.y + table_header_h(t);
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
                    int cell_pad = table_dp(AROMA_TABLE_CELL_PAD_DP);
                    int cell_inset = table_dp(AROMA_TABLE_CELL_INSET_DP);
                    w_rect->x = cur_x + cell_pad;
                    w_rect->y = cur_y + cell_pad;
                    w_rect->width = t->col_widths[c] - cell_inset;
                    w_rect->height = t->row_height - cell_inset;
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
        t->col_widths[col_idx] = table_dp(width);
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
    t->rect.height = table_header_h(t) + (t->num_rows * t->row_height);

    table_sync_scroll_size(table_node, t);

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

    int header_h = table_header_h(t);
    if (header_h > 0)
    {
        gfx->fill_rectangle(window_id, t->rect.x, cur_y, total_w, header_h, theme.colors.surface, false, 0);

        int cur_x = t->rect.x;
        int sep_w = table_dp(AROMA_TABLE_SEPARATOR_WIDTH_DP);
        if (sep_w < 1) sep_w = 1;
        for (int c = 0; c < t->num_cols; c++)
        {

            if (c > 0)
                gfx->fill_rectangle(window_id, cur_x, cur_y, sep_w, t->rect.height, theme.colors.border, false, 0);

            gfx->render_text(window_id, t->font, t->headers[c], cur_x + table_dp(AROMA_TABLE_TEXT_PAD_X_DP), cur_y + (header_h / 2) - table_dp(AROMA_TABLE_TEXT_Y_OFFSET_DP), theme.colors.text_primary, 1.0f);
            cur_x += t->col_widths[c];
        }

        int ul_h = table_dp(AROMA_TABLE_HEADER_UNDERLINE_H_DP);
        if (ul_h < 1) ul_h = 1;
        int ul_off = table_dp(AROMA_TABLE_SEPARATOR_WIDTH_DP);
        if (ul_off < 1) ul_off = 1;
        gfx->fill_rectangle(window_id, t->rect.x, cur_y + header_h - ul_off, total_w, ul_h, theme.colors.border, false, 0);
        cur_y += header_h;
    }

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

        int cur_x = t->rect.x;
        for (int c = 0; c < t->num_cols; c++)
        {
            if (!t->cell_widgets[r][c] || t->cells[r][c][0] != '\0')
            {
                uint32_t text_col = (r == t->selected_row) ? theme.colors.surface : theme.colors.text_primary;
                gfx->render_text(window_id, t->font, t->cells[r][c], cur_x + table_dp(AROMA_TABLE_TEXT_PAD_X_DP), cur_y + (t->row_height / 2) - table_dp(AROMA_TABLE_TEXT_Y_OFFSET_DP), text_col, 1.0f);
            }
            cur_x += t->col_widths[c];
        }

        int row_sep_h = table_dp(AROMA_TABLE_ROW_SEPARATOR_H_DP);
        if (row_sep_h < 1) row_sep_h = 1;
        int row_sep_off = table_dp(AROMA_TABLE_SEPARATOR_WIDTH_DP);
        if (row_sep_off < 1) row_sep_off = 1;
        gfx->fill_rectangle(window_id, t->rect.x, cur_y + t->row_height - row_sep_off, total_w, row_sep_h, theme.colors.border, false, 0);
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

void aroma_table_clear_rows(AromaNode *table_node, bool destroy_widgets)
{
    AromaTableInternal *t = get_table(table_node);
    if (!t)
        return;
    if (destroy_widgets)
    {
        for (int r = 0; r < t->num_rows; r++)
        {
            for (int c = 0; c < t->num_cols; c++)
            {
                if (t->cell_widgets[r][c])
                {


                    __destroy_node_tree(t->cell_widgets[r][c]);
                    t->cell_widgets[r][c] = NULL;
                }
            }
        }
    }
    else
    {
        memset(t->cell_widgets, 0, sizeof(t->cell_widgets));
    }
    t->num_rows = 0;
    t->selected_row = -1;
    t->rect.height = table_header_h(t);
    table_sync_scroll_size(table_node, t);
    aroma_node_invalidate(table_node);
}

void aroma_table_set_row_height(AromaNode *table_node, int height)
{
    AromaTableInternal *t = get_table(table_node);
    if (!t)
        return;
    int min_h = table_dp(AROMA_TABLE_MIN_ROW_H_DP);
    int max_h = table_dp(AROMA_TABLE_MAX_ROW_H_DP);
    int scaled = table_dp(height);
    if (scaled < min_h)
        scaled = min_h;
    if (scaled > max_h)
        scaled = max_h;
    t->row_height = scaled;
    t->rect.height = table_header_h(t) + (t->num_rows * t->row_height);
    _aroma_table_update_widgets(table_node);
    table_sync_scroll_size(table_node, t);
    aroma_node_invalidate(table_node);
}

void aroma_table_set_header_visible(AromaNode *table_node, bool visible)
{
    AromaTableInternal *t = get_table(table_node);
    if (!t)
        return;
    t->header_visible = visible;
    t->rect.height = table_header_h(t) + (t->num_rows * t->row_height);
    _aroma_table_update_widgets(table_node);
    table_sync_scroll_size(table_node, t);
    aroma_node_invalidate(table_node);
}

void aroma_table_set_selected_row(AromaNode *table_node, int row_idx)
{
    AromaTableInternal *t = get_table(table_node);
    if (!t)
        return;
    if (row_idx < -1 || row_idx >= t->num_rows)
        return;
    t->selected_row = row_idx;
    aroma_node_invalidate(table_node);
}

int aroma_table_get_row_count(AromaNode *table_node)
{
    AromaTableInternal *t = get_table(table_node);
    return t ? t->num_rows : 0;
}

void aroma_table_destroy(AromaNode *table_node)
{
    AromaTableInternal *t = get_table(table_node);
    if (!t)
        return;
    for (int r = 0; r < t->num_rows; r++)
    {
        for (int c = 0; c < t->num_cols; c++)
        {
            if (t->cell_widgets[r][c])
            {
                __destroy_node_tree(t->cell_widgets[r][c]);
                t->cell_widgets[r][c] = NULL;
            }
        }
    }
    t->num_rows = 0;

    __destroy_node(table_node);
}
