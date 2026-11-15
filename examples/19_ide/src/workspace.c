#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "workspace.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define IDE_MAX_FILE_BYTES (1024u * 1024u)
#define IDE_MAX_SCAN_DEPTH 16

IdeFile ide_files[IDE_MAX_FILES];
int ide_file_count = 0;
char ide_workspace_root[IDE_PATH_MAX];
char ide_workspace_name[IDE_NAME_MAX];

static char s_last_error[256];

static const char *file_name_of(const char *path)
{
    if (!path)
        return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool extension_matches(const char *name, const char *extension)
{
    size_t name_length = strlen(name);
    size_t extension_length = strlen(extension);
    if (name_length < extension_length)
        return false;
    const char *tail = name + name_length - extension_length;
    for (size_t i = 0; i < extension_length; i++)
    {
        char a = tail[i];
        char b = extension[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

static bool is_text_file_name(const char *name)
{
    static const char *const extensions[] = {
        ".c",       ".h",       ".cc",      ".hh",      ".cpp",    ".hpp",
        ".aroma",   ".md",      ".markdown", ".txt",    ".json",   ".cmake",
        ".yml",     ".yaml",    ".toml",    ".css",     ".html",   ".htm",
        ".js",      ".jsx",     ".ts",      ".tsx",     ".rs",     ".py",
        ".go",      ".sh",      ".xml",     ".ini",     ".cfg",    ".conf",
        ".glsl",    ".vert",    ".frag",    ".gitignore",
    };
    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++)
    {
        if (extension_matches(name, extensions[i]))
            return true;
    }
    return strcmp(name, "CMakeLists.txt") == 0 || strcmp(name, "Makefile") == 0 ||
           strcmp(name, "Dockerfile") == 0 || strcmp(name, "LICENSE") == 0 ||
           strcmp(name, "README") == 0 || strcmp(name, "CHANGELOG") == 0;
}

static bool is_ignored_directory(const char *name)
{
    if (!name || !name[0] || name[0] == '.')
        return true;
    if (strcmp(name, "build") == 0 || strcmp(name, "out") == 0 ||
        strcmp(name, "dist") == 0 || strcmp(name, "target") == 0 ||
        strcmp(name, "node_modules") == 0 || strcmp(name, "vendors") == 0)
        return true;
    if (strncmp(name, "build-", 6) == 0 || strncmp(name, "cmake-build-", 12) == 0)
        return true;
    return false;
}

static void set_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(s_last_error, sizeof(s_last_error), format, args);
    va_end(args);
}

const char *ide_workspace_last_error(void)
{
    return s_last_error;
}

static void free_files(void)
{
    for (int i = 0; i < ide_file_count; i++)
    {
        free(ide_files[i].text);
        ide_files[i].text = NULL;
    }
    memset(ide_files, 0, sizeof(ide_files));
    ide_file_count = 0;
}

static char *read_text_file(const char *absolute_path, size_t size)
{
    FILE *file = fopen(absolute_path, "rb");
    if (!file)
        return NULL;
    char *buffer = (char *)malloc(size + 1);
    if (!buffer)
    {
        fclose(file);
        return NULL;
    }
    size_t read_count = fread(buffer, 1, size, file);
    fclose(file);
    buffer[read_count] = '\0';
    if (memchr(buffer, '\0', read_count) != NULL)
    {
        free(buffer);
        return NULL;
    }
    return buffer;
}

static void add_file(const char *relative_path, const char *absolute_path, size_t size)
{
    (void)size;
    if (ide_file_count >= IDE_MAX_FILES)
        return;
    if (strlen(relative_path) >= IDE_PATH_MAX)
        return;

    char *text = read_text_file(absolute_path, size);
    if (!text)
        return;

    IdeFile *file = &ide_files[ide_file_count];
    memset(file, 0, sizeof(*file));
    snprintf(file->path, sizeof(file->path), "%s", relative_path);
    snprintf(file->name, sizeof(file->name), "%s", file_name_of(relative_path));
    file->language = aroma_code_editor_language_from_path(relative_path);
    file->text = text;
    file->open = false;
    file->dirty = false;
    ide_file_count++;
}

static void scan_directory(const char *absolute_dir, const char *relative_dir, int depth)
{
    if (depth > IDE_MAX_SCAN_DEPTH || ide_file_count >= IDE_MAX_FILES)
        return;

    DIR *directory = opendir(absolute_dir);
    if (!directory)
        return;

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL && ide_file_count < IDE_MAX_FILES)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (entry->d_name[0] == '.' && strcmp(entry->d_name, ".gitignore") != 0)
            continue;

        char absolute_path[PATH_MAX];
        char relative_path[IDE_PATH_MAX];
        if (relative_dir && relative_dir[0])
        {
            if (snprintf(absolute_path, sizeof(absolute_path), "%s/%s", absolute_dir,
                         entry->d_name) >= (int)sizeof(absolute_path))
                continue;
            if (snprintf(relative_path, sizeof(relative_path), "%s/%s", relative_dir,
                         entry->d_name) >= (int)sizeof(relative_path))
                continue;
        }
        else
        {
            if (snprintf(absolute_path, sizeof(absolute_path), "%s/%s", absolute_dir,
                         entry->d_name) >= (int)sizeof(absolute_path))
                continue;
            if (snprintf(relative_path, sizeof(relative_path), "%s", entry->d_name) >=
                (int)sizeof(relative_path))
                continue;
        }

        struct stat info;
        if (stat(absolute_path, &info) != 0)
            continue;

        if (S_ISDIR(info.st_mode))
        {
            if (!is_ignored_directory(entry->d_name))
                scan_directory(absolute_path, relative_path, depth + 1);
        }
        else if (S_ISREG(info.st_mode) && info.st_size <= (off_t)IDE_MAX_FILE_BYTES)
        {
            if (is_text_file_name(entry->d_name))
                add_file(relative_path, absolute_path, (size_t)info.st_size);
        }
    }
    closedir(directory);
}

static int compare_files(const void *a, const void *b)
{
    const IdeFile *left = (const IdeFile *)a;
    const IdeFile *right = (const IdeFile *)b;
    return strcmp(left->path, right->path);
}

static bool ensure_parent_directories(const char *absolute_path)
{
    char buffer[PATH_MAX];
    snprintf(buffer, sizeof(buffer), "%s", absolute_path);
    char *slash = strrchr(buffer, '/');
    if (!slash || slash == buffer)
        return true;
    *slash = '\0';

    for (char *p = buffer + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            if (mkdir(buffer, 0775) != 0 && errno != EEXIST)
            {
                set_error("Cannot create directory %s: %s", buffer, strerror(errno));
                *p = '/';
                return false;
            }
            *p = '/';
        }
    }
    if (mkdir(buffer, 0775) != 0 && errno != EEXIST)
    {
        set_error("Cannot create directory %s: %s", buffer, strerror(errno));
        return false;
    }
    return true;
}

static bool normalize_relative_path(const char *input, char *output, size_t output_size)
{
    if (!input || !input[0] || !output || output_size == 0)
        return false;
    while (*input == '/')
        input++;

    char temporary[IDE_PATH_MAX];
    size_t used = 0;
    while (*input)
    {
        while (*input == '/')
            input++;
        if (!*input)
            break;
        const char *component = input;
        while (*input && *input != '/')
            input++;
        size_t length = (size_t)(input - component);
        if (length == 1 && component[0] == '.')
            continue;
        if (length == 2 && component[0] == '.' && component[1] == '.')
            return false;
        if (used && used + 1 < sizeof(temporary))
            temporary[used++] = '/';
        for (size_t i = 0; i < length; i++)
        {
            if (used + 1 >= sizeof(temporary))
                return false;
            temporary[used++] = component[i];
        }
    }
    if (used == 0 || used >= output_size)
        return false;
    temporary[used] = '\0';
    snprintf(output, output_size, "%s", temporary);
    return true;
}

bool ide_workspace_init(const char *root)
{
    free_files();
    memset(ide_workspace_root, 0, sizeof(ide_workspace_root));
    memset(ide_workspace_name, 0, sizeof(ide_workspace_name));
    s_last_error[0] = '\0';

    if (!root || !root[0])
        root = ".";

    char resolved[PATH_MAX];
    const char *selected = root;
    if (realpath(root, resolved))
        selected = resolved;

    char normalized[PATH_MAX];
    snprintf(normalized, sizeof(normalized), "%s", selected);
    size_t length = strlen(normalized);
    while (length > 1 && normalized[length - 1] == '/')
        normalized[--length] = '\0';

    DIR *check = opendir(normalized);
    if (!check)
    {
        set_error("Cannot open project folder %s: %s", normalized, strerror(errno));
        return false;
    }
    closedir(check);

    snprintf(ide_workspace_root, sizeof(ide_workspace_root), "%s", normalized);
    snprintf(ide_workspace_name, sizeof(ide_workspace_name), "%s",
             file_name_of(normalized));

    scan_directory(ide_workspace_root, "", 0);
    if (ide_file_count > 1)
        qsort(ide_files, (size_t)ide_file_count, sizeof(IdeFile), compare_files);
    return true;
}

void ide_workspace_shutdown(void)
{
    free_files();
    ide_workspace_root[0] = '\0';
    ide_workspace_name[0] = '\0';
    s_last_error[0] = '\0';
}

const char *ide_workspace_root_path(void)
{
    return ide_workspace_root;
}

const char *ide_workspace_project_name(void)
{
    return ide_workspace_name[0] ? ide_workspace_name : "project";
}

bool ide_workspace_reload(void)
{
    typedef struct PreservedFile
    {
        char path[IDE_PATH_MAX];
        char *text;
        bool dirty;
        bool open;
    } PreservedFile;

    PreservedFile *preserved = NULL;
    int preserved_count = 0;
    if (ide_file_count > 0)
    {
        preserved = (PreservedFile *)calloc((size_t)ide_file_count, sizeof(*preserved));
        if (!preserved)
        {
            set_error("Out of memory while reloading workspace");
            return false;
        }
        for (int i = 0; i < ide_file_count; i++)
        {
            if (!ide_files[i].dirty && !ide_files[i].open)
                continue;
            PreservedFile *entry = &preserved[preserved_count++];
            snprintf(entry->path, sizeof(entry->path), "%s", ide_files[i].path);
            entry->dirty = ide_files[i].dirty;
            entry->open = ide_files[i].open;
            if (ide_files[i].dirty && ide_files[i].text)
            {
                size_t length = strlen(ide_files[i].text);
                entry->text = (char *)malloc(length + 1);
                if (entry->text)
                    memcpy(entry->text, ide_files[i].text, length + 1);
            }
        }
    }

    free_files();
    scan_directory(ide_workspace_root, "", 0);
    if (ide_file_count > 1)
        qsort(ide_files, (size_t)ide_file_count, sizeof(IdeFile), compare_files);

    for (int i = 0; i < preserved_count; i++)
    {
        int index = ide_workspace_file_by_path(preserved[i].path);
        if (index < 0)
        {
            free(preserved[i].text);
            continue;
        }
        ide_files[index].open = preserved[i].open;
        if (preserved[i].text)
        {
            free(ide_files[index].text);
            ide_files[index].text = preserved[i].text;
            ide_files[index].dirty = true;
        }
        else
        {
            free(preserved[i].text);
        }
    }
    free(preserved);
    return true;
}

int ide_workspace_file_by_path(const char *path)
{
    if (!path)
        return -1;
    for (int i = 0; i < ide_file_count; i++)
    {
        if (strcmp(ide_files[i].path, path) == 0)
            return i;
    }
    return -1;
}

const char *ide_workspace_absolute_path(int index, char *out, size_t out_size)
{
    if (index < 0 || index >= ide_file_count || !out || out_size == 0)
        return NULL;
    snprintf(out, out_size, "%s/%s", ide_workspace_root, ide_files[index].path);
    return out;
}

bool ide_workspace_save(int index)
{
    if (index < 0 || index >= ide_file_count)
        return false;
    IdeFile *file = &ide_files[index];
    char absolute_path[PATH_MAX];
    if (!ide_workspace_absolute_path(index, absolute_path, sizeof(absolute_path)))
        return false;
    if (!ensure_parent_directories(absolute_path))
        return false;

    FILE *handle = fopen(absolute_path, "wb");
    if (!handle)
    {
        set_error("Cannot save %s: %s", file->path, strerror(errno));
        return false;
    }
    const char *text = file->text ? file->text : "";
    size_t length = strlen(text);
    size_t written = fwrite(text, 1, length, handle);
    bool ok = written == length;
    if (fclose(handle) != 0)
        ok = false;
    if (!ok)
    {
        set_error("Failed while writing %s", file->path);
        return false;
    }
    file->dirty = false;
    return true;
}

int ide_workspace_save_all(void)
{
    int saved = 0;
    for (int i = 0; i < ide_file_count; i++)
    {
        if (ide_files[i].open && ide_files[i].dirty)
        {
            if (ide_workspace_save(i))
                saved++;
        }
    }
    return saved;
}

int ide_workspace_create_file(const char *relative_path)
{
    char normalized[IDE_PATH_MAX];
    if (!normalize_relative_path(relative_path, normalized, sizeof(normalized)))
    {
        set_error("Enter a valid path inside the project");
        return -1;
    }
    int existing = ide_workspace_file_by_path(normalized);
    if (existing >= 0)
        return existing;
    if (ide_file_count >= IDE_MAX_FILES)
    {
        set_error("The workspace already contains the maximum number of files");
        return -1;
    }

    char absolute_path[PATH_MAX];
    if (snprintf(absolute_path, sizeof(absolute_path), "%s/%s", ide_workspace_root,
                 normalized) >= (int)sizeof(absolute_path))
    {
        set_error("The path is too long");
        return -1;
    }
    if (!ensure_parent_directories(absolute_path))
        return -1;

    FILE *handle = fopen(absolute_path, "ab");
    if (!handle)
    {
        set_error("Cannot create %s: %s", normalized, strerror(errno));
        return -1;
    }
    fclose(handle);

    struct stat info;
    if (stat(absolute_path, &info) != 0 || !S_ISREG(info.st_mode))
    {
        set_error("Cannot create %s", normalized);
        return -1;
    }

    IdeFile *file = &ide_files[ide_file_count];
    memset(file, 0, sizeof(*file));
    snprintf(file->path, sizeof(file->path), "%s", normalized);
    snprintf(file->name, sizeof(file->name), "%s", file_name_of(normalized));
    file->language = aroma_code_editor_language_from_path(normalized);
    file->text = (char *)calloc(1, 1);
    if (!file->text)
    {
        set_error("Out of memory while creating %s", normalized);
        return -1;
    }
    ide_file_count++;
    return ide_file_count - 1;
}

bool ide_workspace_delete_file(int index)
{
    if (index < 0 || index >= ide_file_count)
        return false;
    char absolute_path[PATH_MAX];
    if (!ide_workspace_absolute_path(index, absolute_path, sizeof(absolute_path)))
        return false;
    if (unlink(absolute_path) != 0)
    {
        set_error("Cannot delete %s: %s", ide_files[index].path, strerror(errno));
        return false;
    }
    free(ide_files[index].text);
    for (int i = index; i < ide_file_count - 1; i++)
        ide_files[i] = ide_files[i + 1];
    ide_file_count--;
    memset(&ide_files[ide_file_count], 0, sizeof(ide_files[ide_file_count]));
    return true;
}