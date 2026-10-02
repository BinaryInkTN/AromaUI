#include "app_registry.h"
#include "app_state.h"
#include <stdio.h>
#include <stdlib.h>
#include "theme_manager.h"

extern bool open_settings(AromaNode *node, void *user_data);
extern void close_settings(void *user_data);

static bool settings_app_init(AromaAppPlugin *app) {
    return true;
}

static void settings_app_destroy(AromaAppPlugin *app) {
}

static bool settings_app_show(AromaAppPlugin *app, AromaNode *parent) {
    return open_settings(NULL, app->app_root);
}

static void settings_app_hide(AromaAppPlugin *app) {
    close_settings(app->app_root);
}

void register_settings_app(void) {
    static AromaAppPlugin app = {0};
    app.id = "com.aroma.settings";
    app.name = "Settings";
    app.icon = AROMA_ICON_SETTINGS;
    app.card_color = aroma_color_blend(0xFFFFFFFF, 0xFF1A73E8, 0.1f);
    app.init = settings_app_init;
    app.destroy = settings_app_destroy;
    app.show = settings_app_show;
    app.hide = settings_app_hide;
    app.fixed = true;

    app_registry_register_app(&app);
}
