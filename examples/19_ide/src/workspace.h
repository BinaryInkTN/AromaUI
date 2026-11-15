#ifndef IDE_WORKSPACE_H
#define IDE_WORKSPACE_H

#include <stdbool.h>
#include <stddef.h>

#include "widgets/aroma_code_editor.h"

#define IDE_MAX_FILES 256
#define IDE_PATH_MAX 512
#define IDE_NAME_MAX 128

typedef struct IdeFile
{
    char name[IDE_NAME_MAX];
    char path[IDE_PATH_MAX];
    AromaCodeLanguage language;
    char *text;
    size_t cursor;
    size_t anchor;
    bool has_selection;
    int scroll_x;
    int scroll_y;
    bool open;
    bool dirty;

    AromaCodeDiagnostic diagnostics[AROMA_CODE_EDITOR_MAX_DIAGNOSTICS];
    int diagnostic_count;
} IdeFile;

extern IdeFile ide_files[IDE_MAX_FILES];
extern int ide_file_count;
extern char ide_workspace_root[IDE_PATH_MAX];
extern char ide_workspace_name[IDE_NAME_MAX];

bool ide_workspace_init(const char *root);
void ide_workspace_shutdown(void);
bool ide_workspace_reload(void);

const char *ide_workspace_root_path(void);
const char *ide_workspace_project_name(void);
const char *ide_workspace_last_error(void);

bool ide_workspace_save(int index);
int ide_workspace_save_all(void);
int ide_workspace_create_file(const char *relative_path);
bool ide_workspace_delete_file(int index);

int ide_workspace_file_by_path(const char *path);
const char *ide_workspace_absolute_path(int index, char *out, size_t out_size);

#endif