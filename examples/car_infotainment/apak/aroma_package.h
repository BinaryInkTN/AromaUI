#ifndef AROMA_PACKAGE_H
#define AROMA_PACKAGE_H

/*
 * AromaUI Package System (.apak)
 *
 * A package is a distributable bundle (Android-APK style) containing:
 *   manifest.json   - package metadata (required)
 *   ui.aroma        - Incense UI markup, loaded into the host app (optional
 *                     if a native plugin builds its own UI)
 *   assets/...      - images, data files referenced by the UI (optional)
 *   plugin.so       - native code plugin, loaded with dlopen (optional,
 *                     native targets only - not available on Emscripten)
 *
 * Developers build a package directory and pack it with tools/apak.py:
 *   python3 tools/apak.py pack examples/package_examples/calculator -o calc.apak
 *
 * The host (e.g. car infotainment) installs .apak files into a packages
 * directory and loads each installed package at startup or at runtime.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AROMA_PACKAGE_ABI_VERSION 1

#define AROMA_PACKAGE_ID_MAX 128
#define AROMA_PACKAGE_NAME_MAX 128
#define AROMA_PACKAGE_VERSION_MAX 32
#define AROMA_PACKAGE_ICON_MAX 64
#define AROMA_PACKAGE_AUTHOR_MAX 128
#define AROMA_PACKAGE_DESC_MAX 512
#define AROMA_PACKAGE_PATH_MAX 1024
#define AROMA_PACKAGE_ENTRY_MAX 256
#define AROMA_PACKAGE_CATEGORY_MAX 64

typedef struct
{
    char id[AROMA_PACKAGE_ID_MAX];           /* required: reverse-dns, e.g. "com.example.calc" */
    char name[AROMA_PACKAGE_NAME_MAX];       /* required: display name */
    char version[AROMA_PACKAGE_VERSION_MAX]; /* required: "1.0.0" */
    int version_code;                        /* required: integer, must grow on update */
    char icon[AROMA_PACKAGE_ICON_MAX];       /* optional: "AROMA_ICON_CALCULATE", default CALCULATE */
    char author[AROMA_PACKAGE_AUTHOR_MAX];   /* optional */
    char description[AROMA_PACKAGE_DESC_MAX];/* optional */
    char entry[AROMA_PACKAGE_ENTRY_MAX];     /* optional: UI file, default "ui.aroma" */
    char plugin[AROMA_PACKAGE_ENTRY_MAX];    /* optional: native lib, e.g. "plugin.so" */
    char chrome[16];                         /* "host" (default) or "self" */
    int min_abi;                             /* optional: default 1 */
    /* --- Play-store style metadata (all optional, defaults below) --- */
    char category[AROMA_PACKAGE_CATEGORY_MAX]; /* e.g. "Games", "Apps", default "Apps" */
    float rating;                            /* 0.0-5.0, default 0 (unrated) */
    int rating_count;                        /* number of ratings, default 0 */
    long downloads;                          /* install count, default 0 */
    bool featured;                           /* show in Featured carousel */
} AromaPackageManifest;

/* Parse + validate a manifest from a JSON string. err_buf (optional) gets a
 * human readable reason on failure. */
bool aroma_package_parse_manifest(const char *json,
                                  AromaPackageManifest *out_manifest,
                                  char *err_buf, size_t err_buf_len);

/* Same, but reads manifest.json from a file path. */
bool aroma_package_load_manifest_file(const char *path,
                                      AromaPackageManifest *out_manifest,
                                      char *err_buf, size_t err_buf_len);

/* Validate an already-filled manifest (id format, required fields...). */
bool aroma_package_validate_manifest(const AromaPackageManifest *manifest,
                                     char *err_buf, size_t err_buf_len);

/* Compare dotted versions: -1 if a<b, 0 if equal, 1 if a>b. */
int aroma_package_compare_versions(const char *a, const char *b);

/* Read an entire file into memory (NUL-terminated). Caller frees *out_buf.
 * Returns false on error. */
bool aroma_package_read_file(const char *path, char **out_buf, size_t *out_len);

/* Join dir + "/" + name into out (size-checked). */
bool aroma_package_join_path(char *out, size_t out_len,
                             const char *dir, const char *name);

/* --- .apak (zip) helpers -------------------------------------------------
 * Minimal zip reader: stored + deflated entries, no encryption/multi-disk.
 * Used to inspect/install .apak bundles without extra dependencies
 * (requires zlib; AROMA_HAS_ZLIB is set by the build when available).
 */

/* Read a single file from a zip archive into memory (NUL-terminated).
 * Caller frees *out_buf. */
bool aroma_apak_read_file(const char *apak_path, const char *inner_name,
                          char **out_buf, size_t *out_len,
                          char *err_buf, size_t err_buf_len);

/* Extract every entry of a zip into dest_dir. Refuses absolute paths and
 * ".." components. Creates directories as needed. */
bool aroma_apak_extract(const char *apak_path, const char *dest_dir,
                        char *err_buf, size_t err_buf_len);

/* --- Native plugin ABI ----------------------------------------------------
 * A plugin.so must export exactly one symbol:
 *   const AromaPackageHooks *aroma_package_entry(void);
 * The host checks abi_version == AROMA_PACKAGE_ABI_VERSION before use.
 */

struct AromaNode;
typedef struct AromaFont AromaFont;

/* What the host provides to a native plugin at init time. */
typedef struct
{
    AromaFont *ui_font;
    AromaFont *icon_font;
    AromaFont *settings_font; /* may be NULL; fall back to ui_font */
    int screen_w;
    int screen_h;
} AromaPackageHost;

typedef struct
{
    const char *id;      /* must match manifest id */
    const char *name;    /* display name override (may be NULL) */
    const char *version; /* must match manifest version */
} AromaPackageInfo;

typedef struct
{
    /* Called once after install dir + host fonts are known. Must reset all
     * static state (node pointers, flags) so a reinstall in the same
     * process rebuilds from scratch instead of trusting stale globals. */
    bool (*init)(const AromaPackageManifest *manifest,
                 const char *install_dir,
                 const AromaPackageHost *host,
                 struct AromaNode *app_root);
    /* Build extra UI / wire callbacks. May be NULL (ui.aroma is enough).
     * Must rebuild for the given app_root even if called before: the host
     * destroys the previous tree on uninstall/update, so "already built"
     * early-returns on stale pointers produce a blank app. */
    bool (*build_ui)(struct AromaNode *app_root);
    bool (*show)(struct AromaNode *app_root);
    void (*hide)(struct AromaNode *app_root);
    void (*update)(struct AromaNode *app_root);
    /* Required for reinstall safety: stop worker threads (join before
     * returning - the host dlcloses the .so right after) and clear every
     * node pointer, including host globals (e.g. state fields) that point
     * into the dying tree. May be NULL only for stateless pure-UI plugins.
     * A missing destroy leaks threads across reinstalls (crash) and leaves
     * dangling pointers behind (blank app / use-after-free). */
    void (*destroy)(void);
} AromaPackageHooks;

#define AROMA_PACKAGE_ENTRY_SYMBOL "aroma_package_entry"
typedef const AromaPackageHooks *(*AromaPackageEntryFn)(void);

/* Load plugin.so via dlopen and resolve the entry symbol. On success the
 * caller owns *out_handle (pass to aroma_package_native_unload).
 * Always available; returns false with an error on platforms without
 * dynamic loading (e.g. Emscripten). */
bool aroma_package_native_load(const char *so_path,
                               const AromaPackageHooks **out_hooks,
                               void **out_handle,
                               char *err_buf, size_t err_buf_len);

void aroma_package_native_unload(void *handle);

#ifdef __cplusplus
}
#endif

#endif
