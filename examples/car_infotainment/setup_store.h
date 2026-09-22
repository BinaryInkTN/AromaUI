#ifndef SETUP_STORE_H
#define SETUP_STORE_H

/* Tiny key=value settings store for the car infotainment example.
 * Persists first-run setup choices (wifi, bluetooth, theme, device name,
 * store URL, ...) across restarts. No external dependencies.
 */

#include <stdbool.h>

/* Load settings from disk. Safe to call before UI init. A missing file
 * (first boot) is fine and yields defaults; returns true unless the file
 * exists but cannot be read. */
bool setup_store_load(void);

/* Write current settings to disk. */
bool setup_store_save(void);

const char *setup_store_get(const char *key, const char *dflt);
void setup_store_set(const char *key, const char *value);
int setup_store_get_int(const char *key, int dflt);
void setup_store_set_int(const char *key, int value);

/* True once the first-run wizard completed successfully (or was skipped). */
bool setup_is_complete(void);
void setup_mark_complete(void);

/* Resolved settings file path (diagnostics). */
const char *setup_store_path(void);

#endif
