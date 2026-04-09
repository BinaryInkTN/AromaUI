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

#ifdef __cplusplus
}
#endif

#endif
