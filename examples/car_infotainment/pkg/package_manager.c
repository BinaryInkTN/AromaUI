#include "package_manager.h"
#include "aroma_animation.h"
#include "aroma_incense_loader.h"
#include "aroma_material_icons.h"
#include "vehicle_view.h"
#include "apps/media/media_controls.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static InstalledPackage *s_packages[PACKAGE_MAX_INSTALLED];
static int s_package_count = 0;
/* The ONLY source of truth: .apak files live here, nothing else. */
static char s_assets_dir[AROMA_PACKAGE_PATH_MAX] = "";
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

static bool same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    if (!a || !b || stat(a, &sa) != 0 || stat(b, &sb) != 0)
        return false;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static bool mkdir_p(const char *path)
{
    char tmp[AROMA_PACKAGE_PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path ? path : "");
    size_t len = strlen(tmp);
    if (len == 0)
        return false;
    if (tmp[len - 1] == '/')
        tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return false;
    return is_dir(tmp);
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

static bool copy_file(const char *src, const char *dst,
                      char *err_buf, size_t err_buf_len)
{
    FILE *in = fopen(src, "rb");
    if (!in)
    {
        set_err(err_buf, err_buf_len, "cannot read .apak file");
        return false;
    }
    /* Write aside + rename so a failed copy never leaves a half .apak. */
    char tmp[AROMA_PACKAGE_PATH_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", dst) >= (int)sizeof(tmp))
    {
        fclose(in);
        set_err(err_buf, err_buf_len, "install path too long");
        return false;
    }
    FILE *out = fopen(tmp, "wb");
    if (!out)
    {
        fclose(in);
        set_err(err_buf, err_buf_len, "cannot write into assets folder");
        return false;
    }
    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        if (fwrite(buf, 1, n, out) != n)
        {
            ok = false;
            break;
        }
    }
    if (ferror(in))
        ok = false;
    fclose(in);
    if (fclose(out) != 0)
        ok = false;
    if (!ok)
    {
        remove(tmp);
        set_err(err_buf, err_buf_len, "failed to copy .apak into assets");
        return false;
    }
    if (rename(tmp, dst) != 0)
    {
        remove(tmp);
        set_err(err_buf, err_buf_len, "cannot install .apak into assets");
        return false;
    }
    return true;
}

static void resolve_assets_dir(void)
{
    const char *env = getenv("AROMA_ASSETS_DIR");
    if (env && env[0] && is_dir(env))
    {
        snprintf(s_assets_dir, sizeof(s_assets_dir), "%s", env);
        return;
    }
#if defined(__arm__) || defined(__aarch64__)
    /* On ARM targets the first-party .apaks are deployed (via
     * `cmake --install --prefix /usr`) to /usr/share/infotainment/assets,
     * so prefer that location over any working-directory ./assets copy
     * (which may be stale or wrong-arch). */
    static const char *candidates[] = {
        "/usr/share/infotainment/assets",
        "assets",
        "../assets",
        "examples/car_infotainment/assets",
        "/assets",
        NULL,
    };
#else
    static const char *candidates[] = {
        "assets",
        "../assets",
        "examples/car_infotainment/assets",
        "/assets",
        "/usr/share/infotainment/assets",
        NULL,
    };
#endif
    for (int i = 0; candidates[i]; i++)
    {
        if (is_dir(candidates[i]))
        {
            snprintf(s_assets_dir, sizeof(s_assets_dir), "%s", candidates[i]);
            return;
        }
    }
    /* No creation: without an assets folder there is simply nothing
     * installed. The sideload UI reports this on install attempts. */
    s_assets_dir[0] = '\0';
}

/* Disposable runtime cache for native packages (plugin.so + bundled
 * assets need real files for dlopen/asset paths). Derived purely from
 * the .apak, validated on every load, safe to wipe any time: the assets
 * folder stays the only source of truth. */
static void cache_root_path(char *out, size_t out_len)
{
    if (!out || out_len == 0)
        return;
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0])
    {
        snprintf(out, out_len, "%s/aroma/packages", xdg);
        return;
    }
    const char *home = getenv("HOME");
    if (home && home[0])
    {
        snprintf(out, out_len, "%s/.cache/aroma/packages", home);
        return;
    }
    snprintf(out, out_len, "/tmp/aroma_packages");
}

static void package_cache_dir(const char *id, char *out, size_t out_len)
{
    if (!out || out_len == 0)
        return;
    out[0] = '\0';
    if (!id || !id[0])
        return;
    char root[AROMA_PACKAGE_PATH_MAX];
    cache_root_path(root, sizeof(root));
    if (!root[0])
        return;
    aroma_package_join_path(out, out_len, root, id);
}

static void drop_package_cache(const char *id)
{
    char dir[AROMA_PACKAGE_PATH_MAX];
    package_cache_dir(id, dir, sizeof(dir));
    if (!dir[0] || !is_dir(dir))
        return;
    /* Best effort: a stale cache is harmless (validated on load). */
    if (rm_rf(dir) != 0)
        fprintf(stderr, "[packages] warning: cannot drop cache '%s'\n", dir);
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

/* The assets folder is the only source of truth: installed packages
 * are exactly the *.apak files found here. Manifests are read straight
 * out of each archive; nothing is extracted or copied at scan time. */
bool package_manager_scan(void)
{
    if (!s_assets_dir[0])
    {
        /* No assets folder: nothing installed (not an error). */
        for (int i = 0; i < s_package_count;)
        {
            teardown_package(s_packages[i]);
            free(s_packages[i]);
            for (int k = i; k < s_package_count - 1; k++)
                s_packages[k] = s_packages[k + 1];
            s_packages[--s_package_count] = NULL;
        }
        return true;
    }

    AromaPackageManifest found_manifests[PACKAGE_MAX_INSTALLED];
    char found_apaks[PACKAGE_MAX_INSTALLED][AROMA_PACKAGE_PATH_MAX];
    int found_count = 0;

    DIR *d = opendir(s_assets_dir);
    if (!d)
        return false;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && found_count < PACKAGE_MAX_INSTALLED)
    {
        size_t n = strlen(ent->d_name);
        if (n < 6 || strcmp(ent->d_name + n - 5, ".apak") != 0)
            continue;
        char apak[AROMA_PACKAGE_PATH_MAX];
        if (!aroma_package_join_path(apak, sizeof(apak),
                                     s_assets_dir, ent->d_name))
            continue;
        struct stat st;
        if (stat(apak, &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        char *manifest_json = NULL;
        if (!aroma_apak_read_file(apak, "manifest.json", &manifest_json,
                                  NULL, NULL, 0))
        {
            fprintf(stderr, "[packages] ignoring '%s': no manifest.json\n",
                    apak);
            continue;
        }
        AromaPackageManifest m;
        char err[256];
        if (!aroma_package_parse_manifest(manifest_json, &m, err, sizeof(err)))
        {
            fprintf(stderr, "[packages] ignoring '%s': %s\n", apak, err);
            free(manifest_json);
            continue;
        }
        free(manifest_json);
        /* One file per id wins deterministically: highest version_code,
         * ties broken by first filename seen. */
        int dup = -1;
        for (int j = 0; j < found_count; j++)
        {
            if (strcmp(found_manifests[j].id, m.id) == 0)
            {
                dup = j;
                break;
            }
        }
        if (dup >= 0)
        {
            if (m.version_code > found_manifests[dup].version_code)
            {
                fprintf(stderr, "[packages] duplicate id %s: preferring %s\n",
                        m.id, apak);
                found_manifests[dup] = m;
                snprintf(found_apaks[dup], sizeof(found_apaks[dup]),
                         "%s", apak);
            }
            else
            {
                fprintf(stderr, "[packages] duplicate id %s: ignoring %s\n",
                        m.id, apak);
            }
            continue;
        }
        found_manifests[found_count] = m;
        snprintf(found_apaks[found_count], sizeof(found_apaks[found_count]),
                 "%s", apak);
        found_count++;
    }
    closedir(d);

    /* Drop records whose .apak is gone (deleted from assets). */
    for (int i = 0; i < s_package_count;)
    {
        bool still_there = false;
        for (int j = 0; j < found_count; j++)
        {
            if (strcmp(s_packages[i]->manifest.id,
                       found_manifests[j].id) == 0 &&
                strcmp(s_packages[i]->apak_path, found_apaks[j]) == 0)
            {
                still_there = true;
                break;
            }
        }
        if (!still_there)
        {
            teardown_package(s_packages[i]);
            drop_package_cache(s_packages[i]->manifest.id);
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

    for (int j = 0; j < found_count; j++)
    {
        int idx = find_index_by_id(found_manifests[j].id);
        if (idx >= 0)
        {
            /* Same id, different file (upgrade/downgrade on disk):
             * retire the loaded tree so it rebuilds from the new file. */
            if (strcmp(s_packages[idx]->apak_path, found_apaks[j]) != 0)
            {
                teardown_package(s_packages[idx]);
                drop_package_cache(s_packages[idx]->manifest.id);
                s_packages[idx]->rundir[0] = '\0';
            }
            s_packages[idx]->manifest = found_manifests[j];
            snprintf(s_packages[idx]->apak_path,
                     sizeof(s_packages[idx]->apak_path),
                     "%s", found_apaks[j]);
        }
        else if (s_package_count < PACKAGE_MAX_INSTALLED)
        {
            InstalledPackage *slot = calloc(1, sizeof(*slot));
            if (!slot)
                continue;
            slot->manifest = found_manifests[j];
            snprintf(slot->apak_path, sizeof(slot->apak_path),
                     "%s", found_apaks[j]);
            s_packages[s_package_count++] = slot;
        }
    }

    qsort(s_packages, (size_t)s_package_count, sizeof(s_packages[0]),
          compare_by_id);

    /* Purge cache dirs with no corresponding .apak (fully managed dir). */
    {
        char root[AROMA_PACKAGE_PATH_MAX];
        cache_root_path(root, sizeof(root));
        DIR *cd = root[0] ? opendir(root) : NULL;
        if (cd)
        {
            struct dirent *cent;
            while ((cent = readdir(cd)) != NULL)
            {
                if (cent->d_name[0] == '.')
                    continue;
                if (find_index_by_id(cent->d_name) < 0)
                {
                    char stale[AROMA_PACKAGE_PATH_MAX];
                    if (aroma_package_join_path(stale, sizeof(stale),
                                                root, cent->d_name))
                        rm_rf(stale);
                }
            }
            closedir(cd);
        }
    }
    return true;
}

bool package_manager_init(void)
{
    for (int i = 0; i < PACKAGE_MAX_INSTALLED; i++)
        s_packages[i] = NULL;
    s_package_count = 0;
    resolve_assets_dir();
    fprintf(stderr, "[packages] assets: %s\n",
            s_assets_dir[0] ? s_assets_dir : "(none)");
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
    return s_assets_dir;
}

const char *package_manager_last_installed_id(void)
{
    return s_last_installed[0] ? s_last_installed : NULL;
}

bool package_manager_install_time(const InstalledPackage *pkg,
                                  time_t *out_mtime)
{
    if (!pkg || !pkg->apak_path[0])
        return false;
    struct stat st;
    if (stat(pkg->apak_path, &st) != 0)
        return false;
    if (out_mtime)
        *out_mtime = st.st_mtime;
    return true;
}

bool package_manager_format_install_time(const InstalledPackage *pkg,
                                         char *out, size_t out_len)
{
    if (!out || out_len == 0)
        return false;
    time_t t = 0;
    if (!package_manager_install_time(pkg, &t))
    {
        snprintf(out, out_len, "unknown");
        return false;
    }
    struct tm tm_buf;
    if (!localtime_r(&t, &tm_buf))
    {
        snprintf(out, out_len, "unknown");
        return false;
    }
    if (strftime(out, out_len, "%Y-%m-%d %H:%M:%S", &tm_buf) == 0)
    {
        snprintf(out, out_len, "unknown");
        return false;
    }
    return true;
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

void *package_manager_symbol(const char *id, const char *sym)
{
    if (!id || !sym)
        return NULL;
    InstalledPackage *pkg = package_manager_find(id);
    if (!pkg || !pkg->loaded || !pkg->dl_handle)
        return NULL;
    dlerror();
    void *addr = dlsym(pkg->dl_handle, sym);
    return addr;
}

bool package_manager_is_system(const char *id)
{
    static const char prefix[] = "com.aroma.";
    return id && strncmp(id, prefix, sizeof(prefix) - 1) == 0;
}

bool package_manager_install_apak(const char *apak_path,
                                  char *err_buf, size_t err_buf_len)
{
    if (!apak_path || !s_assets_dir[0])
    {
        set_err(err_buf, err_buf_len,
                s_assets_dir[0] ? "invalid arguments"
                                : "no assets folder: cannot install packages");
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

    /* The package must carry something loadable: its UI markup and/or
     * its native plugin, checked inside the archive (nothing extracted). */
    bool has_entry = m.entry[0] && aroma_apak_contains(apak_path, m.entry);
    bool has_plugin = m.plugin[0] && aroma_apak_contains(apak_path, m.plugin);
    if (!has_entry && !has_plugin)
    {
        set_err(err_buf, err_buf_len,
                "package has neither its entry UI nor its plugin file");
        return false;
    }

    /* Install = the .apak file lives in assets, named <id>.apak. */
    char dest[AROMA_PACKAGE_PATH_MAX];
    {
        char fname[AROMA_PACKAGE_ID_MAX + 8];
        snprintf(fname, sizeof(fname), "%s.apak", m.id);
        if (!aroma_package_join_path(dest, sizeof(dest), s_assets_dir, fname))
        {
            set_err(err_buf, err_buf_len, "install path too long");
            return false;
        }
    }

    InstalledPackage *resident = package_manager_find(m.id);
    if (resident && resident->manifest.version_code > m.version_code)
    {
        set_err(err_buf, err_buf_len,
                "a newer version is already installed (downgrade refused)");
        return false;
    }

    /* Already in place: just refresh the in-memory record. */
    if (resident && same_file(apak_path, resident->apak_path) &&
        strcmp(resident->apak_path, dest) == 0)
    {
        if (!package_manager_scan())
        {
            set_err(err_buf, err_buf_len, "installed but rescan failed");
            return false;
        }
        snprintf(s_last_installed, sizeof(s_last_installed), "%s", m.id);
        fprintf(stderr, "[packages] installed %s %s\n", m.id, m.version);
        return true;
    }

    if (resident && resident->loaded)
    {
        fprintf(stderr, "[packages] live-updating %s: %s -> %s\n",
                m.id, resident->manifest.version, m.version);
        teardown_package(resident);
    }

    if (!copy_file(apak_path, dest, err_buf, err_buf_len))
        return false;

    /* A same-id file under a different name is now stale: the <id>.apak
     * copy above is the package. (Atomic copy first, so a failed copy
     * never loses the previous file.) */
    if (resident && strcmp(resident->apak_path, dest) != 0)
    {
        if (unlink(resident->apak_path) != 0 && errno != ENOENT)
            fprintf(stderr, "[packages] warning: cannot remove stale '%s'\n",
                    resident->apak_path);
    }

    if (!package_manager_scan())
    {
        set_err(err_buf, err_buf_len, "installed but rescan failed");
        return false;
    }
    /* The scan above drops the stale same-id record (different file) and
     * picks up the new one; its cache is rebuilt on next load. */
    if (resident)
        drop_package_cache(m.id);
    snprintf(s_last_installed, sizeof(s_last_installed), "%s", m.id);
    fprintf(stderr, "[packages] installed %s %s\n", m.id, m.version);
    return true;
}

bool package_manager_uninstall(const char *id,
                               char *err_buf, size_t err_buf_len)
{
    if (!id || !id[0])
    {
        set_err(err_buf, err_buf_len, "package is not installed");
        return false;
    }
    int idx = find_index_by_id(id);
    if (idx < 0)
    {
        set_err(err_buf, err_buf_len, "package is not installed");
        return false;
    }
    /* Uninstall = the .apak leaves the assets folder. Nothing else to do:
     * no folders to delete, nothing to remember. */
    teardown_package(s_packages[idx]);
    if (unlink(s_packages[idx]->apak_path) != 0 && errno != ENOENT)
    {
        fprintf(stderr, "[packages] warning: cannot remove package file '%s'\n",
                s_packages[idx]->apak_path);
        (void)err_buf;
        (void)err_buf_len;
    }
    drop_package_cache(id);
    free(s_packages[idx]);
    for (int k = idx; k < s_package_count - 1; k++)
        s_packages[k] = s_packages[k + 1];
    s_packages[--s_package_count] = NULL;
    fprintf(stderr, "[packages] uninstalled %s\n", id);
    return true;
}

/* Validated runtime cache for a native package: extracts the .apak to
 * a disposable dir (plugin.so needs a real file for dlopen, and bundled
 * assets need real paths) and reuses it while the .apak is unchanged.
 * Pure-UI packages never touch the filesystem. */
static bool ensure_package_rundir(InstalledPackage *pkg,
                                  char *err_buf, size_t err_buf_len)
{
    if (!pkg || !pkg->manifest.plugin[0])
        return true;
    if (pkg->rundir[0] && is_dir(pkg->rundir))
    {
        struct stat st;
        char marker[AROMA_PACKAGE_PATH_MAX];
        char expect[64];
        if (stat(pkg->apak_path, &st) == 0 &&
            aroma_package_join_path(marker, sizeof(marker),
                                    pkg->rundir, ".src") &&
            snprintf(expect, sizeof(expect), "%lld %ld",
                     (long long)st.st_size, (long)st.st_mtime) > 0)
        {
            char *saved = NULL;
            size_t saved_len = 0;
            if (aroma_package_read_file(marker, &saved, &saved_len) && saved)
            {
                bool fresh = strcmp(saved, expect) == 0;
                free(saved);
                if (fresh)
                    return true;
            }
        }
    }
    char dir[AROMA_PACKAGE_PATH_MAX];
    package_cache_dir(pkg->manifest.id, dir, sizeof(dir));
    if (!dir[0])
    {
        set_err(err_buf, err_buf_len, "cannot resolve package cache dir");
        return false;
    }
    rm_rf(dir);
    if (!mkdir_p(dir))
    {
        set_err(err_buf, err_buf_len, "cannot create package cache dir");
        return false;
    }
    if (!aroma_apak_extract(pkg->apak_path, dir, err_buf, err_buf_len))
    {
        rm_rf(dir);
        return false;
    }
    struct stat st;
    if (stat(pkg->apak_path, &st) == 0)
    {
        char marker[AROMA_PACKAGE_PATH_MAX];
        if (aroma_package_join_path(marker, sizeof(marker), dir, ".src"))
        {
            FILE *f = fopen(marker, "w");
            if (f)
            {
                fprintf(f, "%lld %ld\n",
                        (long long)st.st_size, (long)st.st_mtime);
                fclose(f);
            }
        }
    }
    snprintf(pkg->rundir, sizeof(pkg->rundir), "%s", dir);
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

    bool has_plugin = pkg->manifest.plugin[0] != '\0';
    if (has_plugin && !ensure_package_rundir(pkg, err_buf, err_buf_len))
    {
        __destroy_node_tree(root);
        pkg->app_root = NULL;
        return false;
    }

    if (pkg->manifest.entry[0])
    {
        AromaFont *ui_font = host ? host->ui_font : NULL;
        AromaFont *icon_font = host ? host->icon_font : NULL;
        bool mounted = false;
        bool present = false;
        if (has_plugin)
        {
            /* Native package: UI loads from the validated cache dir, so
             * relative @embed/@include refs keep working. */
            char entry_path[AROMA_PACKAGE_PATH_MAX];
            if (aroma_package_join_path(entry_path, sizeof(entry_path),
                                        pkg->rundir, pkg->manifest.entry) &&
                file_exists(entry_path))
            {
                present = true;
                mounted = IncenseLoadFileIntoParent(entry_path, root, ui_font,
                                                   icon_font, &pkg->registry);
            }
        }
        else
        {
            /* Pure-UI package: markup reads straight out of the .apak. */
            char *source = NULL;
            if (aroma_apak_read_file(pkg->apak_path, pkg->manifest.entry,
                                     &source, NULL, NULL, 0) && source)
            {
                present = true;
                mounted = IncenseLoadStringIntoParent(source, root, ui_font,
                                                     icon_font, &pkg->registry);
                free(source);
            }
        }
        if (present && !mounted)
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
        if (mounted)
        {
            fprintf(stderr, "[packages] mounted %s UI: %llu top-level node(s)\n",
                    pkg->manifest.id,
                    (unsigned long long)root->child_count);
        }
    }

    if (has_plugin)
    {
        char so_path[AROMA_PACKAGE_PATH_MAX];
        if (!aroma_package_join_path(so_path, sizeof(so_path),
                                     pkg->rundir, pkg->manifest.plugin) ||
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
            if (!hooks->init(&pkg->manifest, pkg->rundir, host, root))
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
