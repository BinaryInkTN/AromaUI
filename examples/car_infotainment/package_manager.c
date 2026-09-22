#include "package_manager.h"
#include "aroma_animation.h"
#include "aroma_incense_loader.h"
#include "aroma_material_icons.h"
#include "vehicle_view.h"
#include "apps/media/media_controls.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Heap-stable entries. Drawer buttons, close buttons and animation callbacks
 * keep raw InstalledPackage* pointers, so a rescan must never move live
 * structs (the old memcpy+qsort shuffled them, dangling every callback and
 * crashing - or mis-opening - the next tap). Entries are heap allocated
 * once, matched by id across rescans, and freed only by teardown below,
 * which first destroys the nodes holding those pointers. All animation
 * stops are synchronous on the single UI thread, so no callback can fire
 * with a freed pointer after teardown returns. */
static InstalledPackage *s_packages[PACKAGE_MAX_INSTALLED];
static int s_package_count = 0;
static char s_packages_dir[AROMA_PACKAGE_PATH_MAX] = "";
static char s_last_installed[AROMA_PACKAGE_ID_MAX] = "";

static void set_err(char *buf, size_t len, const char *msg)
{
    if (buf && len > 0)
        snprintf(buf, len, "%s", msg ? msg : "unknown error");
}

static bool is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void resolve_packages_dir(void)
{
    const char *env = getenv("AROMA_PACKAGES_DIR");
    if (env && env[0] && is_dir(env))
    {
        snprintf(s_packages_dir, sizeof(s_packages_dir), "%s", env);
        return;
    }
    static const char *candidates[] = {
        "packages",
        "../packages",
        "examples/car_infotainment/packages",
        "/usr/share/infotainment/packages",
        NULL,
    };
    for (int i = 0; candidates[i]; i++)
    {
        if (is_dir(candidates[i]))
        {
            snprintf(s_packages_dir, sizeof(s_packages_dir), "%s", candidates[i]);
            return;
        }
    }
    /* Nothing found: create ./packages so installs have a home. */
    mkdir("packages", 0755);
    snprintf(s_packages_dir, sizeof(s_packages_dir), "packages");
}

static int compare_by_id(const void *a, const void *b)
{
    const InstalledPackage *pa = *(const InstalledPackage *const *)a;
    const InstalledPackage *pb = *(const InstalledPackage *const *)b;
    return strcmp(pa->manifest.id, pb->manifest.id);
}

static int find_index_by_id(const char *id)
{
    if (!id)
        return -1;
    for (int i = 0; i < s_package_count; i++)
    {
        if (s_packages[i] && strcmp(s_packages[i]->manifest.id, id) == 0)
            return i;
    }
    return -1;
}

/* Stop (and drop) every animation targeting a node tree. Open/close
 * animations only ever target the two roots, but Incense widgets may own
 * their own - walk the whole tree so nothing can tick after teardown. */
static void stop_node_tree_anims(AromaNode *node)
{
    if (!node)
        return;
    aroma_animation_stop(node);
    aroma_animation_cleanup_node(node);
    for (uint64_t i = 0; i < node->child_count; i++)
    {
        if (node->child_nodes[i])
            stop_node_tree_anims(node->child_nodes[i]);
    }
}

/* Full runtime teardown of one entry: no file work, no rescans. Idempotent.
 * Stops animations, hides + destroys both UI trees, runs plugin destroy,
 * unloads the .so and frees the Incense registry. Mirrors the
 * package_closing_anim completion block when the package was shown so
 * global UI state (drawer, bottom bar, mini player) matches a closed app. */
static void teardown_package(InstalledPackage *pkg)
{
    if (!pkg || !pkg->loaded)
        return;
    bool was_shown = pkg->shown;
    package_manager_hide(pkg);
    if (pkg->app_root)
    {
        stop_node_tree_anims(pkg->app_root);
        aroma_node_set_hidden(pkg->app_root, true);
    }
    if (pkg->drawer_card)
    {
        stop_node_tree_anims(pkg->drawer_card);
        aroma_node_set_hidden(pkg->drawer_card, true);
    }
    pkg->shown = false;
    if (pkg->hooks && pkg->hooks->destroy)
        pkg->hooks->destroy();
    pkg->hooks = NULL;
    aroma_package_native_unload(pkg->dl_handle);
    pkg->dl_handle = NULL;
    if (pkg->registry)
    {
        IncenseFreeRegistry(pkg->registry);
        pkg->registry = NULL;
    }
    if (was_shown)
    {
        set_app_open(false);
        apply_deferred_bottom_bar_position();
        update_media_card_display();
        restore_app_drawer_from_behind();
    }
    /* Detach from parents via parent_node links, then free whole subtrees
     * (close_btn dies with app_root, drawer button with drawer_card). */
    if (pkg->app_root)
    {
        __destroy_node_tree(pkg->app_root);
        pkg->app_root = NULL;
    }
    if (pkg->drawer_card)
    {
        __destroy_node_tree(pkg->drawer_card);
        pkg->drawer_card = NULL;
    }
    pkg->close_btn = NULL;
    pkg->loaded = false;
}

bool package_manager_scan(void)
{
    if (!s_packages_dir[0])
        return false;

    /* Phase 1: read what's actually on disk. */
    AromaPackageManifest disk_manifests[PACKAGE_MAX_INSTALLED];
    char disk_dirs[PACKAGE_MAX_INSTALLED][AROMA_PACKAGE_PATH_MAX];
    int disk_count = 0;

    DIR *d = opendir(s_packages_dir);
    if (!d)
        return false;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && disk_count < PACKAGE_MAX_INSTALLED)
    {
        if (ent->d_name[0] == '.')
            continue;
        char subdir[AROMA_PACKAGE_PATH_MAX];
        if (!aroma_package_join_path(subdir, sizeof(subdir),
                                     s_packages_dir, ent->d_name))
            continue;
        if (!is_dir(subdir))
            continue;
        char manifest_path[AROMA_PACKAGE_PATH_MAX];
        if (!aroma_package_join_path(manifest_path, sizeof(manifest_path),
                                     subdir, "manifest.json"))
            continue;
        if (!file_exists(manifest_path))
            continue;
        AromaPackageManifest m;
        char err[256];
        if (!aroma_package_load_manifest_file(manifest_path, &m, err, sizeof(err)))
        {
            fprintf(stderr, "[packages] ignoring '%s': %s\n", subdir, err);
            continue;
        }
        /* The directory name should match the id's last component, but only
         * the manifest id is authoritative. */
        disk_manifests[disk_count] = m;
        snprintf(disk_dirs[disk_count], sizeof(disk_dirs[disk_count]),
                 "%s", subdir);
        disk_count++;
    }
    closedir(d);

    /* Phase 2: tear down + drop entries that vanished from disk. */
    for (int i = 0; i < s_package_count;)
    {
        bool still_there = false;
        for (int j = 0; j < disk_count; j++)
        {
            if (strcmp(s_packages[i]->manifest.id, disk_manifests[j].id) == 0)
            {
                still_there = true;
                break;
            }
        }
        if (!still_there)
        {
            teardown_package(s_packages[i]);
            free(s_packages[i]);
            for (int k = i; k < s_package_count - 1; k++)
                s_packages[k] = s_packages[k + 1];
            s_packages[--s_package_count] = NULL;
        }
        else
        {
            i++;
        }
    }

    /* Phase 3: adopt new entries, refresh manifests of kept ones. */
    for (int j = 0; j < disk_count; j++)
    {
        int idx = find_index_by_id(disk_manifests[j].id);
        if (idx >= 0)
        {
            s_packages[idx]->manifest = disk_manifests[j];
            snprintf(s_packages[idx]->dir, sizeof(s_packages[idx]->dir),
                     "%s", disk_dirs[j]);
        }
        else if (s_package_count < PACKAGE_MAX_INSTALLED)
        {
            InstalledPackage *slot = calloc(1, sizeof(*slot));
            if (!slot)
                continue;
            slot->manifest = disk_manifests[j];
            snprintf(slot->dir, sizeof(slot->dir), "%s", disk_dirs[j]);
            s_packages[s_package_count++] = slot;
        }
    }

    /* Phase 4: stable display order. Sorting pointers keeps every live
     * InstalledPackage address (and all drawer/anim/button references to
     * it) valid across rescans. */
    qsort(s_packages, (size_t)s_package_count, sizeof(s_packages[0]),
          compare_by_id);
    return true;
}

bool package_manager_init(void)
{
    for (int i = 0; i < PACKAGE_MAX_INSTALLED; i++)
        s_packages[i] = NULL;
    s_package_count = 0;
    resolve_packages_dir();
    fprintf(stderr, "[packages] directory: %s\n", s_packages_dir);
    return package_manager_scan();
}

void package_manager_shutdown(void)
{
    for (int i = 0; i < s_package_count; i++)
    {
        teardown_package(s_packages[i]);
        free(s_packages[i]);
        s_packages[i] = NULL;
    }
    s_package_count = 0;
}

const char *package_manager_dir(void)
{
    return s_packages_dir;
}

const char *package_manager_last_installed_id(void)
{
    return s_last_installed[0] ? s_last_installed : NULL;
}

int package_manager_count(void)
{
    return s_package_count;
}

InstalledPackage *package_manager_get(int index)
{
    if (index < 0 || index >= s_package_count)
        return NULL;
    return s_packages[index];
}

InstalledPackage *package_manager_find(const char *id)
{
    int idx = find_index_by_id(id);
    return idx >= 0 ? s_packages[idx] : NULL;
}

static int rm_rf(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0)
        return -1;
    if (!S_ISDIR(st.st_mode))
        return unlink(path);
    DIR *d = opendir(path);
    if (!d)
        return -1;
    struct dirent *ent;
    int rc = 0;
    while ((ent = readdir(d)) != NULL)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        char child[AROMA_PACKAGE_PATH_MAX];
        if (!aroma_package_join_path(child, sizeof(child), path, ent->d_name))
        {
            rc = -1;
            break;
        }
        if (rm_rf(child) != 0)
        {
            rc = -1;
            break;
        }
    }
    closedir(d);
    if (rc == 0 && rmdir(path) != 0)
        rc = -1;
    return rc;
}

bool package_manager_install_apak(const char *apak_path,
                                  char *err_buf, size_t err_buf_len)
{
    if (!apak_path || !s_packages_dir[0])
    {
        set_err(err_buf, err_buf_len, "package manager is not initialized");
        return false;
    }
    char *manifest_json = NULL;
    if (!aroma_apak_read_file(apak_path, "manifest.json", &manifest_json,
                              NULL, err_buf, err_buf_len))
    {
        if (err_buf && err_buf_len > 0 && !err_buf[0])
            set_err(err_buf, err_buf_len, "archive has no manifest.json");
        return false;
    }
    AromaPackageManifest m;
    if (!aroma_package_parse_manifest(manifest_json, &m, err_buf, err_buf_len))
    {
        free(manifest_json);
        return false;
    }
    free(manifest_json);

    if (m.min_abi > AROMA_PACKAGE_ABI_VERSION)
    {
        set_err(err_buf, err_buf_len, "package needs a newer host (min_abi)");
        return false;
    }

    /* Install directory is the full package id (docs: ./packages/<id>/).
     * A leaf-only name would collide between e.g. com.a.nav and com.b.nav
     * and would disagree with the preinstalled staging layout. */
    char dest[AROMA_PACKAGE_PATH_MAX];
    if (!aroma_package_join_path(dest, sizeof(dest), s_packages_dir, m.id))
    {
        set_err(err_buf, err_buf_len, "install path too long");
        return false;
    }

    if (is_dir(dest))
    {
        char old_manifest[AROMA_PACKAGE_PATH_MAX];
        int old_code = -1;
        if (aroma_package_join_path(old_manifest, sizeof(old_manifest),
                                    dest, "manifest.json"))
        {
            AromaPackageManifest old;
            if (aroma_package_load_manifest_file(old_manifest, &old, NULL, 0))
                old_code = old.version_code;
        }
        if (old_code >= 0 && m.version_code < old_code)
        {
            set_err(err_buf, err_buf_len,
                    "a newer version is already installed (downgrade refused)");
            return false;
        }
        /* Live update: a loaded package is torn down first (UI trees
         * destroyed, plugin unloaded, drawer chrome reset), then its files
         * are replaced below and the host re-instantiates it via
         * vehicle_view_add_package_card(). MUST run on the UI thread:
         * teardown destroys nodes and stops animations. */
        InstalledPackage *live = package_manager_find(m.id);
        if (live && live->loaded)
        {
            fprintf(stderr, "[packages] live-updating %s: %s -> %s\n",
                    m.id, live->manifest.version, m.version);
            teardown_package(live);
        }
        if (rm_rf(dest) != 0)
        {
            set_err(err_buf, err_buf_len, "cannot remove previous install");
            return false;
        }
    }

    if (!aroma_apak_extract(apak_path, dest, err_buf, err_buf_len))
        return false;

    /* Sanity: the bundle must contain its entry UI or a native plugin. */
    bool has_entry = false;
    bool has_plugin = false;
    if (m.entry[0])
    {
        char entry_path[AROMA_PACKAGE_PATH_MAX];
        if (aroma_package_join_path(entry_path, sizeof(entry_path),
                                    dest, m.entry) &&
            file_exists(entry_path))
            has_entry = true;
    }
    if (m.plugin[0])
    {
        char plugin_path[AROMA_PACKAGE_PATH_MAX];
        if (aroma_package_join_path(plugin_path, sizeof(plugin_path),
                                    dest, m.plugin) &&
            file_exists(plugin_path))
            has_plugin = true;
    }
    if (!has_entry && !has_plugin)
    {
        rm_rf(dest);
        set_err(err_buf, err_buf_len,
                "package has neither its entry UI nor its plugin file");
        return false;
    }

    if (!package_manager_scan())
    {
        set_err(err_buf, err_buf_len, "installed but rescan failed");
        return false;
    }
    snprintf(s_last_installed, sizeof(s_last_installed), "%s", m.id);
    fprintf(stderr, "[packages] installed %s %s\n", m.id, m.version);
    return true;
}

bool package_manager_uninstall(const char *id,
                               char *err_buf, size_t err_buf_len)
{
    int idx = find_index_by_id(id);
    if (idx < 0)
    {
        set_err(err_buf, err_buf_len, "package is not installed");
        return false;
    }
    char dir[AROMA_PACKAGE_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", s_packages[idx]->dir);
    /* Tear down runtime state FIRST (animations stopped, UI trees
     * destroyed, plugin destroy run + .so unloaded) while the install dir
     * still exists: hide/destroy hooks may read bundled assets. Only then
     * delete the files. The old order (rm before teardown) ran plugin code
     * against a deleted install dir and crashed / left dangling nodes. */
    teardown_package(s_packages[idx]);
    if (rm_rf(dir) != 0)
    {
        fprintf(stderr, "[packages] warning: cannot remove package files '%s'\n",
                dir);
        (void)err_buf;
        (void)err_buf_len;
    }
    /* Drop the entry. No rescan needed, so no other entry moves and no
     * callback dangles. */
    free(s_packages[idx]);
    for (int k = idx; k < s_package_count - 1; k++)
        s_packages[k] = s_packages[k + 1];
    s_packages[--s_package_count] = NULL;
    fprintf(stderr, "[packages] uninstalled %s\n", id);
    return true;
}

bool package_manager_instantiate(InstalledPackage *pkg, AromaNode *ui_parent,
                                 const AromaPackageHost *host,
                                 char *err_buf, size_t err_buf_len)
{
    if (!pkg || !ui_parent)
    {
        set_err(err_buf, err_buf_len, "invalid arguments");
        return false;
    }
    if (pkg->loaded)
        return true;

    AromaNode *root = aroma_ui_card(ui_parent, 0, 0,
                                    host && host->screen_w > 0 ? host->screen_w : 1024,
                                    host && host->screen_h > 0 ? host->screen_h : 600,
                                    CARD_TYPE_ELEVATED);
    if (!root)
    {
        set_err(err_buf, err_buf_len, "cannot create app root");
        return false;
    }
    aroma_node_set_hidden(root, true);
    pkg->app_root = root;
    /* From here on every failure path must release the half-built root
     * (plus any mounted children) so a broken package leaves no orphan
     * nodes behind. No drawer card exists yet, and nothing else can hold
     * this root (instantiate runs synchronously on the UI thread). */

    /* Mount the Incense UI when the entry file is present. A plugin-only
     * package simply has no entry file. */
    if (pkg->manifest.entry[0])
    {
        char entry_path[AROMA_PACKAGE_PATH_MAX];
        if (aroma_package_join_path(entry_path, sizeof(entry_path),
                                    pkg->dir, pkg->manifest.entry) &&
            file_exists(entry_path))
        {
            AromaFont *ui_font = host ? host->ui_font : NULL;
            AromaFont *icon_font = host ? host->icon_font : NULL;
            if (!IncenseLoadFileIntoParent(entry_path, root, ui_font,
                                           icon_font, &pkg->registry))
            {
                int n = 0;
                const IncenseError *errs = IncenseGetErrors(&n);
                if (err_buf && err_buf_len > 0)
                {
                    if (errs && n > 0)
                        snprintf(err_buf, err_buf_len, "UI error line %d: %s",
                                 errs[0].line, errs[0].message);
                    else
                        snprintf(err_buf, err_buf_len,
                                 "cannot load package UI");
                }
                aroma_node_set_hidden(root, true);
                if (pkg->registry)
                {
                    IncenseFreeRegistry(pkg->registry);
                    pkg->registry = NULL;
                }
                __destroy_node_tree(root);
                pkg->app_root = NULL;
                return false;
            }
            fprintf(stderr, "[packages] mounted %s UI: %llu top-level node(s)\n",
                    pkg->manifest.id,
                    (unsigned long long)root->child_count);
        }
    }

    /* Load the native plugin when declared. */
    if (pkg->manifest.plugin[0])
    {
        char so_path[AROMA_PACKAGE_PATH_MAX];
        if (!aroma_package_join_path(so_path, sizeof(so_path),
                                     pkg->dir, pkg->manifest.plugin) ||
            !file_exists(so_path))
        {
            set_err(err_buf, err_buf_len, "declared plugin file is missing");
            if (pkg->registry)
            {
                IncenseFreeRegistry(pkg->registry);
                pkg->registry = NULL;
            }
            __destroy_node_tree(root);
            pkg->app_root = NULL;
            return false;
        }
        const AromaPackageHooks *hooks = NULL;
        void *handle = NULL;
        if (!aroma_package_native_load(so_path, &hooks, &handle,
                                       err_buf, err_buf_len))
        {
            if (pkg->registry)
            {
                IncenseFreeRegistry(pkg->registry);
                pkg->registry = NULL;
            }
            __destroy_node_tree(root);
            pkg->app_root = NULL;
            return false;
        }
        pkg->hooks = hooks;
        pkg->dl_handle = handle;
        if (hooks->init)
        {
            if (!hooks->init(&pkg->manifest, pkg->dir, host, root))
            {
                set_err(err_buf, err_buf_len, "plugin init failed");
                aroma_package_native_unload(handle);
                pkg->hooks = NULL;
                pkg->dl_handle = NULL;
                if (pkg->registry)
                {
                    IncenseFreeRegistry(pkg->registry);
                    pkg->registry = NULL;
                }
                __destroy_node_tree(root);
                pkg->app_root = NULL;
                return false;
            }
        }
        if (hooks->build_ui)
        {
            if (!hooks->build_ui(root))
            {
                set_err(err_buf, err_buf_len, "plugin build_ui failed");
                if (hooks->destroy)
                    hooks->destroy();
                aroma_package_native_unload(handle);
                pkg->hooks = NULL;
                pkg->dl_handle = NULL;
                if (pkg->registry)
                {
                    IncenseFreeRegistry(pkg->registry);
                    pkg->registry = NULL;
                }
                __destroy_node_tree(root);
                pkg->app_root = NULL;
                return false;
            }
        }
    }

    /* Host-managed packages get a generic close button. Self-managed
     * plugins own their full chrome (their own close buttons). */
    bool self_managed = pkg->hooks && pkg->hooks->show &&
                        strcmp(pkg->manifest.chrome, "self") == 0;
    if (host && host->icon_font && !self_managed)
    {
        pkg->close_btn = aroma_ui_iconbutton(
            root, AROMA_ICON_CLOSE, 20, 20, 48, ICON_BUTTON_FILLED,
            package_manager_close_cb, pkg, host->icon_font);
        if (pkg->close_btn)
            aroma_node_set_hidden(pkg->close_btn, true);
    }

    pkg->loaded = true;
    return true;
}

bool package_manager_show(InstalledPackage *pkg)
{
    if (!pkg || !pkg->loaded || !pkg->app_root)
        return false;
    if (pkg->hooks && pkg->hooks->show)
    {
        if (!pkg->hooks->show(pkg->app_root))
            return false;
    }
    if (pkg->close_btn)
        aroma_node_set_hidden(pkg->close_btn, false);
    pkg->shown = true;
    return true;
}

void package_manager_hide(InstalledPackage *pkg)
{
    if (!pkg)
        return;
    if (pkg->hooks && pkg->hooks->hide)
        pkg->hooks->hide(pkg->app_root);
    if (pkg->close_btn)
        aroma_node_set_hidden(pkg->close_btn, true);
    pkg->shown = false;
}

void package_manager_update_all(void)
{
    for (int i = 0; i < s_package_count; i++)
    {
        InstalledPackage *pkg = s_packages[i];
        if (pkg->loaded && pkg->shown && pkg->hooks && pkg->hooks->update)
            pkg->hooks->update(pkg->app_root);
    }
}
