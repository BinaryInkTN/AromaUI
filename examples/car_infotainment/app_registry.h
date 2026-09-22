#ifndef APP_REGISTRY_H
#define APP_REGISTRY_H

#include "aroma.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_REGISTERED_APPS 16

typedef struct AromaAppPlugin
{
    const char *id;
    const char *name;
    const char *icon;
    uint32_t card_color;

    bool (*init)(struct AromaAppPlugin *app);
    bool (*build_ui)(struct AromaAppPlugin *app, AromaNode *parent);
    void (*destroy)(struct AromaAppPlugin *app);
    bool (*show)(struct AromaAppPlugin *app, AromaNode *parent);
    void (*hide)(struct AromaAppPlugin *app);
    void (*update)(struct AromaAppPlugin *app);

    void *app_state;

    AromaNode *drawer_icon;
    AromaNode *drawer_card;
    AromaNode *app_root;
} AromaAppPlugin;

bool app_registry_init(void);
void app_registry_cleanup(void);
bool app_registry_register_app(AromaAppPlugin *app);
int app_registry_get_app_count(void);
AromaAppPlugin *app_registry_get_app(int index);

#endif
