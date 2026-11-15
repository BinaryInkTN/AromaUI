#ifndef AROMA_FILE_DIALOG_H
#define AROMA_FILE_DIALOG_H

/*
 * AromaUI native file dialogs.
 *
 * A small, portable facade over the vendored NFD backend. On desktop
 * platforms (Linux, macOS and Windows) the call is forwarded to NFD. On
 * Android and Emscripten the functions are unsupported stubs that return
 * AROMA_FILE_DIALOG_ERROR.
 *
 * All returned paths are allocated and must be released with
 * aroma_file_dialog_free(). On AROMA_FILE_DIALOG_CANCEL and
 * AROMA_FILE_DIALOG_ERROR, *out_path is set to NULL.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AROMA_FILE_DIALOG_OK = 0,
    AROMA_FILE_DIALOG_CANCEL,
    AROMA_FILE_DIALOG_ERROR
} AromaFileDialogResult;

/*
 * Open an existing file.
 *
 * title        - dialog title, or NULL for the backend default.
 * filter       - NFD-style filter list, or NULL for all files. Filters are
 *                separated by semicolons; each filter is either an extension
 *                list ("png,jpg") or "Name:ext1,ext2" ("Images:png,jpg").
 * default_path - initial file or directory, or NULL.
 * out_path     - receives the selected path on success.
 */
AromaFileDialogResult aroma_file_dialog_open(const char *title,
                                             const char *filter,
                                             const char *default_path,
                                             char **out_path);

/* Save a file. See aroma_file_dialog_open() for the argument semantics. */
AromaFileDialogResult aroma_file_dialog_save(const char *title,
                                             const char *filter,
                                             const char *default_path,
                                             char **out_path);

/*
 * Pick an existing directory. filter is not applicable and is omitted from
 * this entry point.
 */
AromaFileDialogResult aroma_file_dialog_pick_folder(const char *title,
                                                    const char *default_path,
                                                    char **out_path);

/* Release a path returned by any of the functions above. NULL is a no-op. */
void aroma_file_dialog_free(char *path);

/* Description of the last error, or an empty string when none occurred. */
const char *aroma_file_dialog_last_error(void);

/*
 * Returns true when native file dialogs can be shown in the current
 * environment. This checks the platform, the graphical session and the
 * presence of a dialog helper; it does not open a dialog.
 */
bool aroma_file_dialog_available(void);

#ifdef __cplusplus
}
#endif

#endif /* AROMA_FILE_DIALOG_H */
