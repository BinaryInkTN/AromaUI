#include "nfd_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static char g_error[512] = "";

void nfd_set_error(const char *format, ...)
{
    va_list args;

    if (!format) {
        g_error[0] = '\0';
        return;
    }

    va_start(args, format);
    vsnprintf(g_error, sizeof(g_error), format, args);
    va_end(args);
}

void nfd_clear_error(void)
{
    g_error[0] = '\0';
}

const char *NFD_GetError(void)
{
    return g_error;
}

nfdresult_t NFD_Init(void)
{
    /* This backend has no per-process state to initialize. */
    nfd_clear_error();
    return NFD_OKAY;
}

void NFD_Quit(void)
{
    nfd_clear_error();
}

nfdresult_t NFD_OpenDialog(const nfdchar_t *filterlist,
                           const nfdchar_t *default_path,
                           nfdchar_t **out_path)
{
    return NFD_OpenDialogWithTitle(NULL, filterlist, default_path, out_path);
}

nfdresult_t NFD_SaveDialog(const nfdchar_t *filterlist,
                           const nfdchar_t *default_path,
                           nfdchar_t **out_path)
{
    return NFD_SaveDialogWithTitle(NULL, filterlist, default_path, out_path);
}

nfdresult_t NFD_PickFolder(const nfdchar_t *default_path,
                           nfdchar_t **out_path)
{
    return NFD_PickFolderWithTitle(NULL, default_path, out_path);
}

void NFD_FreePath(nfdchar_t *path)
{
    free(path);
}
