#include <aroma.h>
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
static char g_plan[16] = "Free";
static char g_country[32] = "Canada";

static void picked_country(int index, const char *option, void *userdata)
{
    (void)index;
    (void)userdata;
    snprintf(g_country, sizeof(g_country), "%s", option);
}

static void picked_plan(void *userdata)
{
    snprintf(g_plan, sizeof(g_plan), "%s", (const char *)userdata);
}

static void show_summary(const char *title, const char *body)
{
    AromaNode *dialog = IncenseFindWidget(g_registry, "done");
    if (dialog)
    {
        aroma_dialog_set_title(dialog, title);
        aroma_dialog_set_message(dialog, body);
        aroma_dialog_show(dialog);
    }
}

static void submit(void *userdata)
{
    (void)userdata;
    const char *name = "";
    const char *email = "";
    AromaNode *name_box = IncenseFindWidget(g_registry, "name");
    AromaNode *email_box = IncenseFindWidget(g_registry, "email");
    if (name_box)
        name = aroma_textbox_get_text(name_box);
    if (email_box)
        email = aroma_textbox_get_text(email_box);
    AromaNode *terms = IncenseFindWidget(g_registry, "terms");
    bool agreed = terms && aroma_checkbox_is_checked(terms);
    AromaNode *news = IncenseFindWidget(g_registry, "news");
    bool newsletter = news && aroma_checkbox_is_checked(news);
    AromaNode *push = IncenseFindWidget(g_registry, "push");
    bool push_on = push && aroma_switch_get_state(push);
    AromaNode *age = IncenseFindWidget(g_registry, "age");
    int age_val = age ? aroma_slider_get_value(age) : 0;
    AromaNode *seats = IncenseFindWidget(g_registry, "seats");
    int seats_val = seats ? aroma_stepper_get_value(seats) : 0;
    AromaNode *status = IncenseFindWidget(g_registry, "status");
    if (!name || !name[0])
    {
        if (status)
            aroma_label_set_text(status, "Enter your name first");
        return;
    }
    if (!agreed)
    {
        if (status)
            aroma_label_set_text(status, "Accept the terms to continue");
        return;
    }
    char body[512];
    snprintf(body, sizeof(body),
             "%s <%s>\n%s plan, %s\nAge %d, %d seat(s)\nPush %s, news %s",
             name, email && email[0] ? email : "no email",
             g_plan, g_country, age_val, seats_val,
             push_on ? "on" : "off", newsletter ? "on" : "off");
    if (status)
        aroma_label_set_text(status, "Account created");
    show_summary("Welcome aboard", body);
}

static void close_dialog(void *userdata)
{
    (void)userdata;
    AromaNode *dialog = IncenseFindWidget(g_registry, "done");
    if (dialog)
        aroma_dialog_hide(dialog);
}

int main(void)
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 16);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    IncenseRegisterCallback("submit", INCENSE_CALLBACK_VOID_PTR, submit, NULL);
    IncenseRegisterCallback("close_dialog", INCENSE_CALLBACK_VOID_PTR, close_dialog, NULL);
    IncenseRegisterCallback("drop", INCENSE_CALLBACK_INT_STRING_PTR, picked_country, NULL);
    IncenseRegisterCallback("plan_free", INCENSE_CALLBACK_VOID_PTR, picked_plan, "Free");
    IncenseRegisterCallback("plan_pro", INCENSE_CALLBACK_VOID_PTR, picked_plan, "Pro");
    IncenseRegisterCallback("plan_team", INCENSE_CALLBACK_VOID_PTR, picked_plan, "Team");
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
