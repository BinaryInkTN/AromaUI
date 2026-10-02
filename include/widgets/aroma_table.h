#ifndef AROMA_TABLE_H
#define AROMA_TABLE_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_font.h"

#ifdef __cplusplus
extern "C" {
#endif

AromaNode* aroma_table_create(AromaNode* parent, int x, int y, int width, int height, int num_cols);
void aroma_table_set_col_width(AromaNode* table_node, int col_idx, int width);
void aroma_table_set_header(AromaNode* table_node, int col_idx, const char* text);
int aroma_table_add_row(AromaNode* table_node);
void aroma_table_set_cell_text(AromaNode* table_node, int row_idx, int col_idx, const char* text);
void aroma_table_draw(AromaNode* table_node, size_t window_id);
int aroma_table_get_selected_row(AromaNode* table_node);
void aroma_table_set_callback(AromaNode* table_node, void (*callback)(int row_idx, void* user_data), void* user_data);
void aroma_table_set_font(AromaNode* table_node, AromaFont* font);
void aroma_table_set_cell_widget(AromaNode* table_node, int row_idx, int col_idx, AromaNode* widget);
/* Remove every row. When destroy_widgets is true, embedded cell widgets
 * are destroyed too (otherwise they survive, unpositioned, and can be
 * re-attached after re-adding rows). Selection resets to none. */
void aroma_table_clear_rows(AromaNode* table_node, bool destroy_widgets);
/* Row height in pixels (default 40). Repositions cell widgets. */
void aroma_table_set_row_height(AromaNode* table_node, int height);
/* Show/hide the header band (default shown). Hidden tables start at row 0. */
void aroma_table_set_header_visible(AromaNode* table_node, bool visible);
/* Select a row, or -1 to clear the selection. Out-of-range ignored. */
void aroma_table_set_selected_row(AromaNode* table_node, int row_idx);
int aroma_table_get_row_count(AromaNode* table_node);
void aroma_table_destroy(AromaNode* table_node);

#ifdef __cplusplus
}
#endif

#endif
