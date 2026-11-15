#ifndef NFD_H
#define NFD_H

/*
 * NFD - native file dialogs for desktop applications.
 *
 * This is a small, dependency-free NFD-style implementation. On Linux the
 * dialogs are provided by the first available helper (zenity or kdialog),
 * launched directly with fork/exec. No shell is involved.
 *
 * Filter lists use the classic NFD syntax: filters are separated by
 * semicolons, each filter is either a comma-separated extension list or
 * "Name:ext1,ext2". Extensions may be written with or without a leading
 * "*." (for example "png,jpg", "*.png,*.jpg" or "Images:png,jpg").
 *
 * Every returned path is allocated by NFD and must be released with
 * NFD_FreePath().
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef int nfdresult_t;

#define NFD_OKAY     1
#define NFD_CANCEL   0
#define NFD_ERROR   (-1)

typedef char nfdchar_t;

/* Initialize/tear down the backend. These are optional for this backend and
 * are safe to call more than once. */
nfdresult_t NFD_Init(void);
void NFD_Quit(void);

/* Description of the last error, or an empty string when no error occurred. */
const char *NFD_GetError(void);

/* Open an existing file. default_path may be NULL or empty. */
nfdresult_t NFD_OpenDialog(const nfdchar_t *filterlist,
                           const nfdchar_t *default_path,
                           nfdchar_t **out_path);

/* Save a file. default_path may be a directory or a full file path. */
nfdresult_t NFD_SaveDialog(const nfdchar_t *filterlist,
                           const nfdchar_t *default_path,
                           nfdchar_t **out_path);

/* Pick an existing directory. */
nfdresult_t NFD_PickFolder(const nfdchar_t *default_path,
                           nfdchar_t **out_path);

/* Free a path returned by any of the dialog functions. NULL is a no-op. */
void NFD_FreePath(nfdchar_t *path);

/*
 * Extensions: the classic NFD API has no title argument. These variants let
 * callers provide a dialog title while keeping the standard functions above
 * source compatible. title may be NULL.
 */
nfdresult_t NFD_OpenDialogWithTitle(const nfdchar_t *title,
                                    const nfdchar_t *filterlist,
                                    const nfdchar_t *default_path,
                                    nfdchar_t **out_path);

nfdresult_t NFD_SaveDialogWithTitle(const nfdchar_t *title,
                                    const nfdchar_t *filterlist,
                                    const nfdchar_t *default_path,
                                    nfdchar_t **out_path);

nfdresult_t NFD_PickFolderWithTitle(const nfdchar_t *title,
                                    const nfdchar_t *default_path,
                                    nfdchar_t **out_path);

/*
 * Returns non-zero when a graphical session and a supported helper are
 * available. This does not open a dialog and does not modify the last error.
 */
int NFD_IsAvailable(void);

#ifdef __cplusplus
}
#endif

#endif /* NFD_H */
