#include <aroma.h>
#include <aroma_gesture.h>
#include <aroma_incense_loader.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static AromaFont *g_font;
static AromaFont *g_icon_font;
static IncenseRegistry *g_registry;
static AromaGestureObserver *g_stage;

static void show(const char *id, const char *text)
{
    AromaNode *node = IncenseFindWidget(g_registry, id);
    if (node)
        aroma_label_set_text(node, text);
}

static const char *gesture_name(AromaGestureType type)
{
    switch (type)
    {
    case AROMA_GESTURE_TAP: return "Tap";
    case AROMA_GESTURE_DOUBLE_TAP: return "Double tap";
    case AROMA_GESTURE_LONG_PRESS: return "Long press";
    case AROMA_GESTURE_SWIPE_LEFT: return "Swipe left";
    case AROMA_GESTURE_SWIPE_RIGHT: return "Swipe right";
    case AROMA_GESTURE_SWIPE_UP: return "Swipe up";
    case AROMA_GESTURE_SWIPE_DOWN: return "Swipe down";
    case AROMA_GESTURE_PAN: return "Pan";
    case AROMA_GESTURE_PINCH: return "Pinch";
    default: return "Unknown";
    }
}

static void on_gesture(const AromaGestureEvent *event, void *userdata)
{
    (void)userdata;
    char detail[128];
    if (event->type == AROMA_GESTURE_PINCH)
        snprintf(detail, sizeof(detail), "scale %.2f at (%d, %d)", event->scale, event->x, event->y);
    else if (event->type == AROMA_GESTURE_PAN)
        snprintf(detail, sizeof(detail), "at (%d, %d) total (%d, %d)", event->x, event->y, event->dx, event->dy);
    else if (event->type == AROMA_GESTURE_SWIPE_LEFT || event->type == AROMA_GESTURE_SWIPE_RIGHT ||
             event->type == AROMA_GESTURE_SWIPE_UP || event->type == AROMA_GESTURE_SWIPE_DOWN)
        snprintf(detail, sizeof(detail), "move (%d, %d) v (%.0f, %.0f)/s",
                 event->dx, event->dy, event->velocity_x, event->velocity_y);
    else
        snprintf(detail, sizeof(detail), "at (%d, %d)", event->x, event->y);
    show("gesture", gesture_name(event->type));
    show("detail", detail);
}

static void cleared(void *userdata)
{
    (void)userdata;
    show("gesture", "Touch the stage");
    show("detail", "Tap, swipe, pinch, hold");
}

static void toggled(bool on, void *userdata)
{
    (void)userdata;
    aroma_gesture_set_enabled(g_stage, on);
    if (!on)
    {
        show("gesture", "Disabled");
        show("detail", "Flip the switch to resume");
    }
}

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    IncenseRegisterCallback("cleared", INCENSE_CALLBACK_VOID_PTR, cleared, NULL);
    IncenseRegisterCallback("toggled", INCENSE_CALLBACK_BOOL_BOOL_PTR, toggled, NULL);
    AromaWindow *window = IncenseLoadFileEx("ui/app.aroma", g_font, g_icon_font, &g_registry);
    if (!window)
    {
        int count = 0;
        const IncenseError *errors = IncenseGetErrors(&count);
        for (int i = 0; i < count; i++)
            fprintf(stderr, "line %d: %s\n", errors[i].line, errors[i].message);
        aroma_font_destroy(g_font);
        aroma_font_destroy(g_icon_font);
        aroma_ui_shutdown();
        return 1;
    }
    AromaNode *stage = IncenseFindWidget(g_registry, "stage");
    if (stage)
    {
        g_stage = aroma_gesture_observe(stage,
                                        AROMA_GESTURE_TAP | AROMA_GESTURE_DOUBLE_TAP |
                                        AROMA_GESTURE_LONG_PRESS | AROMA_GESTURE_SWIPE_LEFT |
                                        AROMA_GESTURE_SWIPE_RIGHT | AROMA_GESTURE_SWIPE_UP |
                                        AROMA_GESTURE_SWIPE_DOWN | AROMA_GESTURE_PAN |
                                        AROMA_GESTURE_PINCH,
                                        on_gesture, NULL);
    }
    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        aroma_ui_render(window);
#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }
    if (g_stage)
        aroma_gesture_unobserve(g_stage);
    IncenseFreeRegistry(g_registry);
    aroma_font_destroy(g_font);
    aroma_font_destroy(g_icon_font);
    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();
    return 0;
}

#ifdef __ANDROID__
#include <android_native_app_glue.h>

void android_main(struct android_app *state)
{
    aroma_android_set_app(state);
    main();
}
#endif
