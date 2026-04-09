The **Data Table** widget (`AromaTableInternal`) provides a high-level structure for displaying tabular data. It inherently supports headers, scrollable containers (when built using `aroma_ui_table`), resizable columns, selectable rows, and the embedding of child widgets inside specific cells. 

## Features
- **Scrolling Support**: Automatically places the table within an internally managed scroll container using `aroma_ui_table()`.
- **Zebra Striping**: Alternating row backgrounds enhance readability.
- **Row Selection**: Built-in visual highlighting and click callbacks for selected rows.
- **Cell Widgets**: Easily insert interactive elements (like buttons or toggles) into cells, with the table automatically managing their layout during scrolling and resizing.

## Creation
To create a Data Table, typically you will use the `aroma_ui_table` inline function which wraps it in a vertical scroll container:

```c
// Create a table with 3 columns
AromaNode *table = aroma_ui_table(
    parent_node, 
    x, y, width, height, 
    3, 
    on_row_selected_cb, 
    user_data
);
```

## Usage

### Headers & Columns
Set up your columns explicitly by specifying their logical widths and text titles.

```c
aroma_table_set_col_width(table, 0, 150); // width of col 0 is 150px
aroma_table_set_col_width(table, 1, 200);

aroma_table_set_header(table, 0, "Name");
aroma_table_set_header(table, 1, "Age");
```

### Adding Rows
Instead of manually tracking row counts via a custom layout loop, you append a single row and modify its cell contents:

```c
int new_row = aroma_table_add_row(table);
aroma_table_set_cell_text(table, new_row, 0, "Alice");
aroma_table_set_cell_text(table, new_row, 1, "28");
```

### Cell Widgets
You can embed complex nodes inside cells. The data table handles their coordinate translations and layout updates internally. First, make the `table` the parent of your widget. Then, tell the table which cell should dock it:

```c
// Create a button as a child of the table
AromaNode *call_btn = aroma_ui_button(table, "Call", 0, 0, 80, 30, my_callback, NULL, my_font);

// Dock it inside Row 0, Column 2
aroma_table_set_cell_widget(table, 0, 2, call_btn);
```

## APIs
- `AromaNode* aroma_table_create(AromaNode* parent, int x, int y, int width, int height, int num_cols)`
- `void aroma_table_set_col_width(AromaNode* table_node, int col_idx, int width)`
- `void aroma_table_set_header(AromaNode* table_node, int col_idx, const char* text)`
- `int aroma_table_add_row(AromaNode* table_node)`
- `void aroma_table_set_cell_text(AromaNode* table_node, int row_idx, int col_idx, const char* text)`
- `void aroma_table_set_cell_widget(AromaNode* table_node, int row_idx, int col_idx, AromaNode* widget)`
- `int aroma_table_get_selected_row(AromaNode* table_node)`
- `void aroma_table_set_callback(AromaNode* table_node, void (*callback)(int row_idx, void* user_data), void* user_data)`
- `void aroma_table_set_font(AromaNode* table_node, AromaFont* font)`