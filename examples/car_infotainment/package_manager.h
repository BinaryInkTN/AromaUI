#ifndef PACKAGE_MANAGER_H
#define PACKAGE_MANAGER_H

/* Host-side package manager for the car infotainment.
 *
 * - Scans a packages directory for installed packages (subdir + manifest.json).
 * - Installs .apak bundles (validate -> extract -> rescan).
 * - Uninstalls by id (files removed immediately; a loaded package's UI
 *   nodes are hidden and fully released on restart).
 * - Instantiates packages into the UI: creates an app root card, mounts
 *   ui.aroma via the Incense loader, dlopens an optional native plugin.
 */

#include "aroma.h"
#include "aroma_package.h"
#include "aroma_incense_loader.h"
#include <stdbool.h>
#include <stddef.h>

#define PACKAGE_MAX_INSTALLED 32

typedef struct
{
    AromaPackageManifest manifest;
    char dir[AROMA_PACKAGE_PATH_MAX];

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

/* Rescan the packages directory (picks up manual changes). */
bool package_manager_scan(void);

const char *package_manager_dir(void);
const char *package_manager_last_installed_id(void);
int package_manager_count(void);
InstalledPackage *package_manager_get(int index);
InstalledPackage *package_manager_find(const char *id);

/* Install an .apak file. err_buf describes the failure. Reinstalling the
 * same version is allowed, upgrades replace the directory (a loaded
 * package is torn down live first, then re-instantiated by the host via
 * vehicle_view_add_package_card). Downgrades are refused. MUST run on the
 * UI thread: a live update destroys nodes and stops animations. */
bool package_manager_install_apak(const char *apak_path,
                                  char *err_buf, size_t err_buf_len);

/* Remove an installed package by id. Performs a full teardown first
 * (animations stopped, UI trees destroyed, plugin unloaded), so unlike
 * older builds no restart is needed and no callback can dangle. */
bool package_manager_uninstall(const char *id,
                               char *err_buf, size_t err_buf_len);

/* Build the runtime UI for a package: app root card under ui_parent, mount
 * ui.aroma (if its entry file exists), load + init the native plugin (if
 * any). Safe to call once per package; later calls are no-ops. */
bool package_manager_instantiate(InstalledPackage *pkg, AromaNode *ui_parent,
                                 const AromaPackageHost *host,
                                 char *err_buf, size_t err_buf_len);

bool package_manager_show(InstalledPackage *pkg);
void package_manager_hide(InstalledPackage *pkg);
void package_manager_update_all(void);

/* Close-button callback for package app roots (implemented by the host UI;
 * wired here so every package gets the same dismiss behavior). */
void package_manager_close_cb(void *user_data);

#endif
