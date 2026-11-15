/*
 * Linux backend for NFD.
 *
 * Dialogs are shown by launching zenity or kdialog directly with an argv
 * array (never through a shell). The selected path is read from a pipe and
 * the child is always reaped with waitpid(), so no zombies are left behind.
 *
 * This file compiles to stubs on platforms without a fork/exec backend; the
 * public API remains available so portable callers still build.
 */

#if defined(__linux__)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "nfd_internal.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define NFD_EXEC_FAILED_STATUS 127
#define NFD_MAX_OUTPUT_SIZE (1024u * 1024u)

typedef enum {
    NFD_DIALOG_OPEN,
    NFD_DIALOG_SAVE,
    NFD_DIALOG_FOLDER
} NfdDialogMode;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} NfdBuffer;

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} NfdArgv;

/* ------------------------------------------------------------------------- */
/* Small allocation helpers                                                  */
/* ------------------------------------------------------------------------- */

static int nfd_buffer_reserve(NfdBuffer *buffer, size_t extra)
{
    size_t needed;
    size_t capacity;
    char *data;

    if (extra > (size_t)-1 - buffer->length - 1)
        return 0;

    needed = buffer->length + extra + 1;
    if (needed <= buffer->capacity)
        return 1;

    capacity = buffer->capacity ? buffer->capacity : 256;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }

    data = (char *)realloc(buffer->data, capacity);
    if (!data)
        return 0;

    buffer->data = data;
    buffer->capacity = capacity;
    return 1;
}

static int nfd_buffer_append_n(NfdBuffer *buffer, const char *text, size_t length)
{
    if (!nfd_buffer_reserve(buffer, length))
        return 0;

    if (length > 0)
        memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 1;
}

static int nfd_buffer_append(NfdBuffer *buffer, const char *text)
{
    return nfd_buffer_append_n(buffer, text, strlen(text));
}

static int nfd_buffer_append_char(NfdBuffer *buffer, char value)
{
    return nfd_buffer_append_n(buffer, &value, 1);
}

static char *nfd_buffer_take(NfdBuffer *buffer)
{
    char *data = buffer->data;

    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
    return data;
}

static void nfd_buffer_free(NfdBuffer *buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static int nfd_argv_add_owned(NfdArgv *argv, char *argument);

static int nfd_argv_add(NfdArgv *argv, const char *argument)
{
    char *copy;

    if (!argument)
        return 0;

    copy = strdup(argument);
    if (!copy)
        return 0;

    return nfd_argv_add_owned(argv, copy);
}

static int nfd_argv_add_owned(NfdArgv *argv, char *argument)
{
    char **items;
    size_t capacity;

    if (!argument)
        return 0;

    if (argv->count + 2 > argv->capacity) {
        capacity = argv->capacity ? argv->capacity * 2 : 16;
        items = (char **)realloc(argv->items, capacity * sizeof(*items));
        if (!items) {
            free(argument);
            return 0;
        }
        argv->items = items;
        argv->capacity = capacity;
    }

    argv->items[argv->count++] = argument;
    argv->items[argv->count] = NULL;
    return 1;
}

static void nfd_argv_free(NfdArgv *argv)
{
    size_t i;

    for (i = 0; i < argv->count; ++i)
        free(argv->items[i]);
    free(argv->items);
    argv->items = NULL;
    argv->count = 0;
    argv->capacity = 0;
}

/* ------------------------------------------------------------------------- */
/* Environment and helper discovery                                          */
/* ------------------------------------------------------------------------- */

static int nfd_has_display(void)
{
    const char *display = getenv("DISPLAY");
    const char *wayland_display = getenv("WAYLAND_DISPLAY");

    return (display && display[0] != '\0') ||
           (wayland_display && wayland_display[0] != '\0');
}

static char *nfd_find_executable(const char *name)
{
    const char *path;
    const char *cursor;
    struct stat info;

    if (!name || name[0] == '\0')
        return NULL;

    /* An explicit path is allowed, but never search the current directory
       implicitly through an empty PATH component. */
    if (strchr(name, '/')) {
        if (access(name, X_OK) == 0 &&
            stat(name, &info) == 0 && S_ISREG(info.st_mode)) {
            return strdup(name);
        }
        return NULL;
    }

    path = getenv("PATH");
    if (!path || path[0] == '\0')
        path = "/usr/local/bin:/usr/bin:/bin";

    cursor = path;
    while (*cursor) {
        const char *end = strchr(cursor, ':');
        size_t directory_length = end ? (size_t)(end - cursor) : strlen(cursor);

        if (directory_length > 0) {
            size_t candidate_length = directory_length + 1 + strlen(name) + 1;
            char *candidate = (char *)malloc(candidate_length);

            if (!candidate)
                return NULL;

            memcpy(candidate, cursor, directory_length);
            candidate[directory_length] = '/';
            memcpy(candidate + directory_length + 1, name, strlen(name) + 1);

            if (access(candidate, X_OK) == 0 &&
                stat(candidate, &info) == 0 && S_ISREG(info.st_mode)) {
                return candidate;
            }

            free(candidate);
        }

        if (!end)
            break;
        cursor = end + 1;
    }

    return NULL;
}

int NFD_IsAvailable(void)
{
    char *helper;

    if (!nfd_has_display())
        return 0;

    helper = nfd_find_executable("zenity");
    if (!helper)
        helper = nfd_find_executable("kdialog");

    if (helper) {
        free(helper);
        return 1;
    }

    return 0;
}

/* ------------------------------------------------------------------------- */
/* Filter list parsing                                                       */
/* ------------------------------------------------------------------------- */

static char *nfd_trimmed_dup(const char *text, size_t length)
{
    size_t start = 0;
    size_t i;
    char *result;

    while (start < length && isspace((unsigned char)text[start]))
        ++start;
    while (length > start && isspace((unsigned char)text[length - 1]))
        --length;

    result = (char *)malloc(length - start + 1);
    if (!result)
        return NULL;

    for (i = 0; i < length - start; ++i) {
        unsigned char value = (unsigned char)text[start + i];

        /* Keep these characters from confusing the helpers' filter syntax. */
        if (value == '|' || value == '(' || value == ')' ||
            value == '\r' || value == '\n' || value == '\t') {
            result[i] = ' ';
        } else {
            result[i] = (char)value;
        }
    }
    result[length - start] = '\0';
    return result;
}

static char *nfd_make_pattern(const char *token, size_t length)
{
    NfdBuffer pattern = {0};
    char *result;

    while (length > 0 && isspace((unsigned char)*token)) {
        ++token;
        --length;
    }
    while (length > 0 && isspace((unsigned char)token[length - 1]))
        --length;

    if (length == 0)
        return NULL;

    /* Accept "Images (*.png *.jpg)" tokens as well as plain extensions. */
    if (token[0] == '(' && token[length - 1] == ')') {
        ++token;
        length -= 2;
    }

    if (length == 0)
        return NULL;

    if (token[0] == '*') {
        if (!nfd_buffer_append_n(&pattern, token, length))
            return NULL;
    } else if (token[0] == '.') {
        if (!nfd_buffer_append(&pattern, "*") ||
            !nfd_buffer_append_n(&pattern, token, length)) {
            nfd_buffer_free(&pattern);
            return NULL;
        }
    } else {
        if (!nfd_buffer_append(&pattern, "*.") ||
            !nfd_buffer_append_n(&pattern, token, length)) {
            nfd_buffer_free(&pattern);
            return NULL;
        }
    }

    result = nfd_buffer_take(&pattern);
    return result;
}

/*
 * Parse one semicolon-separated filter group. On success *out_name receives
 * an allocated (possibly NULL) display name and patterns receives a
 * space-separated wildcard list.
 */
static int nfd_parse_filter_group(const char *group, size_t length,
                                  char **out_name, NfdBuffer *patterns)
{
    char *copy;
    char *name_start = NULL;
    char *extensions;
    char *colon;
    char *opening;
    char *cursor;
    char *name = NULL;

    *out_name = NULL;

    copy = (char *)malloc(length + 1);
    if (!copy)
        return 0;
    memcpy(copy, group, length);
    copy[length] = '\0';

    extensions = copy;
    colon = strchr(copy, ':');
    opening = strchr(copy, '(');

    if (colon && (!opening || colon < opening)) {
        *colon = '\0';
        name_start = copy;
        extensions = colon + 1;
    } else if (opening) {
        char *closing = strrchr(opening, ')');

        if (closing)
            *closing = '\0';
        *opening = '\0';
        name_start = copy;
        extensions = opening + 1;
    }

    if (name_start) {
        name = nfd_trimmed_dup(name_start, strlen(name_start));
        if (!name) {
            free(copy);
            return 0;
        }
        *out_name = name;
    }

    cursor = extensions;
    while (*cursor) {
        char *token;
        size_t token_length;
        char *pattern;

        while (*cursor && (*cursor == ',' || isspace((unsigned char)*cursor)))
            ++cursor;
        token = cursor;
        while (*cursor && *cursor != ',' && !isspace((unsigned char)*cursor))
            ++cursor;
        token_length = (size_t)(cursor - token);

        if (token_length == 0)
            continue;

        pattern = nfd_make_pattern(token, token_length);
        if (!pattern) {
            free(name);
            *out_name = NULL;
            free(copy);
            return 0;
        }

        if (patterns->length > 0 && !nfd_buffer_append_char(patterns, ' ')) {
            free(pattern);
            free(name);
            *out_name = NULL;
            free(copy);
            return 0;
        }
        if (!nfd_buffer_append(patterns, pattern)) {
            free(pattern);
            free(name);
            *out_name = NULL;
            free(copy);
            return 0;
        }
        free(pattern);
    }

    free(copy);
    return 1;
}

static int nfd_add_zenity_filters(NfdArgv *argv, const char *filterlist)
{
    const char *cursor;

    if (!filterlist || filterlist[0] == '\0')
        return 1;

    cursor = filterlist;
    while (*cursor) {
        const char *end = strchr(cursor, ';');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);

        if (length > 0) {
            char *name = NULL;
            NfdBuffer patterns = {0};
            NfdBuffer argument = {0};
            const char *display_name;

            if (!nfd_parse_filter_group(cursor, length, &name, &patterns)) {
                nfd_buffer_free(&patterns);
                nfd_set_error("NFD: out of memory while parsing the filter list");
                return 0;
            }

            if (patterns.length > 0) {
                display_name = (name && name[0] != '\0') ? name : "Files";

                if (!nfd_buffer_append(&argument, "--file-filter=") ||
                    !nfd_buffer_append(&argument, display_name) ||
                    !nfd_buffer_append(&argument, " | ") ||
                    !nfd_buffer_append(&argument, patterns.data) ||
                    !nfd_argv_add_owned(argv, nfd_buffer_take(&argument))) {
                    free(name);
                    nfd_buffer_free(&patterns);
                    nfd_buffer_free(&argument);
                    nfd_set_error("NFD: out of memory while building the dialog arguments");
                    return 0;
                }
            }

            free(name);
            nfd_buffer_free(&patterns);
            nfd_buffer_free(&argument);
        }

        if (!end)
            break;
        cursor = end + 1;
    }

    return 1;
}

static char *nfd_make_kdialog_filter(const char *filterlist)
{
    const char *cursor;
    NfdBuffer patterns = {0};
    NfdBuffer result = {0};
    char *name = NULL;
    char *filter = NULL;

    if (!filterlist || filterlist[0] == '\0')
        return NULL;

    cursor = filterlist;
    while (*cursor) {
        const char *end = strchr(cursor, ';');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);

        if (length > 0) {
            char *group_name = NULL;
            NfdBuffer group_patterns = {0};

            if (!nfd_parse_filter_group(cursor, length, &group_name, &group_patterns)) {
                nfd_buffer_free(&group_patterns);
                free(name);
                nfd_buffer_free(&patterns);
                nfd_set_error("NFD: out of memory while parsing the filter list");
                return NULL;
            }

            if (group_patterns.length > 0) {
                if (patterns.length > 0 &&
                    !nfd_buffer_append_char(&patterns, ' ')) {
                    free(group_name);
                    nfd_buffer_free(&group_patterns);
                    free(name);
                    nfd_buffer_free(&patterns);
                    nfd_set_error("NFD: out of memory while building the dialog filter");
                    return NULL;
                }
                if (!nfd_buffer_append(&patterns, group_patterns.data)) {
                    free(group_name);
                    nfd_buffer_free(&group_patterns);
                    free(name);
                    nfd_buffer_free(&patterns);
                    nfd_set_error("NFD: out of memory while building the dialog filter");
                    return NULL;
                }

                if (!name && group_name && group_name[0] != '\0')
                    name = group_name;
                else
                    free(group_name);
            } else {
                free(group_name);
            }

            nfd_buffer_free(&group_patterns);
        }

        if (!end)
            break;
        cursor = end + 1;
    }

    if (patterns.length > 0) {
        if (!nfd_buffer_append(&result, (name && name[0] != '\0') ? name : "Files") ||
            !nfd_buffer_append(&result, " (") ||
            !nfd_buffer_append(&result, patterns.data) ||
            !nfd_buffer_append_char(&result, ')')) {
            nfd_set_error("NFD: out of memory while building the dialog filter");
            free(name);
            nfd_buffer_free(&patterns);
            nfd_buffer_free(&result);
            return NULL;
        }
        filter = nfd_buffer_take(&result);
    }

    free(name);
    nfd_buffer_free(&patterns);
    nfd_buffer_free(&result);
    return filter;
}

/* ------------------------------------------------------------------------- */
/* Dialog argument construction                                              */
/* ------------------------------------------------------------------------- */

static int nfd_build_zenity_argv(const char *helper, NfdDialogMode mode,
                                 const char *title, const char *filterlist,
                                 const char *default_path, NfdArgv *argv)
{
    if (!nfd_argv_add(argv, helper) ||
        !nfd_argv_add(argv, "--file-selection"))
        goto out_of_memory;

    if (mode == NFD_DIALOG_SAVE) {
        if (!nfd_argv_add(argv, "--save") ||
            !nfd_argv_add(argv, "--confirm-overwrite"))
            goto out_of_memory;
    } else if (mode == NFD_DIALOG_FOLDER) {
        if (!nfd_argv_add(argv, "--directory"))
            goto out_of_memory;
    }

    if (title && title[0] != '\0') {
        if (!nfd_argv_add(argv, "--title") || !nfd_argv_add(argv, title))
            goto out_of_memory;
    }

    if (default_path && default_path[0] != '\0') {
        NfdBuffer filename = {0};

        if (!nfd_buffer_append(&filename, "--filename=") ||
            !nfd_buffer_append(&filename, default_path) ||
            !nfd_argv_add_owned(argv, nfd_buffer_take(&filename))) {
            nfd_buffer_free(&filename);
            goto out_of_memory;
        }
    }

    if (!nfd_add_zenity_filters(argv, filterlist))
        return 0;

    return 1;

out_of_memory:
    nfd_set_error("NFD: out of memory while building the dialog arguments");
    return 0;
}

static int nfd_build_kdialog_argv(const char *helper, NfdDialogMode mode,
                                  const char *title, const char *filterlist,
                                  const char *default_path, NfdArgv *argv)
{
    const char *start_path = (default_path && default_path[0] != '\0')
                                 ? default_path
                                 : ".";

    if (!nfd_argv_add(argv, helper))
        goto out_of_memory;

    if (title && title[0] != '\0') {
        if (!nfd_argv_add(argv, "--title") || !nfd_argv_add(argv, title))
            goto out_of_memory;
    }

    if (mode == NFD_DIALOG_OPEN) {
        if (!nfd_argv_add(argv, "--getopenfilename"))
            goto out_of_memory;
    } else if (mode == NFD_DIALOG_SAVE) {
        if (!nfd_argv_add(argv, "--getsavefilename"))
            goto out_of_memory;
    } else {
        if (!nfd_argv_add(argv, "--getexistingdirectory"))
            goto out_of_memory;
    }

    if (!nfd_argv_add(argv, start_path))
        goto out_of_memory;

    if (mode != NFD_DIALOG_FOLDER && filterlist && filterlist[0] != '\0') {
        char *filter = nfd_make_kdialog_filter(filterlist);

        if (!filter) {
            /* A filter list without any usable pattern is not an error. */
            if (NFD_GetError()[0] != '\0')
                return 0;
        } else if (!nfd_argv_add_owned(argv, filter)) {
            nfd_set_error("NFD: out of memory while building the dialog arguments");
            return 0;
        }
    }

    return 1;

out_of_memory:
    nfd_set_error("NFD: out of memory while building the dialog arguments");
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Helper process                                                            */
/* ------------------------------------------------------------------------- */

static nfdresult_t nfd_run_dialog(NfdDialogMode mode, const char *title,
                                  const char *filterlist,
                                  const char *default_path,
                                  nfdchar_t **out_path)
{
    NfdArgv argv = {0};
    NfdBuffer output = {0};
    char *helper;
    int helper_is_zenity;
    int pipe_fds[2] = {-1, -1};
    pid_t child;
    int status = 0;
    int wait_result;
    int read_failed = 0;
    int too_large = 0;
    nfdresult_t result = NFD_ERROR;

    if (!out_path) {
        nfd_set_error("NFD: out_path must not be NULL");
        return NFD_ERROR;
    }
    *out_path = NULL;
    nfd_clear_error();

    if (!nfd_has_display()) {
        nfd_set_error("NFD: no graphical display is available "
                      "(DISPLAY and WAYLAND_DISPLAY are not set)");
        return NFD_ERROR;
    }

    helper = nfd_find_executable("zenity");
    helper_is_zenity = helper != NULL;
    if (!helper)
        helper = nfd_find_executable("kdialog");

    if (!helper) {
        nfd_set_error("NFD: no supported file dialog helper was found "
                      "(install zenity or kdialog)");
        return NFD_ERROR;
    }

    if (helper_is_zenity) {
        if (!nfd_build_zenity_argv(helper, mode, title, filterlist,
                                   default_path, &argv))
            goto cleanup;
    } else {
        if (!nfd_build_kdialog_argv(helper, mode, title, filterlist,
                                    default_path, &argv))
            goto cleanup;
    }

    if (pipe(pipe_fds) != 0) {
        nfd_set_error("NFD: pipe() failed: %s", strerror(errno));
        goto cleanup;
    }

    /* The parent must never inherit the write end, otherwise reading the
       child's output would wait forever. */
    (void)fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC);

    child = fork();
    if (child < 0) {
        nfd_set_error("NFD: fork() failed: %s", strerror(errno));
        goto cleanup;
    }

    if (child == 0) {
        /* Child: send only stdout to the pipe. */
        close(pipe_fds[0]);
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0)
            _exit(NFD_EXEC_FAILED_STATUS);
        if (pipe_fds[1] != STDOUT_FILENO)
            close(pipe_fds[1]);

        execv(helper, argv.items);
        _exit(NFD_EXEC_FAILED_STATUS);
    }

    close(pipe_fds[1]);
    pipe_fds[1] = -1;

    for (;;) {
        char chunk[4096];
        ssize_t bytes = read(pipe_fds[0], chunk, sizeof(chunk));

        if (bytes > 0) {
            if (output.length + (size_t)bytes > NFD_MAX_OUTPUT_SIZE) {
                too_large = 1;
                break;
            }
            if (!nfd_buffer_append_n(&output, chunk, (size_t)bytes)) {
                read_failed = 1;
                break;
            }
        } else if (bytes == 0) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            read_failed = 1;
            break;
        }
    }

    close(pipe_fds[0]);
    pipe_fds[0] = -1;

    do {
        wait_result = waitpid(child, &status, 0);
    } while (wait_result < 0 && errno == EINTR);

    if (wait_result < 0) {
        nfd_set_error("NFD: waitpid() failed: %s", strerror(errno));
        goto cleanup;
    }

    if (read_failed) {
        nfd_set_error("NFD: failed to read the dialog helper output");
        goto cleanup;
    }
    if (too_large) {
        nfd_set_error("NFD: dialog helper produced too much output");
        goto cleanup;
    }

    if (!WIFEXITED(status)) {
        nfd_set_error("NFD: dialog helper was terminated abnormally");
        goto cleanup;
    }

    {
        int exit_code = WEXITSTATUS(status);

        if (exit_code == 1) {
            result = NFD_CANCEL;
            goto cleanup;
        }
        if (exit_code == NFD_EXEC_FAILED_STATUS) {
            nfd_set_error("NFD: failed to execute the dialog helper '%s'",
                          helper);
            goto cleanup;
        }
        if (exit_code != 0) {
            nfd_set_error("NFD: dialog helper '%s' exited with status %d",
                          helper, exit_code);
            goto cleanup;
        }
    }

    while (output.length > 0 &&
           (output.data[output.length - 1] == '\n' ||
            output.data[output.length - 1] == '\r')) {
        output.data[--output.length] = '\0';
    }

    if (output.length == 0) {
        nfd_set_error("NFD: dialog helper returned an empty path");
        goto cleanup;
    }

    *out_path = (nfdchar_t *)nfd_buffer_take(&output);
    result = NFD_OKAY;

cleanup:
    if (pipe_fds[0] >= 0)
        close(pipe_fds[0]);
    if (pipe_fds[1] >= 0)
        close(pipe_fds[1]);
    free(helper);
    nfd_argv_free(&argv);
    nfd_buffer_free(&output);
    return result;
}

nfdresult_t NFD_OpenDialogWithTitle(const nfdchar_t *title,
                                    const nfdchar_t *filterlist,
                                    const nfdchar_t *default_path,
                                    nfdchar_t **out_path)
{
    return nfd_run_dialog(NFD_DIALOG_OPEN, title, filterlist, default_path,
                          out_path);
}

nfdresult_t NFD_SaveDialogWithTitle(const nfdchar_t *title,
                                    const nfdchar_t *filterlist,
                                    const nfdchar_t *default_path,
                                    nfdchar_t **out_path)
{
    return nfd_run_dialog(NFD_DIALOG_SAVE, title, filterlist, default_path,
                          out_path);
}

nfdresult_t NFD_PickFolderWithTitle(const nfdchar_t *title,
                                    const nfdchar_t *default_path,
                                    nfdchar_t **out_path)
{
    return nfd_run_dialog(NFD_DIALOG_FOLDER, title, NULL, default_path,
                          out_path);
}

#else /* !__linux__ */

nfdresult_t NFD_OpenDialogWithTitle(const nfdchar_t *title,
                                    const nfdchar_t *filterlist,
                                    const nfdchar_t *default_path,
                                    nfdchar_t **out_path)
{
    (void)title;
    (void)filterlist;
    (void)default_path;
    if (out_path)
        *out_path = NULL;
    nfd_set_error("NFD: the native file dialog backend is not implemented "
                  "on this platform");
    return NFD_ERROR;
}

nfdresult_t NFD_SaveDialogWithTitle(const nfdchar_t *title,
                                    const nfdchar_t *filterlist,
                                    const nfdchar_t *default_path,
                                    nfdchar_t **out_path)
{
    return NFD_OpenDialogWithTitle(title, filterlist, default_path, out_path);
}

nfdresult_t NFD_PickFolderWithTitle(const nfdchar_t *title,
                                    const nfdchar_t *default_path,
                                    nfdchar_t **out_path)
{
    (void)title;
    (void)default_path;
    if (out_path)
        *out_path = NULL;
    nfd_set_error("NFD: the native file dialog backend is not implemented "
                  "on this platform");
    return NFD_ERROR;
}

int NFD_IsAvailable(void)
{
    return 0;
}

#endif /* __linux__ */
