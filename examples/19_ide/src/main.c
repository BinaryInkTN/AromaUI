#define _POSIX_C_SOURCE 200809L

#include <aroma.h>
#include <aroma_incense_loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "ide.h"
#include "workspace.h"

static AromaFont *g_base_font;
static AromaFont *g_icon_font;
static IdeAssets g_assets;
static IncenseRegistry *g_registry;

static AromaTheme ide_theme(void)
{
    AromaTheme theme = aroma_theme_create_material_black();
    theme.colors.primary = 0xFF0A84FFu;
    theme.colors.primary_dark = 0xFF0060DFu;
    theme.colors.primary_light = 0xFF64D2FFu;
    theme.colors.secondary = 0xFF5E5CE6u;
    theme.colors.background = 0xFF1C1C1Eu;
    theme.colors.surface = 0xFF202022u;
    theme.colors.text_primary = 0xFFF2F2F7u;
    theme.colors.text_secondary = 0xFFC7C7CCu;
    theme.colors.border = 0xFF38383Au;
    theme.colors.error = 0xFFFF453Au;
    return theme;
}

static AromaFont *mono_font(int size)
{
    const char *paths[] = {
        "assets/UbuntuMono-Regular.ttf",
        "examples/19_ide/assets/UbuntuMono-Regular.ttf",
        "../assets/UbuntuMono-Regular.ttf",
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
    {
        if (access(paths[i], R_OK) == 0)
        {
            AromaFont *font = aroma_font_create(paths[i], size);
            if (font)
                return font;
        }
    }
    return aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, size);
}

static AromaFont *editor_font(void)
{
    return mono_font(15);
}

static AromaFont *serif_font(int size)
{
    const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
        "/usr/share/fonts/truetype/noto/NotoSerif-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSerif.ttf",
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
    {
        if (access(paths[i], R_OK) == 0)
        {
            AromaFont *font = aroma_font_create(paths[i], size);
            if (font)
                return font;
        }
    }
                                                                    
    return aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                         aroma_ubuntu_ttf_len, size);
}

static bool create_fonts(void)
{
    g_base_font = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                                aroma_ubuntu_ttf_len, 15);
    g_icon_font = aroma_font_create_from_memory(icon_ttf, icon_ttf_len, 24);
    if (!g_base_font || !g_icon_font)
        return false;

    for (int i = 0; i < IDE_UI_FONT_COUNT; i++)
    {
        int size = ide_ui_font_sizes[i];
        g_assets.ui[i] = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                                       aroma_ubuntu_ttf_len, size);
        g_assets.ui_mono[i] = mono_font(size);
        g_assets.ui_serif[i] = serif_font(size);
        IncenseRegisterFontSize(size, g_assets.ui[i]);
    }
    for (int i = 0; i < IDE_ICON_FONT_COUNT; i++)
    {
        int size = ide_icon_font_sizes[i];
        g_assets.icons[i] = aroma_font_create_from_memory(icon_ttf,
                                                          icon_ttf_len, size);
        IncenseRegisterIconFontSize(size, g_assets.icons[i]);
    }
    for (int i = 0; i < IDE_MONO_FONT_COUNT; i++)
    {
        int size = ide_mono_font_sizes[i];
        g_assets.mono[i] = mono_font(size);
    }
    g_assets.editor = editor_font();
    return g_assets.editor != NULL;
}

static void destroy_fonts(void)
{
    for (int i = 0; i < IDE_UI_FONT_COUNT; i++)
    {
        if (g_assets.ui[i])
            aroma_font_destroy(g_assets.ui[i]);
        g_assets.ui[i] = NULL;
        if (g_assets.ui_mono[i])
            aroma_font_destroy(g_assets.ui_mono[i]);
        g_assets.ui_mono[i] = NULL;
        if (g_assets.ui_serif[i])
            aroma_font_destroy(g_assets.ui_serif[i]);
        g_assets.ui_serif[i] = NULL;
    }
    for (int i = 0; i < IDE_ICON_FONT_COUNT; i++)
    {
        if (g_assets.icons[i])
            aroma_font_destroy(g_assets.icons[i]);
        g_assets.icons[i] = NULL;
    }
    for (int i = 0; i < IDE_MONO_FONT_COUNT; i++)
    {
        if (g_assets.mono[i])
            aroma_font_destroy(g_assets.mono[i]);
        g_assets.mono[i] = NULL;
    }
    if (g_assets.editor)
        aroma_font_destroy(g_assets.editor);
    g_assets.editor = NULL;
    if (g_base_font)
        aroma_font_destroy(g_base_font);
    g_base_font = NULL;
    if (g_icon_font)
        aroma_font_destroy(g_icon_font);
    g_icon_font = NULL;
}

static void print_load_errors(void)
{
    int count = 0;
    const IncenseError *errors = IncenseGetErrors(&count);
    for (int i = 0; i < count; i++)
        fprintf(stderr, "line %d: %s\n", errors[i].line, errors[i].message);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    aroma_ui_init();
    AromaTheme theme = ide_theme();
    aroma_ui_set_theme(&theme);

    if (!create_fonts())
    {
        fprintf(stderr, "Failed to create fonts\n");
        destroy_fonts();
        aroma_ui_shutdown();
        return 1;
    }

                                            
    AromaWindow *window = IncenseLoadFileEx("ui/home.aroma", g_base_font,
                                            g_icon_font, &g_registry);
    if (!window)
    {
        print_load_errors();
        destroy_fonts();
        aroma_ui_shutdown();
        return 1;
    }

    aroma_ui_set_window_decorated(window, false);
    ide_home_init(g_registry, &g_assets);

    bool entered_ide = false;

    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        if (!aroma_ui_is_running())
            break;

        if (!entered_ide && !ide_home_active())
        {
            const char *sel = ide_home_project_path();
            if (!sel || !sel[0])
            {
                ide_home_fail("No project selected");
            }
            else if (!ide_workspace_init(sel))
            {
                                                           
                ide_home_fail(ide_workspace_last_error());
            }
            else
            {
                                                                            
                                                    
                aroma_ui_destroy_window(window);
                IncenseFreeRegistry(g_registry);

                window = IncenseLoadFileEx("ui/app.aroma", g_base_font,
                                           g_icon_font, &g_registry);
                if (!window)
                {
                    print_load_errors();
                    destroy_fonts();
                    aroma_ui_shutdown();
                    ide_workspace_shutdown();
                    return 1;
                }

                                                                             
                                                                
                aroma_ui_set_window_opacity(window, IDE_WINDOW_OPACITY);

                ide_after_mount(g_registry, &g_assets);
                entered_ide = true;

                                                                   
                for (int i = 0; i < IDE_UI_FONT_COUNT; i++)
                    IncenseRegisterFontSize(ide_ui_font_sizes[i], g_assets.ui[i]);
                for (int i = 0; i < IDE_ICON_FONT_COUNT; i++)
                    IncenseRegisterIconFontSize(ide_icon_font_sizes[i], g_assets.icons[i]);

                                                                                
                                                  
                ide_home_open_pending_dialog();
            }
        }

        ide_tick();
        if (ide_home_active())
            ide_home_draw();
        aroma_ui_render(window);
#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        struct timespec delay = {0, 16000000L};
        nanosleep(&delay, NULL);
#endif
    }

    ide_shutdown();
    IncenseFreeRegistry(g_registry);
    destroy_fonts();
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