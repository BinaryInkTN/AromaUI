#include "aroma_package.h"

#ifdef AROMA_HAS_ZLIB
#include <zlib.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __EMSCRIPTEN__
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#endif

#define APAK_MAX_ENTRIES 4096
#define APAK_MAX_FILE_BYTES (256u * 1024u * 1024u)

static void set_err(char *buf, size_t len, const char *msg)
{
    if (buf && len > 0)
        snprintf(buf, len, "%s", msg ? msg : "unknown error");
}

#ifdef AROMA_HAS_ZLIB

static uint16_t read_u16(const unsigned char *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
}

typedef struct
{
    char *name;              /* NUL-terminated inner path */
    uint16_t method;         /* 0 = stored, 8 = deflated */
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint32_t crc;
    uint32_t local_offset;
} ApakEntry;

static void free_entries(ApakEntry *entries, size_t count)
{
    if (!entries)
        return;
    for (size_t i = 0; i < count; i++)
        free(entries[i].name);
    free(entries);
}

/* Reject absolute paths, drive letters and ".." components. */
static bool inner_path_safe(const char *name, size_t len)
{
    if (!name || len == 0 || len >= AROMA_PACKAGE_PATH_MAX)
        return false;
    if (name[0] == '/' || name[0] == '\\')
        return false;
    if (len > 1 && name[1] == ':')
        return false;
    size_t i = 0;
    while (i < len)
    {
        size_t j = i;
        while (j < len && name[j] != '/' && name[j] != '\\')
            j++;
        size_t part_len = j - i;
        if (part_len == 2 && name[i] == '.' && name[i + 1] == '.')
            return false;
        i = (j < len) ? j + 1 : j;
    }
    return true;
}

static bool entry_is_dir(const char *name)
{
    size_t len = strlen(name);
    return len > 0 && (name[len - 1] == '/' || name[len - 1] == '\\');
}

static bool read_central_dir(FILE *f, long eocd_offset,
                             ApakEntry **out_entries, size_t *out_count,
                             char *err_buf, size_t err_buf_len)
{
    unsigned char eocd[22];
    if (fseek(f, eocd_offset, SEEK_SET) != 0 ||
        fread(eocd, 1, sizeof(eocd), f) != sizeof(eocd))
    {
        set_err(err_buf, err_buf_len, "cannot read end-of-central-directory");
        return false;
    }
    if (memcmp(eocd, "PK\x05\x06", 4) != 0)
    {
        set_err(err_buf, err_buf_len, "not a zip archive");
        return false;
    }
    uint16_t total = read_u16(eocd + 10);
    uint32_t cd_size = read_u32(eocd + 12);
    uint32_t cd_offset = read_u32(eocd + 16);
    (void)cd_size;
    if (total > APAK_MAX_ENTRIES)
    {
        set_err(err_buf, err_buf_len, "archive has too many entries");
        return false;
    }
    if (fseek(f, cd_offset, SEEK_SET) != 0)
    {
        set_err(err_buf, err_buf_len, "cannot seek to central directory");
        return false;
    }
    unsigned char *cd = (unsigned char *)malloc(cd_size ? cd_size : 1);
    if (!cd)
    {
        set_err(err_buf, err_buf_len, "out of memory");
        return false;
    }
    if (cd_size > 0 && fread(cd, 1, cd_size, f) != cd_size)
    {
        free(cd);
        set_err(err_buf, err_buf_len, "cannot read central directory");
        return false;
    }
    ApakEntry *entries = (ApakEntry *)calloc(total ? total : 1, sizeof(ApakEntry));
    if (!entries)
    {
        free(cd);
        set_err(err_buf, err_buf_len, "out of memory");
        return false;
    }
    size_t pos = 0;
    size_t count = 0;
    for (uint16_t i = 0; i < total; i++)
    {
        if (pos + 46 > cd_size || memcmp(cd + pos, "PK\x01\x02", 4) != 0)
        {
            free(cd);
            free_entries(entries, count);
            set_err(err_buf, err_buf_len, "corrupt central directory");
            return false;
        }
        uint16_t method = read_u16(cd + pos + 10);
        uint32_t crc = read_u32(cd + pos + 16);
        uint32_t comp = read_u32(cd + pos + 20);
        uint32_t uncomp = read_u32(cd + pos + 24);
        uint16_t name_len = read_u16(cd + pos + 28);
        uint16_t extra_len = read_u16(cd + pos + 30);
        uint16_t comment_len = read_u16(cd + pos + 32);
        uint32_t local_off = read_u32(cd + pos + 42);
        pos += 46;
        if (pos + name_len + extra_len + comment_len > cd_size)
        {
            free(cd);
            free_entries(entries, count);
            set_err(err_buf, err_buf_len, "corrupt central directory entry");
            return false;
        }
        char *name = (char *)malloc(name_len + 1);
        if (!name)
        {
            free(cd);
            free_entries(entries, count);
            set_err(err_buf, err_buf_len, "out of memory");
            return false;
        }
        memcpy(name, cd + pos, name_len);
        name[name_len] = '\0';
        pos += name_len + extra_len + comment_len;
        for (size_t k = 0; k < count; k++)
        {
            if (strcmp(entries[k].name, name) == 0)
            {
                free(name);
                free(cd);
                free_entries(entries, count);
                set_err(err_buf, err_buf_len, "archive has duplicate entries");
                return false;
            }
        }
        entries[count].name = name;
        entries[count].method = method;
        entries[count].comp_size = comp;
        entries[count].uncomp_size = uncomp;
        entries[count].crc = crc;
        entries[count].local_offset = local_off;
        count++;
    }
    free(cd);
    *out_entries = entries;
    *out_count = count;
    return true;
}

static bool load_archive(const char *apak_path, FILE **out_f,
                         ApakEntry **out_entries, size_t *out_count,
                         char *err_buf, size_t err_buf_len)
{
    FILE *f = fopen(apak_path, "rb");
    if (!f)
    {
        set_err(err_buf, err_buf_len, "cannot open .apak file");
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        set_err(err_buf, err_buf_len, "cannot seek .apak file");
        return false;
    }
    long file_size = ftell(f);
    if (file_size < 22)
    {
        fclose(f);
        set_err(err_buf, err_buf_len, "file is too small to be an .apak");
        return false;
    }
    /* EOCD lives in the last 64KB + 22 bytes. */
    long search_start = file_size - 22;
    long search_end = file_size - 22 - 65536;
    if (search_end < 0)
        search_end = 0;
    long eocd_offset = -1;
    unsigned char sig[4];
    for (long off = search_start; off >= search_end; off--)
    {
        if (fseek(f, off, SEEK_SET) != 0)
            break;
        if (fread(sig, 1, 4, f) != 4)
            break;
        if (memcmp(sig, "PK\x05\x06", 4) == 0)
        {
            eocd_offset = off;
            break;
        }
    }
    if (eocd_offset < 0)
    {
        fclose(f);
        set_err(err_buf, err_buf_len, "not a zip/.apak archive (EOCD missing)");
        return false;
    }
    ApakEntry *entries = NULL;
    size_t count = 0;
    if (!read_central_dir(f, eocd_offset, &entries, &count, err_buf, err_buf_len))
    {
        fclose(f);
        return false;
    }
    *out_f = f;
    *out_entries = entries;
    *out_count = count;
    return true;
}

/* Decompress one entry into a fresh buffer (NUL-terminated for text use).
 * Caller frees *out_data. */
static bool extract_entry_data(FILE *f, const ApakEntry *e,
                               unsigned char **out_data, size_t *out_len,
                               char *err_buf, size_t err_buf_len)
{
    if (e->uncomp_size > APAK_MAX_FILE_BYTES ||
        e->comp_size > APAK_MAX_FILE_BYTES)
    {
        set_err(err_buf, err_buf_len, "entry too large");
        return false;
    }
    if (e->method != 0 && e->method != 8)
    {
        set_err(err_buf, err_buf_len, "unsupported compression method (need stored/deflated)");
        return false;
    }
    if (fseek(f, e->local_offset, SEEK_SET) != 0)
    {
        set_err(err_buf, err_buf_len, "cannot seek to entry");
        return false;
    }
    unsigned char lh[30];
    if (fread(lh, 1, sizeof(lh), f) != sizeof(lh) ||
        memcmp(lh, "PK\x03\x04", 4) != 0)
    {
        set_err(err_buf, err_buf_len, "corrupt local file header");
        return false;
    }
    uint16_t name_len = read_u16(lh + 26);
    uint16_t extra_len = read_u16(lh + 28);
    if (fseek(f, e->local_offset + 30 + name_len + extra_len, SEEK_SET) != 0)
    {
        set_err(err_buf, err_buf_len, "cannot seek to entry data");
        return false;
    }
    unsigned char *comp = NULL;
    if (e->comp_size > 0)
    {
        comp = (unsigned char *)malloc(e->comp_size);
        if (!comp)
        {
            set_err(err_buf, err_buf_len, "out of memory");
            return false;
        }
        if (fread(comp, 1, e->comp_size, f) != e->comp_size)
        {
            free(comp);
            set_err(err_buf, err_buf_len, "cannot read entry data");
            return false;
        }
    }
    unsigned char *raw = (unsigned char *)malloc(e->uncomp_size + 1);
    if (!raw)
    {
        free(comp);
        set_err(err_buf, err_buf_len, "out of memory");
        return false;
    }
    if (e->method == 0)
    {
        /* Stored entries must carry identical sizes; otherwise the tail
         * of the output buffer would stay uninitialized (and a claimed
         * crc of 0 would previously have skipped the check below). */
        if (e->comp_size != e->uncomp_size)
        {
            free(comp);
            free(raw);
            set_err(err_buf, err_buf_len, "stored entry has mismatched sizes");
            return false;
        }
        if (e->comp_size > 0)
            memcpy(raw, comp, e->comp_size);
    }
    else
    {
        z_stream strm;
        memset(&strm, 0, sizeof(strm));
        strm.next_in = comp ? comp : raw;
        strm.avail_in = e->comp_size;
        strm.next_out = raw;
        strm.avail_out = e->uncomp_size;
        int rc = inflateInit2(&strm, -MAX_WBITS);
        if (rc != Z_OK)
        {
            free(comp);
            free(raw);
            set_err(err_buf, err_buf_len, "zlib init failed");
            return false;
        }
        rc = inflate(&strm, Z_FINISH);
        inflateEnd(&strm);
        if (rc != Z_STREAM_END)
        {
            free(comp);
            free(raw);
            set_err(err_buf, err_buf_len, "deflate decompression failed");
            return false;
        }
    }
    free(comp);
    raw[e->uncomp_size] = '\0';
    /* Always verify the CRC (crc32 of empty input is 0, so empty files
     * still pass). Skipping the check when the claimed CRC is 0 would
     * let a corrupt/tampered entry through. */
    {
        uint32_t actual = (uint32_t)crc32(0L, raw, e->uncomp_size);
        if (actual != e->crc)
        {
            free(raw);
            set_err(err_buf, err_buf_len, "CRC mismatch (archive is corrupt)");
            return false;
        }
    }
    *out_data = raw;
    if (out_len)
        *out_len = e->uncomp_size;
    return true;
}

bool aroma_apak_read_file(const char *apak_path, const char *inner_name,
                          char **out_buf, size_t *out_len,
                          char *err_buf, size_t err_buf_len)
{
    if (!apak_path || !inner_name || !out_buf)
    {
        set_err(err_buf, err_buf_len, "invalid arguments");
        return false;
    }
    FILE *f = NULL;
    ApakEntry *entries = NULL;
    size_t count = 0;
    if (!load_archive(apak_path, &f, &entries, &count, err_buf, err_buf_len))
        return false;
    const ApakEntry *found = NULL;
    for (size_t i = 0; i < count; i++)
    {
        if (strcmp(entries[i].name, inner_name) == 0)
        {
            found = &entries[i];
            break;
        }
    }
    if (!found)
    {
        free_entries(entries, count);
        fclose(f);
        set_err(err_buf, err_buf_len, "file not found in package");
        return false;
    }
    unsigned char *data = NULL;
    size_t len = 0;
    bool ok = extract_entry_data(f, found, &data, &len, err_buf, err_buf_len);
    free_entries(entries, count);
    fclose(f);
    if (!ok)
        return false;
    *out_buf = (char *)data;
    if (out_len)
        *out_len = len;
    return true;
}

bool aroma_apak_contains(const char *apak_path, const char *inner_name)
{
    if (!apak_path || !inner_name)
        return false;
    FILE *f = NULL;
    ApakEntry *entries = NULL;
    size_t count = 0;
    char err[128];
    if (!load_archive(apak_path, &f, &entries, &count, err, sizeof(err)))
        return false;
    bool found = false;
    for (size_t i = 0; i < count; i++)
    {
        if (strcmp(entries[i].name, inner_name) == 0)
        {
            found = true;
            break;
        }
    }
    free_entries(entries, count);
    fclose(f);
    return found;
}

#ifndef __EMSCRIPTEN__
static bool mkdir_p(const char *path)
{
    char tmp[AROMA_PACKAGE_PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
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
    return true;
}
#endif

bool aroma_apak_extract(const char *apak_path, const char *dest_dir,
                        char *err_buf, size_t err_buf_len)
{
#ifdef __EMSCRIPTEN__
    (void)apak_path;
    (void)dest_dir;
    set_err(err_buf, err_buf_len,
            "installing .apak files is not supported on this platform");
    return false;
#else
    if (!apak_path || !dest_dir)
    {
        set_err(err_buf, err_buf_len, "invalid arguments");
        return false;
    }
    FILE *f = NULL;
    ApakEntry *entries = NULL;
    size_t count = 0;
    if (!load_archive(apak_path, &f, &entries, &count, err_buf, err_buf_len))
        return false;
    /* Validate all names before writing anything. */
    for (size_t i = 0; i < count; i++)
    {
        if (!inner_path_safe(entries[i].name, strlen(entries[i].name)))
        {
            free_entries(entries, count);
            fclose(f);
            set_err(err_buf, err_buf_len, "archive contains an unsafe path");
            return false;
        }
    }
    if (!mkdir_p(dest_dir))
    {
        free_entries(entries, count);
        fclose(f);
        set_err(err_buf, err_buf_len, "cannot create destination directory");
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < count && ok; i++)
    {
        char out_path[AROMA_PACKAGE_PATH_MAX];
        if (!aroma_package_join_path(out_path, sizeof(out_path),
                                     dest_dir, entries[i].name))
        {
            set_err(err_buf, err_buf_len, "path too long");
            ok = false;
            break;
        }
        if (entry_is_dir(entries[i].name))
        {
            if (!mkdir_p(out_path))
            {
                set_err(err_buf, err_buf_len, "cannot create directory");
                ok = false;
            }
            continue;
        }
        /* Ensure the parent directory exists. */
        char parent[AROMA_PACKAGE_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s", out_path);
        char *slash = strrchr(parent, '/');
        if (slash)
        {
            *slash = '\0';
            if (!mkdir_p(parent))
            {
                set_err(err_buf, err_buf_len, "cannot create directory");
                ok = false;
                break;
            }
        }
        unsigned char *data = NULL;
        size_t len = 0;
        if (!extract_entry_data(f, &entries[i], &data, &len, err_buf, err_buf_len))
        {
            ok = false;
            break;
        }
        FILE *out = fopen(out_path, "wb");
        if (!out)
        {
            free(data);
            set_err(err_buf, err_buf_len, "cannot write output file");
            ok = false;
            break;
        }
        if (len > 0 && fwrite(data, 1, len, out) != len)
        {
            fclose(out);
            free(data);
            set_err(err_buf, err_buf_len, "cannot write output file");
            ok = false;
            break;
        }
        fclose(out);
        free(data);
    }
    free_entries(entries, count);
    fclose(f);
    return ok;
#endif
}

#else /* !AROMA_HAS_ZLIB */

bool aroma_apak_read_file(const char *apak_path, const char *inner_name,
                          char **out_buf, size_t *out_len,
                          char *err_buf, size_t err_buf_len)
{
    (void)apak_path;
    (void)inner_name;
    (void)out_buf;
    (void)out_len;
    if (err_buf && err_buf_len > 0)
        snprintf(err_buf, err_buf_len,
                 ".apak support needs zlib (rebuild with ZLIB found)");
    return false;
}

bool aroma_apak_extract(const char *apak_path, const char *dest_dir,
                        char *err_buf, size_t err_buf_len)
{
    (void)apak_path;
    (void)dest_dir;
    if (err_buf && err_buf_len > 0)
        snprintf(err_buf, err_buf_len,
                 ".apak support needs zlib (rebuild with ZLIB found)");
    return false;
}

bool aroma_apak_contains(const char *apak_path, const char *inner_name)
{
    (void)apak_path;
    (void)inner_name;
    return false;
}

#endif /* AROMA_HAS_ZLIB */
