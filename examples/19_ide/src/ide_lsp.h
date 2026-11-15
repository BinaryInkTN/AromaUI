#ifndef IDE_LSP_H
#define IDE_LSP_H

#include <stdbool.h>
#include <stddef.h>

#include "widgets/aroma_code_editor.h"

void ide_lsp_refresh_diagnostics(int file_index);
void ide_lsp_refresh_all(void);

int ide_lsp_complete(int file_index, int line, int column,
                     AromaCodeCompletion *out, int max);
bool ide_lsp_hover(int file_index, int line, int column,
                   char *out, size_t out_size);
bool ide_lsp_definition(int file_index, int line, int column,
                        int *target_file, int *target_line, int *target_column,
                        char *symbol, size_t symbol_size);
int ide_lsp_outline(int file_index, AromaCodeSymbol *out, int max);

#endif