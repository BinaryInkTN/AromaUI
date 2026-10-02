#include "app_registry.h"
#include <string.h>

static AromaAppPlugin registered_apps[MAX_REGISTERED_APPS];
static int registered_app_count = 0;

bool app_registry_init(void) {
    registered_app_count = 0;
    memset(registered_apps, 0, sizeof(registered_apps));
    return true;
}

void app_registry_cleanup(void) {
    for (int i = 0; i < registered_app_count; i++) {
        if (registered_apps[i].destroy) {
            registered_apps[i].destroy(&registered_apps[i]);
        }
    }
    registered_app_count = 0;
}

bool app_registry_register_app(AromaAppPlugin *app) {
    if (!app || registered_app_count >= MAX_REGISTERED_APPS) {
        return false;
    }

    registered_apps[registered_app_count] = *app;

    if (registered_apps[registered_app_count].init) {
        if (!registered_apps[registered_app_count].init(&registered_apps[registered_app_count])) {
            return false;
        }
    }

    registered_app_count++;
    return true;
}

int app_registry_get_app_count(void) {
    return registered_app_count;
}

AromaAppPlugin* app_registry_get_app(int index) {
    if (index >= 0 && index < registered_app_count) {
        return &registered_apps[index];
    }
    return NULL;
}
