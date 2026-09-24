#ifndef PACKAGE_MANAGER_H
#define PACKAGE_MANAGER_H

/*
 * Package manager: the assets folder is the ONLY source of truth.
 *
 * Installed packages are exactly the *.apak files in the assets dir
 * (./assets, ../assets, ... or $AROMA_ASSETS_DIR override). Installing
 * copies an .apak into assets; uninstalling deletes it from assets.
 * The manager never creates package folders: manifests and pure-UI
 * markup are read straight out of each .apak, and native plugins are
 * extracted to a disposable runtime cache (outside assets) that is
 * validated against the .apak on every load and may be wiped any time.
 */

#include "aroma.h"
#include "aroma_package.h"
#include "aroma_incense_loader.h"
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define PACKAGE_MAX_INSTALLED 32

typedef struct
{
    AromaPackageManifest manifest;
    char apak_path[AROMA_PACKAGE_PATH_MAX]; /* source of truth: .apak in assets */
    char rundir[AROMA_PACKAGE_PATH_MAX];    /* transient validated cache (native pkgs) */

    bool loaded;
    bool shown;
    AromaNode *app_root;
    AromaNode *drawer_card;
    AromaNode *close_btn;
    IncenseRegistry *registry;
    const AromaPackageHooks *hooks;
    void *dl_handle;
} InstalledPackage;

bool package_manager_init(void);
void package_manager_shutdown(void);

/* Re-read the assets folder: picks up added/removed/updated .apaks. */
bool package_manager_scan(void);

const char *package_manager_dir(void);
const char *package_manager_last_installed_id(void);
int package_manager_count(void);
InstalledPackage *package_manager_get(int index);
InstalledPackage *package_manager_find(const char *id);

void *package_manager_symbol(const char *id, const char *sym);

bool package_manager_is_system(const char *id);

/* Install = copy the .apak into the assets folder (named <id>.apak).
 * Refuses downgrades. A no-op when that exact file is already in place. */
bool package_manager_install_apak(const char *apak_path,
                                  char *err_buf, size_t err_buf_len);

/* Uninstall = delete the .apak from the assets folder (+ drop its cache). */
bool package_manager_uninstall(const char *id,
                               char *err_buf, size_t err_buf_len);

bool package_manager_instantiate(InstalledPackage *pkg, AromaNode *ui_parent,
                                 const AromaPackageHost *host,
                                 char *err_buf, size_t err_buf_len);

/* Install metadata: the install/upgrade time is the mtime of the
 * .apak file in the assets folder. Returns false when unavailable;
 * format writes "unknown" instead. */
bool package_manager_install_time(const InstalledPackage *pkg,
                                  time_t *out_mtime);
bool package_manager_format_install_time(const InstalledPackage *pkg,
                                         char *out, size_t out_len);

bool package_manager_show(InstalledPackage *pkg);
void package_manager_hide(InstalledPackage *pkg);
void package_manager_update_all(void);

void package_manager_close_cb(void *user_data);

#endif
