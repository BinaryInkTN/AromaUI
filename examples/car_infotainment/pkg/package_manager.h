#ifndef PACKAGE_MANAGER_H
#define PACKAGE_MANAGER_H

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

bool package_manager_scan(void);

bool package_manager_seed_from_assets(void);

const char *package_manager_dir(void);
const char *package_manager_last_installed_id(void);
int package_manager_count(void);
InstalledPackage *package_manager_get(int index);
InstalledPackage *package_manager_find(const char *id);

void *package_manager_symbol(const char *id, const char *sym);

bool package_manager_is_system(const char *id);

bool package_manager_install_apak(const char *apak_path,
                                  char *err_buf, size_t err_buf_len);

bool package_manager_uninstall(const char *id,
                               char *err_buf, size_t err_buf_len);

bool package_manager_instantiate(InstalledPackage *pkg, AromaNode *ui_parent,
                                 const AromaPackageHost *host,
                                 char *err_buf, size_t err_buf_len);

bool package_manager_show(InstalledPackage *pkg);
void package_manager_hide(InstalledPackage *pkg);
void package_manager_update_all(void);

void package_manager_close_cb(void *user_data);

#endif
