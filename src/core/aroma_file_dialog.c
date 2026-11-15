#include "aroma_file_dialog.h"

#include <stddef.h>
#include <stdio.h>

#if defined(AROMA_FILE_DIALOG_USE_NFD)
#include "nfd.h"
#endif

#define AROMA_FILE_DIALOG_ERROR_SIZE 512

static char g_last_error[AROMA_FILE_DIALOG_ERROR_SIZE] = "";

static void aroma_file_dialog_clear_error(void)
{
    g_last_error[0] = '\0';
}

static void aroma_file_dialog_set_error(const char *message)
{
    if (!message)
        message = "native file dialog failed";
    snprintf(g_last_error, sizeof(g_last_error), "%s", message);
}

static int aroma_file_dialog_prepare(char **out_path)
{
    if (!out_path) {
        aroma_file_dialog_set_error("out_path must not be NULL");
        return 0;
    }

    *out_path = NULL;
    return 1;
}

#if defined(AROMA_FILE_DIALOG_USE_NFD)

static AromaFileDialogResult aroma_file_dialog_finish(nfdresult_t result,
                                                       nfdchar_t *path,
                                                       char **out_path)
{
    if (result == NFD_OKAY && path) {
        *out_path = (char *)path;
        aroma_file_dialog_clear_error();
        return AROMA_FILE_DIALOG_OK;
    }

    if (path)
        NFD_FreePath(path);

    if (result == NFD_CANCEL) {
        aroma_file_dialog_clear_error();
        return AROMA_FILE_DIALOG_CANCEL;
    }

    if (result == NFD_OKAY) {
        aroma_file_dialog_set_error("the file dialog returned no path");
    } else {
        const char *error = NFD_GetError();

        if (error && error[0] != '\0')
            aroma_file_dialog_set_error(error);
        else
            aroma_file_dialog_set_error("the native file dialog failed");
    }

    return AROMA_FILE_DIALOG_ERROR;
}

AromaFileDialogResult aroma_file_dialog_open(const char *title,
                                             const char *filter,
                                             const char *default_path,
                                             char **out_path)
{
    nfdchar_t *path = NULL;
    nfdresult_t result;

    if (!aroma_file_dialog_prepare(out_path))
        return AROMA_FILE_DIALOG_ERROR;

    result = NFD_OpenDialogWithTitle(title, filter, default_path, &path);
    return aroma_file_dialog_finish(result, path, out_path);
}

AromaFileDialogResult aroma_file_dialog_save(const char *title,
                                             const char *filter,
                                             const char *default_path,
                                             char **out_path)
{
    nfdchar_t *path = NULL;
    nfdresult_t result;

    if (!aroma_file_dialog_prepare(out_path))
        return AROMA_FILE_DIALOG_ERROR;

    result = NFD_SaveDialogWithTitle(title, filter, default_path, &path);
    return aroma_file_dialog_finish(result, path, out_path);
}

AromaFileDialogResult aroma_file_dialog_pick_folder(const char *title,
                                                    const char *default_path,
                                                    char **out_path)
{
    nfdchar_t *path = NULL;
    nfdresult_t result;

    if (!aroma_file_dialog_prepare(out_path))
        return AROMA_FILE_DIALOG_ERROR;

    result = NFD_PickFolderWithTitle(title, default_path, &path);
    return aroma_file_dialog_finish(result, path, out_path);
}

bool aroma_file_dialog_available(void)
{
    return NFD_IsAvailable() != 0;
}

#else /* !AROMA_FILE_DIALOG_USE_NFD */

static AromaFileDialogResult aroma_file_dialog_unsupported(char **out_path)
{
    if (!aroma_file_dialog_prepare(out_path))
        return AROMA_FILE_DIALOG_ERROR;

    aroma_file_dialog_clear_error();
#if defined(__ANDROID__)
    aroma_file_dialog_set_error("native file dialogs are not supported on Android");
#elif defined(__EMSCRIPTEN__)
    aroma_file_dialog_set_error("native file dialogs are not supported on Emscripten");
#else
    aroma_file_dialog_set_error("native file dialogs are not enabled for this build");
#endif
    return AROMA_FILE_DIALOG_ERROR;
}

AromaFileDialogResult aroma_file_dialog_open(const char *title,
                                             const char *filter,
                                             const char *default_path,
                                             char **out_path)
{
    (void)title;
    (void)filter;
    (void)default_path;
    return aroma_file_dialog_unsupported(out_path);
}

AromaFileDialogResult aroma_file_dialog_save(const char *title,
                                             const char *filter,
                                             const char *default_path,
                                             char **out_path)
{
    (void)title;
    (void)filter;
    (void)default_path;
    return aroma_file_dialog_unsupported(out_path);
}

AromaFileDialogResult aroma_file_dialog_pick_folder(const char *title,
                                                    const char *default_path,
                                                    char **out_path)
{
    (void)title;
    (void)default_path;
    return aroma_file_dialog_unsupported(out_path);
}

bool aroma_file_dialog_available(void)
{
    return false;
}

#endif /* AROMA_FILE_DIALOG_USE_NFD */

void aroma_file_dialog_free(char *path)
{
#if defined(AROMA_FILE_DIALOG_USE_NFD)
    NFD_FreePath((nfdchar_t *)path);
#else
    (void)path;
#endif
}

const char *aroma_file_dialog_last_error(void)
{
    return g_last_error;
}
