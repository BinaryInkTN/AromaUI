#ifndef SETUP_STORE_H
#define SETUP_STORE_H

#include <stdbool.h>

bool setup_store_load(void);

bool setup_store_save(void);

const char *setup_store_get(const char *key, const char *dflt);
void setup_store_set(const char *key, const char *value);
int setup_store_get_int(const char *key, int dflt);
void setup_store_set_int(const char *key, int value);

bool setup_is_complete(void);
void setup_mark_complete(void);

const char *setup_store_path(void);

#endif
