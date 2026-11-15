#ifndef AROMA_CODE_EDITOR_H
#define AROMA_CODE_EDITOR_H

#include "aroma_common.h"
#include "aroma_font.h"
#include "aroma_node.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define AROMA_CODE_EDITOR_MAX_DIAGNOSTICS 96
#define AROMA_CODE_EDITOR_MAX_COMPLETIONS 1024

    typedef enum AromaCodeLanguage
    {
        AROMA_CODE_LANGUAGE_PLAIN,
        AROMA_CODE_LANGUAGE_C,
        AROMA_CODE_LANGUAGE_CPP,
        AROMA_CODE_LANGUAGE_TSX,
        AROMA_CODE_LANGUAGE_TYPESCRIPT,
        AROMA_CODE_LANGUAGE_JAVASCRIPT,
        AROMA_CODE_LANGUAGE_RUST,
        AROMA_CODE_LANGUAGE_YAML,
        AROMA_CODE_LANGUAGE_TOML,
        AROMA_CODE_LANGUAGE_JSON,
        AROMA_CODE_LANGUAGE_DOCKERFILE,
        AROMA_CODE_LANGUAGE_AROMA,
        AROMA_CODE_LANGUAGE_MARKDOWN,
        AROMA_CODE_LANGUAGE_PYTHON,
        AROMA_CODE_LANGUAGE_GO,
        AROMA_CODE_LANGUAGE_SHELL,
        AROMA_CODE_LANGUAGE_CSS,
        AROMA_CODE_LANGUAGE_HTML
    } AromaCodeLanguage;

    typedef enum AromaCodeDiagnosticSeverity
    {
        AROMA_CODE_DIAGNOSTIC_ERROR,
        AROMA_CODE_DIAGNOSTIC_WARNING,
        AROMA_CODE_DIAGNOSTIC_INFO,
        AROMA_CODE_DIAGNOSTIC_HINT
    } AromaCodeDiagnosticSeverity;

    typedef enum AromaCodeCompletionKind
    {
        AROMA_CODE_COMPLETION_TEXT,
        AROMA_CODE_COMPLETION_METHOD,
        AROMA_CODE_COMPLETION_FUNCTION,
        AROMA_CODE_COMPLETION_VARIABLE,
        AROMA_CODE_COMPLETION_CLASS,
        AROMA_CODE_COMPLETION_INTERFACE,
        AROMA_CODE_COMPLETION_MODULE,
        AROMA_CODE_COMPLETION_PROPERTY,
        AROMA_CODE_COMPLETION_KEYWORD,
        AROMA_CODE_COMPLETION_SNIPPET
    } AromaCodeCompletionKind;

    typedef struct AromaCodeDiagnostic
    {
        int line;       /* 1-based */
        int column;     /* 1-based */
        int end_line;   /* 1-based, 0 = same line */
        int end_column; /* 1-based, 0 = inferred */
        AromaCodeDiagnosticSeverity severity;
        char message[256];
        char source[40];
        char code[40];
    } AromaCodeDiagnostic;

    typedef struct AromaCodeCompletion
    {
        char label[80];
        char detail[128];
        char insert_text[192];
        AromaCodeCompletionKind kind;
    } AromaCodeCompletion;

    typedef struct AromaCodeSymbol
    {
        char name[80];
        char detail[96];
        int line;
        int column;
    } AromaCodeSymbol;

    typedef struct AromaCodeEditorPalette
    {
        uint32_t background;
        uint32_t gutter_background;
        uint32_t current_line;
        uint32_t selection;
        uint32_t text;
        uint32_t comment;
        uint32_t keyword;
        uint32_t string;
        uint32_t number;
        uint32_t type;
        uint32_t function;
        uint32_t property;
        uint32_t punctuation;
        uint32_t tag;
        uint32_t attribute;
        uint32_t gutter_text;
        uint32_t gutter_active;
        uint32_t cursor;
        uint32_t minimap;
        uint32_t minimap_viewport;
        uint32_t scrollbar;
        uint32_t scrollbar_hover;
        uint32_t popup_background;
        uint32_t popup_border;
        uint32_t popup_selection;
        uint32_t error;
        uint32_t warning;
        uint32_t info;
        uint32_t find_match;
        uint32_t find_bar;
    } AromaCodeEditorPalette;

    typedef void (*AromaCodeEditorChangeFn)(AromaNode *node, const char *text, void *user_data);
    typedef void (*AromaCodeEditorCursorFn)(AromaNode *node, int line, int column,
                                            int selection_chars, void *user_data);
    typedef bool (*AromaCodeEditorSaveFn)(AromaNode *node, const char *text, void *user_data);
    typedef void (*AromaCodeEditorLineColumnFn)(AromaNode *node, int line, int column,
                                                void *user_data);

    AromaNode *aroma_code_editor_create(AromaNode *parent, int x, int y, int width, int height);

    void aroma_code_editor_set_font(AromaNode *node, AromaFont *font);

    void aroma_code_editor_set_text(AromaNode *node, const char *text);
    const char *aroma_code_editor_get_text(AromaNode *node);
    size_t aroma_code_editor_get_length(AromaNode *node);

    void aroma_code_editor_set_language(AromaNode *node, AromaCodeLanguage language);
    AromaCodeLanguage aroma_code_editor_get_language(AromaNode *node);
    AromaCodeLanguage aroma_code_editor_language_from_path(const char *path);
    const char *aroma_code_editor_language_name(AromaCodeLanguage language);

    void aroma_code_editor_set_file_name(AromaNode *node, const char *file_name);
    const char *aroma_code_editor_get_file_name(AromaNode *node);

    void aroma_code_editor_set_palette(AromaNode *node, const AromaCodeEditorPalette *palette);
    AromaCodeEditorPalette aroma_code_editor_default_palette(void);

    void aroma_code_editor_set_line_numbers(AromaNode *node, bool visible);
    void aroma_code_editor_set_minimap(AromaNode *node, bool visible);
    void aroma_code_editor_set_read_only(AromaNode *node, bool read_only);
    void aroma_code_editor_set_tab_width(AromaNode *node, int tab_width);
    void aroma_code_editor_set_word_wrap(AromaNode *node, bool enabled);
    void aroma_code_editor_set_auto_indent(AromaNode *node, bool enabled);
    void aroma_code_editor_set_bracket_match(AromaNode *node, bool enabled);

    void aroma_code_editor_focus(AromaNode *node);
    bool aroma_code_editor_is_focused(AromaNode *node);

    void aroma_code_editor_set_cursor(AromaNode *node, size_t offset);
    size_t aroma_code_editor_get_cursor(AromaNode *node);
    void aroma_code_editor_get_cursor_position(AromaNode *node, int *line, int *column);
    int aroma_code_editor_get_line_count(AromaNode *node);
    void aroma_code_editor_goto_line_col(AromaNode *node, int line, int column);
    void aroma_code_editor_select(AromaNode *node, size_t start, size_t end);
    bool aroma_code_editor_get_selection(AromaNode *node, size_t *start, size_t *end);
    void aroma_code_editor_select_all(AromaNode *node);
    bool aroma_code_editor_get_word_at_cursor(AromaNode *node, char *out, size_t out_size);

    void aroma_code_editor_set_scroll(AromaNode *node, int scroll_x, int scroll_y);
    void aroma_code_editor_get_scroll(AromaNode *node, int *scroll_x, int *scroll_y);
    void aroma_code_editor_set_bounds(AromaNode *node, int x, int y, int width, int height);

    void aroma_code_editor_undo(AromaNode *node);
    void aroma_code_editor_redo(AromaNode *node);
    void aroma_code_editor_copy(AromaNode *node);
    void aroma_code_editor_cut(AromaNode *node);
    void aroma_code_editor_paste(AromaNode *node);
    void aroma_code_editor_insert_newline(AromaNode *node);
    void aroma_code_editor_show_find(AromaNode *node);
    void aroma_code_editor_hide_find(AromaNode *node);

    void aroma_code_editor_set_diagnostics(AromaNode *node,
                                           const AromaCodeDiagnostic *diagnostics,
                                           int count);
    void aroma_code_editor_clear_diagnostics(AromaNode *node);
    int aroma_code_editor_get_diagnostic_count(AromaNode *node);

    /* Provide completion candidates. The widget filters them against the
       identifier prefix at the cursor and shows the dropdown automatically.
       Up/Down select a row; Tab or Enter accepts the highlighted completion. */
    void aroma_code_editor_set_completions(AromaNode *node,
                                           const AromaCodeCompletion *completions,
                                           int count);
    void aroma_code_editor_hide_completions(AromaNode *node);
    bool aroma_code_editor_completions_visible(AromaNode *node);

    void aroma_code_editor_set_hover(AromaNode *node, const char *text, int line, int column);
    void aroma_code_editor_hide_hover(AromaNode *node);

    void aroma_code_editor_set_on_change(AromaNode *node, AromaCodeEditorChangeFn callback,
                                         void *user_data);
    void aroma_code_editor_set_on_cursor_changed(AromaNode *node,
                                                 AromaCodeEditorCursorFn callback,
                                                 void *user_data);
    void aroma_code_editor_set_on_save(AromaNode *node, AromaCodeEditorSaveFn callback,
                                       void *user_data);
    /* Type-to-suggest: typing an identifier character, '.', '>', or ':'
       automatically invokes this request. Provide unfiltered candidates via
       aroma_code_editor_set_completions(); the widget handles filtering. */
    void aroma_code_editor_set_on_completion_request(AromaNode *node,
                                                     AromaCodeEditorLineColumnFn callback,
                                                     void *user_data);
    void aroma_code_editor_set_on_hover_request(AromaNode *node,
                                                AromaCodeEditorLineColumnFn callback,
                                                void *user_data);
    void aroma_code_editor_set_on_definition_request(AromaNode *node,
                                                     AromaCodeEditorLineColumnFn callback,
                                                     void *user_data);

    bool aroma_code_editor_tick(AromaNode *node);

    bool aroma_code_editor_setup_events(AromaNode *node);
    void aroma_code_editor_draw(AromaNode *node, size_t window_id);
    void aroma_code_editor_destroy(AromaNode *node);

#ifdef __cplusplus
}
#endif

#endif
