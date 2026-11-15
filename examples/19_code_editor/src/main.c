#include <aroma.h>
#include <aroma_code_editor.h>
#include <aroma_incense_loader.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static AromaNode *g_editor;
static AromaFont *g_font;
static IncenseRegistry *g_registry;

static const char *g_sample_code =
    "#include <stdio.h>\n"
    "\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"Hello, AromaUI!\\n\");\n"
    "    return 0;\n"
    "}\n";

static void on_change(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)text;
    (void)user_data;
    printf("[code editor] content changed (%zu bytes)\n",
           aroma_code_editor_get_length(node));
}

static void on_cursor(AromaNode *node, int line, int column,
                      int selection_chars, void *user_data)
{
    (void)node;
    (void)user_data;
    if (selection_chars > 0)
        printf("[code editor] cursor L%d:%d sel=%d\n",
               line, column, selection_chars);
    else
        printf("[code editor] cursor L%d:%d\n", line, column);
}

static bool on_save(AromaNode *node, const char *text, void *user_data)
{
    (void)node;
    (void)user_data;
    printf("[code editor] save requested (%zu bytes)\n", strlen(text));
    return true;
}

static void on_completion(AromaNode *node, int line, int column,
                          void *user_data)
{
    (void)line;
    (void)column;
    (void)user_data;
    static const AromaCodeCompletion suggestions[] = {
        {"printf", "Print formatted output", "printf()", AROMA_CODE_COMPLETION_FUNCTION},
        {"malloc", "Allocate memory", "malloc(size)", AROMA_CODE_COMPLETION_FUNCTION},
        {"free", "Free allocated memory", "free(ptr)", AROMA_CODE_COMPLETION_FUNCTION},
        {"return", "Return from function", "return", AROMA_CODE_COMPLETION_KEYWORD},
        {"int", "Integer type", "int", AROMA_CODE_COMPLETION_KEYWORD},
        {"void", "Void type", "void", AROMA_CODE_COMPLETION_KEYWORD},
    };
    aroma_code_editor_set_completions(node, suggestions,
                                      (int)(sizeof(suggestions) / sizeof(suggestions[0])));
}

static void on_hover(AromaNode *node, int line, int column, void *user_data)
{
    (void)node;
    (void)user_data;
    char msg[128];
    snprintf(msg, sizeof(msg), "Line %d, Column %d", line, column);
    aroma_code_editor_set_hover(node, msg, line, column);
}

static void on_definition(AromaNode *node, int line, int column, void *user_data)
{
    (void)node;
    (void)user_data;
    printf("[code editor] goto definition at L%d:%d\n", line, column);
}

int main(void)
{
    aroma_ui_init();

    AromaTheme theme = aroma_theme_create_material_black();
    aroma_ui_set_theme(&theme);

    g_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 14);

    AromaWindow *window = aroma_ui_create_window("Code Editor", 1000, 700);
    if (!window)
    {
        aroma_font_destroy(g_font);
        aroma_ui_shutdown();
        return 1;
    }

    AromaNode *root = g_windows[0].root_node;

    g_editor = aroma_code_editor_create(root, 20, 20, 960, 640);
    if (!g_editor)
    {
        aroma_font_destroy(g_font);
        aroma_ui_destroy_window(window);
        aroma_ui_shutdown();
        return 1;
    }

    aroma_code_editor_set_font(g_editor, g_font);
    aroma_code_editor_set_text(g_editor, g_sample_code);
    aroma_code_editor_set_language(g_editor, AROMA_CODE_LANGUAGE_C);
    aroma_code_editor_set_file_name(g_editor, "main.c");
    aroma_code_editor_set_line_numbers(g_editor, true);
    aroma_code_editor_set_minimap(g_editor, true);
    aroma_code_editor_set_tab_width(g_editor, 4);

    aroma_code_editor_set_on_change(g_editor, on_change, NULL);
    aroma_code_editor_set_on_cursor_changed(g_editor, on_cursor, NULL);
    aroma_code_editor_set_on_save(g_editor, on_save, NULL);
    aroma_code_editor_set_on_completion_request(g_editor, on_completion, NULL);
    aroma_code_editor_set_on_hover_request(g_editor, on_hover, NULL);
    aroma_code_editor_set_on_definition_request(g_editor, on_definition, NULL);

    aroma_code_editor_focus(g_editor);

    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        aroma_code_editor_tick(g_editor);
        aroma_ui_render(window);
#ifdef __EMSCRIPTEN__
        emscripten_sleep(16);
#else
        usleep(16000);
#endif
    }

    aroma_font_destroy(g_font);
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
