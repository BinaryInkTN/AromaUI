#ifndef AROMA_PACKAGE_H
#define AROMA_PACKAGE_H



















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
    char id[AROMA_PACKAGE_ID_MAX];
    char name[AROMA_PACKAGE_NAME_MAX];
    char version[AROMA_PACKAGE_VERSION_MAX];
    int version_code;
    char icon[AROMA_PACKAGE_ICON_MAX];
    char author[AROMA_PACKAGE_AUTHOR_MAX];
    char description[AROMA_PACKAGE_DESC_MAX];
    char entry[AROMA_PACKAGE_ENTRY_MAX];
    char plugin[AROMA_PACKAGE_ENTRY_MAX];
    char chrome[16];
    int min_abi;

    char category[AROMA_PACKAGE_CATEGORY_MAX];
    float rating;
    int rating_count;
    long downloads;
    bool featured;
} AromaPackageManifest;



bool aroma_package_parse_manifest(const char *json,
                                  AromaPackageManifest *out_manifest,
                                  char *err_buf, size_t err_buf_len);


bool aroma_package_load_manifest_file(const char *path,
                                      AromaPackageManifest *out_manifest,
                                      char *err_buf, size_t err_buf_len);


bool aroma_package_validate_manifest(const AromaPackageManifest *manifest,
                                     char *err_buf, size_t err_buf_len);


int aroma_package_compare_versions(const char *a, const char *b);



bool aroma_package_read_file(const char *path, char **out_buf, size_t *out_len);


bool aroma_package_join_path(char *out, size_t out_len,
                             const char *dir, const char *name);









bool aroma_apak_read_file(const char *apak_path, const char *inner_name,
                          char **out_buf, size_t *out_len,
                          char *err_buf, size_t err_buf_len);



bool aroma_apak_contains(const char *apak_path, const char *inner_name);



bool aroma_apak_extract(const char *apak_path, const char *dest_dir,
                        char *err_buf, size_t err_buf_len);







struct AromaNode;
typedef struct AromaFont AromaFont;


typedef struct
{
    AromaFont *ui_font;
    AromaFont *icon_font;
    AromaFont *settings_font;
    int screen_w;
    int screen_h;
} AromaPackageHost;

typedef struct
{
    const char *id;
    const char *name;
    const char *version;
} AromaPackageInfo;

typedef struct
{



    bool (*init)(const AromaPackageManifest *manifest,
                 const char *install_dir,
                 const AromaPackageHost *host,
                 struct AromaNode *app_root);




    bool (*build_ui)(struct AromaNode *app_root);
    bool (*show)(struct AromaNode *app_root);
    void (*hide)(struct AromaNode *app_root);
    void (*update)(struct AromaNode *app_root);






    void (*destroy)(void);
} AromaPackageHooks;

#define AROMA_PACKAGE_ENTRY_SYMBOL "aroma_package_entry"
typedef const AromaPackageHooks *(*AromaPackageEntryFn)(void);





bool aroma_package_native_load(const char *so_path,
                               const AromaPackageHooks **out_hooks,
                               void **out_handle,
                               char *err_buf, size_t err_buf_len);

void aroma_package_native_unload(void *handle);

#ifdef __cplusplus
}
#endif

#endif
