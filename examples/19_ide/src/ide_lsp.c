#include "ide_lsp.h"

#include "workspace.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static bool c_is_ident_char(char c);

                                                                             
                                                                          
                                                                            
static const char *find_in_range(const char *begin, const char *end,
                                 const char *needle, size_t needle_len)
{
    if (!begin || end < begin || needle_len == 0 ||
        (size_t)(end - begin) < needle_len)
        return NULL;
    const char *last = end - needle_len;
    char first = needle[0];
    for (const char *p = begin; p <= last; p++)
    {
        p = (const char *)memchr(p, first, (size_t)(last - p) + 1);
        if (!p)
            return NULL;
        if (memcmp(p, needle, needle_len) == 0)
            return p;
    }
    return NULL;
}

static uint64_t hash_bytes(uint64_t h, const char *data)
{
    for (const unsigned char *p = (const unsigned char *)data; *p; p++)
    {
        h ^= *p;
        h *= 1099511628211ULL;
    }
    return h;
}

                                                                           
                                                                         
typedef struct PathCacheEntry
{
    char path[1024];
    bool exists;
    time_t stamp;
} PathCacheEntry;

static bool path_exists_cached(const char *path)
{
    static PathCacheEntry cache[64];
    static int next_slot;
    time_t now = time(NULL);
    for (int i = 0; i < 64; i++)
    {
        if (cache[i].path[0] && strcmp(cache[i].path, path) == 0 &&
            now - cache[i].stamp < 2)
            return cache[i].exists;
    }
    FILE *check = fopen(path, "rb");
    bool exists = check != NULL;
    if (check)
        fclose(check);
    int slot = next_slot;
    next_slot = (next_slot + 1) % 64;
    snprintf(cache[slot].path, sizeof(cache[slot].path), "%s", path);
    cache[slot].exists = exists;
    cache[slot].stamp = now;
    return exists;
}

typedef struct CompletionSpec
{
    const char *label;
    const char *detail;
    const char *insert;
    AromaCodeCompletionKind kind;
    const char *doc;
} CompletionSpec;

#define COMPLETION_COUNT(list) ((int)(sizeof(list) / sizeof((list)[0])))

static void copy_completion(AromaCodeCompletion *out, const CompletionSpec *spec);

static const char *file_text(int file_index)
{
    if (file_index < 0 || file_index >= ide_file_count)
        return "";
    return ide_files[file_index].text ? ide_files[file_index].text : "";
}

static bool is_word_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '$';
}

static const char *line_pointer(const char *text, int line, int *out_length)
{
    if (!text || line < 1)
    {
        if (out_length)
            *out_length = 0;
        return NULL;
    }
    const char *p = text;
    int current = 1;
    while (current < line)
    {
        const char *nl = strchr(p, '\n');
        if (!nl)
        {
            if (out_length)
                *out_length = 0;
            return NULL;
        }
        p = nl + 1;
        current++;
    }
    const char *end = strchr(p, '\n');
    if (!end)
        end = p + strlen(p);
    if (out_length)
        *out_length = (int)(end - p);
    return p;
}

static bool word_at(const char *text, int line, int column, char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return false;
    out[0] = '\0';
    int length = 0;
    const char *p = line_pointer(text, line, &length);
    if (!p || length <= 0)
        return false;
    int pos = column - 1;
    if (pos < 0)
        pos = 0;
    if (pos > length)
        pos = length;
    if (pos == length || !is_word_char(p[pos]))
    {
        if (pos > 0 && is_word_char(p[pos - 1]))
            pos--;
        else
            return false;
    }
    int start = pos;
    int end = pos;
    while (start > 0 && is_word_char(p[start - 1]))
        start--;
    while (end < length && is_word_char(p[end]))
        end++;
    size_t word_length = (size_t)(end - start);
    if (word_length >= out_size)
        word_length = out_size - 1;
    memcpy(out, p + start, word_length);
    out[word_length] = '\0';
    return word_length > 0;
}

static int word_start_column(const char *text, int line, int column)
{
    int length = 0;
    const char *p = line_pointer(text, line, &length);
    if (!p)
        return 1;
    int pos = column - 1;
    if (pos < 0)
        pos = 0;
    if (pos > length)
        pos = length;
    if (pos == length || !is_word_char(p[pos]))
    {
        if (pos > 0 && is_word_char(p[pos - 1]))
            pos--;
        else
            return column;
    }
    while (pos > 0 && is_word_char(p[pos - 1]))
        pos--;
    return pos + 1;
}

static int find_token_column(const char *line, const char *token, int start)
{
    if (!line || !token || !token[0])
        return 0;
    const char *found = strstr(line + (start > 0 ? start : 0), token);
    if (!found)
        return 0;
    return (int)(found - line) + 1;
}

static void add_diagnostic(IdeFile *file, int line, int column, int end_column,
                           AromaCodeDiagnosticSeverity severity,
                           const char *message, const char *source, const char *code)
{
    if (!file || file->diagnostic_count >= AROMA_CODE_EDITOR_MAX_DIAGNOSTICS)
        return;
    AromaCodeDiagnostic *diag = &file->diagnostics[file->diagnostic_count++];
    memset(diag, 0, sizeof(*diag));
    diag->line = line > 0 ? line : 1;
    diag->column = column > 0 ? column : 1;
    diag->end_line = diag->line;
    diag->end_column = end_column > diag->column ? end_column : diag->column + 1;
    diag->severity = severity;
    snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "");
    snprintf(diag->source, sizeof(diag->source), "%s", source ? source : "aroma-lsp");
    snprintf(diag->code, sizeof(diag->code), "%s", code ? code : "");
}

static int count_occurrences(const char *text, const char *needle)
{
    if (!text || !needle || !needle[0])
        return 0;
    int count = 0;
    size_t length = strlen(needle);
    const char *p = text;
    while ((p = strstr(p, needle)) != NULL)
    {
        count++;
        p += length;
    }
    return count;
}

static void scan_common_diagnostics(IdeFile *file)
{
    const char *text = file ? file->text : NULL;
    if (!text)
        return;
    int line_no = 1;
    const char *p = text;
    while (*p)
    {
        const char *end = p;
        while (*end && *end != '\n')
            end++;
        int length = (int)(end - p);
        char line[512];
        int copy = length < (int)sizeof(line) - 1 ? length : (int)sizeof(line) - 1;
        memcpy(line, p, (size_t)copy);
        line[copy] = '\0';

        const char *todo = strstr(line, "TODO");
        const char *fixme = strstr(line, "FIXME");
        if (fixme)
        {
            add_diagnostic(file, line_no, (int)(fixme - line) + 1,
                           (int)(fixme - line) + 6, AROMA_CODE_DIAGNOSTIC_WARNING,
                           "Resolve this FIXME before shipping.", "workspace", "fixme");
        }
        else if (todo)
        {
            add_diagnostic(file, line_no, (int)(todo - line) + 1,
                           (int)(todo - line) + 5, AROMA_CODE_DIAGNOSTIC_INFO,
                           "TODO left in this file.", "workspace", "todo");
        }

        if (file->language == AROMA_CODE_LANGUAGE_TSX ||
            file->language == AROMA_CODE_LANGUAGE_TYPESCRIPT ||
            file->language == AROMA_CODE_LANGUAGE_JAVASCRIPT)
        {
            const char *any = strstr(line, ": any");
            if (any)
            {
                int col = find_token_column(line, "any", (int)(any - line) + 3);
                add_diagnostic(file, line_no, col, col + 3, AROMA_CODE_DIAGNOSTIC_ERROR,
                               "Unexpected any. Specify a different type.",
                               "eslint", "no-explicit-any");
            }
            if (strstr(line, "console.info"))
            {
                int col = find_token_column(line, "console", 0);
                add_diagnostic(file, line_no, col, col + 7, AROMA_CODE_DIAGNOSTIC_INFO,
                               "Avoid console statements in production code.",
                               "eslint", "no-console");
            }
        }
        else if (file->language == AROMA_CODE_LANGUAGE_CSS)
        {
            if (strstr(line, "text-wrap"))
            {
                int col = find_token_column(line, "text-wrap", 0);
                add_diagnostic(file, line_no, col, col + 9, AROMA_CODE_DIAGNOSTIC_INFO,
                               "text-wrap is outside the target browser matrix.",
                               "stylelint", "browser-compat");
            }
        }
        else if (file->language == AROMA_CODE_LANGUAGE_RUST)
        {
            if (strstr(line, "unused_count"))
            {
                int col = find_token_column(line, "unused_count", 0);
                add_diagnostic(file, line_no, col, col + 12, AROMA_CODE_DIAGNOSTIC_WARNING,
                               "unused variable: `unused_count`", "rustc", "unused_variables");
            }
        }
        else if (file->language == AROMA_CODE_LANGUAGE_YAML)
        {
            if (strstr(line, "version:"))
            {
                int col = find_token_column(line, "version", 0);
                add_diagnostic(file, line_no, col, col + 7, AROMA_CODE_DIAGNOSTIC_WARNING,
                               "The Compose version field is obsolete and ignored.",
                               "compose-lsp", "obsolete-version");
            }
        }
        else if (file->language == AROMA_CODE_LANGUAGE_DOCKERFILE)
        {
            if (strstr(line, "FROM node:20"))
            {
                int col = find_token_column(line, "node:20", 0);
                add_diagnostic(file, line_no, col, col + 7, AROMA_CODE_DIAGNOSTIC_INFO,
                               "Pin the base image to a digest for reproducible builds.",
                               "dockerfile-lsp", "pin-image");
            }
            if (strstr(line, "RUN npm install"))
            {
                int col = find_token_column(line, "npm install", 0);
                add_diagnostic(file, line_no, col, col + 11, AROMA_CODE_DIAGNOSTIC_WARNING,
                               "Use `npm ci` for reproducible installs.",
                               "dockerfile-lsp", "npm-ci");
            }
        }
        else if (file->language == AROMA_CODE_LANGUAGE_AROMA)
        {
            if (strstr(line, "corner_radius: 999"))
            {
                int col = find_token_column(line, "999", 0);
                add_diagnostic(file, line_no, col, col + 3, AROMA_CODE_DIAGNOSTIC_WARNING,
                               "Corner radius exceeds half of the component height.",
                               "aroma-incense", "radius-range");
            }
        }

        p = *end ? end + 1 : end;
        line_no++;
    }
}

static void lsp_scan_file(int file_index)
{
    IdeFile *file = &ide_files[file_index];
    file->diagnostic_count = 0;
    if (!file->text)
        return;

    scan_common_diagnostics(file);

    if (file->language == AROMA_CODE_LANGUAGE_AROMA)
    {
        int braces = 0;
        bool in_string = false;
        bool escaped = false;
        for (const char *cursor = file->text; *cursor; cursor++)
        {
            char c = *cursor;
            if (in_string)
            {
                if (escaped)
                    escaped = false;
                else if (c == '\\')
                    escaped = true;
                else if (c == '"')
                    in_string = false;
                continue;
            }
            if (c == '"')
            {
                in_string = true;
            }
            else if (c == '{')
            {
                braces++;
            }
            else if (c == '}')
            {
                braces--;
                if (braces < 0)
                    break;
            }
        }
        if (braces != 0)
            add_diagnostic(file, 1, 1, 2, AROMA_CODE_DIAGNOSTIC_ERROR,
                           braces > 0 ? "Unclosed Aroma block: add a closing brace."
                                      : "Unexpected closing brace in Aroma markup.",
                           "aroma-incense", "brace-balance");
        /* A Window root is optional: files without one are components /
         * fragments and the designer sizes its canvas to fit the content. */

        char seen_ids[24][64];
        int seen_count = 0;
        int line_no = 1;
        for (const char *cursor = file->text; *cursor; cursor++)
        {
            const char *line_end = strchr(cursor, '\n');
            if (!line_end)
                line_end = cursor + strlen(cursor);
            const char *id = find_in_range(cursor, line_end, "id:", 3);
            if (id)
            {
                const char *quote = (const char *)memchr(id, '"', (size_t)(line_end - id));
                const char *close = quote
                                        ? (const char *)memchr(quote + 1, '"',
                                                               (size_t)(line_end - quote - 1))
                                        : NULL;
                if (quote && close)
                {
                    char value[64];
                    int size = (int)(close - quote - 1);
                    if (size >= (int)sizeof(value))
                        size = (int)sizeof(value) - 1;
                    memcpy(value, quote + 1, (size_t)size);
                    value[size] = '\0';
                    bool duplicate = false;
                    for (int i = 0; i < seen_count; i++)
                    {
                        if (strcmp(seen_ids[i], value) == 0)
                            duplicate = true;
                    }
                    if (duplicate)
                        add_diagnostic(file, line_no, (int)(quote - cursor) + 1,
                                       (int)(close - cursor) + 1,
                                       AROMA_CODE_DIAGNOSTIC_WARNING,
                                       "Duplicate widget id; Incense lookups will return the first match.",
                                       "aroma-incense", "duplicate-id");
                    else if (seen_count < 24)
                        snprintf(seen_ids[seen_count++], sizeof(seen_ids[0]), "%s", value);
                }
            }
            if (*line_end == '\0')
                break;
            cursor = line_end;
            line_no++;
        }
    }
    else if (file->language == AROMA_CODE_LANGUAGE_C ||
             file->language == AROMA_CODE_LANGUAGE_CPP ||
             file->language == AROMA_CODE_LANGUAGE_PLAIN)
    {
        int braces = 0;
        int parentheses = 0;
        int brackets = 0;
        bool in_string = false;
        bool in_char = false;
        bool in_line_comment = false;
        bool in_block_comment = false;
        bool escaped = false;
        int literal_line = 1;
        int literal_column = 1;
        int scan_line = 1;
        int scan_column = 1;
        for (const char *cursor = file->text; *cursor; cursor++)
        {
            char c = *cursor;
            char next = cursor[1];
            if (c == '\n')
            {
                scan_line++;
                scan_column = 1;
            }
            else
            {
                scan_column++;
            }
            if (in_line_comment)
            {
                if (c == '\n')
                    in_line_comment = false;
                continue;
            }
            if (in_block_comment)
            {
                if (c == '*' && next == '/')
                {
                    in_block_comment = false;
                    cursor++;
                }
                continue;
            }
            if (in_string || in_char)
            {
                if (escaped)
                    escaped = false;
                else if (c == '\\')
                    escaped = true;
                else if (in_string && c == '"')
                    in_string = false;
                else if (in_char && c == '\'')
                    in_char = false;
                else if (c == '\n')
                {
                    add_diagnostic(file, literal_line, literal_column,
                                   literal_column + 1,
                                   AROMA_CODE_DIAGNOSTIC_ERROR,
                                   in_string ? "Unterminated string literal."
                                             : "Unterminated character constant.",
                                   "clang", "unterminated-literal");
                    in_string = false;
                    in_char = false;
                }
                continue;
            }
            if (c == '/' && next == '/')
            {
                in_line_comment = true;
                cursor++;
            }
            else if (c == '/' && next == '*')
            {
                in_block_comment = true;
                cursor++;
            }
            else if (c == '"')
            {
                in_string = true;
                literal_line = scan_line;
                literal_column = scan_column;
            }
            else if (c == '\'')
            {
                in_char = true;
                literal_line = scan_line;
                literal_column = scan_column;
            }
            else if (c == '{')
                braces++;
            else if (c == '}')
                braces--;
            else if (c == '(')
                parentheses++;
            else if (c == ')')
                parentheses--;
            else if (c == '[')
                brackets++;
            else if (c == ']')
                brackets--;
        }
        if (in_string || in_char)
            add_diagnostic(file, literal_line, literal_column, literal_column + 1,
                           AROMA_CODE_DIAGNOSTIC_ERROR,
                           in_string ? "Unterminated string literal."
                                     : "Unterminated character constant.",
                           "clang", "unterminated-literal");
        if (braces != 0)
            add_diagnostic(file, 1, 1, 2, AROMA_CODE_DIAGNOSTIC_ERROR,
                           braces > 0 ? "Unclosed block: a closing brace is missing."
                                      : "Unexpected closing brace.",
                           "clang", "brace-balance");
        if (parentheses != 0)
            add_diagnostic(file, 1, 1, 2, AROMA_CODE_DIAGNOSTIC_ERROR,
                           "Unbalanced parentheses in this source file.",
                           "clang", "paren-balance");
        if (brackets != 0)
            add_diagnostic(file, 1, 1, 2, AROMA_CODE_DIAGNOSTIC_ERROR,
                           brackets > 0 ? "Unclosed subscript: a closing bracket is missing."
                                        : "Unexpected closing bracket.",
                           "clang", "bracket-balance");
        if (in_block_comment)
            add_diagnostic(file, 1, 1, 2, AROMA_CODE_DIAGNOSTIC_WARNING,
                           "Unterminated block comment.", "clang", "comment-eof");

        int line_no = 1;
        int preprocessor_depth = 0;
        const char *line = file->text;
        while (*line)
        {
            const char *end = strchr(line, '\n');
            if (!end)
                end = line + strlen(line);
            const char *hash = line;
            while (hash < end && (*hash == ' ' || *hash == '\t'))
                hash++;
            if (hash < end && *hash == '#')
            {
                const char *directive = hash + 1;
                while (directive < end &&
                       (*directive == ' ' || *directive == '\t'))
                    directive++;
                if (strncmp(directive, "if", 2) == 0 &&
                    !c_is_ident_char(directive[2]))
                    preprocessor_depth++;
                else if ((strncmp(directive, "ifdef", 5) == 0 &&
                          !c_is_ident_char(directive[5])) ||
                         (strncmp(directive, "ifndef", 6) == 0 &&
                          !c_is_ident_char(directive[6])))
                    preprocessor_depth++;
                else if (strncmp(directive, "endif", 5) == 0 &&
                         !c_is_ident_char(directive[5]))
                {
                    if (preprocessor_depth <= 0)
                        add_diagnostic(file, line_no, (int)(hash - line) + 1,
                                       (int)(hash - line) + 6,
                                       AROMA_CODE_DIAGNOSTIC_ERROR,
                                       "Unexpected #endif without a matching #if.",
                                       "clang", "pp-endif");
                    else
                        preprocessor_depth--;
                }
                else if ((strncmp(directive, "else", 4) == 0 ||
                          strncmp(directive, "elif", 4) == 0) &&
                         !c_is_ident_char(directive[4]) &&
                         preprocessor_depth == 0)
                    add_diagnostic(file, line_no, (int)(hash - line) + 1,
                                   (int)(hash - line) + 5,
                                   AROMA_CODE_DIAGNOSTIC_WARNING,
                                   "Conditional directive without an active #if.",
                                   "clang", "pp-else");
            }
            const char *include = NULL;
            const char *hit = find_in_range(line, end, "#include", 8);
            if (hit && hit + 8 < end)
                include = hit + 8;
            if (include)
            {
                while (include < end && (*include == ' ' || *include == '\t'))
                    include++;
                if (include < end && *include == '"')
                {
                    const char *close = (const char *)memchr(include + 1, '"',
                                                             (size_t)(end - include - 1));
                    if (!close)
                        add_diagnostic(file, line_no, (int)(include - line) + 1,
                                       (int)(include - line) + 2,
                                       AROMA_CODE_DIAGNOSTIC_ERROR,
                                       "Unterminated include path.", "clang",
                                       "include-quote");
                    else
                    {
                        char name[256];
                        int size = (int)(close - include - 1);
                        if (size >= (int)sizeof(name))
                            size = (int)sizeof(name) - 1;
                        memcpy(name, include + 1, (size_t)size);
                        name[size] = '\0';
                        char candidate[1024];
                        bool found = false;
                        const char *slash = strrchr(file->path, '/');
                        int dir_length = slash ? (int)(slash - file->path) : 0;
                        for (int attempt = 0; attempt < 3 && !found; attempt++)
                        {
                            if (attempt == 0 && dir_length > 0)
                                snprintf(candidate, sizeof(candidate), "%s/%.*s/%s",
                                         ide_workspace_root, dir_length, file->path, name);
                            else if (attempt == 1)
                                snprintf(candidate, sizeof(candidate), "%s/%s",
                                         ide_workspace_root, name);
                            else
                                snprintf(candidate, sizeof(candidate), "%s/include/%s",
                                         ide_workspace_root, name);
                            if (path_exists_cached(candidate))
                                found = true;
                        }
                        if (!found)
                            add_diagnostic(file, line_no, (int)(include - line) + 1,
                                           (int)(include - line) + size + 3,
                                           AROMA_CODE_DIAGNOSTIC_WARNING,
                                           "Included header was not found in this project.",
                                           "clang", "include-not-found");
                    }
                }
                else if (include < end && *include == '<')
                {
                    const char *close = (const char *)memchr(include + 1, '>',
                                                             (size_t)(end - include - 1));
                    if (!close)
                        add_diagnostic(file, line_no, (int)(include - line) + 1,
                                       (int)(include - line) + 2,
                                       AROMA_CODE_DIAGNOSTIC_ERROR,
                                       "Unterminated include path.", "clang",
                                       "include-angle");
                }
            }
            if (*end == '\0')
                break;
            line = end + 1;
            line_no++;
        }
        if (preprocessor_depth > 0)
            add_diagnostic(file, 1, 1, 2, AROMA_CODE_DIAGNOSTIC_ERROR,
                           "Unclosed conditional block: a matching #endif is missing.",
                           "clang", "pp-endif-missing");
    }
}

typedef struct DiagCache
{
    uint64_t hash;
    size_t length;
    int language;
    int count;
    time_t stamp;
    bool valid;
} DiagCache;

static DiagCache s_diag_cache[IDE_MAX_FILES];

                                                                           
                                                                               
void ide_lsp_refresh_diagnostics(int file_index)
{
    if (file_index < 0 || file_index >= ide_file_count)
        return;
    IdeFile *file = &ide_files[file_index];
    DiagCache *cache = &s_diag_cache[file_index];
    if (!file->text)
    {
        file->diagnostic_count = 0;
        cache->valid = false;
        return;
    }
    uint64_t hash = hash_bytes(hash_bytes(1469598103934665603ULL, file->text),
                               file->path);
    size_t length = strlen(file->text);
    bool uses_disk = file->language == AROMA_CODE_LANGUAGE_C ||
                     file->language == AROMA_CODE_LANGUAGE_CPP ||
                     file->language == AROMA_CODE_LANGUAGE_PLAIN;
    if (cache->valid && cache->hash == hash && cache->length == length &&
        cache->language == (int)file->language &&
        cache->count == file->diagnostic_count &&
        (!uses_disk || time(NULL) - cache->stamp < 2))
        return;

    lsp_scan_file(file_index);

    cache->hash = hash;
    cache->length = length;
    cache->language = (int)file->language;
    cache->count = file->diagnostic_count;
    cache->stamp = time(NULL);
    cache->valid = true;
}

void ide_lsp_refresh_all(void)
{
    for (int i = 0; i < ide_file_count; i++)
        ide_lsp_refresh_diagnostics(i);
}

static const CompletionSpec k_ts_completions[] = {
    {"useState", "Hook - local component state", "useState", AROMA_CODE_COMPLETION_FUNCTION},
    {"useEffect", "Hook - run side effects", "useEffect", AROMA_CODE_COMPLETION_FUNCTION},
    {"useMemo", "Hook - memoized value", "useMemo", AROMA_CODE_COMPLETION_FUNCTION},
    {"useRef", "Hook - mutable reference", "useRef", AROMA_CODE_COMPLETION_FUNCTION},
    {"useCallback", "Hook - stable callback", "useCallback", AROMA_CODE_COMPLETION_FUNCTION},
    {"CommandPalette", "Component - src/components/CommandPalette.tsx", "CommandPalette",
     AROMA_CODE_COMPLETION_CLASS},
    {"connectLanguageServer", "Function - src/lib/lsp.ts", "connectLanguageServer",
     AROMA_CODE_COMPLETION_FUNCTION},
    {"Workspace", "Type - workspace metadata", "Workspace", AROMA_CODE_COMPLETION_INTERFACE},
    {"Diagnostic", "Type - language server diagnostic", "Diagnostic",
     AROMA_CODE_COMPLETION_INTERFACE},
    {"initialWorkspace", "Const - workspace seed", "initialWorkspace",
     AROMA_CODE_COMPLETION_VARIABLE},
    {"workspace", "Variable - Workspace", "workspace", AROMA_CODE_COMPLETION_VARIABLE},
    {"status", "Variable - string", "status", AROMA_CODE_COMPLETION_VARIABLE},
    {"setOpen", "Setter - Dispatch<SetStateAction<boolean>>", "setOpen",
     AROMA_CODE_COMPLETION_FUNCTION},
    {"setQuery", "Setter - Dispatch<SetStateAction<string>>", "setQuery",
     AROMA_CODE_COMPLETION_FUNCTION},
    {"console", "Console - debug output", "console", AROMA_CODE_COMPLETION_VARIABLE},
    {"function", "Keyword - function declaration", "function", AROMA_CODE_COMPLETION_KEYWORD},
    {"interface", "Keyword - interface declaration", "interface", AROMA_CODE_COMPLETION_KEYWORD},
    {"type", "Keyword - type alias", "type", AROMA_CODE_COMPLETION_KEYWORD},
    {"const", "Keyword - block scoped constant", "const", AROMA_CODE_COMPLETION_KEYWORD},
    {"export", "Keyword - module export", "export", AROMA_CODE_COMPLETION_KEYWORD},
    {"async", "Keyword - asynchronous function", "async", AROMA_CODE_COMPLETION_KEYWORD},
    {"await", "Keyword - await a promise", "await", AROMA_CODE_COMPLETION_KEYWORD},
    {"return", "Keyword - return from a function", "return", AROMA_CODE_COMPLETION_KEYWORD},
};

static const CompletionSpec k_rust_completions[] = {
    {"Mutex", "std::sync::Mutex<T>", "Mutex", AROMA_CODE_COMPLETION_CLASS},
    {"Arc", "std::sync::Arc<T>", "Arc", AROMA_CODE_COMPLETION_CLASS},
    {"State", "tauri::State<T>", "State", AROMA_CODE_COMPLETION_CLASS},
    {"Workspace", "struct - workspace metadata", "Workspace", AROMA_CODE_COMPLETION_CLASS},
    {"AppState", "struct - managed application state", "AppState", AROMA_CODE_COMPLETION_CLASS},
    {"workspace_info", "command - read workspace state", "workspace_info",
     AROMA_CODE_COMPLETION_FUNCTION},
    {"set_branch", "command - update the current branch", "set_branch",
     AROMA_CODE_COMPLETION_FUNCTION},
    {"tauri", "Module - tauri application runtime", "tauri", AROMA_CODE_COMPLETION_MODULE},
    {"serde", "Module - serialization framework", "serde", AROMA_CODE_COMPLETION_MODULE},
    {"println!", "Macro - print a line to stdout", "println!", AROMA_CODE_COMPLETION_FUNCTION},
    {"format!", "Macro - build a formatted string", "format!", AROMA_CODE_COMPLETION_FUNCTION},
    {"vec!", "Macro - build a Vec", "vec!", AROMA_CODE_COMPLETION_FUNCTION},
    {"pub fn", "Snippet - public function", "pub fn", AROMA_CODE_COMPLETION_SNIPPET},
    {"fn", "Keyword - function declaration", "fn", AROMA_CODE_COMPLETION_KEYWORD},
    {"struct", "Keyword - struct declaration", "struct", AROMA_CODE_COMPLETION_KEYWORD},
    {"enum", "Keyword - enum declaration", "enum", AROMA_CODE_COMPLETION_KEYWORD},
    {"impl", "Keyword - implementation block", "impl", AROMA_CODE_COMPLETION_KEYWORD},
    {"let", "Keyword - immutable binding", "let", AROMA_CODE_COMPLETION_KEYWORD},
    {"mut", "Keyword - mutable binding", "mut", AROMA_CODE_COMPLETION_KEYWORD},
    {"match", "Keyword - pattern matching", "match", AROMA_CODE_COMPLETION_KEYWORD},
    {"use", "Keyword - import a path", "use", AROMA_CODE_COMPLETION_KEYWORD},
};

static const CompletionSpec k_yaml_completions[] = {
    {"services", "Top-level service map", "services", AROMA_CODE_COMPLETION_PROPERTY},
    {"image", "Container image reference", "image", AROMA_CODE_COMPLETION_PROPERTY},
    {"build", "Build configuration", "build", AROMA_CODE_COMPLETION_PROPERTY},
    {"context", "Build context directory", "context", AROMA_CODE_COMPLETION_PROPERTY},
    {"dockerfile", "Dockerfile location", "dockerfile", AROMA_CODE_COMPLETION_PROPERTY},
    {"ports", "Published container ports", "ports", AROMA_CODE_COMPLETION_PROPERTY},
    {"environment", "Environment variables", "environment", AROMA_CODE_COMPLETION_PROPERTY},
    {"volumes", "Bind mounts and named volumes", "volumes", AROMA_CODE_COMPLETION_PROPERTY},
    {"depends_on", "Service dependencies", "depends_on", AROMA_CODE_COMPLETION_PROPERTY},
    {"restart", "Restart policy", "restart", AROMA_CODE_COMPLETION_PROPERTY},
    {"container_name", "Explicit container name", "container_name", AROMA_CODE_COMPLETION_PROPERTY},
    {"healthcheck", "Container health check", "healthcheck", AROMA_CODE_COMPLETION_PROPERTY},
    {"command", "Container command", "command", AROMA_CODE_COMPLETION_PROPERTY},
    {"working_dir", "Working directory", "working_dir", AROMA_CODE_COMPLETION_PROPERTY},
};

static const CompletionSpec k_toml_completions[] = {
    {"[package]", "Package manifest section", "[package]", AROMA_CODE_COMPLETION_MODULE},
    {"name", "Crate name", "name", AROMA_CODE_COMPLETION_PROPERTY},
    {"version", "Crate version", "version", AROMA_CODE_COMPLETION_PROPERTY},
    {"edition", "Rust edition", "edition", AROMA_CODE_COMPLETION_PROPERTY},
    {"rust-version", "Minimum Rust version", "rust-version", AROMA_CODE_COMPLETION_PROPERTY},
    {"[dependencies]", "Dependency section", "[dependencies]", AROMA_CODE_COMPLETION_MODULE},
    {"serde", "Serialization crate", "serde", AROMA_CODE_COMPLETION_MODULE},
    {"tauri", "Desktop application runtime", "tauri", AROMA_CODE_COMPLETION_MODULE},
    {"[profile.release]", "Release profile", "[profile.release]", AROMA_CODE_COMPLETION_MODULE},
    {"lto", "Link time optimization", "lto", AROMA_CODE_COMPLETION_PROPERTY},
    {"codegen-units", "Parallel codegen units", "codegen-units", AROMA_CODE_COMPLETION_PROPERTY},
};

static const CompletionSpec k_docker_completions[] = {
    {"FROM", "Base image", "FROM", AROMA_CODE_COMPLETION_KEYWORD},
    {"RUN", "Execute a build command", "RUN", AROMA_CODE_COMPLETION_KEYWORD},
    {"COPY", "Copy files into the image", "COPY", AROMA_CODE_COMPLETION_KEYWORD},
    {"WORKDIR", "Set the working directory", "WORKDIR", AROMA_CODE_COMPLETION_KEYWORD},
    {"EXPOSE", "Declare a container port", "EXPOSE", AROMA_CODE_COMPLETION_KEYWORD},
    {"CMD", "Default container command", "CMD", AROMA_CODE_COMPLETION_KEYWORD},
    {"ENV", "Set an environment variable", "ENV", AROMA_CODE_COMPLETION_KEYWORD},
    {"ARG", "Build-time argument", "ARG", AROMA_CODE_COMPLETION_KEYWORD},
    {"ENTRYPOINT", "Container entrypoint", "ENTRYPOINT", AROMA_CODE_COMPLETION_KEYWORD},
    {"USER", "Runtime user", "USER", AROMA_CODE_COMPLETION_KEYWORD},
    {"HEALTHCHECK", "Container health probe", "HEALTHCHECK", AROMA_CODE_COMPLETION_KEYWORD},
};

static const CompletionSpec k_aroma_completions[] = {
    {"Window", "Root window widget", "Window", AROMA_CODE_COMPLETION_CLASS},
    {"Container", "Layout container", "Container", AROMA_CODE_COMPLETION_CLASS},
    {"Card", "Surface card", "Card", AROMA_CODE_COMPLETION_CLASS},
    {"Label", "Text label", "Label", AROMA_CODE_COMPLETION_CLASS},
    {"Button", "Interactive button", "Button", AROMA_CODE_COMPLETION_CLASS},
    {"Canvas", "Custom drawing surface", "Canvas", AROMA_CODE_COMPLETION_CLASS},
    {"Icon", "Material icon", "Icon", AROMA_CODE_COMPLETION_CLASS},
    {"Image", "Image widget", "Image", AROMA_CODE_COMPLETION_CLASS},
    {"Textbox", "Text input", "Textbox", AROMA_CODE_COMPLETION_CLASS},
    {"TextBox", "Text input field", "TextBox", AROMA_CODE_COMPLETION_CLASS},
    {"ScrollView", "Scrollable viewport", "ScrollView", AROMA_CODE_COMPLETION_CLASS},
    {"Switch", "Toggle switch", "Switch", AROMA_CODE_COMPLETION_CLASS},
    {"Slider", "Slider control", "Slider", AROMA_CODE_COMPLETION_CLASS},
    {"Chip", "Chip pill", "Chip", AROMA_CODE_COMPLETION_CLASS},
    {"ChipList", "Chip list group", "ChipList", AROMA_CODE_COMPLETION_CLASS},
    {"Divider", "Visual divider", "Divider", AROMA_CODE_COMPLETION_CLASS},
    {"WebView", "Web view widget", "WebView", AROMA_CODE_COMPLETION_CLASS},
    {"Checkbox", "Checkbox control", "Checkbox", AROMA_CODE_COMPLETION_CLASS},
    {"RadioButton", "Radio button", "RadioButton", AROMA_CODE_COMPLETION_CLASS},
    {"ProgressBar", "Progress bar", "ProgressBar", AROMA_CODE_COMPLETION_CLASS},
    {"Tabs", "Tab navigation", "Tabs", AROMA_CODE_COMPLETION_CLASS},
    {"BottomNav", "Bottom navigation bar", "BottomNav", AROMA_CODE_COMPLETION_CLASS},
    {"AppBar", "Top app bar", "AppBar", AROMA_CODE_COMPLETION_CLASS},
    {"List", "List widget", "List", AROMA_CODE_COMPLETION_CLASS},
    {"x", "Horizontal position", "x", AROMA_CODE_COMPLETION_PROPERTY},
    {"y", "Vertical position", "y", AROMA_CODE_COMPLETION_PROPERTY},
    {"width", "Component width", "width", AROMA_CODE_COMPLETION_PROPERTY},
    {"height", "Component height", "height", AROMA_CODE_COMPLETION_PROPERTY},
    {"color", "Fill color", "color", AROMA_CODE_COMPLETION_PROPERTY},
    {"border_color", "Border color", "border_color", AROMA_CODE_COMPLETION_PROPERTY},
    {"corner_radius", "Corner radius", "corner_radius", AROMA_CODE_COMPLETION_PROPERTY},
    {"size", "Font or icon size", "size", AROMA_CODE_COMPLETION_PROPERTY},
    {"text", "Visible text", "text", AROMA_CODE_COMPLETION_PROPERTY},
    {"on_click", "Click callback name", "on_click", AROMA_CODE_COMPLETION_PROPERTY},
    {"type", "Visual variant", "type", AROMA_CODE_COMPLETION_PROPERTY},
    {"layout", "Layout mode", "layout", AROMA_CODE_COMPLETION_PROPERTY},
    {"direction", "Flex direction", "direction", AROMA_CODE_COMPLETION_PROPERTY},
    {"icon", "Leading material icon", "icon", AROMA_CODE_COMPLETION_PROPERTY},
    {"on_change", "Change callback name", "on_change", AROMA_CODE_COMPLETION_PROPERTY},
    {"on_submit", "Submit callback name", "on_submit", AROMA_CODE_COMPLETION_PROPERTY},
    {"checked", "Checkbox state", "checked", AROMA_CODE_COMPLETION_PROPERTY},
    {"placeholder", "Input placeholder", "placeholder", AROMA_CODE_COMPLETION_PROPERTY},
    {"Option", "Dropdown option", "Option", AROMA_CODE_COMPLETION_PROPERTY},
    {"id", "Widget lookup id", "id", AROMA_CODE_COMPLETION_PROPERTY},
};

/* Every C language keyword, standard type, macro and library function used
   by the IDE.  The completion widget only owns presentation and filtering;
   the language surface is defined here. */
#define C_KW(name, detail) \
    { name, detail, name, AROMA_CODE_COMPLETION_KEYWORD, detail }
#define C_TYPE(name, detail) \
    { name, detail, name, AROMA_CODE_COMPLETION_CLASS, detail }
#define C_MACRO(name, detail) \
    { name, detail, name, AROMA_CODE_COMPLETION_VARIABLE, detail }
#define C_FN(name, signature, detail) \
    { name, signature, name, AROMA_CODE_COMPLETION_FUNCTION, detail }
#define C_SNIP(name, insert, detail) \
    { name, detail, insert, AROMA_CODE_COMPLETION_SNIPPET, detail }

static const CompletionSpec k_c_completions[] = {
    /* C89/C90 keywords. */
    C_KW("auto", "Storage class - automatic storage duration"),
    C_KW("break", "Jump statement - leave the innermost loop or switch"),
    C_KW("case", "Label - starts a switch case"),
    C_KW("char", "Fundamental type - character"),
    C_KW("const", "Type qualifier - read-only object"),
    C_KW("continue", "Jump statement - next loop iteration"),
    C_KW("default", "Label - fallback switch case"),
    C_KW("do", "Iteration statement - do/while body"),
    C_KW("double", "Fundamental type - double precision floating point"),
    C_KW("else", "Statement - alternative branch"),
    C_KW("enum", "Type specifier - enumeration"),
    C_KW("extern", "Storage class - external linkage"),
    C_KW("float", "Fundamental type - single precision floating point"),
    C_KW("for", "Iteration statement - counted loop"),
    C_KW("goto", "Jump statement - unconditional branch"),
    C_KW("if", "Statement - conditional branch"),
    C_KW("inline", "Function specifier - inline expansion hint"),
    C_KW("int", "Fundamental type - integer"),
    C_KW("long", "Type specifier - extended integer width"),
    C_KW("register", "Storage class - register allocation hint"),
    C_KW("return", "Jump statement - leave the current function"),
    C_KW("short", "Type specifier - reduced integer width"),
    C_KW("signed", "Type specifier - signed integer"),
    C_KW("sizeof", "Operator - size of a type or expression in bytes"),
    C_KW("static", "Storage class - internal linkage or static duration"),
    C_KW("switch", "Selection statement - multi-way branch"),
    C_KW("typedef", "Declaration specifier - introduce a type alias"),
    C_KW("union", "Type specifier - overlapping storage aggregate"),
    C_KW("unsigned", "Type specifier - unsigned integer"),
    C_KW("void", "Fundamental type - incomplete/no value"),
    C_KW("volatile", "Type qualifier - observable side effects"),
    C_KW("while", "Iteration statement - condition loop"),

    /* C99. */
    C_KW("_Bool", "C99 fundamental type - boolean"),
    C_KW("_Complex", "C99 type specifier - complex floating point"),
    C_KW("_Imaginary", "C99 type specifier - imaginary floating point"),
    C_KW("restrict", "C99 type qualifier - unaliased pointer"),

    /* C11. */
    C_KW("_Alignas", "C11 alignment specifier"),
    C_KW("_Alignof", "C11 operator - alignment requirement"),
    C_KW("_Atomic", "C11 type qualifier/specifier - atomic object"),
    C_KW("_Generic", "C11 selection - choose an expression by type"),
    C_KW("_Noreturn", "C11 function specifier - function does not return"),
    C_KW("_Static_assert", "C11 compile-time assertion"),
    C_KW("_Thread_local", "C11 storage class - thread-local duration"),

    /* C23. */
    C_KW("_BitInt", "C23 type specifier - arbitrary-width integer"),
    C_KW("_Countof", "C23 operator - number of elements in an array"),
    C_KW("_Decimal32", "C23 decimal floating-point type"),
    C_KW("_Decimal64", "C23 decimal floating-point type"),
    C_KW("_Decimal128", "C23 decimal floating-point type"),
    C_KW("alignas", "C23 alias for _Alignas"),
    C_KW("alignof", "C23 alias for _Alignof"),
    C_KW("bool", "C23/stdbool boolean type"),
    C_KW("constexpr", "C23 compile-time constant object"),
    C_KW("false", "C23/stdbool false value"),
    C_KW("nullptr", "C23 null pointer constant"),
    C_KW("static_assert", "C23 alias for _Static_assert"),
    C_KW("thread_local", "C23 alias for _Thread_local"),
    C_KW("true", "C23/stdbool true value"),
    C_KW("typeof", "C23/GNU operator - type of an expression"),
    C_KW("typeof_unqual", "C23 operator - unqualified type of an expression"),

    /* Common GNU/Clang extensions accepted by the IDE. */
    C_KW("asm", "GNU extension - inline assembly"),
    C_KW("__asm__", "GNU extension - inline assembly"),
    C_KW("__attribute__", "GNU extension - declaration attributes"),
    C_KW("__auto_type", "GNU extension - inferred variable type"),
    C_KW("__extension__", "GNU extension - suppress pedantic diagnostics"),
    C_KW("__inline__", "GNU extension - inline function"),
    C_KW("__restrict__", "GNU extension - unaliased pointer"),
    C_KW("__thread", "GNU extension - thread-local storage"),
    C_KW("__typeof__", "GNU extension - type of an expression"),

    /* Useful language snippets. */
    C_SNIP("if", "if (condition) {\n    \n}", "Statement - conditional branch"),
    C_SNIP("for", "for (int i = 0; i < count; i++) {\n    \n}",
           "Statement - counted loop"),
    C_SNIP("while", "while (condition) {\n    \n}", "Statement - condition loop"),
    C_SNIP("do", "do {\n    \n} while (condition);", "Statement - do/while loop"),
    C_SNIP("switch", "switch (value) {\ncase 0:\n    break;\ndefault:\n    break;\n}",
           "Statement - multi-way branch"),
    C_SNIP("struct", "struct Name {\n    \n};", "Aggregate - structure declaration"),
    C_SNIP("union", "union Name {\n    \n};", "Aggregate - union declaration"),
    C_SNIP("enum", "enum Name {\n    VALUE,\n};", "Aggregate - enumeration declaration"),
    C_SNIP("typedef", "typedef Type Name;", "Declaration - type alias"),
    C_SNIP("_Generic", "_Generic(expression, type: result, default: result)",
           "C11 generic selection"),
    C_SNIP("_Static_assert", "_Static_assert(condition, \"message\");",
           "C11 compile-time assertion"),
    C_SNIP("static_assert", "static_assert(condition, \"message\");",
           "C23 compile-time assertion"),
    C_SNIP("_Alignas", "_Alignas(alignment)", "C11 alignment specifier"),
    C_SNIP("alignas", "alignas(alignment)", "C23 alignment specifier"),
    C_SNIP("_Alignof", "_Alignof(type)", "C11 alignment operator"),
    C_SNIP("alignof", "alignof(type)", "C23 alignment operator"),
    C_SNIP("_Atomic", "_Atomic(Type)", "C11 atomic type specifier"),
    C_SNIP("_BitInt", "_BitInt(bits)", "C23 arbitrary-width integer type"),
    C_SNIP("_Countof", "_Countof(array)", "C23 array element count"),
    C_SNIP("typeof", "typeof(expression)", "C23/GNU type operator"),
    C_SNIP("typeof_unqual", "typeof_unqual(expression)", "C23 unqualified type operator"),
    C_SNIP("constexpr", "constexpr Type name = value;", "C23 compile-time constant"),
    C_SNIP("thread_local", "thread_local Type name;", "C23 thread-local object"),
    C_SNIP("__attribute__", "__attribute__((attribute))", "GNU declaration attribute"),
    C_SNIP("asm", "asm(\"instruction\")", "GNU inline assembly"),
    C_SNIP("__asm__", "__asm__(\"instruction\")", "GNU inline assembly"),

    /* Standard typedefs and library types. */
    C_TYPE("size_t", "stddef.h - object size type"),
    C_TYPE("ssize_t", "POSIX - signed size type"),
    C_TYPE("ptrdiff_t", "stddef.h - pointer difference type"),
    C_TYPE("wchar_t", "stddef.h - wide character type"),
    C_TYPE("char8_t", "C23 - UTF-8 character unit"),
    C_TYPE("char16_t", "uchar.h - UTF-16 character unit"),
    C_TYPE("char32_t", "uchar.h - UTF-32 character unit"),
    C_TYPE("max_align_t", "stddef.h - maximum alignment type"),
    C_TYPE("nullptr_t", "stddef.h - C23 null pointer type"),
    C_TYPE("int8_t", "stdint.h - signed 8-bit integer"),
    C_TYPE("int16_t", "stdint.h - signed 16-bit integer"),
    C_TYPE("int32_t", "stdint.h - signed 32-bit integer"),
    C_TYPE("int64_t", "stdint.h - signed 64-bit integer"),
    C_TYPE("uint8_t", "stdint.h - unsigned 8-bit integer"),
    C_TYPE("uint16_t", "stdint.h - unsigned 16-bit integer"),
    C_TYPE("uint32_t", "stdint.h - unsigned 32-bit integer"),
    C_TYPE("uint64_t", "stdint.h - unsigned 64-bit integer"),
    C_TYPE("int_least8_t", "stdint.h - smallest signed 8-bit type"),
    C_TYPE("int_least16_t", "stdint.h - smallest signed 16-bit type"),
    C_TYPE("int_least32_t", "stdint.h - smallest signed 32-bit type"),
    C_TYPE("int_least64_t", "stdint.h - smallest signed 64-bit type"),
    C_TYPE("uint_least8_t", "stdint.h - smallest unsigned 8-bit type"),
    C_TYPE("uint_least16_t", "stdint.h - smallest unsigned 16-bit type"),
    C_TYPE("uint_least32_t", "stdint.h - smallest unsigned 32-bit type"),
    C_TYPE("uint_least64_t", "stdint.h - smallest unsigned 64-bit type"),
    C_TYPE("int_fast8_t", "stdint.h - fastest signed 8-bit type"),
    C_TYPE("int_fast16_t", "stdint.h - fastest signed 16-bit type"),
    C_TYPE("int_fast32_t", "stdint.h - fastest signed 32-bit type"),
    C_TYPE("int_fast64_t", "stdint.h - fastest signed 64-bit type"),
    C_TYPE("uint_fast8_t", "stdint.h - fastest unsigned 8-bit type"),
    C_TYPE("uint_fast16_t", "stdint.h - fastest unsigned 16-bit type"),
    C_TYPE("uint_fast32_t", "stdint.h - fastest unsigned 32-bit type"),
    C_TYPE("uint_fast64_t", "stdint.h - fastest unsigned 64-bit type"),
    C_TYPE("intptr_t", "stdint.h - signed integer that holds a pointer"),
    C_TYPE("uintptr_t", "stdint.h - unsigned integer that holds a pointer"),
    C_TYPE("intmax_t", "stdint.h - widest signed integer"),
    C_TYPE("uintmax_t", "stdint.h - widest unsigned integer"),
    C_TYPE("imaxdiv_t", "inttypes.h - result of imaxdiv"),
    C_TYPE("__int128", "GNU - signed 128-bit integer"),
    C_TYPE("__int128_t", "GNU - signed 128-bit integer"),
    C_TYPE("__uint128_t", "GNU - unsigned 128-bit integer"),
    C_TYPE("_Float16", "Clang/GNU - 16-bit floating point"),
    C_TYPE("_Float32", "Clang/GNU - 32-bit floating point"),
    C_TYPE("_Float64", "Clang/GNU - 64-bit floating point"),
    C_TYPE("_Float128", "Clang/GNU - 128-bit floating point"),
    C_TYPE("_Float32x", "Clang/GNU - extended 32-bit floating point"),
    C_TYPE("_Float64x", "Clang/GNU - extended 64-bit floating point"),
    C_TYPE("_Float128x", "Clang/GNU - extended 128-bit floating point"),

    /* Atomic and concurrency types. */
    C_TYPE("atomic_bool", "stdatomic.h - atomic boolean"),
    C_TYPE("atomic_char", "stdatomic.h - atomic char"),
    C_TYPE("atomic_schar", "stdatomic.h - atomic signed char"),
    C_TYPE("atomic_uchar", "stdatomic.h - atomic unsigned char"),
    C_TYPE("atomic_short", "stdatomic.h - atomic short"),
    C_TYPE("atomic_ushort", "stdatomic.h - atomic unsigned short"),
    C_TYPE("atomic_int", "stdatomic.h - atomic int"),
    C_TYPE("atomic_uint", "stdatomic.h - atomic unsigned int"),
    C_TYPE("atomic_long", "stdatomic.h - atomic long"),
    C_TYPE("atomic_ulong", "stdatomic.h - atomic unsigned long"),
    C_TYPE("atomic_llong", "stdatomic.h - atomic long long"),
    C_TYPE("atomic_ullong", "stdatomic.h - atomic unsigned long long"),
    C_TYPE("atomic_size_t", "stdatomic.h - atomic size_t"),
    C_TYPE("atomic_ptrdiff_t", "stdatomic.h - atomic ptrdiff_t"),
    C_TYPE("atomic_intptr_t", "stdatomic.h - atomic intptr_t"),
    C_TYPE("atomic_uintptr_t", "stdatomic.h - atomic uintptr_t"),
    C_TYPE("atomic_intmax_t", "stdatomic.h - atomic intmax_t"),
    C_TYPE("atomic_uintmax_t", "stdatomic.h - atomic uintmax_t"),
    C_TYPE("atomic_flag", "stdatomic.h - lock-free flag"),
    C_TYPE("thrd_t", "threads.h - thread handle"),
    C_TYPE("mtx_t", "threads.h - mutex handle"),
    C_TYPE("cnd_t", "threads.h - condition variable handle"),
    C_TYPE("tss_t", "threads.h - thread-specific storage key"),
    C_TYPE("once_flag", "threads.h - one-time initialization flag"),
    C_TYPE("thrd_start_t", "threads.h - thread entry function type"),

    /* I/O, time, locale and setjmp types. */
    C_TYPE("FILE", "stdio.h - stream handle"),
    C_TYPE("fpos_t", "stdio.h - file position"),
    C_TYPE("clock_t", "time.h - processor clock ticks"),
    C_TYPE("time_t", "time.h - calendar time"),
    C_TYPE("tm", "time.h - broken-down calendar time"),
    C_TYPE("timespec", "time.h - seconds and nanoseconds"),
    C_TYPE("div_t", "stdlib.h - integer division result"),
    C_TYPE("ldiv_t", "stdlib.h - long division result"),
    C_TYPE("lldiv_t", "stdlib.h - long long division result"),
    C_TYPE("va_list", "stdarg.h - variadic argument list"),
    C_TYPE("jmp_buf", "setjmp.h - saved execution context"),
    C_TYPE("sig_atomic_t", "signal.h - async-signal-safe integer"),
    C_TYPE("wint_t", "wchar.h - wide character or WEOF"),
    C_TYPE("mbstate_t", "wchar.h - multibyte conversion state"),
    C_TYPE("wctrans_t", "wctype.h - wide character transformation"),
    C_TYPE("wctype_t", "wctype.h - wide character class"),
    C_TYPE("fenv_t", "fenv.h - floating-point environment"),
    C_TYPE("fexcept_t", "fenv.h - floating-point exception flags"),
    C_TYPE("lconv", "locale.h - numeric and monetary formatting"),

    /* AromaUI project types. */
    C_TYPE("AromaNode", "AromaUI - scene graph node"),
    C_TYPE("AromaWindow", "AromaUI - native window handle"),
    C_TYPE("AromaFont", "AromaUI - font handle"),
    C_TYPE("AromaCanvas", "AromaUI - immediate-mode drawing surface"),
    C_TYPE("AromaContainer", "AromaUI - layout container widget"),
    C_TYPE("AromaLabel", "AromaUI - text label widget"),
    C_TYPE("AromaButton", "AromaUI - button widget"),
    C_TYPE("AromaCard", "AromaUI - surface card widget"),
    C_TYPE("AromaTheme", "AromaUI - color and typography theme"),
    C_TYPE("AromaEvent", "AromaUI - input event"),
    C_TYPE("AromaRect", "AromaUI - rectangle"),
    C_TYPE("AromaGraphicsInterface", "AromaUI - graphics backend interface"),
    C_TYPE("AromaPlatformInterface", "AromaUI - platform backend interface"),
    C_TYPE("IncenseRegistry", "AromaUI - markup widget registry"),
    C_TYPE("IncenseCallbackKind", "AromaUI - markup callback category"),
    C_TYPE("IdeFile", "IDE - workspace file record"),

    /* Standard macros and constants. */
    C_MACRO("NULL", "stddef.h - null pointer constant"),
    C_MACRO("EOF", "stdio.h - end-of-file result"),
    C_MACRO("BUFSIZ", "stdio.h - default stream buffer size"),
    C_MACRO("FILENAME_MAX", "stdio.h - maximum file name length"),
    C_MACRO("FOPEN_MAX", "stdio.h - maximum open streams"),
    C_MACRO("TMP_MAX", "stdio.h - maximum temporary file names"),
    C_MACRO("L_tmpnam", "stdio.h - temporary file name buffer size"),
    C_MACRO("_IOFBF", "stdio.h - fully buffered stream"),
    C_MACRO("_IOLBF", "stdio.h - line buffered stream"),
    C_MACRO("_IONBF", "stdio.h - unbuffered stream"),
    C_MACRO("SEEK_SET", "stdio.h - seek from beginning"),
    C_MACRO("SEEK_CUR", "stdio.h - seek from current position"),
    C_MACRO("SEEK_END", "stdio.h - seek from end"),
    C_MACRO("EXIT_SUCCESS", "stdlib.h - successful termination status"),
    C_MACRO("EXIT_FAILURE", "stdlib.h - unsuccessful termination status"),
    C_MACRO("RAND_MAX", "stdlib.h - maximum rand() result"),
    C_MACRO("MB_CUR_MAX", "stdlib.h - maximum multibyte character length"),
    C_MACRO("CLOCKS_PER_SEC", "time.h - clock ticks per second"),
    C_MACRO("TIME_UTC", "time.h - UTC time base"),
    C_MACRO("TIME_MONOTONIC", "C23 time.h - monotonic time base"),
    C_MACRO("TIME_PROCESS", "C23 time.h - process CPU time base"),
    C_MACRO("TIME_THREAD_ACTIVE", "C23 time.h - thread CPU time base"),
    C_MACRO("SIG_DFL", "signal.h - default signal handler"),
    C_MACRO("SIG_ERR", "signal.h - signal registration error"),
    C_MACRO("SIG_IGN", "signal.h - ignore signal"),
    C_MACRO("SIGABRT", "signal.h - abnormal termination"),
    C_MACRO("SIGFPE", "signal.h - arithmetic exception"),
    C_MACRO("SIGILL", "signal.h - illegal instruction"),
    C_MACRO("SIGINT", "signal.h - interactive attention"),
    C_MACRO("SIGSEGV", "signal.h - invalid memory access"),
    C_MACRO("SIGTERM", "signal.h - termination request"),
    C_MACRO("SIG_ATOMIC_MIN", "stdint.h - minimum sig_atomic_t"),
    C_MACRO("SIG_ATOMIC_MAX", "stdint.h - maximum sig_atomic_t"),
    C_MACRO("CHAR_BIT", "limits.h - bits in a byte"),
    C_MACRO("SCHAR_MIN", "limits.h - minimum signed char"),
    C_MACRO("SCHAR_MAX", "limits.h - maximum signed char"),
    C_MACRO("UCHAR_MAX", "limits.h - maximum unsigned char"),
    C_MACRO("CHAR_MIN", "limits.h - minimum char"),
    C_MACRO("CHAR_MAX", "limits.h - maximum char"),
    C_MACRO("MB_LEN_MAX", "limits.h - maximum multibyte length"),
    C_MACRO("SHRT_MIN", "limits.h - minimum short"),
    C_MACRO("SHRT_MAX", "limits.h - maximum short"),
    C_MACRO("USHRT_MAX", "limits.h - maximum unsigned short"),
    C_MACRO("INT_MIN", "limits.h - minimum int"),
    C_MACRO("INT_MAX", "limits.h - maximum int"),
    C_MACRO("UINT_MAX", "limits.h - maximum unsigned int"),
    C_MACRO("LONG_MIN", "limits.h - minimum long"),
    C_MACRO("LONG_MAX", "limits.h - maximum long"),
    C_MACRO("ULONG_MAX", "limits.h - maximum unsigned long"),
    C_MACRO("LLONG_MIN", "limits.h - minimum long long"),
    C_MACRO("LLONG_MAX", "limits.h - maximum long long"),
    C_MACRO("ULLONG_MAX", "limits.h - maximum unsigned long long"),
    C_MACRO("SIZE_MAX", "stdint.h - maximum size_t"),
    C_MACRO("PTRDIFF_MIN", "stdint.h - minimum ptrdiff_t"),
    C_MACRO("PTRDIFF_MAX", "stdint.h - maximum ptrdiff_t"),
    C_MACRO("WCHAR_MIN", "stdint.h - minimum wchar_t"),
    C_MACRO("WCHAR_MAX", "stdint.h - maximum wchar_t"),
    C_MACRO("WINT_MIN", "stdint.h - minimum wint_t"),
    C_MACRO("WINT_MAX", "stdint.h - maximum wint_t"),
    C_MACRO("INT8_MIN", "stdint.h - minimum int8_t"),
    C_MACRO("INT8_MAX", "stdint.h - maximum int8_t"),
    C_MACRO("UINT8_MAX", "stdint.h - maximum uint8_t"),
    C_MACRO("INT16_MIN", "stdint.h - minimum int16_t"),
    C_MACRO("INT16_MAX", "stdint.h - maximum int16_t"),
    C_MACRO("UINT16_MAX", "stdint.h - maximum uint16_t"),
    C_MACRO("INT32_MIN", "stdint.h - minimum int32_t"),
    C_MACRO("INT32_MAX", "stdint.h - maximum int32_t"),
    C_MACRO("UINT32_MAX", "stdint.h - maximum uint32_t"),
    C_MACRO("INT64_MIN", "stdint.h - minimum int64_t"),
    C_MACRO("INT64_MAX", "stdint.h - maximum int64_t"),
    C_MACRO("UINT64_MAX", "stdint.h - maximum uint64_t"),
    C_MACRO("INTMAX_MIN", "stdint.h - minimum intmax_t"),
    C_MACRO("INTMAX_MAX", "stdint.h - maximum intmax_t"),
    C_MACRO("UINTMAX_MAX", "stdint.h - maximum uintmax_t"),
    C_MACRO("INT8_C", "stdint.h - int8_t constant suffix"),
    C_MACRO("UINT8_C", "stdint.h - uint8_t constant suffix"),
    C_MACRO("INT16_C", "stdint.h - int16_t constant suffix"),
    C_MACRO("UINT16_C", "stdint.h - uint16_t constant suffix"),
    C_MACRO("INT32_C", "stdint.h - int32_t constant suffix"),
    C_MACRO("UINT32_C", "stdint.h - uint32_t constant suffix"),
    C_MACRO("INT64_C", "stdint.h - int64_t constant suffix"),
    C_MACRO("UINT64_C", "stdint.h - uint64_t constant suffix"),
    C_MACRO("INTMAX_C", "stdint.h - intmax_t constant suffix"),
    C_MACRO("UINTMAX_C", "stdint.h - uintmax_t constant suffix"),
    C_MACRO("FLT_RADIX", "float.h - floating-point radix"),
    C_MACRO("FLT_MANT_DIG", "float.h - float significand digits"),
    C_MACRO("FLT_DIG", "float.h - decimal digits of precision"),
    C_MACRO("FLT_MIN_EXP", "float.h - minimum float exponent"),
    C_MACRO("FLT_MAX_EXP", "float.h - maximum float exponent"),
    C_MACRO("FLT_MIN", "float.h - minimum normalized float"),
    C_MACRO("FLT_MAX", "float.h - maximum float"),
    C_MACRO("FLT_EPSILON", "float.h - float machine epsilon"),
    C_MACRO("FLT_TRUE_MIN", "float.h - minimum positive float"),
    C_MACRO("DBL_MANT_DIG", "float.h - double significand digits"),
    C_MACRO("DBL_DIG", "float.h - double decimal digits"),
    C_MACRO("DBL_MIN_EXP", "float.h - minimum double exponent"),
    C_MACRO("DBL_MAX_EXP", "float.h - maximum double exponent"),
    C_MACRO("DBL_MIN", "float.h - minimum normalized double"),
    C_MACRO("DBL_MAX", "float.h - maximum double"),
    C_MACRO("DBL_EPSILON", "float.h - double machine epsilon"),
    C_MACRO("DBL_TRUE_MIN", "float.h - minimum positive double"),
    C_MACRO("LDBL_MANT_DIG", "float.h - long double significand digits"),
    C_MACRO("LDBL_DIG", "float.h - long double decimal digits"),
    C_MACRO("LDBL_MIN_EXP", "float.h - minimum long double exponent"),
    C_MACRO("LDBL_MAX_EXP", "float.h - maximum long double exponent"),
    C_MACRO("LDBL_MIN", "float.h - minimum normalized long double"),
    C_MACRO("LDBL_MAX", "float.h - maximum long double"),
    C_MACRO("LDBL_EPSILON", "float.h - long double machine epsilon"),
    C_MACRO("LDBL_TRUE_MIN", "float.h - minimum positive long double"),
    C_MACRO("DECIMAL_DIG", "float.h - decimal digits for widest type"),
    C_MACRO("FLT_EVAL_METHOD", "float.h - evaluation format"),
    C_MACRO("FLT_ROUNDS", "float.h - current rounding direction"),
    C_MACRO("FLT_HAS_SUBNORM", "float.h - float subnormal support"),
    C_MACRO("DBL_HAS_SUBNORM", "float.h - double subnormal support"),
    C_MACRO("LDBL_HAS_SUBNORM", "float.h - long double subnormal support"),
    C_MACRO("ATOMIC_VAR_INIT", "stdatomic.h - atomic initialization"),
    C_MACRO("ATOMIC_FLAG_INIT", "stdatomic.h - atomic flag initialization"),
    C_MACRO("memory_order_relaxed", "stdatomic.h - relaxed memory ordering"),
    C_MACRO("memory_order_consume", "stdatomic.h - consume memory ordering"),
    C_MACRO("memory_order_acquire", "stdatomic.h - acquire memory ordering"),
    C_MACRO("memory_order_release", "stdatomic.h - release memory ordering"),
    C_MACRO("memory_order_acq_rel", "stdatomic.h - acquire/release ordering"),
    C_MACRO("memory_order_seq_cst", "stdatomic.h - sequential consistency"),
    C_MACRO("ONCE_FLAG_INIT", "threads.h - one-time flag initializer"),
    C_MACRO("TSS_DTOR_ITERATIONS", "threads.h - thread storage destructor passes"),
    C_MACRO("thrd_success", "threads.h - success status"),
    C_MACRO("thrd_timedout", "threads.h - timeout status"),
    C_MACRO("thrd_busy", "threads.h - resource busy status"),
    C_MACRO("thrd_error", "threads.h - failure status"),
    C_MACRO("thrd_nomem", "threads.h - out-of-memory status"),
    C_MACRO("mtx_plain", "threads.h - plain mutex"),
    C_MACRO("mtx_recursive", "threads.h - recursive mutex"),
    C_MACRO("mtx_timed", "threads.h - timed mutex"),
    C_MACRO("FE_DIVBYZERO", "fenv.h - divide-by-zero exception"),
    C_MACRO("FE_INEXACT", "fenv.h - inexact result exception"),
    C_MACRO("FE_INVALID", "fenv.h - invalid operation exception"),
    C_MACRO("FE_OVERFLOW", "fenv.h - overflow exception"),
    C_MACRO("FE_UNDERFLOW", "fenv.h - underflow exception"),
    C_MACRO("FE_ALL_EXCEPT", "fenv.h - all exception flags"),
    C_MACRO("FE_DOWNWARD", "fenv.h - round toward negative infinity"),
    C_MACRO("FE_TONEAREST", "fenv.h - round to nearest"),
    C_MACRO("FE_TOWARDZERO", "fenv.h - round toward zero"),
    C_MACRO("FE_UPWARD", "fenv.h - round toward positive infinity"),
    C_MACRO("FE_DFL_ENV", "fenv.h - default environment"),
    C_MACRO("FP_NORMAL", "math.h - normal floating-point class"),
    C_MACRO("FP_SUBNORMAL", "math.h - subnormal floating-point class"),
    C_MACRO("FP_ZERO", "math.h - zero floating-point class"),
    C_MACRO("FP_INFINITE", "math.h - infinite floating-point class"),
    C_MACRO("FP_NAN", "math.h - NaN floating-point class"),
    C_MACRO("FP_ILOGB0", "math.h - ilogb(0) result"),
    C_MACRO("FP_ILOGBNAN", "math.h - ilogb(NaN) result"),
    C_MACRO("MATH_ERRNO", "math.h - report errors through errno"),
    C_MACRO("MATH_ERREXCEPT", "math.h - report errors through exceptions"),
    C_MACRO("math_errhandling", "math.h - active error reporting mode"),
    C_MACRO("complex", "complex.h - complex type macro"),
    C_MACRO("_Complex_I", "complex.h - imaginary unit"),
    C_MACRO("I", "complex.h - imaginary unit"),
    C_MACRO("CMPLX", "complex.h - construct complex double"),
    C_MACRO("CMPLXF", "complex.h - construct complex float"),
    C_MACRO("CMPLXL", "complex.h - construct complex long double"),
    C_MACRO("errno", "errno.h - last error number"),
    C_MACRO("EDOM", "errno.h - domain error"),
    C_MACRO("EILSEQ", "errno.h - invalid multibyte sequence"),
    C_MACRO("ERANGE", "errno.h - range error"),
    C_MACRO("LC_ALL", "locale.h - entire locale"),
    C_MACRO("LC_COLLATE", "locale.h - collation locale"),
    C_MACRO("LC_CTYPE", "locale.h - character classification locale"),
    C_MACRO("LC_MONETARY", "locale.h - monetary formatting locale"),
    C_MACRO("LC_NUMERIC", "locale.h - numeric formatting locale"),
    C_MACRO("LC_TIME", "locale.h - time formatting locale"),
    C_MACRO("WEOF", "wchar.h - wide end-of-file result"),
    C_MACRO("noreturn", "stdnoreturn.h - function does not return"),
    C_MACRO("offsetof", "stddef.h - byte offset of a member"),
    C_MACRO("assert", "assert.h - runtime assertion"),
    C_MACRO("__FILE__", "predefined - current source file"),
    C_MACRO("__LINE__", "predefined - current line number"),
    C_MACRO("__DATE__", "predefined - compilation date"),
    C_MACRO("__TIME__", "predefined - compilation time"),
    C_MACRO("__TIMESTAMP__", "predefined - source modification time"),
    C_MACRO("__STDC__", "predefined - ISO C conformance"),
    C_MACRO("__STDC_VERSION__", "predefined - ISO C version"),
    C_MACRO("__STDC_HOSTED__", "predefined - hosted implementation"),
    C_MACRO("__STDC_NO_ATOMICS__", "predefined - atomics unavailable"),
    C_MACRO("__STDC_NO_THREADS__", "predefined - threads unavailable"),
    C_MACRO("__STDC_NO_VLA__", "predefined - variable length arrays unavailable"),
    C_MACRO("__func__", "predefined - current function name"),
    C_MACRO("__VA_ARGS__", "predefined - variadic macro arguments"),
    C_MACRO("__VA_OPT__", "C23 - optional variadic macro content"),
    C_MACRO("__has_include", "preprocessor - test whether a header exists"),
    C_MACRO("__has_c_attribute", "C23 - test whether an attribute is supported"),
    C_MACRO("__COUNTER__", "GNU/Clang - monotonically increasing counter"),
    C_MACRO("__GNUC__", "predefined - GNU compiler version"),
    C_MACRO("__clang__", "predefined - Clang compiler"),
    C_MACRO("__linux__", "predefined - Linux target"),
    C_MACRO("__x86_64__", "predefined - x86-64 target"),
    C_MACRO("__STDC_IEC_559__", "predefined - IEC 60559 floating point"),
    C_MACRO("__STDC_IEC_60559_BFP__", "C23 - IEC 60559 binary floating point"),

    /* stdio.h. */
    C_FN("remove", "int remove(const char *filename)", "Deletes a file."),
    C_FN("rename", "int rename(const char *old, const char *new_name)", "Renames a file."),
    C_FN("tmpfile", "FILE *tmpfile(void)", "Creates a temporary stream."),
    C_FN("tmpnam", "char *tmpnam(char *s)", "Generates a temporary file name."),
    C_FN("fclose", "int fclose(FILE *stream)", "Closes a stream."),
    C_FN("fflush", "int fflush(FILE *stream)", "Flushes buffered output."),
    C_FN("fopen", "FILE *fopen(const char *path, const char *mode)", "Opens a file stream."),
    C_FN("freopen", "FILE *freopen(const char *path, const char *mode, FILE *stream)",
          "Reopens a stream with another file."),
    C_FN("setbuf", "void setbuf(FILE *stream, char *buf)", "Sets a stream buffer."),
    C_FN("setvbuf", "int setvbuf(FILE *stream, char *buf, int mode, size_t size)",
          "Sets a stream buffer and buffering mode."),
    C_FN("fprintf", "int fprintf(FILE *stream, const char *format, ...)",
          "Writes formatted output to a stream."),
    C_FN("fscanf", "int fscanf(FILE *stream, const char *format, ...)",
          "Reads formatted input from a stream."),
    C_FN("printf", "int printf(const char *format, ...)", "Writes formatted output to stdout."),
    C_FN("scanf", "int scanf(const char *format, ...)", "Reads formatted input from stdin."),
    C_FN("snprintf", "int snprintf(char *s, size_t n, const char *format, ...)",
          "Writes bounded formatted output to a buffer."),
    C_FN("sprintf", "int sprintf(char *s, const char *format, ...)",
          "Writes formatted output to a buffer."),
    C_FN("sscanf", "int sscanf(const char *s, const char *format, ...)",
          "Reads formatted input from a string."),
    C_FN("vfprintf", "int vfprintf(FILE *stream, const char *format, va_list ap)",
          "Writes a va_list to a stream."),
    C_FN("vfscanf", "int vfscanf(FILE *stream, const char *format, va_list ap)",
          "Reads a va_list from a stream."),
    C_FN("vprintf", "int vprintf(const char *format, va_list ap)",
          "Writes a va_list to stdout."),
    C_FN("vscanf", "int vscanf(const char *format, va_list ap)",
          "Reads a va_list from stdin."),
    C_FN("vsnprintf", "int vsnprintf(char *s, size_t n, const char *format, va_list ap)",
          "Writes a bounded va_list to a buffer."),
    C_FN("vsprintf", "int vsprintf(char *s, const char *format, va_list ap)",
          "Writes a va_list to a buffer."),
    C_FN("vsscanf", "int vsscanf(const char *s, const char *format, va_list ap)",
          "Reads a va_list from a string."),
    C_FN("fgetc", "int fgetc(FILE *stream)", "Reads one character from a stream."),
    C_FN("fgets", "char *fgets(char *s, int n, FILE *stream)", "Reads a line from a stream."),
    C_FN("fputc", "int fputc(int c, FILE *stream)", "Writes one character to a stream."),
    C_FN("fputs", "int fputs(const char *s, FILE *stream)", "Writes a string to a stream."),
    C_FN("getc", "int getc(FILE *stream)", "Reads one character from a stream."),
    C_FN("getchar", "int getchar(void)", "Reads one character from stdin."),
    C_FN("putc", "int putc(int c, FILE *stream)", "Writes one character to a stream."),
    C_FN("putchar", "int putchar(int c)", "Writes one character to stdout."),
    C_FN("puts", "int puts(const char *s)", "Writes a string and newline to stdout."),
    C_FN("ungetc", "int ungetc(int c, FILE *stream)", "Pushes a character back onto a stream."),
    C_FN("fread", "size_t fread(void *ptr, size_t size, size_t count, FILE *stream)",
          "Reads binary data from a stream."),
    C_FN("fwrite", "size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream)",
          "Writes binary data to a stream."),
    C_FN("fgetpos", "int fgetpos(FILE *stream, fpos_t *pos)", "Gets the stream position."),
    C_FN("fseek", "int fseek(FILE *stream, long offset, int origin)", "Sets the stream position."),
    C_FN("fsetpos", "int fsetpos(FILE *stream, const fpos_t *pos)", "Sets a saved stream position."),
    C_FN("ftell", "long ftell(FILE *stream)", "Returns the current stream position."),
    C_FN("rewind", "void rewind(FILE *stream)", "Rewinds a stream to the beginning."),
    C_FN("clearerr", "void clearerr(FILE *stream)", "Clears stream error and EOF indicators."),
    C_FN("feof", "int feof(FILE *stream)", "Tests the end-of-file indicator."),
    C_FN("ferror", "int ferror(FILE *stream)", "Tests the stream error indicator."),
    C_FN("perror", "void perror(const char *s)", "Prints a system error message."),

    /* stdlib.h. */
    C_FN("atof", "double atof(const char *s)", "Converts a string to double."),
    C_FN("atoi", "int atoi(const char *s)", "Converts a string to int."),
    C_FN("atol", "long atol(const char *s)", "Converts a string to long."),
    C_FN("atoll", "long long atoll(const char *s)", "Converts a string to long long."),
    C_FN("strtod", "double strtod(const char *s, char **end)", "Parses a double."),
    C_FN("strtof", "float strtof(const char *s, char **end)", "Parses a float."),
    C_FN("strtold", "long double strtold(const char *s, char **end)", "Parses a long double."),
    C_FN("strtol", "long strtol(const char *s, char **end, int base)", "Parses a long integer."),
    C_FN("strtoll", "long long strtoll(const char *s, char **end, int base)",
          "Parses a long long integer."),
    C_FN("strtoul", "unsigned long strtoul(const char *s, char **end, int base)",
          "Parses an unsigned long integer."),
    C_FN("strtoull", "unsigned long long strtoull(const char *s, char **end, int base)",
          "Parses an unsigned long long integer."),
    C_FN("rand", "int rand(void)", "Returns a pseudo-random integer."),
    C_FN("srand", "void srand(unsigned int seed)", "Seeds the pseudo-random generator."),
    C_FN("aligned_alloc", "void *aligned_alloc(size_t alignment, size_t size)",
          "Allocates aligned memory."),
    C_FN("calloc", "void *calloc(size_t count, size_t size)", "Allocates zeroed memory."),
    C_FN("free", "void free(void *ptr)", "Releases allocated memory."),
    C_FN("malloc", "void *malloc(size_t size)", "Allocates uninitialized memory."),
    C_FN("realloc", "void *realloc(void *ptr, size_t size)", "Resizes an allocation."),
    C_FN("abort", "void abort(void)", "Terminates abnormally."),
    C_FN("atexit", "int atexit(void (*func)(void))", "Registers an exit handler."),
    C_FN("at_quick_exit", "int at_quick_exit(void (*func)(void))",
          "Registers a quick-exit handler."),
    C_FN("exit", "void exit(int status)", "Terminates the process normally."),
    C_FN("_Exit", "void _Exit(int status)", "Terminates without cleanup."),
    C_FN("quick_exit", "void quick_exit(int status)", "Terminates with quick-exit handlers."),
    C_FN("getenv", "char *getenv(const char *name)", "Reads an environment variable."),
    C_FN("system", "int system(const char *command)", "Runs a command through the shell."),
    C_FN("bsearch", "void *bsearch(const void *key, const void *base, size_t count, size_t size, "
                    "int (*compare)(const void *, const void *))",
          "Binary-searches a sorted array."),
    C_FN("qsort", "void qsort(void *base, size_t count, size_t size, "
                  "int (*compare)(const void *, const void *))",
          "Sorts an array."),
    C_FN("abs", "int abs(int value)", "Absolute value of an int."),
    C_FN("labs", "long labs(long value)", "Absolute value of a long."),
    C_FN("llabs", "long long llabs(long long value)", "Absolute value of a long long."),
    C_FN("div", "div_t div(int numerator, int denominator)", "Integer division and remainder."),
    C_FN("ldiv", "ldiv_t ldiv(long numerator, long denominator)", "Long division and remainder."),
    C_FN("lldiv", "lldiv_t lldiv(long long numerator, long long denominator)",
          "Long long division and remainder."),
    C_FN("mblen", "int mblen(const char *s, size_t n)", "Length of a multibyte character."),
    C_FN("mbtowc", "int mbtowc(wchar_t *pwc, const char *s, size_t n)",
          "Converts a multibyte character to wide."),
    C_FN("wctomb", "int wctomb(char *s, wchar_t wc)", "Converts a wide character to multibyte."),
    C_FN("mbstowcs", "size_t mbstowcs(wchar_t *dst, const char *src, size_t n)",
          "Converts a multibyte string to wide."),
    C_FN("wcstombs", "size_t wcstombs(char *dst, const wchar_t *src, size_t n)",
          "Converts a wide string to multibyte."),

    /* string.h. */
    C_FN("memcpy", "void *memcpy(void *dst, const void *src, size_t n)", "Copies bytes."),
    C_FN("memmove", "void *memmove(void *dst, const void *src, size_t n)",
          "Copies bytes with overlap support."),
    C_FN("strcpy", "char *strcpy(char *dst, const char *src)", "Copies a string."),
    C_FN("strncpy", "char *strncpy(char *dst, const char *src, size_t n)",
          "Copies at most n characters."),
    C_FN("strcat", "char *strcat(char *dst, const char *src)", "Appends a string."),
    C_FN("strncat", "char *strncat(char *dst, const char *src, size_t n)",
          "Appends at most n characters."),
    C_FN("memcmp", "int memcmp(const void *a, const void *b, size_t n)", "Compares bytes."),
    C_FN("strcmp", "int strcmp(const char *a, const char *b)", "Compares strings."),
    C_FN("strcoll", "int strcoll(const char *a, const char *b)", "Compares collated strings."),
    C_FN("strncmp", "int strncmp(const char *a, const char *b, size_t n)",
          "Compares at most n characters."),
    C_FN("strxfrm", "size_t strxfrm(char *dst, const char *src, size_t n)",
          "Transforms a string for collation."),
    C_FN("memchr", "void *memchr(const void *s, int c, size_t n)", "Finds a byte."),
    C_FN("strchr", "char *strchr(const char *s, int c)", "Finds the first occurrence of a character."),
    C_FN("strcspn", "size_t strcspn(const char *s, const char *reject)",
          "Length of a prefix without reject characters."),
    C_FN("strpbrk", "char *strpbrk(const char *s, const char *accept)",
          "Finds the first accepted character."),
    C_FN("strrchr", "char *strrchr(const char *s, int c)", "Finds the last occurrence of a character."),
    C_FN("strspn", "size_t strspn(const char *s, const char *accept)",
          "Length of a prefix of accepted characters."),
    C_FN("strstr", "char *strstr(const char *haystack, const char *needle)",
          "Finds a substring."),
    C_FN("strtok", "char *strtok(char *s, const char *delim)", "Tokenizes a string."),
    C_FN("memset", "void *memset(void *dst, int value, size_t n)", "Fills bytes."),
    C_FN("strerror", "char *strerror(int errnum)", "Describes an error number."),
    C_FN("strlen", "size_t strlen(const char *s)", "Returns string length."),
    C_FN("memccpy", "void *memccpy(void *dst, const void *src, int c, size_t n)",
          "Copies until a byte or limit."),
    C_FN("strdup", "char *strdup(const char *s)", "POSIX - duplicates a string."),
    C_FN("strndup", "char *strndup(const char *s, size_t n)", "POSIX - duplicates at most n bytes."),
    C_FN("strtok_r", "char *strtok_r(char *s, const char *delim, char **saveptr)",
          "POSIX - reentrant tokenizer."),
    C_FN("strcasecmp", "int strcasecmp(const char *a, const char *b)",
          "POSIX - case-insensitive string comparison."),
    C_FN("strncasecmp", "int strncasecmp(const char *a, const char *b, size_t n)",
          "POSIX - case-insensitive bounded comparison."),

    /* ctype.h. */
    C_FN("isalnum", "int isalnum(int c)", "Tests for an alphanumeric character."),
    C_FN("isalpha", "int isalpha(int c)", "Tests for an alphabetic character."),
    C_FN("isblank", "int isblank(int c)", "Tests for a blank character."),
    C_FN("iscntrl", "int iscntrl(int c)", "Tests for a control character."),
    C_FN("isdigit", "int isdigit(int c)", "Tests for a decimal digit."),
    C_FN("isgraph", "int isgraph(int c)", "Tests for a graphical character."),
    C_FN("islower", "int islower(int c)", "Tests for a lowercase character."),
    C_FN("isprint", "int isprint(int c)", "Tests for a printable character."),
    C_FN("ispunct", "int ispunct(int c)", "Tests for punctuation."),
    C_FN("isspace", "int isspace(int c)", "Tests for whitespace."),
    C_FN("isupper", "int isupper(int c)", "Tests for an uppercase character."),
    C_FN("isxdigit", "int isxdigit(int c)", "Tests for a hexadecimal digit."),
    C_FN("tolower", "int tolower(int c)", "Converts to lowercase."),
    C_FN("toupper", "int toupper(int c)", "Converts to uppercase."),

    /* math.h. */
    C_FN("acos", "double acos(double x)", "Arc cosine."),
    C_FN("asin", "double asin(double x)", "Arc sine."),
    C_FN("atan", "double atan(double x)", "Arc tangent."),
    C_FN("atan2", "double atan2(double y, double x)", "Two-argument arc tangent."),
    C_FN("cos", "double cos(double x)", "Cosine."),
    C_FN("sin", "double sin(double x)", "Sine."),
    C_FN("tan", "double tan(double x)", "Tangent."),
    C_FN("acosh", "double acosh(double x)", "Inverse hyperbolic cosine."),
    C_FN("asinh", "double asinh(double x)", "Inverse hyperbolic sine."),
    C_FN("atanh", "double atanh(double x)", "Inverse hyperbolic tangent."),
    C_FN("cosh", "double cosh(double x)", "Hyperbolic cosine."),
    C_FN("sinh", "double sinh(double x)", "Hyperbolic sine."),
    C_FN("tanh", "double tanh(double x)", "Hyperbolic tangent."),
    C_FN("exp", "double exp(double x)", "Base-e exponential."),
    C_FN("exp2", "double exp2(double x)", "Base-2 exponential."),
    C_FN("expm1", "double expm1(double x)", "exp(x) - 1."),
    C_FN("frexp", "double frexp(double x, int *exp)", "Splits a float into fraction and exponent."),
    C_FN("ilogb", "int ilogb(double x)", "Extracts the exponent as an integer."),
    C_FN("ldexp", "double ldexp(double x, int exp)", "Multiplies by a power of two."),
    C_FN("log", "double log(double x)", "Natural logarithm."),
    C_FN("log10", "double log10(double x)", "Base-10 logarithm."),
    C_FN("log1p", "double log1p(double x)", "log(1 + x)."),
    C_FN("log2", "double log2(double x)", "Base-2 logarithm."),
    C_FN("logb", "double logb(double x)", "Extracts the exponent as a float."),
    C_FN("modf", "double modf(double x, double *ipart)", "Splits into integral and fractional parts."),
    C_FN("scalbn", "double scalbn(double x, int n)", "Scales by FLT_RADIX^n."),
    C_FN("scalbln", "double scalbln(double x, long n)", "Scales by FLT_RADIX^n."),
    C_FN("cbrt", "double cbrt(double x)", "Cube root."),
    C_FN("fabs", "double fabs(double x)", "Absolute value."),
    C_FN("hypot", "double hypot(double x, double y)", "Euclidean distance."),
    C_FN("pow", "double pow(double x, double y)", "Raise to a power."),
    C_FN("sqrt", "double sqrt(double x)", "Square root."),
    C_FN("erf", "double erf(double x)", "Error function."),
    C_FN("erfc", "double erfc(double x)", "Complementary error function."),
    C_FN("lgamma", "double lgamma(double x)", "Log gamma function."),
    C_FN("tgamma", "double tgamma(double x)", "Gamma function."),
    C_FN("ceil", "double ceil(double x)", "Round upward."),
    C_FN("floor", "double floor(double x)", "Round downward."),
    C_FN("nearbyint", "double nearbyint(double x)", "Round using the current mode."),
    C_FN("rint", "double rint(double x)", "Round using the current mode, may raise exceptions."),
    C_FN("lrint", "long lrint(double x)", "Round to long."),
    C_FN("llrint", "long long llrint(double x)", "Round to long long."),
    C_FN("round", "double round(double x)", "Round to nearest, halfway away from zero."),
    C_FN("lround", "long lround(double x)", "Round to nearest long."),
    C_FN("llround", "long long llround(double x)", "Round to nearest long long."),
    C_FN("trunc", "double trunc(double x)", "Round toward zero."),
    C_FN("fmod", "double fmod(double x, double y)", "Floating-point remainder."),
    C_FN("remainder", "double remainder(double x, double y)", "IEEE remainder."),
    C_FN("remquo", "double remquo(double x, double y, int *quo)", "Remainder and quotient bits."),
    C_FN("copysign", "double copysign(double x, double y)", "Copies the sign of y to x."),
    C_FN("nan", "double nan(const char *tag)", "Creates a quiet NaN."),
    C_FN("nextafter", "double nextafter(double x, double y)", "Next representable value."),
    C_FN("nexttoward", "double nexttoward(double x, long double y)", "Next value toward y."),
    C_FN("fdim", "double fdim(double x, double y)", "Positive difference."),
    C_FN("fmax", "double fmax(double x, double y)", "Maximum of two values."),
    C_FN("fmin", "double fmin(double x, double y)", "Minimum of two values."),
    C_FN("fma", "double fma(double x, double y, double z)", "Fused multiply-add."),
    C_FN("fpclassify", "int fpclassify(real-floating x)", "Classifies a floating-point value."),
    C_FN("isfinite", "int isfinite(real-floating x)", "Tests for a finite value."),
    C_FN("isinf", "int isinf(real-floating x)", "Tests for infinity."),
    C_FN("isnan", "int isnan(real-floating x)", "Tests for NaN."),
    C_FN("isnormal", "int isnormal(real-floating x)", "Tests for a normal value."),
    C_FN("signbit", "int signbit(real-floating x)", "Tests the sign bit."),
    C_FN("isgreater", "int isgreater(x, y)", "Tests x > y without raising invalid."),
    C_FN("isgreaterequal", "int isgreaterequal(x, y)", "Tests x >= y without raising invalid."),
    C_FN("isless", "int isless(x, y)", "Tests x < y without raising invalid."),
    C_FN("islessequal", "int islessequal(x, y)", "Tests x <= y without raising invalid."),
    C_FN("islessgreater", "int islessgreater(x, y)", "Tests x < y or x > y."),
    C_FN("isunordered", "int isunordered(x, y)", "Tests whether either value is NaN."),

    /* time.h. */
    C_FN("clock", "clock_t clock(void)", "Processor time used by the process."),
    C_FN("difftime", "double difftime(time_t end, time_t begin)", "Difference between times."),
    C_FN("mktime", "time_t mktime(struct tm *timeptr)", "Converts local time to time_t."),
    C_FN("time", "time_t time(time_t *result)", "Returns the current calendar time."),
    C_FN("asctime", "char *asctime(const struct tm *timeptr)", "Formats a tm as text."),
    C_FN("ctime", "char *ctime(const time_t *timer)", "Formats a time_t as text."),
    C_FN("gmtime", "struct tm *gmtime(const time_t *timer)", "Converts to UTC calendar time."),
    C_FN("localtime", "struct tm *localtime(const time_t *timer)", "Converts to local calendar time."),
    C_FN("strftime", "size_t strftime(char *s, size_t max, const char *format, "
                     "const struct tm *timeptr)",
          "Formats calendar time."),
    C_FN("timespec_get", "int timespec_get(struct timespec *ts, int base)",
          "Reads a time base into timespec."),
    C_FN("timespec_getres", "int timespec_getres(struct timespec *ts, int base)",
          "Reads the resolution of a time base."),

    /* signal.h and setjmp.h. */
    C_FN("signal", "void (*signal(int sig, void (*handler)(int)))(int)",
          "Installs a signal handler."),
    C_FN("raise", "int raise(int sig)", "Sends a signal to the current process."),
    C_FN("setjmp", "int setjmp(jmp_buf env)", "Saves the current execution context."),
    C_FN("longjmp", "void longjmp(jmp_buf env, int value)", "Restores a saved context."),

    /* stdarg.h. */
    C_FN("va_start", "void va_start(va_list ap, last)", "Starts variadic argument access."),
    C_FN("va_arg", "type va_arg(va_list ap, type)", "Reads the next variadic argument."),
    C_FN("va_end", "void va_end(va_list ap)", "Ends variadic argument access."),
    C_FN("va_copy", "void va_copy(va_list dst, va_list src)", "Copies a variadic argument list."),

    /* wchar.h and wctype.h. */
    C_FN("wprintf", "int wprintf(const wchar_t *format, ...)", "Writes wide formatted output."),
    C_FN("fwprintf", "int fwprintf(FILE *stream, const wchar_t *format, ...)",
          "Writes wide formatted output to a stream."),
    C_FN("swprintf", "int swprintf(wchar_t *s, size_t n, const wchar_t *format, ...)",
          "Writes wide formatted output to a buffer."),
    C_FN("wscanf", "int wscanf(const wchar_t *format, ...)", "Reads wide formatted input."),
    C_FN("fwscanf", "int fwscanf(FILE *stream, const wchar_t *format, ...)",
          "Reads wide formatted input from a stream."),
    C_FN("swscanf", "int swscanf(const wchar_t *s, const wchar_t *format, ...)",
          "Reads wide formatted input from a string."),
    C_FN("fgetwc", "wint_t fgetwc(FILE *stream)", "Reads a wide character from a stream."),
    C_FN("fgetws", "wchar_t *fgetws(wchar_t *s, int n, FILE *stream)", "Reads a wide line."),
    C_FN("fputwc", "wint_t fputwc(wchar_t c, FILE *stream)", "Writes a wide character."),
    C_FN("fputws", "int fputws(const wchar_t *s, FILE *stream)", "Writes a wide string."),
    C_FN("getwc", "wint_t getwc(FILE *stream)", "Reads a wide character from a stream."),
    C_FN("putwc", "wint_t putwc(wchar_t c, FILE *stream)", "Writes a wide character."),
    C_FN("ungetwc", "wint_t ungetwc(wint_t c, FILE *stream)", "Pushes a wide character back."),
    C_FN("wcslen", "size_t wcslen(const wchar_t *s)", "Wide string length."),
    C_FN("wcscmp", "int wcscmp(const wchar_t *a, const wchar_t *b)", "Compares wide strings."),
    C_FN("wcsncmp", "int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n)",
          "Compares bounded wide strings."),
    C_FN("wcscpy", "wchar_t *wcscpy(wchar_t *dst, const wchar_t *src)", "Copies a wide string."),
    C_FN("wcsncpy", "wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n)",
          "Copies a bounded wide string."),
    C_FN("wcscat", "wchar_t *wcscat(wchar_t *dst, const wchar_t *src)", "Appends a wide string."),
    C_FN("wcsncat", "wchar_t *wcsncat(wchar_t *dst, const wchar_t *src, size_t n)",
          "Appends a bounded wide string."),
    C_FN("wcsstr", "wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle)",
          "Finds a wide substring."),
    C_FN("wcschr", "wchar_t *wcschr(const wchar_t *s, wchar_t c)", "Finds a wide character."),
    C_FN("wcsrchr", "wchar_t *wcsrchr(const wchar_t *s, wchar_t c)",
          "Finds the last wide character."),
    C_FN("wcstol", "long wcstol(const wchar_t *s, wchar_t **end, int base)",
          "Parses a wide long integer."),
    C_FN("wcstoul", "unsigned long wcstoul(const wchar_t *s, wchar_t **end, int base)",
          "Parses a wide unsigned long."),
    C_FN("wcstod", "double wcstod(const wchar_t *s, wchar_t **end)", "Parses a wide double."),
    C_FN("mbstate_reset", "void mbstate_reset(mbstate_t *state)", "Implementation helper."),
    C_FN("mbrtowc", "size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps)",
          "Converts a multibyte character to wide."),
    C_FN("wcrtomb", "size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps)",
          "Converts a wide character to multibyte."),
    C_FN("mbsrtowcs", "size_t mbsrtowcs(wchar_t *dst, const char **src, size_t n, mbstate_t *ps)",
          "Converts a multibyte string to wide."),
    C_FN("wcsrtombs", "size_t wcsrtombs(char *dst, const wchar_t **src, size_t n, mbstate_t *ps)",
          "Converts a wide string to multibyte."),
    C_FN("iswalnum", "int iswalnum(wint_t c)", "Tests a wide alphanumeric character."),
    C_FN("iswalpha", "int iswalpha(wint_t c)", "Tests a wide alphabetic character."),
    C_FN("iswblank", "int iswblank(wint_t c)", "Tests a wide blank character."),
    C_FN("iswcntrl", "int iswcntrl(wint_t c)", "Tests a wide control character."),
    C_FN("iswdigit", "int iswdigit(wint_t c)", "Tests a wide digit."),
    C_FN("iswgraph", "int iswgraph(wint_t c)", "Tests a wide graphical character."),
    C_FN("iswlower", "int iswlower(wint_t c)", "Tests a wide lowercase character."),
    C_FN("iswprint", "int iswprint(wint_t c)", "Tests a wide printable character."),
    C_FN("iswpunct", "int iswpunct(wint_t c)", "Tests wide punctuation."),
    C_FN("iswspace", "int iswspace(wint_t c)", "Tests wide whitespace."),
    C_FN("iswupper", "int iswupper(wint_t c)", "Tests a wide uppercase character."),
    C_FN("iswxdigit", "int iswxdigit(wint_t c)", "Tests a wide hexadecimal digit."),
    C_FN("towlower", "wint_t towlower(wint_t c)", "Converts a wide character to lowercase."),
    C_FN("towupper", "wint_t towupper(wint_t c)", "Converts a wide character to uppercase."),
    C_FN("wctype", "wctype_t wctype(const char *name)", "Looks up a wide character class."),
    C_FN("iswctype", "int iswctype(wint_t c, wctype_t desc)", "Tests a wide character class."),

    /* uchar.h. */
    C_FN("mbrtoc16", "size_t mbrtoc16(char16_t *pc16, const char *s, size_t n, mbstate_t *ps)",
          "Converts multibyte to UTF-16."),
    C_FN("c16rtomb", "size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps)",
          "Converts UTF-16 to multibyte."),
    C_FN("mbrtoc32", "size_t mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps)",
          "Converts multibyte to UTF-32."),
    C_FN("c32rtomb", "size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps)",
          "Converts UTF-32 to multibyte."),

    /* stdatomic.h operations. */
    C_FN("atomic_init", "void atomic_init(atomic *obj, value)", "Initializes an atomic object."),
    C_FN("atomic_store", "void atomic_store(atomic *obj, value)",
          "Stores a value with sequential consistency."),
    C_FN("atomic_load", "value atomic_load(const atomic *obj)",
          "Loads a value with sequential consistency."),
    C_FN("atomic_exchange", "value atomic_exchange(atomic *obj, value)",
          "Replaces an atomic value."),
    C_FN("atomic_compare_exchange_strong",
          "bool atomic_compare_exchange_strong(atomic *obj, expected, desired)",
          "Strong atomic compare-and-exchange."),
    C_FN("atomic_compare_exchange_weak",
          "bool atomic_compare_exchange_weak(atomic *obj, expected, desired)",
          "Weak atomic compare-and-exchange."),
    C_FN("atomic_fetch_add", "value atomic_fetch_add(atomic *obj, value)",
          "Atomic addition."),
    C_FN("atomic_fetch_sub", "value atomic_fetch_sub(atomic *obj, value)",
          "Atomic subtraction."),
    C_FN("atomic_fetch_or", "value atomic_fetch_or(atomic *obj, value)", "Atomic bitwise OR."),
    C_FN("atomic_fetch_xor", "value atomic_fetch_xor(atomic *obj, value)", "Atomic bitwise XOR."),
    C_FN("atomic_fetch_and", "value atomic_fetch_and(atomic *obj, value)", "Atomic bitwise AND."),
    C_FN("atomic_thread_fence", "void atomic_thread_fence(memory_order order)",
          "Establishes a thread fence."),
    C_FN("atomic_signal_fence", "void atomic_signal_fence(memory_order order)",
          "Establishes a signal fence."),
    C_FN("atomic_is_lock_free", "bool atomic_is_lock_free(const atomic *obj)",
          "Tests whether an atomic is lock-free."),
    C_FN("kill_dependency", "type kill_dependency(type y)", "Breaks a data dependency."),
    C_FN("atomic_flag_test_and_set", "bool atomic_flag_test_and_set(atomic_flag *flag)",
          "Sets an atomic flag and returns its previous value."),
    C_FN("atomic_flag_clear", "void atomic_flag_clear(atomic_flag *flag)", "Clears an atomic flag."),

    /* threads.h. */
    C_FN("thrd_create", "int thrd_create(thrd_t *thr, thrd_start_t func, void *arg)",
          "Creates a thread."),
    C_FN("thrd_equal", "int thrd_equal(thrd_t a, thrd_t b)", "Tests whether two handles are equal."),
    C_FN("thrd_current", "thrd_t thrd_current(void)", "Returns the current thread handle."),
    C_FN("thrd_sleep", "int thrd_sleep(const struct timespec *duration, struct timespec *remaining)",
          "Suspends the current thread."),
    C_FN("thrd_yield", "void thrd_yield(void)", "Yields execution to another thread."),
    C_FN("thrd_exit", "void thrd_exit(int result)", "Terminates the current thread."),
    C_FN("thrd_detach", "int thrd_detach(thrd_t thr)", "Detaches a thread."),
    C_FN("thrd_join", "int thrd_join(thrd_t thr, int *result)", "Waits for a thread."),
    C_FN("mtx_init", "int mtx_init(mtx_t *mtx, int type)", "Initializes a mutex."),
    C_FN("mtx_lock", "int mtx_lock(mtx_t *mtx)", "Locks a mutex."),
    C_FN("mtx_timedlock", "int mtx_timedlock(mtx_t *mtx, const struct timespec *ts)",
          "Locks a mutex with a timeout."),
    C_FN("mtx_trylock", "int mtx_trylock(mtx_t *mtx)", "Tries to lock a mutex."),
    C_FN("mtx_unlock", "int mtx_unlock(mtx_t *mtx)", "Unlocks a mutex."),
    C_FN("mtx_destroy", "void mtx_destroy(mtx_t *mtx)", "Destroys a mutex."),
    C_FN("cnd_init", "int cnd_init(cnd_t *cond)", "Initializes a condition variable."),
    C_FN("cnd_signal", "int cnd_signal(cnd_t *cond)", "Signals one waiter."),
    C_FN("cnd_broadcast", "int cnd_broadcast(cnd_t *cond)", "Signals all waiters."),
    C_FN("cnd_wait", "int cnd_wait(cnd_t *cond, mtx_t *mtx)", "Waits on a condition variable."),
    C_FN("cnd_timedwait", "int cnd_timedwait(cnd_t *cond, mtx_t *mtx, "
                          "const struct timespec *ts)",
          "Waits on a condition variable with a timeout."),
    C_FN("cnd_destroy", "void cnd_destroy(cnd_t *cond)", "Destroys a condition variable."),
    C_FN("tss_create", "int tss_create(tss_t *key, void (*dtor)(void *))",
          "Creates thread-specific storage."),
    C_FN("tss_get", "void *tss_get(tss_t key)", "Reads thread-specific storage."),
    C_FN("tss_set", "int tss_set(tss_t key, void *value)", "Writes thread-specific storage."),
    C_FN("tss_delete", "void tss_delete(tss_t key)", "Deletes thread-specific storage."),
    C_FN("call_once", "void call_once(once_flag *flag, void (*func)(void))",
          "Runs a function exactly once."),

    /* complex.h arithmetic. */
    C_FN("creal", "double creal(double complex z)", "Real part of a complex value."),
    C_FN("cimag", "double cimag(double complex z)", "Imaginary part of a complex value."),
    C_FN("carg", "double carg(double complex z)", "Phase angle of a complex value."),
    C_FN("cabs", "double cabs(double complex z)", "Magnitude of a complex value."),
    C_FN("cexp", "double complex cexp(double complex z)", "Complex exponential."),
    C_FN("clog", "double complex clog(double complex z)", "Complex logarithm."),
    C_FN("cpow", "double complex cpow(double complex x, double complex y)",
          "Complex power."),
    C_FN("csqrt", "double complex csqrt(double complex z)", "Complex square root."),
    C_FN("csin", "double complex csin(double complex z)", "Complex sine."),
    C_FN("ccos", "double complex ccos(double complex z)", "Complex cosine."),
    C_FN("ctan", "double complex ctan(double complex z)", "Complex tangent."),
    C_FN("csinh", "double complex csinh(double complex z)", "Complex hyperbolic sine."),
    C_FN("ccosh", "double complex ccosh(double complex z)", "Complex hyperbolic cosine."),
    C_FN("ctanh", "double complex ctanh(double complex z)", "Complex hyperbolic tangent."),
    C_FN("cproj", "double complex cproj(double complex z)", "Projects onto the Riemann sphere."),

    /* fenv.h. */
    C_FN("feclearexcept", "int feclearexcept(int excepts)", "Clears floating-point exceptions."),
    C_FN("fegetexceptflag", "int fegetexceptflag(fexcept_t *flagp, int excepts)",
          "Saves exception flags."),
    C_FN("feraiseexcept", "int feraiseexcept(int excepts)", "Raises floating-point exceptions."),
    C_FN("fesetexceptflag", "int fesetexceptflag(const fexcept_t *flagp, int excepts)",
          "Restores exception flags."),
    C_FN("fetestexcept", "int fetestexcept(int excepts)", "Tests exception flags."),
    C_FN("fegetround", "int fegetround(void)", "Reads the rounding direction."),
    C_FN("fesetround", "int fesetround(int round)", "Sets the rounding direction."),
    C_FN("fegetenv", "int fegetenv(fenv_t *envp)", "Saves the floating-point environment."),
    C_FN("feholdexcept", "int feholdexcept(fenv_t *envp)", "Saves and clears the environment."),
    C_FN("fesetenv", "int fesetenv(const fenv_t *envp)", "Restores the floating-point environment."),
    C_FN("feupdateenv", "int feupdateenv(const fenv_t *envp)", "Restores and re-raises exceptions."),

    /* stdbit.h (C23). */
    C_FN("stdc_leading_zeros", "unsigned int stdc_leading_zeros(type value)",
          "Counts leading zero bits."),
    C_FN("stdc_trailing_zeros", "unsigned int stdc_trailing_zeros(type value)",
          "Counts trailing zero bits."),
    C_FN("stdc_count_ones", "unsigned int stdc_count_ones(type value)", "Counts one bits."),
    C_FN("stdc_count_zeros", "unsigned int stdc_count_zeros(type value)", "Counts zero bits."),
    C_FN("stdc_bit_width", "unsigned int stdc_bit_width(type value)",
          "Minimum bits needed to represent a value."),
    C_FN("stdc_bit_floor", "type stdc_bit_floor(type value)", "Largest power of two not above value."),
    C_FN("stdc_bit_ceil", "type stdc_bit_ceil(type value)", "Smallest power of two not below value."),
    C_FN("stdc_has_single_bit", "bool stdc_has_single_bit(type value)",
          "Tests whether exactly one bit is set."),
    C_FN("stdc_bit_rotate_left", "type stdc_bit_rotate_left(type value, unsigned int shift)",
          "Rotates bits left."),
    C_FN("stdc_bit_rotate_right", "type stdc_bit_rotate_right(type value, unsigned int shift)",
          "Rotates bits right."),

    /* stdckdint.h (C23). */
    C_FN("ckd_add", "bool ckd_add(type *result, type a, type b)", "Checked addition."),
    C_FN("ckd_sub", "bool ckd_sub(type *result, type a, type b)", "Checked subtraction."),
    C_FN("ckd_mul", "bool ckd_mul(type *result, type a, type b)", "Checked multiplication."),

    /* AromaUI API used by IDE projects. */
    C_FN("aroma_ui_init", "void aroma_ui_init(void)", "Initializes the AromaUI runtime."),
    C_FN("aroma_ui_create_window", "AromaWindow *aroma_ui_create_window(const char *title, int width, int height)",
          "Creates a native window."),
    C_FN("aroma_ui_render", "void aroma_ui_render(AromaWindow *window)", "Renders a window."),
    C_FN("aroma_ui_process_events", "void aroma_ui_process_events(void)", "Pumps native events."),
    C_FN("aroma_ui_shutdown", "void aroma_ui_shutdown(void)", "Releases the AromaUI runtime."),
    C_FN("aroma_label_set_text", "void aroma_label_set_text(AromaNode *node, const char *text)",
          "Updates label text."),
    C_FN("aroma_canvas_create", "AromaNode *aroma_canvas_create(AromaNode *parent, int x, int y, int width, int height)",
          "Creates a Canvas drawing surface."),
    C_FN("IncenseLoadFileEx", "AromaWindow *IncenseLoadFileEx(const char *path, AromaFont *font, AromaFont *icon_font, IncenseRegistry **registry)",
          "Loads an Incense markup document."),
    C_FN("IncenseFindWidget", "AromaNode *IncenseFindWidget(IncenseRegistry *registry, const char *id)",
          "Finds a widget by Incense id."),
    C_FN("IncenseRegisterCallback", "void IncenseRegisterCallback(const char *name, IncenseCallbackKind kind, void *callback, void *userdata)",
          "Registers a markup callback."),
    C_FN("aroma_font_create_from_memory", "AromaFont *aroma_font_create_from_memory(const unsigned char *data, size_t size, int pixel_size)",
          "Loads a font from embedded bytes."),
    C_FN("aroma_theme_create_material_black", "AromaTheme *aroma_theme_create_material_black(void)",
          "Creates the dark Material theme."),
    C_FN("AROMA_NODE_AS", "AROMA_NODE_AS(node, Type)", "Casts a node to a widget type."),
};

static const char *const k_c_headers[] = {
    "assert.h", "complex.h", "ctype.h", "errno.h", "fenv.h", "float.h",
    "inttypes.h", "iso646.h", "limits.h", "locale.h", "math.h", "setjmp.h",
    "signal.h", "stdalign.h", "stdarg.h", "stdatomic.h", "stdbool.h",
    "stdckdint.h", "stddef.h", "stdint.h", "stdio.h", "stdlib.h",
    "stdnoreturn.h", "stdbit.h", "string.h", "tgmath.h", "threads.h",
    "time.h", "uchar.h", "wchar.h", "wctype.h"
};

static const char *const k_cpp_headers[] = {
    "algorithm", "any", "array", "atomic", "bit", "bitset", "cassert",
    "cctype", "cerrno", "cfenv", "cfloat", "charconv", "chrono", "cinttypes",
    "climits", "clocale", "cmath", "codecvt", "compare", "complex", "concepts",
    "condition_variable", "coroutine", "csetjmp", "csignal", "cstdarg",
    "cstddef", "cstdint", "cstdio", "cstdlib", "cstring", "ctime", "cuchar",
    "cwchar", "cwctype", "deque", "exception", "execution", "filesystem",
    "format", "forward_list", "fstream", "functional", "future", "initializer_list",
    "iomanip", "ios", "iosfwd", "iostream", "istream", "iterator", "latch",
    "limits", "list", "locale", "map", "memory", "memory_resource", "mutex",
    "new", "numbers", "numeric", "optional", "ostream", "queue", "random",
    "ranges", "ratio", "regex", "scoped_allocator", "semaphore", "set",
    "shared_mutex", "source_location", "span", "sstream", "stack", "stdexcept",
    "stop_token", "streambuf", "string", "string_view", "syncstream", "system_error",
    "thread", "tuple", "type_traits", "typeindex", "typeinfo", "unordered_map",
    "unordered_set", "utility", "valarray", "variant", "vector", "version"
};

static const CompletionSpec k_c_directives[] = {
    {"include", "Preprocessor - include a header", "include", AROMA_CODE_COMPLETION_KEYWORD,
     "#include <header>"},
    {"include_next", "GNU preprocessor - include the next matching header", "include_next",
     AROMA_CODE_COMPLETION_KEYWORD, "#include_next <header>"},
    {"import", "Preprocessor - import a header once", "import", AROMA_CODE_COMPLETION_KEYWORD,
     "#import <header>"},
    {"define", "Preprocessor - define a macro", "define", AROMA_CODE_COMPLETION_KEYWORD,
     "#define NAME value"},
    {"undef", "Preprocessor - remove a macro", "undef", AROMA_CODE_COMPLETION_KEYWORD,
     "#undef NAME"},
    {"if", "Preprocessor - conditional compilation", "if", AROMA_CODE_COMPLETION_KEYWORD,
     "#if expression"},
    {"ifdef", "Preprocessor - compile when a macro is defined", "ifdef",
     AROMA_CODE_COMPLETION_KEYWORD, "#ifdef NAME"},
    {"ifndef", "Preprocessor - compile when a macro is undefined", "ifndef",
     AROMA_CODE_COMPLETION_KEYWORD, "#ifndef NAME"},
    {"elif", "Preprocessor - alternative conditional branch", "elif",
     AROMA_CODE_COMPLETION_KEYWORD, "#elif expression"},
    {"elifdef", "C23 preprocessor - branch when a macro is defined", "elifdef",
     AROMA_CODE_COMPLETION_KEYWORD, "#elifdef NAME"},
    {"elifndef", "C23 preprocessor - branch when a macro is undefined", "elifndef",
     AROMA_CODE_COMPLETION_KEYWORD, "#elifndef NAME"},
    {"else", "Preprocessor - fallback conditional branch", "else",
     AROMA_CODE_COMPLETION_KEYWORD, "#else"},
    {"endif", "Preprocessor - close a conditional block", "endif",
     AROMA_CODE_COMPLETION_KEYWORD, "#endif"},
    {"error", "Preprocessor - emit a compile error", "error", AROMA_CODE_COMPLETION_KEYWORD,
     "#error message"},
    {"warning", "Preprocessor - emit a compile warning", "warning",
     AROMA_CODE_COMPLETION_KEYWORD, "#warning message"},
    {"pragma", "Preprocessor - implementation-defined directive", "pragma",
     AROMA_CODE_COMPLETION_KEYWORD, "#pragma once"},
    {"line", "Preprocessor - change the reported line number", "line",
     AROMA_CODE_COMPLETION_KEYWORD, "#line number \"file\""},
    {"embed", "C23 preprocessor - embed a binary resource", "embed",
     AROMA_CODE_COMPLETION_KEYWORD, "#embed \"file\""},
    {"assert", "Preprocessor - GNU static assertion", "assert",
     AROMA_CODE_COMPLETION_KEYWORD, "#assert expression"},
    {"ident", "Preprocessor - implementation identification", "ident",
     AROMA_CODE_COMPLETION_KEYWORD, "#ident \"string\""},
    {"sccs", "Preprocessor - SCCS identification", "sccs",
     AROMA_CODE_COMPLETION_KEYWORD, "#sccs \"string\""},
};

typedef enum CCompletionContext
{
    C_COMPLETION_NORMAL,
    C_COMPLETION_DIRECTIVE,
    C_COMPLETION_INCLUDE
} CCompletionContext;

static bool c_is_ident_start(char c)
{
    return isalpha((unsigned char)c) || c == '_';
}

static bool c_is_ident_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static bool c_is_c_like_language(AromaCodeLanguage language)
{
    return language == AROMA_CODE_LANGUAGE_C || language == AROMA_CODE_LANGUAGE_CPP;
}

static bool c_word_equal(const char *text, int length, const char *word)
{
    return text && word && (int)strlen(word) == length &&
           strncmp(text, word, (size_t)length) == 0;
}

static int c_copy_word(char *out, size_t out_size, const char *text, int length)
{
    if (!out || out_size == 0)
        return 0;
    if (length < 0)
        length = 0;
    if ((size_t)length >= out_size)
        length = (int)out_size - 1;
    if (length > 0)
        memcpy(out, text, (size_t)length);
    out[length] = '\0';
    return length;
}

static int c_append_completion(AromaCodeCompletion *out, int count, int max,
                               const char *label, const char *detail,
                               const char *insert, AromaCodeCompletionKind kind)
{
    if (!out || count >= max || !label || !label[0])
        return count;
    AromaCodeCompletion *completion = &out[count++];
    snprintf(completion->label, sizeof(completion->label), "%s", label);
    snprintf(completion->detail, sizeof(completion->detail), "%s",
             detail ? detail : "");
    snprintf(completion->insert_text, sizeof(completion->insert_text), "%s",
             insert && insert[0] ? insert : label);
    completion->kind = kind;
    return count;
}

static bool c_completion_exists(const AromaCodeCompletion *out, int count,
                                const char *label)
{
    for (int i = 0; i < count; i++)
    {
        if (strcmp(out[i].label, label) == 0)
            return true;
    }
    return false;
}

static CCompletionContext c_completion_context(const char *line, int length,
                                               int cursor, char *partial,
                                               size_t partial_size, char *closer)
{
    if (partial && partial_size)
        partial[0] = '\0';
    if (closer)
        *closer = '\0';
    if (!line || cursor < 0)
        return C_COMPLETION_NORMAL;
    if (cursor > length)
        cursor = length;

    int start = 0;
    while (start < length && (line[start] == ' ' || line[start] == '\t'))
        start++;
    if (start >= length || line[start] != '#')
        return C_COMPLETION_NORMAL;
    if (cursor <= start + 1)
        return C_COMPLETION_DIRECTIVE;

    int p = start + 1;
    while (p < length && (line[p] == ' ' || line[p] == '\t'))
        p++;
    if (cursor <= p)
        return C_COMPLETION_DIRECTIVE;

    int word_start = p;
    while (p < length && c_is_ident_char(line[p]))
        p++;
    if (p == word_start)
        return C_COMPLETION_NORMAL;
    if (cursor <= p)
    {
        c_copy_word(partial, partial_size, line + word_start, cursor - word_start);
        return C_COMPLETION_DIRECTIVE;
    }

    char directive[32];
    c_copy_word(directive, sizeof(directive), line + word_start, p - word_start);
    bool include = strcmp(directive, "include") == 0 ||
                   strcmp(directive, "include_next") == 0 ||
                   strcmp(directive, "import") == 0 ||
                   strcmp(directive, "embed") == 0;
    if (!include)
        return C_COMPLETION_NORMAL;

    int q = p;
    while (q < length && (line[q] == ' ' || line[q] == '\t'))
        q++;
    if (q >= length || (line[q] != '<' && line[q] != '"'))
        return C_COMPLETION_NORMAL;
    if (cursor <= q + 1)
    {
        if (closer)
            *closer = line[q] == '<' ? '>' : '"';
        return C_COMPLETION_INCLUDE;
    }

    char close = line[q] == '<' ? '>' : '"';
    for (int i = q + 1; i < cursor; i++)
    {
        if (line[i] == close)
            return C_COMPLETION_NORMAL;
    }
    c_copy_word(partial, partial_size, line + q + 1, cursor - q - 1);
    if (closer)
        *closer = close;
    return C_COMPLETION_INCLUDE;
}

static int append_c_directives(AromaCodeCompletion *out, int count, int max)
{
    for (int i = 0; i < COMPLETION_COUNT(k_c_directives) && count < max; i++)
        copy_completion(&out[count++], &k_c_directives[i]);
    return count;
}

static int append_c_headers(AromaCodeCompletion *out, int count, int max,
                            AromaCodeLanguage language, char closer)
{
    char suffix[2] = {closer, '\0'};
    char insert[192];
    for (int i = 0; i < COMPLETION_COUNT(k_c_headers) && count < max; i++)
    {
        const char *header = k_c_headers[i];
        if (c_completion_exists(out, count, header))
            continue;
        snprintf(insert, sizeof(insert), "%s%s", header, suffix);
        count = c_append_completion(out, count, max, header,
                                    "Standard C header", insert,
                                    AROMA_CODE_COMPLETION_MODULE);
    }
    if (language == AROMA_CODE_LANGUAGE_CPP)
    {
        for (int i = 0; i < COMPLETION_COUNT(k_cpp_headers) && count < max; i++)
        {
            const char *header = k_cpp_headers[i];
            if (c_completion_exists(out, count, header))
                continue;
            snprintf(insert, sizeof(insert), "%s%s", header, suffix);
            count = c_append_completion(out, count, max, header,
                                        "Standard C++ header", insert,
                                        AROMA_CODE_COMPLETION_MODULE);
        }
    }

    for (int file = 0; file < ide_file_count && count < max; file++)
    {
        const char *name = ide_files[file].name;
        const char *path = ide_files[file].path;
        if (!name || !strstr(name, "."))
            continue;
        const char *dot = strrchr(name, '.');
        if (!dot)
            continue;
        if (strcmp(dot, ".h") != 0 && strcmp(dot, ".hpp") != 0 &&
            strcmp(dot, ".hh") != 0 && strcmp(dot, ".hxx") != 0)
            continue;
        if (!c_completion_exists(out, count, name))
        {
            snprintf(insert, sizeof(insert), "%s%s", name, suffix);
            char detail[160];
            snprintf(detail, sizeof(detail), "Project header - %s", path);
            count = c_append_completion(out, count, max, name, detail, insert,
                                        AROMA_CODE_COMPLETION_MODULE);
        }
        /* Also expose the project-relative path so includes such as
           `#include "ui/main.h"` can be completed from the start. */
        if (path && path[0] && strcmp(path, name) != 0 &&
            strlen(path) < sizeof(out[0].label) &&
            !c_completion_exists(out, count, path))
        {
            snprintf(insert, sizeof(insert), "%s%s", path, suffix);
            char detail[160];
            snprintf(detail, sizeof(detail), "Project header - %s", path);
            count = c_append_completion(out, count, max, path, detail, insert,
                                        AROMA_CODE_COMPLETION_MODULE);
        }
    }
    return count;
}

static bool c_keyword_word(const char *word)
{
    static const char *const words[] = {
        "return", "if", "else", "for", "while", "do", "switch", "case",
        "default", "break", "continue", "goto", "sizeof", "typedef", "struct",
        "union", "enum", "static", "extern", "const", "volatile", "register",
        "auto", "inline", "restrict", "void", "char", "short", "int", "long",
        "float", "double", "signed", "unsigned", "bool", "_Bool", "true",
        "false", "NULL", "nullptr"
    };
    if (!word || !word[0])
        return false;
    for (int i = 0; i < COMPLETION_COUNT(words); i++)
    {
        if (strcmp(word, words[i]) == 0)
            return true;
    }
    return false;
}

static int append_project_symbol(AromaCodeCompletion *out, int count, int max,
                                 const char *name, const char *detail,
                                 AromaCodeCompletionKind kind)
{
    if (!name || !name[0] || count >= max || c_completion_exists(out, count, name))
        return count;
    return c_append_completion(out, count, max, name, detail, name, kind);
}

static int collect_project_c_symbols_from_text(const char *text, const char *path,
                                               AromaCodeCompletion *out, int count,
                                               int max)
{
    if (!text || !path)
        return count;
    const char *line = text;
    int brace_depth = 0;
    bool in_block_comment = false;
    while (*line && count < max)
    {
        const char *end = strchr(line, '\n');
        if (!end)
            end = line + strlen(line);
        int length = (int)(end - line);
        char buffer[512];
        if (length >= (int)sizeof(buffer))
            length = (int)sizeof(buffer) - 1;
        memcpy(buffer, line, (size_t)length);
        buffer[length] = '\0';

        const char *trimmed = buffer;
        while (*trimmed == ' ' || *trimmed == '\t')
            trimmed++;

        int brace_delta = 0;
        bool in_string = false;
        bool in_char = false;
        bool escaped = false;
        for (const char *q = trimmed; *q; q++)
        {
            if (in_block_comment)
            {
                if (q[0] == '*' && q[1] == '/')
                {
                    in_block_comment = false;
                    q++;
                }
                continue;
            }
            if (in_string || in_char)
            {
                if (escaped)
                    escaped = false;
                else if (q[0] == '\\')
                    escaped = true;
                else if (in_string && q[0] == '"')
                    in_string = false;
                else if (in_char && q[0] == '\'')
                    in_char = false;
                continue;
            }
            if (q[0] == '/' && q[1] == '/')
                break;
            if (q[0] == '/' && q[1] == '*')
            {
                in_block_comment = true;
                q++;
            }
            else if (q[0] == '"')
                in_string = true;
            else if (q[0] == '\'')
                in_char = true;
            else if (q[0] == '{')
                brace_delta++;
            else if (q[0] == '}')
                brace_delta--;
        }
        bool top_level = brace_depth == 0;
        bool preprocessor = trimmed[0] == '#';

        if (*trimmed == '\0' || strncmp(trimmed, "//", 2) == 0 ||
            strncmp(trimmed, "/*", 2) == 0 || *trimmed == '*')
        {
            if (!preprocessor)
                brace_depth += brace_delta;
            if (*end == '\0')
                break;
            line = end + 1;
            continue;
        }

        if (strncmp(trimmed, "#define ", 8) == 0)
        {
            const char *name = trimmed + 8;
            while (*name == ' ' || *name == '\t')
                name++;
            int name_length = 0;
            while (name[name_length] &&
                   (isalnum((unsigned char)name[name_length]) ||
                    name[name_length] == '_'))
                name_length++;
            if (name_length > 0)
            {
                char symbol[80];
                c_copy_word(symbol, sizeof(symbol), name, name_length);
                char detail[160];
                snprintf(detail, sizeof(detail), "Project macro - %s", path);
                count = append_project_symbol(out, count, max, symbol, detail,
                                               AROMA_CODE_COMPLETION_VARIABLE);
            }
        }
        else if (*trimmed != '#')
        {
            int content_length = (int)strlen(trimmed);

            if (top_level && strncmp(trimmed, "typedef", 7) == 0)
            {                int last_start = -1, last_end = -1;
                for (int i = 0; i < content_length; i++)
                {
                    if (c_is_ident_start(trimmed[i]))
                    {
                        int start = i++;
                        while (i < content_length && c_is_ident_char(trimmed[i]))
                            i++;
                        if (!c_keyword_word(trimmed + start))
                        {
                            last_start = start;
                            last_end = i;
                        }
                        i--;
                    }
                }
                if (last_start >= 0)
                {
                    char symbol[80];
                    c_copy_word(symbol, sizeof(symbol), trimmed + last_start,
                                last_end - last_start);
                    char detail[160];
                    snprintf(detail, sizeof(detail), "Project type - %s", path);
                    count = append_project_symbol(out, count, max, symbol, detail,
                                                  AROMA_CODE_COMPLETION_CLASS);
                }
            }

            if (top_level)
            {
                for (int i = 0; i < content_length; i++)
                {
                    char c = trimmed[i];
                    if (c != 's' && c != 'u' && c != 'e')
                        continue;
                    const char *kind = NULL;
                    int kind_len = 0;
                    if (strncmp(trimmed + i, "struct", 6) == 0)
                    {
                        kind = "struct";
                        kind_len = 6;
                    }
                    else if (strncmp(trimmed + i, "union", 5) == 0)
                    {
                        kind = "union";
                        kind_len = 5;
                    }
                    else if (strncmp(trimmed + i, "enum", 4) == 0)
                    {
                        kind = "enum";
                        kind_len = 4;
                    }
                    if (!kind || (i > 0 && c_is_ident_char(trimmed[i - 1])) ||
                        c_is_ident_char(trimmed[i + kind_len]))
                        continue;
                    int p = i + kind_len;
                    while (p < content_length &&
                           (trimmed[p] == ' ' || trimmed[p] == '\t'))
                        p++;
                    int start = p;
                    while (p < content_length && c_is_ident_char(trimmed[p]))
                        p++;
                    if (p > start)
                    {
                        char symbol[80];
                        c_copy_word(symbol, sizeof(symbol), trimmed + start,
                                    p - start);
                        char detail[160];
                        snprintf(detail, sizeof(detail), "Project %s - %s", kind,
                                 path);
                        count = append_project_symbol(
                            out, count, max, symbol, detail,
                            AROMA_CODE_COMPLETION_CLASS);
                    }
                    i = p > i ? p - 1 : i;
                }
            }

            int paren = -1;
            int semicolon = -1;
            for (int i = 0; i < content_length; i++)
            {
                if (trimmed[i] == '(' && paren < 0)
                    paren = i;
                if (trimmed[i] == ';')
                {
                    semicolon = i;
                    break;
                }
            }
            if (top_level && paren > 0)
            {
                int name_end = paren;
                while (name_end > 0 && (trimmed[name_end - 1] == ' ' ||
                                        trimmed[name_end - 1] == '\t'))
                    name_end--;
                int name_start = name_end;
                while (name_start > 0 && c_is_ident_char(trimmed[name_start - 1]))
                    name_start--;
                if (name_end > name_start && name_end - name_start < 80)
                {
                    char symbol[80];
                    c_copy_word(symbol, sizeof(symbol), trimmed + name_start,
                                name_end - name_start);
                    if (!c_keyword_word(symbol))
                    {
                        char detail[160];
                        snprintf(detail, sizeof(detail), "Project function - %s", path);
                        count = append_project_symbol(out, count, max, symbol, detail,
                                                      AROMA_CODE_COMPLETION_FUNCTION);
                    }
                }
            }
            else if (top_level && semicolon > 0 && trimmed[0] != '{' &&
                     trimmed[0] != '}' &&
                     strncmp(trimmed, "typedef", 7) != 0 &&
                     (!strchr(trimmed, '{') || semicolon <
                      (int)(strchr(trimmed, '{') - trimmed)))
            {
                int decl_end = semicolon;
                for (int i = 0; i < semicolon; i++)
                {
                    if (trimmed[i] == '=')
                    {
                        decl_end = i;
                        break;
                    }
                }
                int name_end = decl_end;
                while (name_end > 0 && (trimmed[name_end - 1] == ' ' ||
                                        trimmed[name_end - 1] == '\t'))
                    name_end--;
                if (name_end > 0 && trimmed[name_end - 1] == ']')
                {
                    int bracket = name_end - 1;
                    while (bracket > 0 && trimmed[bracket] != '[')
                        bracket--;
                    if (trimmed[bracket] == '[')
                    {
                        name_end = bracket;
                        while (name_end > 0 &&
                               (trimmed[name_end - 1] == ' ' ||
                                trimmed[name_end - 1] == '\t'))
                            name_end--;
                    }
                }
                int name_start = name_end;
                while (name_start > 0 && c_is_ident_char(trimmed[name_start - 1]))
                    name_start--;
                if (name_end > name_start && name_end - name_start < 80 &&
                    c_is_ident_start(trimmed[name_start]))
                {
                    char symbol[80];
                    c_copy_word(symbol, sizeof(symbol), trimmed + name_start,
                                name_end - name_start);
                    if (!c_keyword_word(symbol))
                    {
                        char detail[160];
                        snprintf(detail, sizeof(detail), "Project variable - %s", path);
                        count = append_project_symbol(out, count, max, symbol, detail,
                                                      AROMA_CODE_COMPLETION_VARIABLE);
                    }
                }
            }
        }

        if (!preprocessor)
            brace_depth += brace_delta;
        if (*end == '\0')
            break;
        line = end + 1;
    }
    return count;
}

static int append_project_c_completions(AromaCodeCompletion *out, int count,
                                        int max, int current_file)
{
    if (current_file >= 0 && current_file < ide_file_count)
    {
        IdeFile *file = &ide_files[current_file];
        if (c_is_c_like_language(file->language) && file->text)
            count = collect_project_c_symbols_from_text(file->text, file->path,
                                                        out, count, max);
    }
    for (int i = 0; i < ide_file_count && count < max; i++)
    {
        if (i == current_file)
            continue;
        IdeFile *file = &ide_files[i];
        if (!c_is_c_like_language(file->language) || !file->text)
            continue;
        count = collect_project_c_symbols_from_text(file->text, file->path,
                                                    out, count, max);
    }
    return count;
}

typedef struct CBuiltinMember
{
    const char *type;
    const char *name;
    const char *detail;
} CBuiltinMember;

static const CBuiltinMember k_c_builtin_members[] = {
    {"tm", "tm_sec", "Seconds after the minute [0, 60]"},
    {"tm", "tm_min", "Minutes after the hour [0, 59]"},
    {"tm", "tm_hour", "Hours after midnight [0, 23]"},
    {"tm", "tm_mday", "Day of the month [1, 31]"},
    {"tm", "tm_mon", "Months since January [0, 11]"},
    {"tm", "tm_year", "Years since 1900"},
    {"tm", "tm_wday", "Days since Sunday [0, 6]"},
    {"tm", "tm_yday", "Days since January 1 [0, 365]"},
    {"tm", "tm_isdst", "Daylight saving time flag"},
    {"timespec", "tv_sec", "Whole seconds"},
    {"timespec", "tv_nsec", "Nanoseconds [0, 999999999]"},
    {"div_t", "quot", "Quotient"},
    {"div_t", "rem", "Remainder"},
    {"ldiv_t", "quot", "Quotient"},
    {"ldiv_t", "rem", "Remainder"},
    {"lldiv_t", "quot", "Quotient"},
    {"lldiv_t", "rem", "Remainder"},
    {"lconv", "decimal_point", "Decimal point character"},
    {"lconv", "thousands_sep", "Thousands separator"},
    {"lconv", "grouping", "Digit grouping"},
    {"lconv", "int_curr_symbol", "International currency symbol"},
    {"lconv", "currency_symbol", "Local currency symbol"},
    {"lconv", "mon_decimal_point", "Monetary decimal point"},
    {"lconv", "mon_thousands_sep", "Monetary thousands separator"},
    {"lconv", "mon_grouping", "Monetary digit grouping"},
    {"lconv", "positive_sign", "Positive sign"},
    {"lconv", "negative_sign", "Negative sign"},
    {"lconv", "int_frac_digits", "International fractional digits"},
    {"lconv", "frac_digits", "Local fractional digits"},
    {"lconv", "p_cs_precedes", "Currency symbol before positive value"},
    {"lconv", "p_sep_by_space", "Space between currency and positive value"},
    {"lconv", "n_cs_precedes", "Currency symbol before negative value"},
    {"lconv", "n_sep_by_space", "Space between currency and negative value"},
    {"lconv", "p_sign_posn", "Positive sign position"},
    {"lconv", "n_sign_posn", "Negative sign position"},
    {"lconv", "int_p_cs_precedes", "International positive currency placement"},
    {"lconv", "int_p_sep_by_space", "International positive space placement"},
    {"lconv", "int_n_cs_precedes", "International negative currency placement"},
    {"lconv", "int_n_sep_by_space", "International negative space placement"},
    {"lconv", "int_p_sign_posn", "International positive sign position"},
    {"lconv", "int_n_sign_posn", "International negative sign position"},
};

static bool c_find_receiver_type(const char *receiver, char *type, size_t type_size)
{
    if (!receiver || !receiver[0] || !type || type_size == 0)
        return false;
    type[0] = '\0';
    size_t receiver_length = strlen(receiver);

    for (int file_index = 0; file_index < ide_file_count; file_index++)
    {
        IdeFile *file = &ide_files[file_index];
        if (!c_is_c_like_language(file->language) || !file->text)
            continue;
        const char *text = file->text;
        const char *line = text;
        while (*line)
        {
            const char *end = strchr(line, '\n');
            if (!end)
                end = line + strlen(line);
            for (const char *p = line; p + receiver_length <= end; p++)
            {
                if (strncmp(p, receiver, receiver_length) != 0)
                    continue;
                if ((p > line && c_is_ident_char(p[-1])) ||
                    (p + receiver_length < end && c_is_ident_char(p[receiver_length])))
                    continue;
                char after = p[receiver_length];
                if (after && after != ' ' && after != '\t' && after != ';' &&
                    after != ',' && after != '=' && after != '[' && after != ')')
                    continue;

                const char *q = p;
                while (q > line && (q[-1] == ' ' || q[-1] == '\t'))
                    q--;
                while (q > line && q[-1] == '*')
                    q--;
                while (q > line && (q[-1] == ' ' || q[-1] == '\t'))
                    q--;
                const char *name_end = q;
                while (q > line && c_is_ident_char(q[-1]))
                    q--;
                if (q == name_end || name_end - q >= (int)type_size)
                    continue;
                char candidate[80];
                c_copy_word(candidate, sizeof(candidate), q, (int)(name_end - q));
                if (c_keyword_word(candidate))
                    continue;
                snprintf(type, type_size, "%s", candidate);
                return true;
            }
            if (*end == '\0')
                break;
            line = end + 1;
        }
    }
    return false;
}

static int append_c_member_name(AromaCodeCompletion *out, int count, int max,
                                const char *name, const char *detail)
{
    if (!name || !name[0] || count >= max || c_completion_exists(out, count, name))
        return count;
    return c_append_completion(out, count, max, name, detail, name,
                               AROMA_CODE_COMPLETION_PROPERTY);
}

static int parse_c_struct_fields(const char *begin, const char *end,
                                 const char *type, AromaCodeCompletion *out,
                                 int count, int max)
{
    const char *statement = begin;
    int depth = 0;
    for (const char *p = begin; p <= end && count < max; p++)
    {
        char c = p < end ? *p : ';';
        if (c == '{' || c == '(' || c == '[')
            depth++;
        else if (c == '}' || c == ')' || c == ']')
            depth--;
        if (c != ';' || depth != 0)
            continue;

        int length = (int)(p - statement);
        char buffer[512];
        if (length < 0)
            length = 0;
        if (length >= (int)sizeof(buffer))
            length = (int)sizeof(buffer) - 1;
        memcpy(buffer, statement, (size_t)length);
        buffer[length] = '\0';

        const char *fp = strstr(buffer, "(*");
        if (fp)
        {
            fp += 2;
            int name_length = 0;
            while (fp[name_length] &&
                   (isalnum((unsigned char)fp[name_length]) || fp[name_length] == '_'))
                name_length++;
            if (name_length > 0)
            {
                char name[80];
                c_copy_word(name, sizeof(name), fp, name_length);
                char detail[128];
                snprintf(detail, sizeof(detail), "%s member - function pointer", type);
                count = append_c_member_name(out, count, max, name, detail);
            }
        }
        else
        {
            char *segment = buffer;
            while (segment && *segment && count < max)
            {
                char *comma = strchr(segment, ',');
                if (comma)
                    *comma = '\0';
                char *colon = strchr(segment, ':');
                if (colon)
                    *colon = '\0';
                int last_start = -1, last_end = -1;
                for (int i = 0; segment[i]; i++)
                {
                    if (!c_is_ident_start(segment[i]))
                        continue;
                    int start = i++;
                    while (segment[i] && c_is_ident_char(segment[i]))
                        i++;
                    if (!c_keyword_word(segment + start))
                    {
                        last_start = start;
                        last_end = i;
                    }
                    i--;
                }
                if (last_start >= 0)
                {
                    char name[80];
                    c_copy_word(name, sizeof(name), segment + last_start,
                                last_end - last_start);
                    char detail[128];
                    snprintf(detail, sizeof(detail), "%s member", type);
                    count = append_c_member_name(out, count, max, name, detail);
                }
                segment = comma ? comma + 1 : NULL;
            }
        }
        statement = p + 1;
    }
    return count;
}

static bool c_decl_matches_type(const char *after_keyword, const char *type,
                                const char **brace_out, const char **close_out)
{
    if (!after_keyword || !type || !type[0])
        return false;
    const char *p = after_keyword;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    const char *tag = p;
    int tag_length = 0;
    while (c_is_ident_char(tag[tag_length]))
        tag_length++;
    const char *body = tag + tag_length;
    while (*body == ' ' || *body == '\t' || *body == '\n' || *body == '\r')
        body++;
    if (*body != '{' && strncmp(body, "__attribute__", 13) != 0 &&
        strncmp(body, "alignas", 7) != 0)
        return false;
    const char *brace = strchr(body, '{');
    if (!brace)
        return false;
    const char *close = brace + 1;
    int depth = 1;
    while (*close && depth > 0)
    {
        if (*close == '{')
            depth++;
        else if (*close == '}')
            depth--;
        close++;
    }
    if (depth != 0)
        return false;

    bool match = tag_length > 0 && (int)strlen(type) == tag_length &&
                 strncmp(tag, type, (size_t)tag_length) == 0;
    if (!match)
    {
        /* `typedef struct { ... } Alias;` and `typedef struct Tag { ... } Alias;`
           expose the fields through the typedef alias as well as the tag. */
        const char *tail = close;
        while (*tail && *tail != ';')
        {
            if (c_is_ident_start(*tail))
            {
                const char *start = tail++;
                while (c_is_ident_char(*tail))
                    tail++;
                int length = (int)(tail - start);
                if ((int)strlen(type) == length &&
                    strncmp(start, type, (size_t)length) == 0)
                {
                    match = true;
                    break;
                }
            }
            else
            {
                tail++;
            }
        }
    }
    if (!match)
        return false;
    if (brace_out)
        *brace_out = brace;
    if (close_out)
        *close_out = close - 1;
    return true;
}

static int append_c_member_completions(AromaCodeCompletion *out, int count,
                                       int max, const char *receiver)
{
    char type[80];
    if (!c_find_receiver_type(receiver, type, sizeof(type)))
        return count;

    for (int i = 0; i < COMPLETION_COUNT(k_c_builtin_members) && count < max; i++)
    {
        if (strcmp(k_c_builtin_members[i].type, type) != 0)
            continue;
        count = append_c_member_name(out, count, max, k_c_builtin_members[i].name,
                                     k_c_builtin_members[i].detail);
    }

    for (int file_index = 0; file_index < ide_file_count && count < max; file_index++)
    {
        IdeFile *file = &ide_files[file_index];
        if (!c_is_c_like_language(file->language) || !file->text)
            continue;
        const char *text = file->text;
        for (const char *p = text; *p && count < max; p++)
        {
            int keyword_length = 0;
            if (strncmp(p, "struct", 6) == 0)
                keyword_length = 6;
            else if (strncmp(p, "union", 5) == 0)
                keyword_length = 5;
            if (keyword_length == 0)
                continue;
            if (p > text && c_is_ident_char(p[-1]))
                continue;
            if (c_is_ident_char(p[keyword_length]))
                continue;

            const char *brace = NULL;
            const char *close = NULL;
            if (!c_decl_matches_type(p + keyword_length, type, &brace, &close))
                continue;
            count = parse_c_struct_fields(brace + 1, close, type, out, count, max);
            p = close;
        }
    }
    return count;
}

static const CompletionSpec k_css_completions[] = {
    {"display", "display: grid", "display: grid;", AROMA_CODE_COMPLETION_PROPERTY},
    {"grid-template-rows", "grid-template-rows: 36px 1fr 28px;",
     "grid-template-rows: 36px 1fr 28px;", AROMA_CODE_COMPLETION_PROPERTY},
    {"align-items", "align-items: center;", "align-items: center;",
     AROMA_CODE_COMPLETION_PROPERTY},
    {"justify-content", "justify-content: space-between;", "justify-content: space-between;",
     AROMA_CODE_COMPLETION_PROPERTY},
    {"background", "background: var(--bg);", "background: var(--bg);",
     AROMA_CODE_COMPLETION_PROPERTY},
    {"border-radius", "border-radius: 999px;", "border-radius: 999px;",
     AROMA_CODE_COMPLETION_PROPERTY},
    {"font-family", "font-family: \"Inter\", sans-serif;", "font-family: \"Inter\", sans-serif;",
     AROMA_CODE_COMPLETION_PROPERTY},
    {"padding", "padding: 12px 22px;", "padding: 12px 22px;", AROMA_CODE_COMPLETION_PROPERTY},
    {"--accent", "Design token - accent color", "--accent: #f97316;",
     AROMA_CODE_COMPLETION_VARIABLE},
    {"--surface", "Design token - surface color", "--surface: #151922;",
     AROMA_CODE_COMPLETION_VARIABLE},
};

static const CompletionSpec k_json_completions[] = {
    {"name", "Package name", "\"name\": \"vibe-studio\"", AROMA_CODE_COMPLETION_PROPERTY},
    {"version", "Semantic version", "\"version\": \"0.1.0\"", AROMA_CODE_COMPLETION_PROPERTY},
    {"private", "Keep the package private", "\"private\": true", AROMA_CODE_COMPLETION_PROPERTY},
    {"scripts", "Command scripts", "\"scripts\": {}", AROMA_CODE_COMPLETION_PROPERTY},
    {"dependencies", "Runtime dependencies", "\"dependencies\": {}", AROMA_CODE_COMPLETION_PROPERTY},
    {"devDependencies", "Development dependencies", "\"devDependencies\": {}",
     AROMA_CODE_COMPLETION_PROPERTY},
    {"compilerOptions", "TypeScript compiler flags", "\"compilerOptions\": {}",
     AROMA_CODE_COMPLETION_PROPERTY},
    {"include", "Included source globs", "\"include\": [\"src\"]",
     AROMA_CODE_COMPLETION_PROPERTY},
    {"strict", "Enable strict type checking", "\"strict\": true", AROMA_CODE_COMPLETION_PROPERTY},
};

static const CompletionSpec k_markdown_completions[] = {
    {"Heading", "# Heading", "# Heading", AROMA_CODE_COMPLETION_SNIPPET},
    {"Section", "## Section", "## Section", AROMA_CODE_COMPLETION_SNIPPET},
    {"List item", "- List item", "- List item", AROMA_CODE_COMPLETION_SNIPPET},
    {"Code fence", "```bash", "```bash\n\n```", AROMA_CODE_COMPLETION_SNIPPET},
    {"Link", "[label](url)", "[label](url)", AROMA_CODE_COMPLETION_SNIPPET},
};

typedef struct MemberSpec
{
    const char *receiver;
    const char *name;
    const char *description;
    const char *insert_text;
    AromaCodeCompletionKind kind;
} MemberSpec;

static const MemberSpec k_members[] = {
    {"workspace", "name", "string - display name", "name", AROMA_CODE_COMPLETION_PROPERTY},
    {"workspace", "branch", "string - git branch", "branch", AROMA_CODE_COMPLETION_PROPERTY},
    {"workspace", "files", "number - indexed files", "files", AROMA_CODE_COMPLETION_PROPERTY},
    {"workspace", "dirty", "boolean - unsaved changes", "dirty", AROMA_CODE_COMPLETION_PROPERTY},
    {"client", "on", "method - subscribe to notifications", "on", AROMA_CODE_COMPLETION_METHOD},
    {"client", "notify", "method - send a notification", "notify", AROMA_CODE_COMPLETION_METHOD},
    {"client", "request", "method - send a request", "request", AROMA_CODE_COMPLETION_METHOD},
    {"client", "dispose", "method - close the connection", "dispose", AROMA_CODE_COMPLETION_METHOD},
    {"props", "open", "boolean - palette visibility", "open", AROMA_CODE_COMPLETION_PROPERTY},
    {"props", "query", "string - current search query", "query", AROMA_CODE_COMPLETION_PROPERTY},
    {"props", "onQueryChange", "method - update the query", "onQueryChange",
     AROMA_CODE_COMPLETION_METHOD},
    {"props", "onClose", "method - dismiss the palette", "onClose", AROMA_CODE_COMPLETION_METHOD},
    {"theme", "colors", "object - color scale", "colors", AROMA_CODE_COMPLETION_PROPERTY},
    {"theme", "spacing", "object - spacing scale", "spacing", AROMA_CODE_COMPLETION_PROPERTY},
    {"theme", "radius", "object - corner radii", "radius", AROMA_CODE_COMPLETION_PROPERTY},
    {"theme", "fonts", "object - type scale", "fonts", AROMA_CODE_COMPLETION_PROPERTY},
    {"command", "id", "string - command identifier", "id", AROMA_CODE_COMPLETION_PROPERTY},
    {"command", "label", "string - display label", "label", AROMA_CODE_COMPLETION_PROPERTY},
    {"command", "shortcut", "string - keyboard shortcut", "shortcut", AROMA_CODE_COMPLETION_PROPERTY},
    {"socket", "send", "method - send a frame", "send", AROMA_CODE_COMPLETION_METHOD},
    {"socket", "close", "method - close the socket", "close", AROMA_CODE_COMPLETION_METHOD},
    {"socket", "addEventListener", "method - subscribe to socket events", "addEventListener",
     AROMA_CODE_COMPLETION_METHOD},
    {"console", "log", "method - debug log", "log", AROMA_CODE_COMPLETION_METHOD},
    {"console", "info", "method - informational log", "info", AROMA_CODE_COMPLETION_METHOD},
    {"console", "warn", "method - warning log", "warn", AROMA_CODE_COMPLETION_METHOD},
    {"console", "error", "method - error log", "error", AROMA_CODE_COMPLETION_METHOD},
    {"state", "workspace", "method - read managed state", "workspace", AROMA_CODE_COMPLETION_METHOD},
    {"params", "jsonrpc", "string - protocol version", "jsonrpc", AROMA_CODE_COMPLETION_PROPERTY},
    {"params", "id", "number - request id", "id", AROMA_CODE_COMPLETION_PROPERTY},
    {"params", "method", "string - LSP method", "method", AROMA_CODE_COMPLETION_PROPERTY},
};

static void copy_completion(AromaCodeCompletion *out, const CompletionSpec *spec)
{
    snprintf(out->label, sizeof(out->label), "%s", spec->label);
    snprintf(out->detail, sizeof(out->detail), "%s", spec->detail ? spec->detail : "");
    snprintf(out->insert_text, sizeof(out->insert_text), "%s",
             spec->insert && spec->insert[0] ? spec->insert : spec->label);
    out->kind = spec->kind;
}

static int append_specs(AromaCodeCompletion *out, int count, int max,
                        const CompletionSpec *specs, int spec_count)
{
    for (int i = 0; i < spec_count && count < max; i++)
    {
        if (c_completion_exists(out, count, specs[i].label))
            continue;
        copy_completion(&out[count++], &specs[i]);
    }
    return count;
}

static int append_member_specs(AromaCodeCompletion *out, int count, int max,
                               const char *receiver)
{
    for (int i = 0; i < (int)(sizeof(k_members) / sizeof(k_members[0])) && count < max; i++)
    {
        if (strcmp(k_members[i].receiver, receiver) != 0)
            continue;
        AromaCodeCompletion *completion = &out[count++];
        snprintf(completion->label, sizeof(completion->label), "%s",
                 k_members[i].name);
        snprintf(completion->detail, sizeof(completion->detail), "%s",
                 k_members[i].description);
        snprintf(completion->insert_text, sizeof(completion->insert_text), "%s",
                 k_members[i].insert_text);
        completion->kind = k_members[i].kind;
    }
    return count;
}

static const CompletionSpec *language_completions(AromaCodeLanguage language, int *count)
{
    switch (language)
    {
    case AROMA_CODE_LANGUAGE_TSX:
    case AROMA_CODE_LANGUAGE_TYPESCRIPT:
    case AROMA_CODE_LANGUAGE_JAVASCRIPT:
        *count = COMPLETION_COUNT(k_ts_completions);
        return k_ts_completions;
    case AROMA_CODE_LANGUAGE_RUST:
        *count = COMPLETION_COUNT(k_rust_completions);
        return k_rust_completions;
    case AROMA_CODE_LANGUAGE_YAML:
        *count = COMPLETION_COUNT(k_yaml_completions);
        return k_yaml_completions;
    case AROMA_CODE_LANGUAGE_TOML:
        *count = COMPLETION_COUNT(k_toml_completions);
        return k_toml_completions;
    case AROMA_CODE_LANGUAGE_DOCKERFILE:
        *count = COMPLETION_COUNT(k_docker_completions);
        return k_docker_completions;
    case AROMA_CODE_LANGUAGE_AROMA:
        *count = COMPLETION_COUNT(k_aroma_completions);
        return k_aroma_completions;
    case AROMA_CODE_LANGUAGE_C:
    case AROMA_CODE_LANGUAGE_CPP:
        *count = COMPLETION_COUNT(k_c_completions);
        return k_c_completions;
    case AROMA_CODE_LANGUAGE_CSS:
        *count = COMPLETION_COUNT(k_css_completions);
        return k_css_completions;
    case AROMA_CODE_LANGUAGE_JSON:
        *count = COMPLETION_COUNT(k_json_completions);
        return k_json_completions;
    case AROMA_CODE_LANGUAGE_MARKDOWN:
        *count = COMPLETION_COUNT(k_markdown_completions);
        return k_markdown_completions;
    default:
        *count = 0;
        return NULL;
    }
}

static bool receiver_before_cursor(const char *line, int length, int cursor,
                                   char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return false;
    out[0] = '\0';
    if (!line || cursor <= 0)
        return false;
    int i = cursor;
    if (i > length)
        i = length;
    while (i > 0 && isspace((unsigned char)line[i - 1]))
        i--;
    int end = -1;
    if (i > 0 && line[i - 1] == '.')
        end = i - 1;
    else if (i > 1 && line[i - 1] == '>' && line[i - 2] == '-')
        end = i - 2;
    else if (i > 1 && line[i - 1] == ':' && line[i - 2] == ':')
        end = i - 2;
    if (end < 0)
        return false;

    while (end > 0 && isspace((unsigned char)line[end - 1]))
        end--;
    int start = end;
    while (start > 0 && is_word_char(line[start - 1]))
        start--;
    size_t size = (size_t)(end - start);
    if (size == 0 || size >= out_size)
        return false;
    memcpy(out, line + start, size);
    out[size] = '\0';
    return true;
}

int ide_lsp_complete(int file_index, int line, int column,
                     AromaCodeCompletion *out, int max)
{
    if (!out || max <= 0 || file_index < 0 || file_index >= ide_file_count)
        return 0;
    const char *text = file_text(file_index);
    int length = 0;
    const char *p = line_pointer(text, line, &length);
    if (!p)
        return 0;
    int cursor = column - 1;
    if (cursor < 0)
        cursor = 0;
    if (cursor > length)
        cursor = length;

    int prefix_start = cursor;
    while (prefix_start > 0 && is_word_char(p[prefix_start - 1]))
        prefix_start--;

    char receiver[80];
    bool member_access =
        receiver_before_cursor(p, length, prefix_start, receiver, sizeof(receiver));
    AromaCodeLanguage language = ide_files[file_index].language;
    bool c_like = c_is_c_like_language(language);
    int count = 0;

    if (member_access)
    {
        if (c_like)
        {
            count = append_c_member_completions(out, count, max, receiver);
            if (count > 0)
                return count;
            /* After foo-> or foo., C keywords are not useful. */
            return 0;
        }
        if (receiver[0])
        {
            count = append_member_specs(out, count, max, receiver);
            if (count > 0)
                return count;
        }
    }

    if (c_like)
    {
        char partial[256];
        char closer = '\0';
        CCompletionContext context =
            c_completion_context(p, length, cursor, partial, sizeof(partial), &closer);
        if (context == C_COMPLETION_DIRECTIVE)
            return append_c_directives(out, count, max);
        if (context == C_COMPLETION_INCLUDE)
            return append_c_headers(out, count, max, language, closer);
        count = append_project_c_completions(out, count, max, file_index);
    }

    int spec_count = 0;
    const CompletionSpec *specs = language_completions(language, &spec_count);
    if (specs)
        count = append_specs(out, count, max, specs, spec_count);
    return count;
}

typedef struct HoverEntry
{
    const char *word;
    const char *doc;
} HoverEntry;

static const HoverEntry k_hover_entries[] = {
    {"useState",
     "useState<T>(initialState: T | (() => T))\n\nReact hook that returns a stateful value and a setter. "
     "Updating the value schedules a re-render."},
    {"useEffect",
     "useEffect(effect: () => void | Destructor, deps?: DependencyList)\n\nRuns a side effect after the "
     "component is painted. Return a cleanup function to dispose subscriptions."},
    {"useMemo",
     "useMemo<T>(factory: () => T, deps: DependencyList): T\n\nCaches the result of a calculation between "
     "renders while dependencies stay stable."},
    {"useRef",
     "useRef<T>(initialValue: T): MutableRefObject<T>\n\nReturns a mutable ref object whose .current "
     "property persists for the full lifetime of the component."},
    {"CommandPalette",
     "function CommandPalette(props: CommandPaletteProps)\n\nModal command launcher for the vibe-studio "
     "workspace. Filters project commands and closes on backdrop click."},
    {"connectLanguageServer",
     "function connectLanguageServer(options: ServerOptions)\n\nOpens a WebSocket transport to the local "
     "language server and exposes on/notify/dispose methods."},
    {"Workspace",
     "type Workspace = {\n  name: string\n  branch: string\n  files: number\n  dirty?: boolean\n}\n\n"
     "Workspace metadata shared between the React surface and the Tauri bridge."},
    {"AppState",
     "struct AppState\n\nTauri managed state. Guards a Workspace behind a Mutex and is injected into commands."},
    {"workspace_info",
     "#[tauri::command]\nfn workspace_info(state: State<AppState>) -> Workspace\n\nReturns a snapshot of the "
     "current workspace metadata."},
    {"set_branch",
     "#[tauri::command]\nfn set_branch(state: State<AppState>, branch: String)\n\nUpdates the active git branch "
     "and publishes the change to the frontend."},
    {"theme",
     "theme: DesignTokens\n\nShared design tokens imported from src/styles/theme.css. Hover and go-to-definition "
     "cross the TypeScript/CSS boundary."},
    {"initialWorkspace", "const initialWorkspace: Workspace\n\nThe seed workspace rendered before native state is loaded."},
    {"workspace", "const workspace: Workspace\n\nCurrent workspace metadata. Clicking the title marks it dirty."},
    {"status", "const status: string\n\nDerived footer label: \"Unsaved changes\" or \"All changes saved\"."},
    {"client", "const client: LanguageClient\n\nWebSocket language client created inside useEffect."},
    {"commands", "const commands: Command[]\n\nStatic command registry filtered by the palette query."},
    {"aroma_ui_init",
     "void aroma_ui_init(void)\n\nBootstraps the AromaUI runtime. Call once before creating windows or fonts."},
    {"aroma_ui_create_window",
     "AromaWindow *aroma_ui_create_window(const char *title, int width, int height)\n\nCreates a native window and returns its handle. Check for NULL on unsupported platforms."},
    {"aroma_ui_render",
     "void aroma_ui_render(AromaWindow *window)\n\nDraws the current scene graph and presents the frame."},
    {"aroma_ui_process_events",
     "void aroma_ui_process_events(void)\n\nPumps the platform event queue. Call inside the main loop before rendering."},
    {"aroma_label_set_text",
     "void aroma_label_set_text(AromaNode *node, const char *text)\n\nUpdates a Label node. The node takes a copy of the string."},
    {"IncenseLoadFileEx",
     "AromaWindow *IncenseLoadFileEx(const char *path, AromaFont *font, AromaFont *icon_font, IncenseRegistry **registry)\n\nParses an `.aroma` document, builds its widget tree and returns the window."},
    {"IncenseFindWidget",
     "AromaNode *IncenseFindWidget(IncenseRegistry *registry, const char *id)\n\nLooks up a widget by its markup `id` attribute. Returns NULL when the id is unknown."},
    {"IncenseRegisterCallback",
     "void IncenseRegisterCallback(const char *name, IncenseCallbackKind kind, void *callback, void *userdata)\n\nBinds a markup callback such as `on_click` to a C function."},
    {"aroma_canvas_create",
     "AromaNode *aroma_canvas_create(AromaNode *parent, int x, int y, int width, int height)\n\nCreates a Canvas node for immediate-mode drawing ops with `aroma_canvas_fill_rect` and friends."},
    {"aroma_font_create_from_memory",
     "AromaFont *aroma_font_create_from_memory(const unsigned char *data, size_t size, int pixel_size)\n\nLoads an embedded TrueType font. Destroy it with `aroma_font_destroy`."},
    {"AROMA_NODE_AS",
     "AROMA_NODE_AS(node, Type)\n\nSafely casts an AromaNode pointer when the node's widget type matches `Type`."},
    {"notes_add",
     "int notes_add(const char *title, const char *body)\n\nAdds a note to the fixed-size model. Returns the new index, or -1 when the inbox is full."},
    {"notes_count",
     "int notes_count(void)\n\nReturns the number of notes currently held by the sample model."},
    {"theme_surface_for_index",
     "uint32_t theme_surface_for_index(int index)\n\nAlternates the card surface colour for list rows in the sample application."},
    {"FROM", "FROM <image> [AS <stage>]\n\nDockerfile instruction that sets the base image for the following build stage."},
    {"WORKDIR", "WORKDIR /path\n\nSets the working directory for RUN, CMD and ENTRYPOINT instructions."},
    {"RUN", "RUN <command>\n\nExecutes a shell command inside a new layer during the image build."},
    {"COPY", "COPY <src> <dest>\n\nCopies files from the build context into the image."},
    {"EXPOSE", "EXPOSE <port>\n\nDocuments the ports the container listens on at runtime."},
    {"CMD", "CMD [\"executable\"]\n\nProvides the default command for the container."},
    {"services", "services:\n\nTop-level Compose map. Each child key defines one container service."},
    {"version", "version: \"3.9\"\n\nLegacy Compose schema version. Current Compose ignores this field."},
    {"image", "image: <reference>\n\nContainer image used by the service."},
    {"ports", "ports:\n  - \"host:container\"\n\nPublishes container ports on the host network."},
    {"depends_on", "depends_on:\n  - service\n\nControls service startup order."},
    {"environment", "environment:\n  KEY: value\n\nEnvironment variables passed to the container."},
    {"volume", "volumes:\n  - ./src:/app/src\n\nBind mounts or named volumes attached to the service."},
    {"Window", "Window { width height title }\n\nIncense root node. Optional: a file without a Window is treated as a component and the designer canvas fits its content."},
    {"Container", "Container { x y width height }\n\nPositioned layout container. Add layout: flex for automatic child placement."},
    {"Card", "Card { type color border_color corner_radius }\n\nSurface primitive used for panels, cards and dividers."},
    {"Canvas", "Canvas { x y width height }\n\nCustom drawing surface backed by an AromaCanvas op list."},
    {"corner_radius", "corner_radius: <px>\n\nRounds the corners of a Card. Values larger than half the height are clamped."},
    {"const", "const name = value\n\nDeclares a block-scoped binding that cannot be reassigned."},
    {"function", "function name(params) { ... }\n\nDeclares a function in the current module scope."},
    {"interface", "interface Name { ... }\n\nDescribes the shape of an object at compile time."},
    {"type", "type Name = ...\n\nCreates an alias for a TypeScript type."},
    {"fn", "fn name(args) -> ReturnType\n\nDeclares a Rust function inside the current module or impl block."},
    {"struct", "struct Name { ... }\n\nDeclares a Rust product type with named fields."},
    {"impl", "impl Type { ... }\n\nAdds inherent methods or trait implementations to a type."},
};

static void copy_hover(char *out, size_t out_size, const char *text)
{
    if (out_size == 0)
        return;
    snprintf(out, out_size, "%s", text ? text : "");
}

static bool line_declares_word(const char *line, const char *word, int *column,
                               char *kind, size_t kind_size)
{
    if (!line || !word || !word[0])
        return false;
    const char *p = line;
    while (*p == ' ' || *p == '\t')
        p++;
    if (strncmp(p, "export ", 7) == 0)
        p += 7;
    if (strncmp(p, "default ", 8) == 0)
        p += 8;
    if (strncmp(p, "async ", 6) == 0)
        p += 6;
    if (strncmp(p, "pub ", 4) == 0)
        p += 4;

    static const struct
    {
        const char *prefix;
        const char *kind;
    } patterns[] = {
        {"function ", "function"},
        {"interface ", "interface"},
        {"type ", "type"},
        {"class ", "class"},
        {"const [", "state"},
        {"const ", "constant"},
        {"let ", "variable"},
        {"struct ", "struct"},
        {"enum ", "enum"},
        {"fn ", "function"},
        {"impl ", "impl"},
    };

    for (int i = 0; i < (int)(sizeof(patterns) / sizeof(patterns[0])); i++)
    {
        size_t prefix_len = strlen(patterns[i].prefix);
        if (strncmp(p, patterns[i].prefix, prefix_len) != 0)
            continue;
        const char *name = p + prefix_len;
        if (patterns[i].prefix[prefix_len - 1] == '[')
        {
            while (*name == ' ')
                name++;
        }
        if (strncmp(name, word, strlen(word)) != 0)
            continue;
        char next = name[strlen(word)];
        if (next && is_word_char(next))
            continue;
        if (column)
            *column = (int)(name - line) + 1;
        if (kind && kind_size)
            snprintf(kind, kind_size, "%s", patterns[i].kind);
        return true;
    }

    if (strncmp(p, "#define ", 8) == 0)
    {
        const char *name = p + 8;
        while (*name == ' ' || *name == '\t')
            name++;
        if (strncmp(name, word, strlen(word)) == 0 &&
            !is_word_char(name[strlen(word)]))
        {
            if (column)
                *column = (int)(name - line) + 1;
            if (kind && kind_size)
                snprintf(kind, kind_size, "%s", "macro");
            return true;
        }
    }

    if (strncmp(p, "typedef struct ", 15) == 0 ||
        strncmp(p, "typedef enum ", 13) == 0)
    {
        const char *name = strstr(p, word);
        if (name && !is_word_char(name[strlen(word)]))
        {
            if (column)
                *column = (int)(name - line) + 1;
            if (kind && kind_size)
                snprintf(kind, kind_size, "%s", "type");
            return true;
        }
    }

    if (*p != '#' && *p != '{' && *p != '}' && strstr(p, "return") != p &&
        strstr(p, "if") != p && strstr(p, "for") != p && strstr(p, "while") != p &&
        strstr(p, "switch") != p)
    {
        const char *found = strstr(p, word);
        if (found && found > p && (found[-1] == ' ' || found[-1] == '*' ||
                                   found[-1] == '\t'))
        {
            const char *after = found + strlen(word);
            const char *equals = strchr(p, '=');
            bool looks_like_function = *after == '(' &&
                                       (equals == NULL || equals > found);
            bool looks_like_variable = (*after == ';' || *after == '[' ||
                                        (*after == '=' && (!equals || equals == after)));
            if (looks_like_function || looks_like_variable)
            {
                if (column)
                    *column = (int)(found - line) + 1;
                if (kind && kind_size)
                    snprintf(kind, kind_size, "%s",
                             looks_like_function ? "function" : "variable");
                return true;
            }
        }
    }

                                                                       
    if (*p == '[' && strstr(p, word))
    {
        const char *found = strstr(p, word);
        if (column)
            *column = (int)(found - line) + 1;
        if (kind && kind_size)
            snprintf(kind, kind_size, "%s", "section");
        return true;
    }
    if (*p == '"')
    {
        char needle[128];
        snprintf(needle, sizeof(needle), "\"%s\"", word);
        const char *found = strstr(p, needle);
        if (found)
        {
            if (column)
                *column = (int)(found - line) + 2;
            if (kind && kind_size)
                snprintf(kind, kind_size, "%s", "property");
            return true;
        }
    }
    if (strstr(p, word))
    {
        const char *found = strstr(p, word);
        const char *colon = strchr(found, ':');
        if (colon)
        {
            if (column)
                *column = (int)(found - line) + 1;
            if (kind && kind_size)
                snprintf(kind, kind_size, "%s", "key");
            return true;
        }
        if (strchr(p, '{') && p == found && isupper((unsigned char)word[0]))
        {
            if (column)
                *column = (int)(found - line) + 1;
            if (kind && kind_size)
                snprintf(kind, kind_size, "%s", "widget");
            return true;
        }
    }
    return false;
}

static bool find_declaration(const char *word, int prefer_file,
                             int *out_file, int *out_line, int *out_column)
{
    if (!word || !word[0])
        return false;
    for (int pass = 0; pass < 2; pass++)
    {
        for (int file_index = 0; file_index < ide_file_count; file_index++)
        {
            if (pass == 0 && file_index != prefer_file)
                continue;
            if (pass == 1 && file_index == prefer_file)
                continue;
            const char *text = file_text(file_index);
                                                                              
            if (!strstr(text, word))
                continue;
            size_t word_len = strlen(word);
            int line_no = 1;
            const char *p = text;
            while (*p)
            {
                const char *end = strchr(p, '\n');
                if (!end)
                    end = p + strlen(p);
                if (!find_in_range(p, end, word, word_len))
                {
                    p = *end ? end + 1 : end;
                    line_no++;
                    continue;
                }
                int length = (int)(end - p);
                char line[512];
                int copy = length < (int)sizeof(line) - 1 ? length : (int)sizeof(line) - 1;
                memcpy(line, p, (size_t)copy);
                line[copy] = '\0';
                int column = 0;
                char kind[32];
                if (line_declares_word(line, word, &column, kind, sizeof(kind)))
                {
                    if (out_file)
                        *out_file = file_index;
                    if (out_line)
                        *out_line = line_no;
                    if (out_column)
                        *out_column = column;
                    return true;
                }
                p = *end ? end + 1 : end;
                line_no++;
            }
        }
    }
    return false;
}

bool ide_lsp_hover(int file_index, int line, int column,
                   char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return false;
    out[0] = '\0';
    char word[96];
    if (!word_at(file_text(file_index), line, column, word, sizeof(word)))
        return false;

    if (c_is_c_like_language(ide_files[file_index].language))
    {
        for (int i = 0; i < COMPLETION_COUNT(k_c_completions); i++)
        {
            if (strcmp(k_c_completions[i].label, word) == 0)
            {
                copy_hover(out, out_size,
                           k_c_completions[i].doc ? k_c_completions[i].doc
                                                  : k_c_completions[i].detail);
                return true;
            }
        }
        for (int i = 0; i < COMPLETION_COUNT(k_c_directives); i++)
        {
            if (strcmp(k_c_directives[i].label, word) == 0)
            {
                copy_hover(out, out_size,
                           k_c_directives[i].doc ? k_c_directives[i].doc
                                                 : k_c_directives[i].detail);
                return true;
            }
        }
    }

    for (int i = 0; i < (int)(sizeof(k_hover_entries) / sizeof(k_hover_entries[0])); i++)
    {
        if (strcmp(k_hover_entries[i].word, word) == 0)
        {
            copy_hover(out, out_size, k_hover_entries[i].doc);
            return true;
        }
    }

    int target_file = -1, target_line = 0, target_column = 0;
    if (find_declaration(word, file_index, &target_file, &target_line, &target_column))
    {
        char buffer[512];
        snprintf(buffer, sizeof(buffer),
                 "%s\n\nProject symbol in %s:%d\n\nHover, completion and definition are provided "
                 "by the embedded language server.",
                 word, ide_files[target_file].path, target_line);
        copy_hover(out, out_size, buffer);
        return true;
    }

    return false;
}

bool ide_lsp_definition(int file_index, int line, int column,
                        int *target_file, int *target_line, int *target_column,
                        char *symbol, size_t symbol_size)
{
    char word[96];
    if (!word_at(file_text(file_index), line, column, word, sizeof(word)))
        return false;
    if (symbol && symbol_size)
        snprintf(symbol, symbol_size, "%s", word);

    if (strcmp(word, "theme") == 0)
    {
        if (target_file)
            *target_file = 2;
        if (target_line)
            *target_line = 1;
        if (target_column)
            *target_column = 1;
        return true;
    }

    if (find_declaration(word, file_index, target_file, target_line, target_column))
        return true;

                                                                   
    if (strcmp(word, "useState") == 0 || strcmp(word, "useEffect") == 0 ||
        strcmp(word, "useMemo") == 0 || strcmp(word, "useRef") == 0)
    {
        if (target_file)
            *target_file = 0;
        if (target_line)
            *target_line = 1;
        if (target_column)
            *target_column = 10;
        return true;
    }
    return false;
}

static void append_symbol(AromaCodeSymbol *out, int *count, int max,
                          const char *name, const char *detail, int line, int column)
{
    if (!out || *count >= max)
        return;
    AromaCodeSymbol *symbol = &out[(*count)++];
    snprintf(symbol->name, sizeof(symbol->name), "%s", name ? name : "");
    snprintf(symbol->detail, sizeof(symbol->detail), "%s", detail ? detail : "");
    symbol->line = line;
    symbol->column = column > 0 ? column : 1;
}

static const char *after_prefix(const char *trimmed, const char *prefix)
{
    size_t length = strlen(prefix);
    if (strncmp(trimmed, prefix, length) == 0)
        return trimmed + length;
    return NULL;
}

int ide_lsp_outline(int file_index, AromaCodeSymbol *out, int max)
{
    if (file_index < 0 || file_index >= ide_file_count || !out || max <= 0)
        return 0;
    IdeFile *file = &ide_files[file_index];
    const char *text = file_text(file_index);
    int count = 0;
    int line_no = 1;
    const char *p = text;
    bool c_in_enum = false;

    while (*p && count < max)
    {
        const char *end = p;
        while (*end && *end != '\n')
            end++;
        int length = (int)(end - p);
        char raw[512];
        int copy = length < (int)sizeof(raw) - 1 ? length : (int)sizeof(raw) - 1;
        memcpy(raw, p, (size_t)copy);
        raw[copy] = '\0';

        int indent = 0;
        while (raw[indent] == ' ')
            indent++;
        const char *trimmed = raw + indent;

        if (file->language == AROMA_CODE_LANGUAGE_TSX ||
            file->language == AROMA_CODE_LANGUAGE_TYPESCRIPT ||
            file->language == AROMA_CODE_LANGUAGE_JAVASCRIPT)
        {
            if (indent <= 2)
            {
                const char *decl = trimmed;
                if (strncmp(decl, "export ", 7) == 0)
                    decl += 7;
                if (strncmp(decl, "default ", 8) == 0)
                    decl += 8;
                const char *name = NULL;
                const char *detail = NULL;
                if ((name = after_prefix(decl, "function ")) != NULL)
                    detail = "function";
                else if ((name = after_prefix(decl, "interface ")) != NULL)
                    detail = "interface";
                else if ((name = after_prefix(decl, "type ")) != NULL)
                    detail = "type";
                else if ((name = after_prefix(decl, "const ")) != NULL)
                {
                    if (*name == '[' || *name == '{')
                        name = NULL;
                    else if (strstr(decl, "useState") || strstr(decl, "useEffect") ||
                             strstr(decl, "useMemo") || strstr(decl, "useRef"))
                        name = NULL;
                    else
                        detail = strchr(decl, '=') && strstr(decl, "=>") ? "function" : "const";
                }
                if (name && detail)
                {
                    char symbol_name[80];
                    int i = 0;
                    while (name[i] && (isalnum((unsigned char)name[i]) || name[i] == '_') &&
                           i < (int)sizeof(symbol_name) - 1)
                    {
                        symbol_name[i] = name[i];
                        i++;
                    }
                    symbol_name[i] = '\0';
                    if (isupper((unsigned char)symbol_name[0]) && strcmp(detail, "function") == 0)
                        detail = "component";
                    append_symbol(out, &count, max, symbol_name, detail, line_no,
                                  (int)(name - raw) + 1);
                }
            }
        }
        else if (file->language == AROMA_CODE_LANGUAGE_RUST)
        {
            const char *decl = trimmed;
            if (strncmp(decl, "pub ", 4) == 0)
                decl += 4;
            if (strncmp(decl, "async ", 6) == 0)
                decl += 6;
            const char *name = NULL;
            const char *detail = NULL;
            if ((name = after_prefix(decl, "fn ")) != NULL)
                detail = "function";
            else if ((name = after_prefix(decl, "struct ")) != NULL)
                detail = "struct";
            else if ((name = after_prefix(decl, "enum ")) != NULL)
                detail = "enum";
            else if ((name = after_prefix(decl, "impl ")) != NULL)
                detail = "impl";
            if (name && detail)
            {
                char symbol_name[80];
                int i = 0;
                while (name[i] && (isalnum((unsigned char)name[i]) || name[i] == '_') &&
                       i < (int)sizeof(symbol_name) - 1)
                {
                    symbol_name[i] = name[i];
                    i++;
                }
                symbol_name[i] = '\0';
                append_symbol(out, &count, max, symbol_name, detail, line_no,
                              (int)(name - raw) + 1);
            }
        }
        else if (file->language == AROMA_CODE_LANGUAGE_YAML)
        {
            if (indent == 0 || indent == 2)
            {
                const char *colon = strchr(trimmed, ':');
                if (colon && colon != trimmed)
                {
                    char key[80];
                    int size = (int)(colon - trimmed);
                    if (size >= (int)sizeof(key))
                        size = (int)sizeof(key) - 1;
                    memcpy(key, trimmed, (size_t)size);
                    key[size] = '\0';
                    if (key[0] != '-' && strcmp(key, "version") != 0)
                        append_symbol(out, &count, max, key,
                                      indent == 2 ? "service" : "section", line_no, indent + 1);
                }
            }
        }
        else if (file->language == AROMA_CODE_LANGUAGE_TOML)
        {
            if (trimmed[0] == '[')
            {
                char section[80];
                snprintf(section, sizeof(section), "%s", trimmed);
                char *close = strchr(section, ']');
                if (close)
                    *(close + 1) = '\0';
                append_symbol(out, &count, max, section, "section", line_no, indent + 1);
            }
        }
        else if (file->language == AROMA_CODE_LANGUAGE_DOCKERFILE)
        {
            const char *instruction = trimmed;
            if (isupper((unsigned char)instruction[0]))
            {
                char word[32];
                int i = 0;
                while (instruction[i] && !isspace((unsigned char)instruction[i]) &&
                       i < (int)sizeof(word) - 1)
                {
                    word[i] = instruction[i];
                    i++;
                }
                word[i] = '\0';
                if (strcmp(word, "FROM") == 0 || strcmp(word, "RUN") == 0 ||
                    strcmp(word, "COPY") == 0 || strcmp(word, "WORKDIR") == 0 ||
                    strcmp(word, "EXPOSE") == 0 || strcmp(word, "CMD") == 0)
                    append_symbol(out, &count, max, word, "instruction", line_no, indent + 1);
            }
        }
        else if (file->language == AROMA_CODE_LANGUAGE_AROMA)
        {
            static const char *const widgets[] = {
                "Window", "Container", "Card", "Label", "Button", "Canvas", "Icon", "Image",
                "Textbox", "ScrollView"};
            for (int i = 0; i < (int)(sizeof(widgets) / sizeof(widgets[0])); i++)
            {
                const char *name = after_prefix(trimmed, widgets[i]);
                if (name && (*name == ' ' || *name == '{'))
                {
                    char detail[32];
                    snprintf(detail, sizeof(detail), "widget");
                    append_symbol(out, &count, max, widgets[i], detail, line_no, indent + 1);
                    break;
                }
            }
        }
        else if (file->language == AROMA_CODE_LANGUAGE_C ||
                 file->language == AROMA_CODE_LANGUAGE_CPP)
        {
            if (c_in_enum)
            {
                if (strchr(trimmed, '}'))
                {
                    c_in_enum = false;
                }
                else
                {
                    const char *name = trimmed;
                    while (*name == ' ' || *name == '\t')
                        name++;
                    if (c_is_ident_start(*name))
                    {
                        int length = 0;
                        while (c_is_ident_char(name[length]))
                            length++;
                        char symbol[80];
                        c_copy_word(symbol, sizeof(symbol), name, length);
                        if (symbol[0] && !c_keyword_word(symbol))
                            append_symbol(out, &count, max, symbol, "enumerator",
                                          line_no, (int)(name - raw) + 1);
                    }
                }
            }
            if (strstr(trimmed, "enum ") && strchr(trimmed, '{') &&
                !strchr(trimmed, '}'))
                c_in_enum = true;

            if (strncmp(trimmed, "#define ", 8) == 0)
            {
                const char *name = trimmed + 8;
                while (*name == ' ' || *name == '\t')
                    name++;
                char symbol[80];
                int i = 0;
                while (name[i] &&
                       (isalnum((unsigned char)name[i]) || name[i] == '_') &&
                       i < (int)sizeof(symbol) - 1)
                {
                    symbol[i] = name[i];
                    i++;
                }
                symbol[i] = '\0';
                if (symbol[0])
                    append_symbol(out, &count, max, symbol, "macro", line_no, 1);
            }
            else if ((strncmp(trimmed, "typedef struct ", 15) == 0 ||
                      strncmp(trimmed, "struct ", 7) == 0 ||
                      strncmp(trimmed, "typedef union ", 14) == 0 ||
                      strncmp(trimmed, "union ", 6) == 0 ||
                      strncmp(trimmed, "typedef enum ", 13) == 0 ||
                      strncmp(trimmed, "enum ", 5) == 0) &&
                     (strncmp(trimmed, "typedef ", 8) == 0 ||
                      strchr(trimmed, '{') != NULL))
            {
                const char *name = strstr(trimmed, "struct ");
                if (name)
                    name += 7;
                else
                {
                    name = strstr(trimmed, "union ");
                    if (name)
                        name += 6;
                }
                if (!name)
                {
                    name = strstr(trimmed, "enum ");
                    if (name)
                        name += 5;
                }
                if (name)
                {
                    while (*name == ' ' || *name == '\t')
                        name++;
                    char symbol[80];
                    int i = 0;
                    while (name[i] &&
                           (isalnum((unsigned char)name[i]) || name[i] == '_') &&
                           i < (int)sizeof(symbol) - 1)
                    {
                        symbol[i] = name[i];
                        i++;
                    }
                    symbol[i] = '\0';
                    if (symbol[0])
                        append_symbol(out, &count, max, symbol, "type", line_no,
                                      (int)(name - raw) + 1);
                    else
                    {
                        const char *close = strrchr(trimmed, '}');
                        if (close)
                        {
                            const char *alias = close + 1;
                            while (*alias == ' ' || *alias == '\t')
                                alias++;
                            int alias_length = 0;
                            while (c_is_ident_char(alias[alias_length]))
                                alias_length++;
                            if (alias_length > 0)
                            {
                                c_copy_word(symbol, sizeof(symbol), alias,
                                            alias_length);
                                if (!c_keyword_word(symbol))
                                    append_symbol(out, &count, max, symbol, "type",
                                                  line_no,
                                                  (int)(alias - raw) + 1);
                            }
                        }
                    }
                }
            }
            else if (strncmp(trimmed, "typedef ", 8) == 0)
            {
                int last_start = -1;
                int last_end = -1;
                int content_length = (int)strlen(trimmed);
                for (int i = 0; i < content_length; i++)
                {
                    if (c_is_ident_start(trimmed[i]))
                    {
                        int start = i++;
                        while (i < content_length && c_is_ident_char(trimmed[i]))
                            i++;
                        if (!c_keyword_word(trimmed + start))
                        {
                            last_start = start;
                            last_end = i;
                        }
                        i--;
                    }
                }
                if (last_start >= 0 && last_end > last_start)
                {
                    char symbol[80];
                    c_copy_word(symbol, sizeof(symbol), trimmed + last_start,
                                last_end - last_start);
                    append_symbol(out, &count, max, symbol, "type", line_no,
                                  last_start + 1);
                }
            }
            else
            {
                const char *brace = strchr(trimmed, '{');
                const char *paren = strchr(trimmed, '(');
                const char *semicolon = strrchr(trimmed, ';');
                bool prototype = paren && semicolon && (!brace || semicolon < brace);
                bool function_added = false;
                if (indent == 0 && paren && (brace || prototype))
                {
                    const char *name_end = paren;
                    while (name_end > trimmed && isspace((unsigned char)name_end[-1]))
                        name_end--;
                    const char *name_start = name_end;
                    while (name_start > trimmed &&
                           (isalnum((unsigned char)name_start[-1]) ||
                            name_start[-1] == '_'))
                        name_start--;
                    if (name_start < name_end && name_end - name_start < 80 &&
                        strncmp(name_start, "if", 2) != 0 &&
                        strncmp(name_start, "for", 3) != 0 &&
                        strncmp(name_start, "while", 5) != 0 &&
                        strncmp(name_start, "switch", 6) != 0)
                    {
                        char symbol[80];
                        int size = (int)(name_end - name_start);
                        memcpy(symbol, name_start, (size_t)size);
                        symbol[size] = '\0';
                        append_symbol(out, &count, max, symbol,
                                      prototype ? "prototype" : "function", line_no,
                                      (int)(name_start - raw) + 1);
                        function_added = true;
                    }
                }
                if (!function_added && indent == 0 && semicolon &&
                    (!brace || semicolon < brace) && trimmed[0] != '}' &&
                    trimmed[0] != '#')
                {
                    const char *decl_end = semicolon;
                    const char *equals = strchr(trimmed, '=');
                    if (equals && equals < decl_end)
                        decl_end = equals;
                    while (decl_end > trimmed &&
                           isspace((unsigned char)decl_end[-1]))
                        decl_end--;
                    if (decl_end > trimmed && decl_end[-1] == ']')
                    {
                        const char *bracket = decl_end - 1;
                        while (bracket > trimmed && *bracket != '[')
                            bracket--;
                        if (*bracket == '[')
                            decl_end = bracket;
                    }
                    while (decl_end > trimmed &&
                           isspace((unsigned char)decl_end[-1]))
                        decl_end--;
                    const char *name_start = decl_end;
                    while (name_start > trimmed &&
                           (isalnum((unsigned char)name_start[-1]) ||
                            name_start[-1] == '_'))
                        name_start--;
                    if (name_start < decl_end && decl_end - name_start < 80 &&
                        c_is_ident_start(*name_start))
                    {
                        char symbol[80];
                        int size = (int)(decl_end - name_start);
                        memcpy(symbol, name_start, (size_t)size);
                        symbol[size] = '\0';
                        if (!c_keyword_word(symbol))
                            append_symbol(out, &count, max, symbol, "variable",
                                          line_no, (int)(name_start - raw) + 1);
                    }
                }
            }
        }
        else if (file->language == AROMA_CODE_LANGUAGE_JSON)
        {
            if (indent == 2 && trimmed[0] == '"')
            {
                const char *quote = strchr(trimmed + 1, '"');
                if (quote)
                {
                    char key[80];
                    int size = (int)(quote - (trimmed + 1));
                    if (size >= (int)sizeof(key))
                        size = (int)sizeof(key) - 1;
                    memcpy(key, trimmed + 1, (size_t)size);
                    key[size] = '\0';
                    append_symbol(out, &count, max, key, "property", line_no, indent + 1);
                }
            }
        }
        else if (file->language == AROMA_CODE_LANGUAGE_CSS)
        {
            if (indent == 0 && strchr(trimmed, '{'))
            {
                char selector[80];
                snprintf(selector, sizeof(selector), "%s", trimmed);
                char *brace = strchr(selector, '{');
                if (brace)
                    *brace = '\0';
                char *trim_end = selector + strlen(selector);
                while (trim_end > selector && isspace((unsigned char)trim_end[-1]))
                    *--trim_end = '\0';
                if (selector[0])
                    append_symbol(out, &count, max, selector, "rule", line_no, 1);
            }
        }
        else if (file->language == AROMA_CODE_LANGUAGE_MARKDOWN)
        {
            if (trimmed[0] == '#')
            {
                const char *heading = trimmed;
                while (*heading == '#')
                    heading++;
                while (*heading == ' ')
                    heading++;
                append_symbol(out, &count, max, heading, "heading", line_no, indent + 1);
            }
        }

        p = *end ? end + 1 : end;
        line_no++;
    }
    return count;
}