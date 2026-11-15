#include "widgets/aroma_code_editor.h"

#include "aroma_ui.h"
#include "aroma_time.h"
#include "aroma_ubuntu_font.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "core/aroma_event.h"
#include "core/aroma_font.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CE_MAX_UNDO 128
#define CE_MAX_FIND_MATCHES 512
#define CE_UNDO_TEXT_LIMIT (8u * 1024u * 1024u)
#define CE_CURSOR_BLINK_MS 520u
#define CE_HOVER_DELAY_MS 420u
#define CE_MIN_LINE_HEIGHT 12
#define CE_POPUP_MAX_ROWS 9

#define CE_C_WHITE 0xFFFFFFFFu

typedef struct CeSnapshot
{
    char *text;
    size_t cursor;
    size_t anchor;
} CeSnapshot;

typedef enum CeTokenKind
{
    CE_TOKEN_TEXT,
    CE_TOKEN_COMMENT,
    CE_TOKEN_KEYWORD,
    CE_TOKEN_STRING,
    CE_TOKEN_NUMBER,
    CE_TOKEN_TYPE,
    CE_TOKEN_FUNCTION,
    CE_TOKEN_PROPERTY,
    CE_TOKEN_PUNCTUATION,
    CE_TOKEN_TAG,
    CE_TOKEN_ATTRIBUTE
} CeTokenKind;

typedef struct CeToken
{
    int start;
    int end;
    CeTokenKind kind;
} CeToken;

typedef struct CeEditor
{
    AromaRect rect; /* Must stay first: layout code updates widget rects in place. */
    AromaNode *node;
    AromaFont *font;
    bool owns_font;

    char *text;
    size_t length;
    size_t capacity;

    size_t *line_starts;
    int line_count;
    int line_capacity;
    int max_columns;

    size_t cursor;
    size_t anchor;
    int scroll_x;
    int scroll_y;
    int desired_column;
    int tab_width;
    bool line_numbers;
    bool minimap;
    bool word_wrap;
    bool auto_indent;
    bool bracket_match;
    bool read_only;
    bool focused;
    bool show_cursor;
    uint64_t cursor_blink_ms;

    bool dragging;
    bool minimap_dragging;
    int minimap_drag_offset;
    uint64_t last_click_ms;
    size_t last_click_offset;
    char *clipboard_text;

    AromaCodeLanguage language;
    char file_name[160];
    AromaCodeEditorPalette palette;

    AromaCodeDiagnostic diagnostics[AROMA_CODE_EDITOR_MAX_DIAGNOSTICS];
    int diagnostic_count;

    AromaCodeCompletion completions[AROMA_CODE_EDITOR_MAX_COMPLETIONS];
    int completion_count;
    int completion_index;
    bool completions_visible;

    char hover_text[2048];
    int hover_line;
    int hover_column;
    bool hover_visible;
    int hover_request_line;
    int hover_request_column;
    uint64_t hover_request_ms;
    bool hover_waiting;

    bool find_visible;
    char find_query[160];
    int find_total;
    int find_index;
    size_t find_offsets[CE_MAX_FIND_MATCHES];

    CeSnapshot undo_stack[CE_MAX_UNDO];
    int undo_count;
    CeSnapshot redo_stack[CE_MAX_UNDO];
    int redo_count;

    int last_window_id;
    int last_font_window;
    float char_width;
    int last_line;
    int last_column;
    int last_selection_chars;

    AromaCodeEditorChangeFn on_change;
    void *on_change_data;
    AromaCodeEditorCursorFn on_cursor_changed;
    void *on_cursor_data;
    AromaCodeEditorSaveFn on_save;
    void *on_save_data;
    AromaCodeEditorLineColumnFn on_completion_request;
    void *on_completion_data;
    AromaCodeEditorLineColumnFn on_hover_request;
    void *on_hover_data;
    AromaCodeEditorLineColumnFn on_definition_request;
    void *on_definition_data;
} CeEditor;

typedef struct CeKeywordList
{
    const char *const *words;
    int count;
} CeKeywordList;

#define CE_KEYWORDS(name) name, (int)(sizeof(name) / sizeof((name)[0]))

static const char *const ce_keywords_c[] = {
    /* C89/C90. */
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if", "int",
    "long", "register", "return", "short", "signed", "sizeof", "static",
    "struct", "switch", "typedef", "union", "unsigned", "void", "volatile",
    "while",
    /* C99. */
    "_Bool", "_Complex", "_Imaginary", "inline", "restrict",
    /* C11. */
    "_Alignas", "_Alignof", "_Atomic", "_Generic", "_Noreturn",
    "_Static_assert", "_Thread_local",
    /* C23. */
    "_BitInt", "_Countof", "_Decimal32", "_Decimal64", "_Decimal128",
    "alignas", "alignof", "bool", "constexpr", "false", "nullptr",
    "static_assert", "thread_local", "true", "typeof", "typeof_unqual",
    /* Common GNU/Clang extensions. */
    "__asm__", "__attribute__", "__auto_type", "__const", "__extension__",
    "__inline__", "__restrict__", "__signed__", "__thread", "__typeof__",
    "__volatile__", "__int128", "__int128_t", "__uint128_t", "_Float16",
    "_Float32", "_Float64", "_Float128", "_Float32x", "_Float64x",
    "_Float128x", "asm", "NULL"};

/* Standard names that should read as types rather than as functions or
   ordinary identifiers. */
static const char *const ce_c_types[] = {
    "char8_t", "char16_t", "char32_t", "wchar_t", "size_t", "ssize_t",
    "ptrdiff_t", "max_align_t", "nullptr_t", "int8_t", "int16_t", "int32_t",
    "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int_least8_t", "int_least16_t", "int_least32_t", "int_least64_t",
    "uint_least8_t", "uint_least16_t", "uint_least32_t", "uint_least64_t",
    "int_fast8_t", "int_fast16_t", "int_fast32_t", "int_fast64_t",
    "uint_fast8_t", "uint_fast16_t", "uint_fast32_t", "uint_fast64_t",
    "intptr_t", "uintptr_t", "intmax_t", "uintmax_t", "imaxdiv_t",
    "atomic_bool", "atomic_char", "atomic_schar", "atomic_uchar",
    "atomic_short", "atomic_ushort", "atomic_int", "atomic_uint",
    "atomic_long", "atomic_ulong", "atomic_llong", "atomic_ullong",
    "atomic_char16_t", "atomic_char32_t", "atomic_wchar_t",
    "atomic_int_least8_t", "atomic_uint_least8_t", "atomic_int_least16_t",
    "atomic_uint_least16_t", "atomic_int_least32_t", "atomic_uint_least32_t",
    "atomic_int_least64_t", "atomic_uint_least64_t", "atomic_int_fast8_t",
    "atomic_uint_fast8_t", "atomic_int_fast16_t", "atomic_uint_fast16_t",
    "atomic_int_fast32_t", "atomic_uint_fast32_t", "atomic_int_fast64_t",
    "atomic_uint_fast64_t", "atomic_intptr_t", "atomic_uintptr_t",
    "atomic_size_t", "atomic_ptrdiff_t", "atomic_intmax_t", "atomic_uintmax_t",
    "atomic_flag", "FILE", "fpos_t", "clock_t", "time_t", "timespec", "tm",
    "div_t", "ldiv_t", "lldiv_t", "va_list", "jmp_buf", "sig_atomic_t",
    "wint_t", "mbstate_t", "wctrans_t", "wctype_t", "thrd_t", "mtx_t",
    "cnd_t", "tss_t", "once_flag", "fenv_t", "fexcept_t", "lconv",
    "AromaNode", "AromaWindow", "AromaFont", "AromaCanvas"};
static const char *const ce_keywords_cpp[] = {
    "alignas", "alignof", "auto", "bool", "break", "case", "catch", "char",
    "class", "const", "constexpr", "continue", "decltype", "default", "delete",
    "do", "double", "else", "enum", "explicit", "export", "extern", "false",
    "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable",
    "namespace", "new", "noexcept", "nullptr", "operator", "override", "private",
    "protected", "public", "register", "return", "short", "signed", "sizeof",
    "static", "struct", "switch", "template", "this", "throw", "true", "try",
    "typedef", "typename", "union", "unsigned", "using", "virtual", "void",
    "volatile", "while"};
static const char *const ce_keywords_ts[] = {
    "abstract", "as", "async", "await", "break", "case", "catch", "class",
    "const", "continue", "declare", "default", "delete", "do", "else", "enum",
    "export", "extends", "false", "finally", "for", "from", "function", "get",
    "if", "implements", "import", "in", "instanceof", "interface", "keyof",
    "let", "namespace", "new", "null", "of", "override", "private", "protected",
    "public", "readonly", "return", "satisfies", "set", "static", "super",
    "switch", "this", "throw", "true", "try", "type", "typeof", "undefined",
    "var", "void", "while", "with", "yield"};
static const char *const ce_keywords_js[] = {
    "async", "await", "break", "case", "catch", "class", "const", "continue",
    "debugger", "default", "delete", "do", "else", "export", "extends", "false",
    "finally", "for", "from", "function", "if", "import", "in", "instanceof",
    "let", "new", "null", "of", "return", "static", "super", "switch", "this",
    "throw", "true", "try", "typeof", "undefined", "var", "void", "while",
    "with", "yield"};
static const char *const ce_keywords_rust[] = {
    "as", "async", "await", "break", "const", "continue", "crate", "dyn",
    "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in", "let",
    "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "self",
    "Self", "static", "struct", "super", "trait", "true", "type", "unsafe",
    "use", "where", "while", "yield"};
static const char *const ce_keywords_yaml[] = {
    "true", "false", "null", "yes", "no", "on", "off", "True", "False",
    "Null", "TRUE", "FALSE", "NULL"};
static const char *const ce_keywords_toml[] = {
    "true", "false"};
static const char *const ce_keywords_json[] = {
    "true", "false", "null"};
static const char *const ce_keywords_docker[] = {
    "ADD", "ARG", "CMD", "COPY", "ENTRYPOINT", "ENV", "EXPOSE", "FROM",
    "HEALTHCHECK", "LABEL", "MAINTAINER", "ONBUILD", "RUN", "SHELL", "STOPSIGNAL",
    "USER", "VOLUME", "WORKDIR"};
static const char *const ce_keywords_aroma[] = {
    "Window", "Container", "Label", "Button", "Card", "Textbox", "Image",
    "Icon", "Slider", "Switch", "Checkbox", "Dropdown", "Canvas", "Chart",
    "true", "false", "null", "if", "else", "for", "while", "function",
    "state", "var", "let", "const", "include", "import"};
static const char *const ce_keywords_markdown[] = {
    "true", "false", "null"};
static const char *const ce_keywords_python[] = {
    "and", "as", "assert", "async", "await", "break", "class", "continue",
    "def", "del", "elif", "else", "except", "False", "finally", "for", "from",
    "global", "if", "import", "in", "is", "lambda", "None", "nonlocal", "not",
    "or", "pass", "raise", "return", "True", "try", "while", "with", "yield",
    "self"};
static const char *const ce_keywords_go[] = {
    "break", "case", "chan", "const", "continue", "default", "defer", "else",
    "fallthrough", "for", "func", "go", "goto", "if", "import", "interface",
    "map", "package", "range", "return", "select", "struct", "switch", "type",
    "var", "true", "false", "nil"};
static const char *const ce_keywords_shell[] = {
    "case", "do", "done", "elif", "else", "esac", "export", "fi", "for",
    "function", "if", "in", "local", "readonly", "return", "set", "then",
    "unset", "until", "while"};
static const char *const ce_keywords_css[] = {
    "important", "inherit", "initial", "none", "auto", "block", "flex", "grid",
    "relative", "absolute", "fixed", "sticky", "solid", "transparent", "var",
    "calc", "rgba", "rgb", "px", "rem", "em", "vh", "vw"};
static const char *const ce_keywords_html[] = {
    "html", "head", "body", "div", "span", "section", "header", "footer",
    "main", "nav", "button", "input", "label", "form", "a", "p", "h1", "h2",
    "h3", "h4", "ul", "ol", "li", "img", "script", "style", "template"};

static const CeKeywordList ce_language_keywords[] = {
    {NULL, 0},                              /* plain */
    {CE_KEYWORDS(ce_keywords_c)},           /* C */
    {CE_KEYWORDS(ce_keywords_cpp)},         /* C++ */
    {CE_KEYWORDS(ce_keywords_ts)},          /* TSX */
    {CE_KEYWORDS(ce_keywords_ts)},          /* TypeScript */
    {CE_KEYWORDS(ce_keywords_js)},          /* JavaScript */
    {CE_KEYWORDS(ce_keywords_rust)},        /* Rust */
    {CE_KEYWORDS(ce_keywords_yaml)},        /* YAML */
    {CE_KEYWORDS(ce_keywords_toml)},        /* TOML */
    {CE_KEYWORDS(ce_keywords_json)},        /* JSON */
    {CE_KEYWORDS(ce_keywords_docker)},      /* Dockerfile */
    {CE_KEYWORDS(ce_keywords_aroma)},       /* Aroma */
    {CE_KEYWORDS(ce_keywords_markdown)},    /* Markdown */
    {CE_KEYWORDS(ce_keywords_python)},      /* Python */
    {CE_KEYWORDS(ce_keywords_go)},          /* Go */
    {CE_KEYWORDS(ce_keywords_shell)},       /* Shell */
    {CE_KEYWORDS(ce_keywords_css)},         /* CSS */
    {CE_KEYWORDS(ce_keywords_html)}         /* HTML */
};

static uint64_t ce_now_ms(void)
{
    return aroma_time_now_ms();
}

static int ce_clamp_int(int value, int low, int high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static size_t ce_clamp_offset(const CeEditor *e, size_t offset)
{
    if (!e)
        return 0;
    return offset > e->length ? e->length : offset;
}

static bool ce_is_word_char(unsigned char c)
{
    return isalnum(c) || c == '_';
}

static bool ce_is_completion_char(unsigned char c)
{
    return ce_is_word_char(c) || c == '-' || c == '$';
}

#define CE_NO_OFFSET ((size_t)-1)

/* Return the offset of the opening '<' or '"' when the cursor is inside a
   preprocessor include path. This lets completion replace the whole path
   and keeps '#' directives working on the first column. */
static size_t ce_include_open_offset(const CeEditor *e)
{
    if (!e || !e->text || e->cursor == 0)
        return CE_NO_OFFSET;
    size_t line_start = e->cursor;
    while (line_start > 0 && e->text[line_start - 1] != '\n')
        line_start--;
    size_t p = line_start;
    while (p < e->cursor &&
           (e->text[p] == ' ' || e->text[p] == '\t'))
        p++;
    if (p >= e->cursor || e->text[p] != '#')
        return CE_NO_OFFSET;
    p++;
    while (p < e->cursor &&
           (e->text[p] == ' ' || e->text[p] == '\t'))
        p++;
    size_t word_start = p;
    while (p < e->cursor && isalpha((unsigned char)e->text[p]))
        p++;
    size_t word_length = p - word_start;
    bool include = (word_length == 7 &&
                    strncmp(e->text + word_start, "include", 7) == 0) ||
                   (word_length == 12 &&
                    strncmp(e->text + word_start, "include_next", 12) == 0) ||
                   (word_length == 6 &&
                    strncmp(e->text + word_start, "import", 6) == 0) ||
                   (word_length == 5 &&
                    strncmp(e->text + word_start, "embed", 5) == 0);
    if (!include)
        return CE_NO_OFFSET;
    while (p < e->cursor &&
           (e->text[p] == ' ' || e->text[p] == '\t'))
        p++;
    if (p >= e->cursor || (e->text[p] != '<' && e->text[p] != '"'))
        return CE_NO_OFFSET;
    char close = e->text[p] == '<' ? '>' : '"';
    for (size_t i = p + 1; i < e->cursor; i++)
    {
        if (e->text[i] == close)
            return CE_NO_OFFSET;
    }
    return p;
}

static bool ce_key_is_tab(uint32_t key)
{
    /* ASCII Tab, plus the X11/XKB Tab keysym used by some backends. */
    return key == 9 || key == 0xFF09;
}

static bool ce_starts_with_ci(const char *text, const char *prefix)
{
    if (!prefix[0])
        return true;
    if (!text)
        return false;
    while (*prefix)
    {
        if (!*text)
            return false;
        if (tolower((unsigned char)*text) != tolower((unsigned char)*prefix))
            return false;
        text++;
        prefix++;
    }
    return true;
}

static void ce_completion_prefix(const CeEditor *e, char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    if (!e || !e->text)
        return;
    size_t start = e->cursor;
    size_t include_open = ce_include_open_offset(e);
    if (include_open != CE_NO_OFFSET)
    {
        start = include_open + 1;
    }
    else
    {
        while (start > 0 && ce_is_completion_char((unsigned char)e->text[start - 1]))
            start--;
    }
    size_t length = e->cursor - start;
    if (length >= out_size)
        length = out_size - 1;
    memcpy(out, e->text + start, length);
    out[length] = '\0';
}

static bool ce_completion_matches(const AromaCodeCompletion *completion,
                                  const char *prefix)
{
    if (!completion)
        return false;
    if (ce_starts_with_ci(completion->label, prefix) ||
        ce_starts_with_ci(completion->insert_text, prefix))
        return true;
    /* Project headers are often typed with a directory prefix. Match the
       basename too, while acceptance still inserts the full path. */
    const char *slash = strrchr(completion->label, '/');
    if (slash && ce_starts_with_ci(slash + 1, prefix))
        return true;
    slash = strrchr(completion->insert_text, '/');
    return slash && ce_starts_with_ci(slash + 1, prefix);
}

static bool ce_has_selection(const CeEditor *e)
{
    return e && e->cursor != e->anchor;
}

static size_t ce_sel_start(const CeEditor *e)
{
    return e->cursor < e->anchor ? e->cursor : e->anchor;
}

static size_t ce_sel_end(const CeEditor *e)
{
    return e->cursor < e->anchor ? e->anchor : e->cursor;
}

static void ce_safe_copy(char *dst, size_t size, const char *src)
{
    if (!dst || size == 0)
        return;
    if (!src)
        src = "";
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static const char *ce_language_name_internal(AromaCodeLanguage language)
{
    switch (language)
    {
    case AROMA_CODE_LANGUAGE_C: return "C";
    case AROMA_CODE_LANGUAGE_CPP: return "C++";
    case AROMA_CODE_LANGUAGE_TSX: return "TypeScript React";
    case AROMA_CODE_LANGUAGE_TYPESCRIPT: return "TypeScript";
    case AROMA_CODE_LANGUAGE_JAVASCRIPT: return "JavaScript";
    case AROMA_CODE_LANGUAGE_RUST: return "Rust";
    case AROMA_CODE_LANGUAGE_YAML: return "YAML";
    case AROMA_CODE_LANGUAGE_TOML: return "TOML";
    case AROMA_CODE_LANGUAGE_JSON: return "JSON";
    case AROMA_CODE_LANGUAGE_DOCKERFILE: return "Dockerfile";
    case AROMA_CODE_LANGUAGE_AROMA: return "Aroma";
    case AROMA_CODE_LANGUAGE_MARKDOWN: return "Markdown";
    case AROMA_CODE_LANGUAGE_PYTHON: return "Python";
    case AROMA_CODE_LANGUAGE_GO: return "Go";
    case AROMA_CODE_LANGUAGE_SHELL: return "Shell";
    case AROMA_CODE_LANGUAGE_CSS: return "CSS";
    case AROMA_CODE_LANGUAGE_HTML: return "HTML";
    case AROMA_CODE_LANGUAGE_PLAIN:
    default:
        return "Plain Text";
    }
}

static bool ce_path_has_suffix(const char *path, const char *suffix)
{
    if (!path || !suffix)
        return false;
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > path_len)
        return false;
    return strcmp(path + path_len - suffix_len, suffix) == 0;
}

static const char *ce_path_name(const char *path)
{
    if (!path)
        return "";
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash))
        slash = backslash;
    return slash ? slash + 1 : path;
}

AromaCodeLanguage aroma_code_editor_language_from_path(const char *path)
{
    if (!path)
        return AROMA_CODE_LANGUAGE_PLAIN;
    const char *name = ce_path_name(path);
    if (strcmp(name, "Dockerfile") == 0 || strcmp(name, "Containerfile") == 0 ||
        ce_path_has_suffix(name, ".dockerfile"))
        return AROMA_CODE_LANGUAGE_DOCKERFILE;
    if (ce_path_has_suffix(name, ".tsx"))
        return AROMA_CODE_LANGUAGE_TSX;
    if (ce_path_has_suffix(name, ".ts"))
        return AROMA_CODE_LANGUAGE_TYPESCRIPT;
    if (ce_path_has_suffix(name, ".jsx") || ce_path_has_suffix(name, ".js") ||
        ce_path_has_suffix(name, ".mjs") || ce_path_has_suffix(name, ".cjs"))
        return AROMA_CODE_LANGUAGE_JAVASCRIPT;
    if (ce_path_has_suffix(name, ".rs"))
        return AROMA_CODE_LANGUAGE_RUST;
    if (ce_path_has_suffix(name, ".yaml") || ce_path_has_suffix(name, ".yml"))
        return AROMA_CODE_LANGUAGE_YAML;
    if (ce_path_has_suffix(name, ".toml"))
        return AROMA_CODE_LANGUAGE_TOML;
    if (ce_path_has_suffix(name, ".json"))
        return AROMA_CODE_LANGUAGE_JSON;
    if (ce_path_has_suffix(name, ".c") || ce_path_has_suffix(name, ".h"))
        return AROMA_CODE_LANGUAGE_C;
    if (ce_path_has_suffix(name, ".cpp") || ce_path_has_suffix(name, ".cc") ||
        ce_path_has_suffix(name, ".cxx") || ce_path_has_suffix(name, ".hpp") ||
        ce_path_has_suffix(name, ".hxx"))
        return AROMA_CODE_LANGUAGE_CPP;
    if (ce_path_has_suffix(name, ".aroma"))
        return AROMA_CODE_LANGUAGE_AROMA;
    if (ce_path_has_suffix(name, ".md") || ce_path_has_suffix(name, ".markdown"))
        return AROMA_CODE_LANGUAGE_MARKDOWN;
    if (ce_path_has_suffix(name, ".py"))
        return AROMA_CODE_LANGUAGE_PYTHON;
    if (ce_path_has_suffix(name, ".go"))
        return AROMA_CODE_LANGUAGE_GO;
    if (ce_path_has_suffix(name, ".sh") || ce_path_has_suffix(name, ".bash") ||
        ce_path_has_suffix(name, ".zsh"))
        return AROMA_CODE_LANGUAGE_SHELL;
    if (ce_path_has_suffix(name, ".css") || ce_path_has_suffix(name, ".scss") ||
        ce_path_has_suffix(name, ".less"))
        return AROMA_CODE_LANGUAGE_CSS;
    if (ce_path_has_suffix(name, ".html") || ce_path_has_suffix(name, ".htm") ||
        ce_path_has_suffix(name, ".xml") || ce_path_has_suffix(name, ".svg"))
        return AROMA_CODE_LANGUAGE_HTML;
    return AROMA_CODE_LANGUAGE_PLAIN;
}

const char *aroma_code_editor_language_name(AromaCodeLanguage language)
{
    return ce_language_name_internal(language);
}

AromaCodeEditorPalette aroma_code_editor_default_palette(void)
{
    AromaCodeEditorPalette p;
    memset(&p, 0, sizeof(p));
    p.background = 0xFF1E1E1E;
    p.gutter_background = 0xFF1E1E1E;
    p.current_line = 0xFF252526;
    p.selection = 0xFF264F78;
    p.text = 0xFFD4D4D4;
    p.comment = 0xFF6A9955;
    p.keyword = 0xFFC586C0;
    p.string = 0xFFCE9178;
    p.number = 0xFFB5CEA8;
    p.type = 0xFF4EC9B0;
    p.function = 0xFFDCDCAA;
    p.property = 0xFF9CDCFE;
    p.punctuation = 0xFFD4D4D4;
    p.tag = 0xFF569CD6;
    p.attribute = 0xFF9CDCFE;
    p.gutter_text = 0xFF858585;
    p.gutter_active = 0xFFC6C6C6;
    p.cursor = 0xFFAEAFAD;
    p.minimap = 0xFF202020;
    p.minimap_viewport = 0x33FFFFFF;
    p.scrollbar = 0x55A0A0A0;
    p.scrollbar_hover = 0x99C6C6C6;
    p.popup_background = 0xFF252526;
    p.popup_border = 0xFF454545;
    p.popup_selection = 0xFF04395E;
    p.error = 0xFFF14C4C;
    p.warning = 0xFFCCA700;
    p.info = 0xFF3794FF;
    p.find_match = 0x663A6EA5;
    p.find_bar = 0xFF2D2D30;
    return p;
}

static uint32_t ce_token_color(const CeEditor *e, CeTokenKind kind)
{
    switch (kind)
    {
    case CE_TOKEN_COMMENT: return e->palette.comment;
    case CE_TOKEN_KEYWORD: return e->palette.keyword;
    case CE_TOKEN_STRING: return e->palette.string;
    case CE_TOKEN_NUMBER: return e->palette.number;
    case CE_TOKEN_TYPE: return e->palette.type;
    case CE_TOKEN_FUNCTION: return e->palette.function;
    case CE_TOKEN_PROPERTY: return e->palette.property;
    case CE_TOKEN_PUNCTUATION: return e->palette.punctuation;
    case CE_TOKEN_TAG: return e->palette.tag;
    case CE_TOKEN_ATTRIBUTE: return e->palette.attribute;
    case CE_TOKEN_TEXT:
    default:
        return e->palette.text;
    }
}

static bool ce_word_in_list(const char *word, const char *const *words, int count)
{
    if (!word || !words)
        return false;
    for (int i = 0; i < count; i++)
    {
        if (words[i] && strcmp(word, words[i]) == 0)
            return true;
    }
    return false;
}

static bool ce_language_has_hash_comments(AromaCodeLanguage language)
{
    return language == AROMA_CODE_LANGUAGE_YAML || language == AROMA_CODE_LANGUAGE_TOML ||
           language == AROMA_CODE_LANGUAGE_PYTHON || language == AROMA_CODE_LANGUAGE_SHELL ||
           language == AROMA_CODE_LANGUAGE_DOCKERFILE || language == AROMA_CODE_LANGUAGE_AROMA;
}

static bool ce_language_has_slash_comments(AromaCodeLanguage language)
{
    return language == AROMA_CODE_LANGUAGE_C || language == AROMA_CODE_LANGUAGE_CPP ||
           language == AROMA_CODE_LANGUAGE_TSX || language == AROMA_CODE_LANGUAGE_TYPESCRIPT ||
           language == AROMA_CODE_LANGUAGE_JAVASCRIPT || language == AROMA_CODE_LANGUAGE_RUST ||
           language == AROMA_CODE_LANGUAGE_GO || language == AROMA_CODE_LANGUAGE_JSON ||
           language == AROMA_CODE_LANGUAGE_CSS;
}

static bool ce_language_is_markup(AromaCodeLanguage language)
{
    return language == AROMA_CODE_LANGUAGE_HTML;
}

static bool ce_language_properties_before_colon(AromaCodeLanguage language)
{
    return language == AROMA_CODE_LANGUAGE_YAML || language == AROMA_CODE_LANGUAGE_TOML ||
           language == AROMA_CODE_LANGUAGE_JSON || language == AROMA_CODE_LANGUAGE_CSS ||
           language == AROMA_CODE_LANGUAGE_AROMA || language == AROMA_CODE_LANGUAGE_TSX ||
           language == AROMA_CODE_LANGUAGE_TYPESCRIPT || language == AROMA_CODE_LANGUAGE_JAVASCRIPT;
}

static void ce_token_add(CeToken *tokens, int *count, int max, int start, int end,
                         CeTokenKind kind)
{
    if (start >= end || *count >= max)
        return;
    tokens[*count].start = start;
    tokens[*count].end = end;
    tokens[*count].kind = kind;
    (*count)++;
}

static int ce_lex_line(const CeEditor *e, const char *s, int len, CeToken *tokens, int max)
{
    int count = 0;
    int i = 0;
    bool first_word = true;
    bool in_tag = false;
    AromaCodeLanguage language = e ? e->language : AROMA_CODE_LANGUAGE_PLAIN;

    if (!s || len <= 0 || max <= 0)
        return 0;
    if (language == AROMA_CODE_LANGUAGE_PLAIN)
        return 0;
    if (language == AROMA_CODE_LANGUAGE_MARKDOWN)
    {
        int p = 0;
        while (p < len && (s[p] == ' ' || s[p] == '\t'))
            p++;
        if (p < len && s[p] == '#')
        {
            ce_token_add(tokens, &count, max, p, len, CE_TOKEN_KEYWORD);
            return count;
        }
        if (p + 3 <= len && strncmp(s + p, "```", 3) == 0)
        {
            ce_token_add(tokens, &count, max, p, len, CE_TOKEN_STRING);
            return count;
        }
        while (p < len)
        {
            if (s[p] == '`')
            {
                int start = p++;
                while (p < len && s[p] != '`')
                    p++;
                if (p < len)
                    p++;
                ce_token_add(tokens, &count, max, start, p, CE_TOKEN_STRING);
                continue;
            }
            p++;
        }
        return count;
    }

    if (ce_language_is_markup(language))
    {
        while (i < len)
        {
            if (s[i] == '<' && i + 3 < len && s[i + 1] == '!' && s[i + 2] == '-' && s[i + 3] == '-')
            {
                ce_token_add(tokens, &count, max, i, len, CE_TOKEN_COMMENT);
                break;
            }
            if (s[i] == '<')
            {
                ce_token_add(tokens, &count, max, i, i + 1, CE_TOKEN_PUNCTUATION);
                in_tag = true;
                i++;
                continue;
            }
            if (s[i] == '>')
            {
                ce_token_add(tokens, &count, max, i, i + 1, CE_TOKEN_PUNCTUATION);
                in_tag = false;
                i++;
                continue;
            }
            if (in_tag && (isalpha((unsigned char)s[i]) || s[i] == '_' || s[i] == '-'))
            {
                int start = i++;
                while (i < len && (isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '-' || s[i] == ':'))
                    i++;
                CeTokenKind kind = first_word ? CE_TOKEN_TAG : CE_TOKEN_ATTRIBUTE;
                ce_token_add(tokens, &count, max, start, i, kind);
                first_word = false;
                continue;
            }
            if (s[i] == '"' || s[i] == '\'')
            {
                char quote = s[i];
                int start = i++;
                while (i < len)
                {
                    if (s[i] == '\\' && i + 1 < len)
                    {
                        i += 2;
                        continue;
                    }
                    if (s[i] == quote)
                    {
                        i++;
                        break;
                    }
                    i++;
                }
                ce_token_add(tokens, &count, max, start, i, CE_TOKEN_STRING);
                continue;
            }
            if (s[i] == '/')
            {
                first_word = true;
                ce_token_add(tokens, &count, max, i, i + 1, CE_TOKEN_PUNCTUATION);
                i++;
                continue;
            }
            i++;
        }
        return count;
    }

    bool c_family = language == AROMA_CODE_LANGUAGE_C ||
                    language == AROMA_CODE_LANGUAGE_CPP;
    bool preprocessor_include = false;
    int first_nonspace = 0;
    while (first_nonspace < len &&
           (s[first_nonspace] == ' ' || s[first_nonspace] == '\t'))
        first_nonspace++;

    while (i < len)
    {
        unsigned char c = (unsigned char)s[i];

        if (c == ' ' || c == '\t' || c == '\r')
        {
            i++;
            continue;
        }

        /* C/C++ preprocessor directives are language tokens, not comments. */
        if (c_family && c == '#' && i == first_nonspace)
        {
            int start = i++;
            while (i < len && (s[i] == ' ' || s[i] == '\t'))
                i++;
            int word_start = i;
            while (i < len && (isalnum((unsigned char)s[i]) || s[i] == '_'))
                i++;
            if (i > word_start)
            {
                char directive[40];
                int length = i - word_start;
                if (length >= (int)sizeof(directive))
                    length = (int)sizeof(directive) - 1;
                memcpy(directive, s + word_start, (size_t)length);
                directive[length] = '\0';
                preprocessor_include = strcmp(directive, "include") == 0 ||
                                       strcmp(directive, "include_next") == 0 ||
                                       strcmp(directive, "import") == 0 ||
                                       strcmp(directive, "embed") == 0;
                ce_token_add(tokens, &count, max, start, i, CE_TOKEN_KEYWORD);
            }
            else
            {
                ce_token_add(tokens, &count, max, start, i, CE_TOKEN_PUNCTUATION);
            }
            continue;
        }

        /* Highlight the header name in #include <...>. */
        if (c_family && preprocessor_include && c == '<')
        {
            int start = i++;
            while (i < len && s[i] != '>')
                i++;
            if (i < len)
                i++;
            ce_token_add(tokens, &count, max, start, i, CE_TOKEN_STRING);
            preprocessor_include = false;
            continue;
        }

        if (ce_language_has_hash_comments(language) && c == '#')
        {
            ce_token_add(tokens, &count, max, i, len, CE_TOKEN_COMMENT);
            break;
        }

        if (ce_language_has_slash_comments(language) && c == '/' && i + 1 < len &&
            s[i + 1] == '/')
        {
            ce_token_add(tokens, &count, max, i, len, CE_TOKEN_COMMENT);
            break;
        }

        if (ce_language_has_slash_comments(language) && c == '/' && i + 1 < len &&
            s[i + 1] == '*')
        {
            int start = i;
            i += 2;
            while (i + 1 < len && !(s[i] == '*' && s[i + 1] == '/'))
                i++;
            if (i + 1 < len)
                i += 2;
            else
                i = len;
            ce_token_add(tokens, &count, max, start, i, CE_TOKEN_COMMENT);
            continue;
        }

        if (language == AROMA_CODE_LANGUAGE_DOCKERFILE && first_word && i == 0)
        {
            int start = i;
            while (i < len && !isspace((unsigned char)s[i]) && s[i] != '#' && s[i] != '=')
                i++;
            ce_token_add(tokens, &count, max, start, i, CE_TOKEN_KEYWORD);
            first_word = false;
            continue;
        }

        /* C string/character literal prefixes: L"", u"", U"", u8"". */
        if (c_family)
        {
            int prefix_length = 0;
            if (c == 'u' && i + 2 < len && s[i + 1] == '8' &&
                (s[i + 2] == '"' || s[i + 2] == '\''))
                prefix_length = 2;
            else if ((c == 'L' || c == 'u' || c == 'U') && i + 1 < len &&
                     (s[i + 1] == '"' || s[i + 1] == '\''))
                prefix_length = 1;
            if (prefix_length > 0)
            {
                int start = i;
                char quote = s[i + prefix_length];
                i += prefix_length + 1;
                while (i < len)
                {
                    if (s[i] == '\\' && i + 1 < len)
                    {
                        i += 2;
                        continue;
                    }
                    if (s[i] == quote)
                    {
                        i++;
                        break;
                    }
                    i++;
                }
                ce_token_add(tokens, &count, max, start, i, CE_TOKEN_STRING);
                first_word = false;
                continue;
            }
        }

        if (c == '"' || c == '\'' || (c == '`' &&
            (language == AROMA_CODE_LANGUAGE_TSX || language == AROMA_CODE_LANGUAGE_TYPESCRIPT ||
             language == AROMA_CODE_LANGUAGE_JAVASCRIPT)))
        {
            char quote = (char)c;
            int start = i++;
            while (i < len)
            {
                if (s[i] == '\\' && i + 1 < len)
                {
                    i += 2;
                    continue;
                }
                if (s[i] == quote)
                {
                    i++;
                    break;
                }
                i++;
            }
            CeTokenKind kind = CE_TOKEN_STRING;
            if (language == AROMA_CODE_LANGUAGE_JSON)
            {
                int look = i;
                while (look < len && (s[look] == ' ' || s[look] == '\t'))
                    look++;
                if (look < len && s[look] == ':')
                    kind = CE_TOKEN_PROPERTY;
            }
            ce_token_add(tokens, &count, max, start, i, kind);
            first_word = false;
            continue;
        }

        if (isdigit((unsigned char)c) ||
            (c == '.' && i + 1 < len && isdigit((unsigned char)s[i + 1])))
        {
            int start = i++;
            while (i < len)
            {
                unsigned char n = (unsigned char)s[i];
                if (isalnum(n) || n == '_' || n == '.' || n == '\'')
                {
                    i++;
                    continue;
                }
                if ((n == '+' || n == '-') && i > start &&
                    (s[i - 1] == 'e' || s[i - 1] == 'E' ||
                     s[i - 1] == 'p' || s[i - 1] == 'P'))
                {
                    i++;
                    continue;
                }
                break;
            }
            ce_token_add(tokens, &count, max, start, i, CE_TOKEN_NUMBER);
            first_word = false;
            continue;
        }

        /* C23 attributes: [[nodiscard]], [[deprecated("...")]], etc. */
        if (c_family && c == '[' && i + 1 < len && s[i + 1] == '[')
        {
            int start = i;
            i += 2;
            while (i + 1 < len && !(s[i] == ']' && s[i + 1] == ']'))
                i++;
            if (i + 1 < len)
                i += 2;
            else
                i = len;
            ce_token_add(tokens, &count, max, start, i, CE_TOKEN_ATTRIBUTE);
            first_word = false;
            continue;
        }

        if (isalpha((unsigned char)c) || c == '_' || c == '$')
        {
            int start = i++;
            if (c == '$')
            {
                while (i < len && (isalnum((unsigned char)s[i]) || s[i] == '_'))
                    i++;
                ce_token_add(tokens, &count, max, start, i, CE_TOKEN_PROPERTY);
                first_word = false;
                continue;
            }
            while (i < len && (isalnum((unsigned char)s[i]) || s[i] == '_'))
                i++;

            int word_len = i - start;
            char word[96];
            if (word_len >= (int)sizeof(word))
                word_len = (int)sizeof(word) - 1;
            memcpy(word, s + start, (size_t)word_len);
            word[word_len] = '\0';

            int look = i;
            while (look < len && (s[look] == ' ' || s[look] == '\t'))
                look++;

            CeTokenKind kind = CE_TOKEN_TEXT;
            const CeKeywordList *kw = &ce_language_keywords[language];
            bool is_c_keyword = c_family &&
                                ce_word_in_list(word, ce_keywords_c,
                                                (int)(sizeof(ce_keywords_c) /
                                                      sizeof(ce_keywords_c[0])));
            bool is_c_type = c_family &&
                             ce_word_in_list(word, ce_c_types,
                                             (int)(sizeof(ce_c_types) /
                                                   sizeof(ce_c_types[0])));
            if ((kw && ce_word_in_list(word, kw->words, kw->count)) || is_c_keyword)
            {
                kind = CE_TOKEN_KEYWORD;
            }
            else if (is_c_type)
            {
                kind = CE_TOKEN_TYPE;
            }
            else if (first_word && language == AROMA_CODE_LANGUAGE_DOCKERFILE)
            {
                kind = CE_TOKEN_KEYWORD;
            }
            else if (language == AROMA_CODE_LANGUAGE_CSS && i < len && s[i] == ':')
            {
                kind = CE_TOKEN_PROPERTY;
            }
            else if (ce_language_properties_before_colon(language) && look < len &&
                     s[look] == ':' && !(look + 1 < len && s[look + 1] == ':'))
            {
                kind = CE_TOKEN_PROPERTY;
            }
            else if (language == AROMA_CODE_LANGUAGE_TOML && look < len && s[look] == '=')
            {
                kind = CE_TOKEN_PROPERTY;
            }
            else if (look < len && s[look] == '(')
            {
                kind = CE_TOKEN_FUNCTION;
            }
            else if (isupper((unsigned char)word[0]))
            {
                kind = CE_TOKEN_TYPE;
            }
            else if (language == AROMA_CODE_LANGUAGE_TSX && i < len && s[i] == '!')
            {
                kind = CE_TOKEN_FUNCTION;
            }
            ce_token_add(tokens, &count, max, start, i, kind);
            first_word = false;
            continue;
        }

        if (strchr("(){}[]<>+-*/%=!&|^~?:;,.@", (int)c))
        {
            int start = i++;
            if (c == '<' && (language == AROMA_CODE_LANGUAGE_TSX ||
                             language == AROMA_CODE_LANGUAGE_TYPESCRIPT))
            {
                in_tag = true;
            }
            if (c == '>' && in_tag)
                in_tag = false;
            ce_token_add(tokens, &count, max, start, i, CE_TOKEN_PUNCTUATION);
            first_word = false;
            continue;
        }

        {
            int start = i++;
            ce_token_add(tokens, &count, max, start, i, CE_TOKEN_TEXT);
            first_word = false;
        }
    }

    return count;
}

static void ce_font_line_height(const CeEditor *e, int *out_height)
{
    int height = e && e->font ? aroma_font_get_line_height(e->font) : 0;
    if (height < CE_MIN_LINE_HEIGHT)
        height = 18;
    if (out_height)
        *out_height = height;
}

static float ce_measure_char(CeEditor *e)
{
    if (e->char_width > 0.0f)
        return e->char_width;
    if (e->font && e->last_window_id >= 0)
    {
        AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
        if (gfx && gfx->measure_text)
        {
            float width = gfx->measure_text((size_t)e->last_window_id, e->font, "M", 1.0f);
            if (width >= 2.0f && width <= 40.0f)
            {
                e->char_width = width;
                return width;
            }
        }
    }
    e->char_width = 8.0f;
    return e->char_width;
}

typedef struct CeMetrics CeMetrics;

static int ce_expand_line(const CeEditor *editor, int line, char *out, size_t out_size);

static int ce_text_width(const CeEditor *editor, const char *text, int len)
{
    if (!text || len <= 0)
        return 0;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx || !gfx->measure_text || !editor->font || editor->last_window_id < 0)
    {
        return len * 8;
    }
    char buf[768];
    if (len > (int)sizeof(buf) - 1)
        len = (int)sizeof(buf) - 1;
    memcpy(buf, text, (size_t)len);
    buf[len] = '\0';
    return (int)gfx->measure_text((size_t)editor->last_window_id, editor->font, buf, 1.0f);
}

/* Return pixel x from line start (not including gutter or scroll) for a
 * visual column on a line. visual_column indexes into the tab-expanded line
 * buffer produced by ce_expand_line. */
static int ce_pixel_x_from_column(const CeEditor *editor, int line, int visual_column)
{
    if (!editor || !editor->font || editor->last_window_id < 0)
        return visual_column * 8;
    char display[4096];
    int display_len = ce_expand_line(editor, line, display, sizeof(display));
    if (display_len <= 0)
        return 0;
    if (visual_column > display_len)
        visual_column = display_len;
    if (visual_column < 0)
        visual_column = 0;
    if (visual_column > 0)
    {
        int measure_len = visual_column;
        if (measure_len > 767)
            measure_len = 767;
        return ce_text_width(editor, display, measure_len);
    }
    return 0;
}

/* Return visual column for a pixel x on a line. pixel_x is relative to line
 * start (not including gutter or scroll). Walks characters measuring
 * cumulative width to find which character contains the pixel. */
static int ce_column_for_pixel(const CeEditor *editor, int line, int pixel_x)
{
    if (!editor || !editor->font || editor->last_window_id < 0)
    {
        return pixel_x / 8;
    }
    char display[4096];
    int display_len = ce_expand_line(editor, line, display, sizeof(display));
    if (display_len <= 0)
        return 0;
    if (pixel_x <= 0)
        return 0;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    int cumulative = 0;
    for (int i = 0; i < display_len; i++)
    {
        char ch = display[i];
        char buf[2] = {ch, '\0'};
        int w = 8;
        if (gfx && gfx->measure_text)
        {
            w = (int)gfx->measure_text((size_t)editor->last_window_id, editor->font,
                                       buf, 1.0f);
            if (w < 1)
                w = 1;
        }
        if (cumulative + w > pixel_x)
            return i;
        cumulative += w;
    }
    return display_len;
}

static int ce_visual_columns_for_text(const char *s, size_t start, size_t end, int tab_width)
{
    int col = 0;
    if (!s || start >= end)
        return 0;
    for (size_t i = start; i < end && s[i] && s[i] != '\n' && s[i] != '\r'; i++)
    {
        if (s[i] == '\t')
            col += tab_width - (col % tab_width);
        else
            col++;
    }
    return col;
}

static int ce_line_display_columns(const CeEditor *e, int line)
{
    if (!e || line < 0 || line >= e->line_count)
        return 0;
    size_t start = e->line_starts[line];
    size_t end = start;
    while (end < e->length && e->text[end] != '\n' && e->text[end] != '\r')
        end++;
    return ce_visual_columns_for_text(e->text, start, end, e->tab_width > 0 ? e->tab_width : 4);
}

static int ce_wrap_cols(const CeEditor *e, int code_width, int char_width)
{
    if (!e || !e->word_wrap)
        return 0;
    if (char_width < 1)
        char_width = 8;
    int cols = code_width / char_width;
    return cols > 0 ? cols : 1;
}

static int ce_line_segment_count(const CeEditor *e, int line, int wrap_cols)
{
    if (wrap_cols <= 0)
        return 1;
    int cols = ce_line_display_columns(e, line);
    int segments = (cols + wrap_cols - 1) / wrap_cols;
    return segments > 0 ? segments : 1;
}

static int ce_visual_row_for_line(const CeEditor *e, int line, int wrap_cols)
{
    if (!e || wrap_cols <= 0)
        return line;
    int row = 0;
    if (line > e->line_count)
        line = e->line_count;
    for (int i = 0; i < line; i++)
        row += ce_line_segment_count(e, i, wrap_cols);
    return row;
}

static int ce_total_visual_rows(const CeEditor *e, int wrap_cols)
{
    if (!e)
        return 1;
    if (wrap_cols <= 0)
        return e->line_count > 0 ? e->line_count : 1;
    int rows = 0;
    for (int i = 0; i < e->line_count; i++)
        rows += ce_line_segment_count(e, i, wrap_cols);
    return rows > 0 ? rows : 1;
}

static void ce_visual_row_to_line(const CeEditor *e, int row, int wrap_cols,
                                  int *line, int *segment)
{
    int out_line = 0;
    int out_segment = 0;
    if (e && wrap_cols > 0)
    {
        int acc = 0;
        for (int i = 0; i < e->line_count; i++)
        {
            int n = ce_line_segment_count(e, i, wrap_cols);
            if (row < acc + n)
            {
                out_line = i;
                out_segment = row - acc;
                break;
            }
            acc += n;
            out_line = i;
        }
    }
    else
    {
        out_line = row;
    }
    if (e && e->line_count > 0)
    {
        if (out_line < 0)
            out_line = 0;
        if (out_line >= e->line_count)
            out_line = e->line_count - 1;
    }
    if (line)
        *line = out_line;
    if (segment)
        *segment = out_segment;
}

static void ce_free_snapshot(CeSnapshot *snapshot)
{
    if (snapshot && snapshot->text)
    {
        free(snapshot->text);
        snapshot->text = NULL;
    }
}

static void ce_clear_undo_redo(CeEditor *e)
{
    if (!e)
        return;
    for (int i = 0; i < e->undo_count; i++)
        ce_free_snapshot(&e->undo_stack[i]);
    for (int i = 0; i < e->redo_count; i++)
        ce_free_snapshot(&e->redo_stack[i]);
    e->undo_count = 0;
    e->redo_count = 0;
}

static bool ce_push_snapshot(CeSnapshot *stack, int *count, int max, const CeEditor *e)
{
    if (*count >= max)
    {
        ce_free_snapshot(&stack[0]);
        memmove(&stack[0], &stack[1], sizeof(CeSnapshot) * (size_t)(max - 1));
        (*count)--;
    }
    CeSnapshot *snapshot = &stack[*count];
    memset(snapshot, 0, sizeof(*snapshot));
    if (e->length > CE_UNDO_TEXT_LIMIT)
        return false;
    snapshot->text = (char *)malloc(e->length + 1);
    if (!snapshot->text)
        return false;
    memcpy(snapshot->text, e->text, e->length + 1);
    snapshot->cursor = e->cursor;
    snapshot->anchor = e->anchor;
    (*count)++;
    return true;
}

static void ce_clear_redo(CeEditor *e)
{
    if (!e)
        return;
    for (int i = 0; i < e->redo_count; i++)
        ce_free_snapshot(&e->redo_stack[i]);
    e->redo_count = 0;
}

static void ce_push_undo(CeEditor *e)
{
    if (!e || e->read_only)
        return;
    if (!ce_push_snapshot(e->undo_stack, &e->undo_count, CE_MAX_UNDO, e))
        return;
    ce_clear_redo(e);
}

static void ce_rebuild_lines(CeEditor *e)
{
    if (!e)
        return;
    if (e->line_capacity < 16)
    {
        int new_cap = 64;
        size_t *new_starts = (size_t *)realloc(e->line_starts, sizeof(size_t) * (size_t)new_cap);
        if (!new_starts)
            return;
        e->line_starts = new_starts;
        e->line_capacity = new_cap;
    }
    int count = 1;
    e->line_starts[0] = 0;
    for (size_t i = 0; i < e->length; i++)
    {
        if (e->text[i] == '\n')
        {
            if (count >= e->line_capacity)
            {
                int new_cap = e->line_capacity * 2;
                size_t *new_starts = (size_t *)realloc(e->line_starts,
                                                       sizeof(size_t) * (size_t)new_cap);
                if (!new_starts)
                    break;
                e->line_starts = new_starts;
                e->line_capacity = new_cap;
            }
            e->line_starts[count++] = i + 1;
        }
    }
    e->line_count = count;
    e->max_columns = 0;
    for (int line = 0; line < e->line_count; line++)
    {
        int cols = ce_line_display_columns(e, line);
        if (cols > e->max_columns)
            e->max_columns = cols;
    }
}

static int ce_line_of_offset(const CeEditor *e, size_t offset)
{
    if (!e || e->line_count <= 0)
        return 0;
    if (offset >= e->length)
        return e->line_count - 1;
    int low = 0;
    int high = e->line_count - 1;
    while (low <= high)
    {
        int mid = (low + high) / 2;
        size_t start = e->line_starts[mid];
        size_t next = mid + 1 < e->line_count ? e->line_starts[mid + 1] : e->length + 1;
        if (offset < start)
            high = mid - 1;
        else if (offset >= next)
            low = mid + 1;
        else
            return mid;
    }
    return ce_clamp_int(low, 0, e->line_count - 1);
}

static int ce_column_of_offset(const CeEditor *e, size_t offset)
{
    if (!e || e->line_count <= 0)
        return 0;
    int line = ce_line_of_offset(e, offset);
    size_t start = e->line_starts[line];
    if (offset < start)
        return 0;
    return ce_visual_columns_for_text(e->text, start, offset, e->tab_width > 0 ? e->tab_width : 4);
}

static size_t ce_line_start(const CeEditor *e, int line)
{
    if (!e || e->line_count <= 0)
        return 0;
    line = ce_clamp_int(line, 0, e->line_count - 1);
    return e->line_starts[line];
}

static size_t ce_line_end_content(const CeEditor *e, int line)
{
    if (!e)
        return 0;
    line = ce_clamp_int(line, 0, e->line_count - 1);
    size_t pos = e->line_starts[line];
    while (pos < e->length && e->text[pos] != '\n' && e->text[pos] != '\r')
        pos++;
    return pos;
}

static size_t ce_line_end_include_newline(const CeEditor *e, int line)
{
    if (!e)
        return 0;
    line = ce_clamp_int(line, 0, e->line_count - 1);
    if (line + 1 < e->line_count)
        return e->line_starts[line + 1];
    return e->length;
}

static size_t ce_offset_for_line_column(const CeEditor *e, int line, int column)
{
    if (!e)
        return 0;
    line = ce_clamp_int(line, 0, e->line_count - 1);
    size_t pos = e->line_starts[line];
    size_t end = ce_line_end_content(e, line);
    int col = 0;
    int target = column > 0 ? column - 1 : 0;
    while (pos < end)
    {
        int advance = 1;
        if (e->text[pos] == '\t')
            advance = (e->tab_width > 0 ? e->tab_width : 4) - (col % (e->tab_width > 0 ? e->tab_width : 4));
        if (col + advance > target)
            break;
        if (col >= target)
            break;
        col += advance;
        pos++;
    }
    return pos;
}

static void ce_reset_display(CeEditor *e)
{
    if (!e)
        return;
    e->desired_column = ce_column_of_offset(e, e->cursor);
    e->show_cursor = true;
    e->cursor_blink_ms = ce_now_ms();
    e->hover_visible = false;
    e->hover_waiting = false;
    e->find_total = 0;
    e->find_index = 0;
    e->completion_count = 0;
    e->completions_visible = false;
    e->completion_index = 0;
}

static void ce_notify_cursor(CeEditor *e)
{
    if (!e || !e->node)
        return;
    int line = ce_line_of_offset(e, e->cursor) + 1;
    int column = ce_column_of_offset(e, e->cursor) + 1;
    int selection_chars = 0;
    if (ce_has_selection(e))
        selection_chars = (int)(ce_sel_end(e) - ce_sel_start(e));
    if (line == e->last_line && column == e->last_column &&
        selection_chars == e->last_selection_chars)
        return;
    e->last_line = line;
    e->last_column = column;
    e->last_selection_chars = selection_chars;
    if (e->on_cursor_changed)
        e->on_cursor_changed(e->node, line, column, selection_chars, e->on_cursor_data);
}

static void ce_notify_change(CeEditor *e)
{
    if (!e || !e->node)
        return;
    if (e->on_change)
        e->on_change(e->node, e->text ? e->text : "", e->on_change_data);
    aroma_node_invalidate(e->node);
    ce_notify_cursor(e);
}

static void ce_update_cursor(CeEditor *e, size_t cursor, bool extend_selection)
{
    if (!e)
        return;
    cursor = ce_clamp_offset(e, cursor);
    e->cursor = cursor;
    if (!extend_selection)
        e->anchor = cursor;
    e->desired_column = ce_column_of_offset(e, cursor);
    e->show_cursor = true;
    e->cursor_blink_ms = ce_now_ms();
    aroma_node_invalidate(e->node);
    ce_notify_cursor(e);
}

static void ce_ensure_capacity(CeEditor *e, size_t needed)
{
    if (!e || needed + 1 <= e->capacity)
        return;
    size_t capacity = e->capacity ? e->capacity : 256;
    while (capacity < needed + 1)
        capacity *= 2;
    char *grown = (char *)realloc(e->text, capacity);
    if (!grown)
        return;
    e->text = grown;
    e->capacity = capacity;
}

static void ce_replace_raw(CeEditor *e, size_t start, size_t end, const char *insert,
                           size_t insert_len)
{
    if (!e || e->read_only)
        return;
    start = ce_clamp_offset(e, start);
    end = ce_clamp_offset(e, end);
    if (end < start)
    {
        size_t tmp = start;
        start = end;
        end = tmp;
    }
    if (!insert)
    {
        insert = "";
        insert_len = 0;
    }
    size_t removed = end - start;
    if (insert_len > removed)
    {
        ce_ensure_capacity(e, e->length + insert_len - removed);
        if (e->length + insert_len - removed + 1 > e->capacity)
            return;
    }
    memmove(e->text + start + insert_len, e->text + end, e->length - end + 1);
    if (insert_len > 0)
        memcpy(e->text + start, insert, insert_len);
    e->length = e->length - removed + insert_len;
    e->text[e->length] = '\0';
    ce_rebuild_lines(e);
    aroma_node_invalidate(e->node);
}

static void ce_after_text_edit(CeEditor *e)
{
    if (!e)
        return;
    ce_notify_change(e);
    ce_clear_redo(e);
}

static void ce_set_cursor_after_edit(CeEditor *e, size_t cursor)
{
    if (!e)
        return;
    e->cursor = ce_clamp_offset(e, cursor);
    e->anchor = e->cursor;
    e->desired_column = ce_column_of_offset(e, e->cursor);
    e->show_cursor = true;
    e->cursor_blink_ms = ce_now_ms();
    ce_notify_cursor(e);
}

static void ce_delete_selection_internal(CeEditor *e)
{
    if (!e || !ce_has_selection(e))
        return;
    size_t start = ce_sel_start(e);
    size_t end = ce_sel_end(e);
    ce_push_undo(e);
    ce_replace_raw(e, start, end, "", 0);
    ce_set_cursor_after_edit(e, start);
    ce_after_text_edit(e);
}

static void ce_insert_internal(CeEditor *e, const char *text, size_t len)
{
    if (!e || e->read_only || !text || len == 0)
        return;
    size_t start = ce_has_selection(e) ? ce_sel_start(e) : e->cursor;
    size_t end = ce_has_selection(e) ? ce_sel_end(e) : e->cursor;
    ce_push_undo(e);
    ce_replace_raw(e, start, end, text, len);
    ce_set_cursor_after_edit(e, start + len);
    ce_after_text_edit(e);
}

static void ce_insert_char_internal(CeEditor *e, char ch)
{
    ce_insert_internal(e, &ch, 1);
}

static void ce_backspace_internal(CeEditor *e)
{
    if (!e || e->read_only)
        return;
    if (ce_has_selection(e))
    {
        ce_delete_selection_internal(e);
        return;
    }
    if (e->cursor == 0)
        return;
    size_t start = e->cursor - 1;
    if (e->cursor >= 2 && (unsigned char)e->text[e->cursor - 1] >= 0x80 &&
        (unsigned char)e->text[e->cursor - 1] < 0xC0)
    {
        while (start > 0 && ((unsigned char)e->text[start] & 0xC0) == 0x80)
            start--;
    }
    ce_push_undo(e);
    ce_replace_raw(e, start, e->cursor, "", 0);
    ce_set_cursor_after_edit(e, start);
    ce_after_text_edit(e);
}

static void ce_delete_forward_internal(CeEditor *e)
{
    if (!e || e->read_only)
        return;
    if (ce_has_selection(e))
    {
        ce_delete_selection_internal(e);
        return;
    }
    if (e->cursor >= e->length)
        return;
    size_t end = e->cursor + 1;
    if ((unsigned char)e->text[e->cursor] >= 0xC0)
    {
        while (end < e->length && ((unsigned char)e->text[end] & 0xC0) == 0x80)
            end++;
    }
    ce_push_undo(e);
    ce_replace_raw(e, e->cursor, end, "", 0);
    ce_set_cursor_after_edit(e, e->cursor);
    ce_after_text_edit(e);
}

static void ce_delete_word_backward(CeEditor *e)
{
    if (!e || e->read_only)
        return;
    if (ce_has_selection(e))
    {
        ce_delete_selection_internal(e);
        return;
    }
    if (e->cursor == 0)
        return;
    size_t start = e->cursor - 1;
    while (start > 0 && (e->text[start - 1] == ' ' || e->text[start - 1] == '\t'))
        start--;
    while (start > 0 && e->text[start - 1] != ' ' && e->text[start - 1] != '\t' &&
           e->text[start - 1] != '\n')
        start--;
    ce_push_undo(e);
    ce_replace_raw(e, start, e->cursor, "", 0);
    ce_set_cursor_after_edit(e, start);
    ce_after_text_edit(e);
}

static void ce_delete_word_forward(CeEditor *e)
{
    if (!e || e->read_only)
        return;
    if (ce_has_selection(e))
    {
        ce_delete_selection_internal(e);
        return;
    }
    if (e->cursor >= e->length)
        return;
    size_t end = e->cursor;
    while (end < e->length && (e->text[end] == ' ' || e->text[end] == '\t'))
        end++;
    while (end < e->length && e->text[end] != ' ' && e->text[end] != '\t' &&
           e->text[end] != '\n')
        end++;
    ce_push_undo(e);
    ce_replace_raw(e, e->cursor, end, "", 0);
    ce_set_cursor_after_edit(e, e->cursor);
    ce_after_text_edit(e);
}

static void ce_delete_to_end_of_line(CeEditor *e)
{
    if (!e || e->read_only)
        return;
    if (ce_has_selection(e))
    {
        ce_delete_selection_internal(e);
        return;
    }
    size_t end = e->cursor;
    while (end < e->length && e->text[end] != '\n')
        end++;
    if (end == e->cursor)
        return;
    ce_push_undo(e);
    ce_replace_raw(e, e->cursor, end, "", 0);
    ce_set_cursor_after_edit(e, e->cursor);
    ce_after_text_edit(e);
}

static void ce_delete_to_start_of_line(CeEditor *e)
{
    if (!e || e->read_only)
        return;
    if (ce_has_selection(e))
    {
        ce_delete_selection_internal(e);
        return;
    }
    if (e->cursor == 0)
        return;
    size_t start = e->cursor - 1;
    while (start > 0 && e->text[start - 1] != '\n')
        start--;
    ce_push_undo(e);
    ce_replace_raw(e, start, e->cursor, "", 0);
    ce_set_cursor_after_edit(e, start);
    ce_after_text_edit(e);
}

static void ce_adjust_scroll(CeEditor *e, int delta_x, int delta_y)
{
    if (!e)
        return;
    int line_height = 18;
    ce_font_line_height(e, &line_height);

    int digits = 1;
    int lines = e->line_count > 0 ? e->line_count : 1;
    while (lines >= 10)
    {
        digits++;
        lines /= 10;
    }
    int gutter = e->line_numbers ? 18 + digits * (int)ce_measure_char(e) + 8 : 8;
    int minimap = (e->minimap && e->rect.width > 620) ? 82 : 0;
    int code_width = e->rect.width - gutter - minimap - 8;
    int code_height = e->rect.height - 8;
    if (code_width < 32)
        code_width = 32;
    if (code_height < line_height * 2)
        code_height = line_height * 2;

    int char_width = (int)ce_measure_char(e);
    if (char_width < 1)
        char_width = 8;
    int wrap_cols = ce_wrap_cols(e, code_width, char_width);
    int rows = ce_total_visual_rows(e, wrap_cols);
    int content_height = rows * line_height + 8;
    int content_width = (int)((float)e->max_columns * ce_measure_char(e)) + 24;
    int max_y = content_height > code_height ? content_height - code_height : 0;
    int max_x = wrap_cols > 0 ? 0
                              : (content_width > code_width ? content_width - code_width : 0);
    e->scroll_x = ce_clamp_int(e->scroll_x + delta_x, 0, max_x);
    e->scroll_y = ce_clamp_int(e->scroll_y + delta_y, 0, max_y);
    aroma_node_invalidate(e->node);
}

static void ce_clamp_scroll(CeEditor *e)
{
    if (!e)
        return;
    int line_height = 18;
    ce_font_line_height(e, &line_height);

    int digits = 1;
    int lines = e->line_count > 0 ? e->line_count : 1;
    while (lines >= 10)
    {
        digits++;
        lines /= 10;
    }
    int gutter = e->line_numbers ? 18 + digits * (int)ce_measure_char(e) + 8 : 8;
    int minimap = (e->minimap && e->rect.width > 620) ? 82 : 0;
    int code_width = e->rect.width - gutter - minimap - 8;
    int code_height = e->rect.height - 8;
    if (code_width < 32)
        code_width = 32;
    if (code_height < line_height * 2)
        code_height = line_height * 2;

    int char_width = (int)ce_measure_char(e);
    if (char_width < 1)
        char_width = 8;
    int wrap_cols = ce_wrap_cols(e, code_width, char_width);
    int rows = ce_total_visual_rows(e, wrap_cols);
    int content_height = rows * line_height + 8;
    int content_width = (int)((float)e->max_columns * ce_measure_char(e)) + 24;
    int max_y = content_height > code_height ? content_height - code_height : 0;
    int max_x = wrap_cols > 0 ? 0
                              : (content_width > code_width ? content_width - code_width : 0);
    e->scroll_x = ce_clamp_int(e->scroll_x, 0, max_x);
    e->scroll_y = ce_clamp_int(e->scroll_y, 0, max_y);
}

static void ce_ensure_cursor_visible(CeEditor *e)
{
    if (!e)
        return;
    int line_height = 18;
    ce_font_line_height(e, &line_height);
    int line = ce_line_of_offset(e, e->cursor);
    int column = ce_column_of_offset(e, e->cursor);
    float char_width = ce_measure_char(e);

    int digits = 1;
    int lines = e->line_count > 0 ? e->line_count : 1;
    while (lines >= 10)
    {
        digits++;
        lines /= 10;
    }
    int gutter = e->line_numbers ? 18 + digits * (int)char_width + 8 : 8;
    int minimap = (e->minimap && e->rect.width > 620) ? 82 : 0;
    int code_width = e->rect.width - gutter - minimap - 8;
    int code_height = e->rect.height - 8;
    if (code_width < 32)
        code_width = 32;
    if (code_height < line_height * 2)
        code_height = line_height * 2;

    int char_width_px = (int)char_width;
    if (char_width_px < 1)
        char_width_px = 8;
    int wrap_cols = ce_wrap_cols(e, code_width, char_width_px);
    int visual_row = ce_visual_row_for_line(e, line, wrap_cols);
    int segment = 0;
    int cursor_x = ce_pixel_x_from_column(e, line, column);
    if (wrap_cols > 0)
    {
        segment = column / wrap_cols;
        visual_row += segment;
        int segment_start_px = ce_pixel_x_from_column(e, line, segment * wrap_cols);
        cursor_x -= segment_start_px;
    }
    int cursor_y = visual_row * line_height;
    if (cursor_y < e->scroll_y)
        e->scroll_y = cursor_y;
    else if (cursor_y + line_height > e->scroll_y + code_height)
        e->scroll_y = cursor_y + line_height - code_height;
    if (wrap_cols > 0)
    {
        e->scroll_x = 0;
    }
    else if (cursor_x < e->scroll_x)
        e->scroll_x = cursor_x;
    else if (cursor_x + char_width_px > e->scroll_x + code_width)
        e->scroll_x = cursor_x + char_width_px - code_width;
    ce_clamp_scroll(e);
}

static void ce_move_horizontal(CeEditor *e, int direction, bool extend)
{
    if (!e)
        return;
    size_t pos = e->cursor;
    if (direction < 0)
    {
        if (!extend && ce_has_selection(e) && e->cursor == ce_sel_end(e))
            pos = ce_sel_start(e);
        else if (pos > 0)
        {
            pos--;
            while (pos > 0 && ((unsigned char)e->text[pos] & 0xC0) == 0x80)
                pos--;
        }
    }
    else
    {
        if (!extend && ce_has_selection(e) && e->cursor == ce_sel_start(e))
            pos = ce_sel_end(e);
        else if (pos < e->length)
        {
            pos++;
            while (pos < e->length && ((unsigned char)e->text[pos] & 0xC0) == 0x80)
                pos++;
        }
    }
    ce_update_cursor(e, pos, extend);
    if (!extend)
        e->anchor = e->cursor;
    ce_ensure_cursor_visible(e);
}

static void ce_move_word(CeEditor *e, int direction, bool extend)
{
    if (!e)
        return;
    size_t pos = e->cursor;
    if (direction < 0)
    {
        while (pos > 0 && isspace((unsigned char)e->text[pos - 1]))
            pos--;
        while (pos > 0 && ce_is_word_char((unsigned char)e->text[pos - 1]))
            pos--;
    }
    else
    {
        while (pos < e->length && ce_is_word_char((unsigned char)e->text[pos]))
            pos++;
        while (pos < e->length && isspace((unsigned char)e->text[pos]))
            pos++;
    }
    ce_update_cursor(e, pos, extend);
    ce_ensure_cursor_visible(e);
}

static void ce_move_vertical(CeEditor *e, int direction, bool extend)
{
    if (!e)
        return;
    int line = ce_line_of_offset(e, e->cursor);
    int target_line = ce_clamp_int(line + direction, 0, e->line_count - 1);
    size_t pos = ce_offset_for_line_column(e, target_line, e->desired_column + 1);
    size_t end = ce_line_end_content(e, target_line);
    if (pos > end)
        pos = end;
    e->cursor = pos;
    if (!extend)
        e->anchor = pos;
    e->show_cursor = true;
    e->cursor_blink_ms = ce_now_ms();
    ce_notify_cursor(e);
    ce_ensure_cursor_visible(e);
    aroma_node_invalidate(e->node);
}

static void ce_move_home(CeEditor *e, bool extend, bool document)
{
    if (!e)
        return;
    if (document)
    {
        ce_update_cursor(e, 0, extend);
        ce_ensure_cursor_visible(e);
        return;
    }
    int line = ce_line_of_offset(e, e->cursor);
    size_t start = ce_line_start(e, line);
    size_t first = start;
    while (first < ce_line_end_content(e, line) &&
           (e->text[first] == ' ' || e->text[first] == '\t'))
        first++;
    size_t pos = (e->cursor == first) ? start : first;
    ce_update_cursor(e, pos, extend);
    ce_ensure_cursor_visible(e);
}

static void ce_move_end(CeEditor *e, bool extend, bool document)
{
    if (!e)
        return;
    if (document)
    {
        ce_update_cursor(e, e->length, extend);
        ce_ensure_cursor_visible(e);
        return;
    }
    int line = ce_line_of_offset(e, e->cursor);
    ce_update_cursor(e, ce_line_end_content(e, line), extend);
    ce_ensure_cursor_visible(e);
}

static int ce_leading_spaces(const CeEditor *e, int line)
{
    if (!e)
        return 0;
    size_t pos = ce_line_start(e, line);
    size_t end = ce_line_end_content(e, line);
    int count = 0;
    while (pos < end && e->text[pos] == ' ')
    {
        count++;
        pos++;
    }
    return count;
}

static void ce_insert_newline(CeEditor *e)
{
    if (!e || e->read_only)
        return;
    int line = ce_line_of_offset(e, e->cursor);
    int indent = e->auto_indent ? ce_leading_spaces(e, line) : 0;
    char previous = ' ';
    if (e->cursor > 0)
        previous = e->text[e->cursor - 1];
    bool extra = e->auto_indent &&
                 (previous == '{' || previous == '[' || previous == '(' || previous == ':');
    char buffer[512];
    size_t n = 0;
    buffer[n++] = '\n';
    for (int i = 0; i < indent && n + 1 < sizeof(buffer); i++)
        buffer[n++] = ' ';
    if (extra)
    {
        int add = e->tab_width > 0 ? e->tab_width : 4;
        for (int i = 0; i < add && n + 1 < sizeof(buffer); i++)
            buffer[n++] = ' ';
    }
    size_t start = ce_has_selection(e) ? ce_sel_start(e) : e->cursor;
    size_t end = ce_has_selection(e) ? ce_sel_end(e) : e->cursor;
    size_t cursor = start + n;
    ce_push_undo(e);
    ce_replace_raw(e, start, end, buffer, n);
    ce_set_cursor_after_edit(e, cursor);
    ce_after_text_edit(e);
    ce_ensure_cursor_visible(e);
}

void aroma_code_editor_insert_newline(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    ce_insert_newline(editor);
}

static void ce_type_character(CeEditor *e, char ch)
{
    if (!e || e->read_only)
        return;
    if (ce_has_selection(e))
    {
        ce_delete_selection_internal(e);
    }
    char close = 0;
    if (ch == '(') close = ')';
    else if (ch == '[') close = ']';
    else if (ch == '{') close = '}';
    else if (ch == '"') close = '"';
    else if (ch == '\'') close = '\'';
    else if (ch == '`') close = '`';

    if (close && e->cursor < e->length)
    {
        char next = e->text[e->cursor];
        if (next == close)
        {
            ce_update_cursor(e, e->cursor + 1, false);
            return;
        }
    }

    if (close)
    {
        char pair[2] = {ch, close};
        ce_push_undo(e);
        size_t start = e->cursor;
        ce_replace_raw(e, start, start, pair, 2);
        e->cursor = ce_clamp_offset(e, start + 1);
        e->anchor = e->cursor;
        e->desired_column = ce_column_of_offset(e, e->cursor);
        e->show_cursor = true;
        e->cursor_blink_ms = ce_now_ms();
        ce_after_text_edit(e);
        ce_ensure_cursor_visible(e);
        return;
    }
    ce_insert_char_internal(e, ch);
    ce_ensure_cursor_visible(e);
}

static void ce_indent_selection(CeEditor *e, bool outdent)
{
    if (!e || e->read_only)
        return;
    int start_line = ce_line_of_offset(e, ce_sel_start(e));
    int end_line = ce_line_of_offset(e, ce_sel_end(e));
    if (ce_has_selection(e) && ce_sel_end(e) == ce_line_start(e, end_line) && end_line > start_line)
        end_line--;
    int indent = e->tab_width > 0 ? e->tab_width : 4;
    ce_push_undo(e);
    if (outdent)
    {
        for (int line = start_line; line <= end_line; line++)
        {
            size_t start = ce_line_start(e, line);
            int remove = 0;
            while (remove < indent && start + (size_t)remove < e->length &&
                   e->text[start + (size_t)remove] == ' ')
                remove++;
            if (remove > 0)
                ce_replace_raw(e, start, start + (size_t)remove, "", 0);
        }
    }
    else
    {
        for (int line = start_line; line <= end_line; line++)
        {
            size_t start = ce_line_start(e, line);
            char spaces[32];
            int n = indent > (int)sizeof(spaces) ? (int)sizeof(spaces) : indent;
            memset(spaces, ' ', (size_t)n);
            ce_replace_raw(e, start, start, spaces, (size_t)n);
        }
    }
    ce_set_cursor_after_edit(e, ce_line_start(e, start_line));
    ce_after_text_edit(e);
    ce_ensure_cursor_visible(e);
}

static void ce_insert_tab(CeEditor *e)
{
    if (!e || e->read_only)
        return;
    if (ce_has_selection(e))
    {
        ce_indent_selection(e, false);
        return;
    }
    int tab_width = e->tab_width > 0 ? e->tab_width : 4;
    int column = ce_column_of_offset(e, e->cursor);
    int spaces = tab_width - (column % tab_width);
    char buffer[16];
    memset(buffer, ' ', (size_t)spaces);
    ce_insert_internal(e, buffer, (size_t)spaces);
    ce_ensure_cursor_visible(e);
}

static size_t ce_find_next_match(const CeEditor *e, size_t from, const char *query,
                                 bool case_sensitive)
{
    if (!e || !e->text || !query || query[0] == '\0')
        return SIZE_MAX;
    size_t query_len = strlen(query);
    if (query_len > e->length)
        return SIZE_MAX;
    for (size_t i = from; i + query_len <= e->length; i++)
    {
        bool match = true;
        for (size_t j = 0; j < query_len; j++)
        {
            unsigned char a = (unsigned char)e->text[i + j];
            unsigned char b = (unsigned char)query[j];
            if (!case_sensitive)
            {
                a = (unsigned char)tolower(a);
                b = (unsigned char)tolower(b);
            }
            if (a != b)
            {
                match = false;
                break;
            }
        }
        if (match)
            return i;
    }
    return SIZE_MAX;
}

static void ce_rebuild_find_matches(CeEditor *e, bool select_first)
{
    if (!e)
        return;
    e->find_total = 0;
    e->find_index = 0;
    if (!e->find_visible || e->find_query[0] == '\0')
        return;
    size_t from = 0;
    while (e->find_total < CE_MAX_FIND_MATCHES)
    {
        size_t match = ce_find_next_match(e, from, e->find_query, false);
        if (match == SIZE_MAX)
            break;
        e->find_offsets[e->find_total++] = match;
        from = match + 1;
    }
    if (select_first && e->find_total > 0)
    {
        size_t match = e->find_offsets[0];
        size_t len = strlen(e->find_query);
        e->cursor = match;
        e->anchor = match + len;
        e->find_index = 0;
        e->show_cursor = true;
        e->cursor_blink_ms = ce_now_ms();
        e->desired_column = ce_column_of_offset(e, e->cursor);
        ce_ensure_cursor_visible(e);
        ce_notify_cursor(e);
    }
}

static void ce_find_next(CeEditor *e, int direction)
{
    if (!e || e->find_total <= 0)
        return;
    if (direction < 0)
        e->find_index = (e->find_index + e->find_total - 1) % e->find_total;
    else
        e->find_index = (e->find_index + 1) % e->find_total;
    size_t match = e->find_offsets[e->find_index];
    size_t len = strlen(e->find_query);
    e->cursor = match;
    e->anchor = match + len;
    e->show_cursor = true;
    e->cursor_blink_ms = ce_now_ms();
    e->desired_column = ce_column_of_offset(e, e->cursor);
    ce_ensure_cursor_visible(e);
    ce_notify_cursor(e);
    aroma_node_invalidate(e->node);
}

static void ce_accept_completion(CeEditor *e)
{
    if (!e || !e->completions_visible || e->completion_count <= 0)
        return;
    int index = ce_clamp_int(e->completion_index, 0, e->completion_count - 1);
    AromaCodeCompletion *completion = &e->completions[index];
    const char *insert = completion->insert_text[0] ? completion->insert_text : completion->label;
    if (!insert || !insert[0])
        return;
    size_t len = strlen(insert);
    size_t start = e->cursor;
    size_t end = e->cursor;
    size_t include_open = ce_include_open_offset(e);
    if (ce_has_selection(e))
    {
        start = ce_sel_start(e);
        end = ce_sel_end(e);
    }
    else if (include_open != CE_NO_OFFSET)
    {
        start = include_open + 1;
    }
    else
    {
        while (start > 0 && ce_is_completion_char((unsigned char)e->text[start - 1]))
            start--;
    }
    /* The editor auto-inserts a closing quote for `#include "`. Reuse that
       quote instead of adding a second one. */
    if (include_open != CE_NO_OFFSET && end < e->length && len > 0)
    {
        char close = e->text[include_open] == '<' ? '>' : '"';
        if (e->text[end] == close && insert[len - 1] == close)
            len--;
    }
    if (start < end)
    {
        ce_push_undo(e);
        ce_replace_raw(e, start, end, insert, len);
        ce_set_cursor_after_edit(e, start + len);
        ce_after_text_edit(e);
    }
    else
    {
        ce_insert_internal(e, insert, len);
    }
    e->completions_visible = false;
    e->completion_count = 0;
    e->hover_visible = false;
    ce_ensure_cursor_visible(e);
}
AromaNode *aroma_code_editor_create(AromaNode *parent, int x, int y, int width, int height)
{
    if (!parent || width <= 0 || height <= 0)
    {
        LOG_ERROR("Invalid code editor parameters\n");
        return NULL;
    }

    CeEditor *editor = (CeEditor *)aroma_widget_alloc(sizeof(CeEditor));
    if (!editor)
    {
        LOG_ERROR("Failed to allocate code editor\n");
        return NULL;
    }
    memset(editor, 0, sizeof(*editor));
    editor->rect = (AromaRect){x, y, width, height};
    editor->palette = aroma_code_editor_default_palette();
    editor->line_numbers = true;
    editor->minimap = true;
    editor->word_wrap = false;
    editor->auto_indent = true;
    editor->bracket_match = true;
    editor->tab_width = 4;
    editor->language = AROMA_CODE_LANGUAGE_PLAIN;
    editor->last_window_id = -1;
    editor->last_font_window = -1;
    editor->last_line = -1;
    editor->last_column = -1;
    editor->last_selection_chars = -1;
    editor->cursor = 0;
    editor->anchor = 0;
    editor->show_cursor = true;
    editor->cursor_blink_ms = ce_now_ms();
    editor->find_total = 0;
    editor->find_index = 0;
    ce_safe_copy(editor->file_name, sizeof(editor->file_name), "untitled");

    editor->capacity = 512;
    editor->text = (char *)malloc(editor->capacity);
    if (!editor->text)
    {
        aroma_widget_free(editor);
        return NULL;
    }
    editor->text[0] = '\0';

    editor->line_capacity = 64;
    editor->line_starts = (size_t *)malloc(sizeof(size_t) * (size_t)editor->line_capacity);
    if (!editor->line_starts)
    {
        free(editor->text);
        aroma_widget_free(editor);
        return NULL;
    }
    editor->line_count = 1;
    editor->line_starts[0] = 0;
    editor->max_columns = 0;

    editor->font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 14);
    editor->owns_font = editor->font != NULL;

    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, editor);
    if (!node)
    {
        if (editor->owns_font && editor->font)
            aroma_font_destroy(editor->font);
        free(editor->line_starts);
        free(editor->text);
        aroma_widget_free(editor);
        LOG_ERROR("Failed to create code editor node\n");
        return NULL;
    }

    editor->node = node;
    aroma_node_set_draw_cb(node, aroma_code_editor_draw);
    node->destroy_cb = aroma_code_editor_destroy;
    aroma_code_editor_setup_events(node);
    return node;
}

void aroma_code_editor_set_font(AromaNode *node, AromaFont *font)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    if (editor->owns_font && editor->font && editor->font != font)
    {
        aroma_font_destroy(editor->font);
        editor->owns_font = false;
        editor->font = NULL;
    }
    if (font)
    {
        editor->font = font;
        editor->char_width = 0.0f;
        editor->last_font_window = -1;
    }
    else if (!editor->font)
    {
        editor->font = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                                    aroma_ubuntu_ttf_len, 14);
        editor->owns_font = editor->font != NULL;
        editor->char_width = 0.0f;
        editor->last_font_window = -1;
    }
    ce_rebuild_lines(editor);
    ce_clamp_scroll(editor);
    aroma_node_invalidate(node);
}

static void ce_load_text(CeEditor *editor, const char *text)
{
    if (!editor)
        return;
    if (!text)
        text = "";
    size_t length = strlen(text);
    ce_ensure_capacity(editor, length);
    if (length + 1 > editor->capacity)
        return;
    memcpy(editor->text, text, length + 1);
    editor->length = length;
    ce_rebuild_lines(editor);
    ce_clear_undo_redo(editor);
    editor->cursor = 0;
    editor->anchor = 0;
    editor->scroll_x = 0;
    editor->scroll_y = 0;
    editor->desired_column = 0;
    editor->show_cursor = true;
    editor->cursor_blink_ms = ce_now_ms();
    editor->hover_visible = false;
    editor->hover_waiting = false;
    editor->completions_visible = false;
    editor->completion_count = 0;
    editor->completion_index = 0;
    editor->diagnostic_count = 0;
    editor->find_total = 0;
    editor->find_index = 0;
    editor->last_line = -1;
    editor->last_column = -1;
    editor->last_selection_chars = -1;
    aroma_node_invalidate(editor->node);
}

void aroma_code_editor_set_text(AromaNode *node, const char *text)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (editor)
        ce_load_text(editor, text);
}

const char *aroma_code_editor_get_text(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    return editor && editor->text ? editor->text : "";
}

size_t aroma_code_editor_get_length(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    return editor ? editor->length : 0;
}

void aroma_code_editor_set_language(AromaNode *node, AromaCodeLanguage language)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    if (language < AROMA_CODE_LANGUAGE_PLAIN || language > AROMA_CODE_LANGUAGE_HTML)
        language = AROMA_CODE_LANGUAGE_PLAIN;
    editor->language = language;
    aroma_node_invalidate(node);
}

AromaCodeLanguage aroma_code_editor_get_language(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    return editor ? editor->language : AROMA_CODE_LANGUAGE_PLAIN;
}

void aroma_code_editor_set_file_name(AromaNode *node, const char *file_name)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    ce_safe_copy(editor->file_name, sizeof(editor->file_name),
                 file_name && file_name[0] ? file_name : "untitled");
    aroma_node_invalidate(node);
}

const char *aroma_code_editor_get_file_name(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    return editor ? editor->file_name : "";
}

void aroma_code_editor_set_palette(AromaNode *node, const AromaCodeEditorPalette *palette)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    if (palette)
        editor->palette = *palette;
    else
        editor->palette = aroma_code_editor_default_palette();
    aroma_node_invalidate(node);
}

void aroma_code_editor_set_line_numbers(AromaNode *node, bool visible)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->line_numbers = visible;
    aroma_node_invalidate(node);
}

void aroma_code_editor_set_minimap(AromaNode *node, bool visible)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->minimap = visible;
    ce_clamp_scroll(editor);
    aroma_node_invalidate(node);
}

void aroma_code_editor_set_read_only(AromaNode *node, bool read_only)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->read_only = read_only;
    if (read_only)
        editor->completions_visible = false;
}

void aroma_code_editor_set_tab_width(AromaNode *node, int tab_width)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->tab_width = ce_clamp_int(tab_width, 1, 16);
    ce_rebuild_lines(editor);
    ce_clamp_scroll(editor);
    aroma_node_invalidate(node);
}

void aroma_code_editor_set_word_wrap(AromaNode *node, bool enabled)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->word_wrap = enabled;
    if (enabled)
        editor->scroll_x = 0;
    ce_clamp_scroll(editor);
    aroma_node_invalidate(node);
}

void aroma_code_editor_set_auto_indent(AromaNode *node, bool enabled)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->auto_indent = enabled;
}

void aroma_code_editor_set_bracket_match(AromaNode *node, bool enabled)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->bracket_match = enabled;
    aroma_node_invalidate(node);
}

void aroma_code_editor_focus(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    AromaNode *focused = aroma_ui_get_focused_node();
    if (focused && focused != node)
    {
        AromaEvent *lost = aroma_event_create(EVENT_TYPE_FOCUS_LOST, focused->node_id);
        if (lost)
        {
            aroma_event_dispatch(lost);
            aroma_event_destroy(lost);
        }
    }
    editor->focused = true;
    editor->show_cursor = true;
    editor->cursor_blink_ms = ce_now_ms();
    aroma_ui_set_focused_node(node);
    ce_ensure_cursor_visible(editor);
    aroma_node_invalidate(node);
}

bool aroma_code_editor_is_focused(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    return editor ? editor->focused : false;
}

void aroma_code_editor_set_cursor(AromaNode *node, size_t offset)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    ce_update_cursor(editor, offset, false);
    ce_ensure_cursor_visible(editor);
}

size_t aroma_code_editor_get_cursor(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    return editor ? editor->cursor : 0;
}

void aroma_code_editor_get_cursor_position(AromaNode *node, int *line, int *column)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
    {
        if (line)
            *line = 1;
        if (column)
            *column = 1;
        return;
    }
    if (line)
        *line = ce_line_of_offset(editor, editor->cursor) + 1;
    if (column)
        *column = ce_column_of_offset(editor, editor->cursor) + 1;
}

int aroma_code_editor_get_line_count(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    return editor ? editor->line_count : 0;
}

void aroma_code_editor_goto_line_col(AromaNode *node, int line, int column)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    line = ce_clamp_int(line, 1, editor->line_count);
    size_t offset = ce_offset_for_line_column(editor, line - 1, column > 0 ? column : 1);
    ce_update_cursor(editor, offset, false);
    int line_height = 18;
    ce_font_line_height(editor, &line_height);
    editor->scroll_y = (line - 1) * line_height - editor->rect.height / 3;
    if (editor->scroll_y < 0)
        editor->scroll_y = 0;
    ce_ensure_cursor_visible(editor);
    ce_clamp_scroll(editor);
    aroma_node_invalidate(node);
}

void aroma_code_editor_select(AromaNode *node, size_t start, size_t end)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    start = ce_clamp_offset(editor, start);
    end = ce_clamp_offset(editor, end);
    editor->anchor = start;
    editor->cursor = end;
    editor->show_cursor = true;
    editor->cursor_blink_ms = ce_now_ms();
    editor->desired_column = ce_column_of_offset(editor, editor->cursor);
    ce_ensure_cursor_visible(editor);
    ce_notify_cursor(editor);
    aroma_node_invalidate(node);
}

bool aroma_code_editor_get_selection(AromaNode *node, size_t *start, size_t *end)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor || !ce_has_selection(editor))
    {
        if (start)
            *start = 0;
        if (end)
            *end = 0;
        return false;
    }
    if (start)
        *start = ce_sel_start(editor);
    if (end)
        *end = ce_sel_end(editor);
    return true;
}

void aroma_code_editor_select_all(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->anchor = 0;
    editor->cursor = editor->length;
    editor->show_cursor = true;
    editor->cursor_blink_ms = ce_now_ms();
    ce_notify_cursor(editor);
    aroma_node_invalidate(node);
}

bool aroma_code_editor_get_word_at_cursor(AromaNode *node, char *out, size_t out_size)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!out || out_size == 0)
        return false;
    out[0] = '\0';
    if (!editor)
        return false;
    size_t start = editor->cursor;
    size_t end = editor->cursor;
    while (start > 0 && ce_is_word_char((unsigned char)editor->text[start - 1]))
        start--;
    while (end < editor->length && ce_is_word_char((unsigned char)editor->text[end]))
        end++;
    if (start == end)
        return false;
    size_t len = end - start;
    if (len >= out_size)
        len = out_size - 1;
    memcpy(out, editor->text + start, len);
    out[len] = '\0';
    return true;
}

void aroma_code_editor_set_scroll(AromaNode *node, int scroll_x, int scroll_y)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->scroll_x = scroll_x < 0 ? 0 : scroll_x;
    editor->scroll_y = scroll_y < 0 ? 0 : scroll_y;
    ce_clamp_scroll(editor);
    aroma_node_invalidate(node);
}

void aroma_code_editor_get_scroll(AromaNode *node, int *scroll_x, int *scroll_y)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (scroll_x)
        *scroll_x = editor ? editor->scroll_x : 0;
    if (scroll_y)
        *scroll_y = editor ? editor->scroll_y : 0;
}

void aroma_code_editor_set_bounds(AromaNode *node, int x, int y, int width, int height)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor || width <= 0 || height <= 0)
        return;
    editor->rect = (AromaRect){x, y, width, height};
    AromaRect *rect = aroma_node_get_rect(node);
    if (rect)
        *rect = editor->rect;
    ce_clamp_scroll(editor);
    ce_ensure_cursor_visible(editor);
    aroma_node_invalidate(node);
}

static void ce_restore_from_snapshot(CeEditor *editor, CeSnapshot *snapshot)
{
    if (!editor || !snapshot || !snapshot->text)
        return;
    char *old = editor->text;
    editor->text = snapshot->text;
    editor->length = strlen(snapshot->text);
    editor->capacity = editor->length + 1;
    snapshot->text = NULL;
    free(old);
    editor->cursor = ce_clamp_offset(editor, snapshot->cursor);
    editor->anchor = ce_clamp_offset(editor, snapshot->anchor);
    editor->desired_column = ce_column_of_offset(editor, editor->cursor);
    editor->show_cursor = true;
    editor->cursor_blink_ms = ce_now_ms();
    ce_rebuild_lines(editor);
    ce_ensure_cursor_visible(editor);
    ce_clamp_scroll(editor);
    ce_notify_change(editor);
    ce_notify_cursor(editor);
    aroma_node_invalidate(editor->node);
}

void aroma_code_editor_undo(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor || editor->undo_count <= 0)
        return;
    if (!ce_push_snapshot(editor->redo_stack, &editor->redo_count, CE_MAX_UNDO, editor))
        return;
    CeSnapshot snapshot = editor->undo_stack[--editor->undo_count];
    memset(&editor->undo_stack[editor->undo_count], 0, sizeof(CeSnapshot));
    ce_restore_from_snapshot(editor, &snapshot);
    ce_free_snapshot(&snapshot);
}

void aroma_code_editor_redo(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor || editor->redo_count <= 0)
        return;
    if (!ce_push_snapshot(editor->undo_stack, &editor->undo_count, CE_MAX_UNDO, editor))
        return;
    CeSnapshot snapshot = editor->redo_stack[--editor->redo_count];
    memset(&editor->redo_stack[editor->redo_count], 0, sizeof(CeSnapshot));
    ce_restore_from_snapshot(editor, &snapshot);
    ce_free_snapshot(&snapshot);
}

void aroma_code_editor_show_find(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->find_visible = true;
    if (!ce_has_selection(editor) || editor->find_query[0] == '\0')
    {
        if (ce_has_selection(editor))
        {
            size_t start = ce_sel_start(editor);
            size_t end = ce_sel_end(editor);
            size_t len = end - start;
            bool one_line = true;
            for (size_t i = start; i < end; i++)
            {
                if (editor->text[i] == '\n' || editor->text[i] == '\r')
                {
                    one_line = false;
                    break;
                }
            }
            if (one_line && len > 0 && len < sizeof(editor->find_query))
            {
                memcpy(editor->find_query, editor->text + start, len);
                editor->find_query[len] = '\0';
            }
        }
    }
    ce_rebuild_find_matches(editor, false);
    aroma_node_invalidate(node);
}

void aroma_code_editor_hide_find(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->find_visible = false;
    editor->find_total = 0;
    editor->find_index = 0;
    aroma_node_invalidate(node);
}

void aroma_code_editor_set_diagnostics(AromaNode *node,
                                       const AromaCodeDiagnostic *diagnostics,
                                       int count)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    if (!diagnostics || count <= 0)
    {
        editor->diagnostic_count = 0;
        aroma_node_invalidate(node);
        return;
    }
    count = ce_clamp_int(count, 0, AROMA_CODE_EDITOR_MAX_DIAGNOSTICS);
    for (int i = 0; i < count; i++)
        editor->diagnostics[i] = diagnostics[i];
    editor->diagnostic_count = count;
    aroma_node_invalidate(node);
}

void aroma_code_editor_clear_diagnostics(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->diagnostic_count = 0;
    aroma_node_invalidate(node);
}

int aroma_code_editor_get_diagnostic_count(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    return editor ? editor->diagnostic_count : 0;
}

void aroma_code_editor_set_completions(AromaNode *node,
                                       const AromaCodeCompletion *completions,
                                       int count)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    if (!completions || count <= 0)
    {
        editor->completion_count = 0;
        editor->completion_index = 0;
        editor->completions_visible = false;
        aroma_node_invalidate(node);
        return;
    }
    count = ce_clamp_int(count, 0, AROMA_CODE_EDITOR_MAX_COMPLETIONS);
    char prefix[80];
    ce_completion_prefix(editor, prefix, sizeof(prefix));
    int matched = 0;
    for (int i = 0; i < count; i++)
    {
        if (ce_completion_matches(&completions[i], prefix))
            editor->completions[matched++] = completions[i];
    }
    editor->completion_count = matched;
    editor->completion_index = 0;
    editor->completions_visible = matched > 0;
    aroma_node_invalidate(node);
}

void aroma_code_editor_hide_completions(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->completions_visible = false;
    editor->completion_count = 0;
    aroma_node_invalidate(node);
}

bool aroma_code_editor_completions_visible(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    return editor && editor->completions_visible && editor->completion_count > 0;
}

void aroma_code_editor_set_hover(AromaNode *node, const char *text, int line, int column)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    if (!text || text[0] == '\0')
    {
        editor->hover_visible = false;
        aroma_node_invalidate(node);
        return;
    }
    ce_safe_copy(editor->hover_text, sizeof(editor->hover_text), text);
    editor->hover_line = line;
    editor->hover_column = column;
    editor->hover_visible = true;
    editor->hover_waiting = false;
    aroma_node_invalidate(node);
}

void aroma_code_editor_hide_hover(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->hover_visible = false;
    editor->hover_waiting = false;
    aroma_node_invalidate(node);
}

void aroma_code_editor_set_on_change(AromaNode *node, AromaCodeEditorChangeFn callback,
                                     void *user_data)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->on_change = callback;
    editor->on_change_data = user_data;
}

void aroma_code_editor_set_on_cursor_changed(AromaNode *node,
                                             AromaCodeEditorCursorFn callback,
                                             void *user_data)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->on_cursor_changed = callback;
    editor->on_cursor_data = user_data;
}

void aroma_code_editor_set_on_save(AromaNode *node, AromaCodeEditorSaveFn callback,
                                   void *user_data)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->on_save = callback;
    editor->on_save_data = user_data;
}

void aroma_code_editor_set_on_completion_request(AromaNode *node,
                                                 AromaCodeEditorLineColumnFn callback,
                                                 void *user_data)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->on_completion_request = callback;
    editor->on_completion_data = user_data;
}

void aroma_code_editor_set_on_hover_request(AromaNode *node,
                                            AromaCodeEditorLineColumnFn callback,
                                            void *user_data)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->on_hover_request = callback;
    editor->on_hover_data = user_data;
}

void aroma_code_editor_set_on_definition_request(AromaNode *node,
                                                 AromaCodeEditorLineColumnFn callback,
                                                 void *user_data)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    editor->on_definition_request = callback;
    editor->on_definition_data = user_data;
}

static void ce_request_completion(CeEditor *editor)
{
    if (!editor || !editor->on_completion_request)
        return;
    int line = ce_line_of_offset(editor, editor->cursor) + 1;
    int column = ce_column_of_offset(editor, editor->cursor) + 1;
    editor->on_completion_request(editor->node, line, column, editor->on_completion_data);
}

/* Open the suggestion dropdown while typing identifiers, after member
   access operators ('->' and '::'), or on a preprocessor marker. */
static bool ce_should_auto_complete(const CeEditor *e, char ch)
{
    if (ce_is_word_char((unsigned char)ch) || ch == '$' || ch == '.')
        return true;
    if (ch == '#')
        return true;
    if (ch == '<' || ch == '"')
        return ce_include_open_offset(e) != CE_NO_OFFSET;
    if (!e || !e->text || e->cursor < 2)
        return false;
    if (ch == '>')
        return e->text[e->cursor - 2] == '-';
    if (ch == ':')
        return e->text[e->cursor - 2] == ':';
    return false;
}

static void ce_request_definition(CeEditor *editor)
{
    if (!editor || !editor->on_definition_request)
        return;
    int line = ce_line_of_offset(editor, editor->cursor) + 1;
    int column = ce_column_of_offset(editor, editor->cursor) + 1;
    editor->on_definition_request(editor->node, line, column, editor->on_definition_data);
}

static void ce_copy_selection(CeEditor *editor)
{
    if (!editor || !ce_has_selection(editor))
        return;
    size_t start = ce_sel_start(editor);
    size_t end = ce_sel_end(editor);
    size_t len = end - start;
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return;
    memcpy(copy, editor->text + start, len);
    copy[len] = '\0';
    free(editor->clipboard_text);
    editor->clipboard_text = copy;

    /* Also push to system clipboard */
    {
        AromaPlatformInterface *plat = aroma_get_platform_interface();
        if (plat && plat->set_clipboard_text)
            plat->set_clipboard_text(copy);
    }
}

static void ce_paste_clipboard(CeEditor *editor)
{
    if (!editor)
        return;

    /* Try system clipboard first */
    {
        AromaPlatformInterface *plat = aroma_get_platform_interface();
        if (plat && plat->get_clipboard_text)
        {
            char buf[4096];
            if (plat->get_clipboard_text(buf, sizeof(buf)) && buf[0])
            {
                ce_insert_internal(editor, buf, strlen(buf));
                ce_ensure_cursor_visible(editor);
                return;
            }
        }
    }

    /* Fall back to internal clipboard */
    if (!editor->clipboard_text)
        return;
    ce_insert_internal(editor, editor->clipboard_text, strlen(editor->clipboard_text));
    ce_ensure_cursor_visible(editor);
}

void aroma_code_editor_copy(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (editor)
        ce_copy_selection(editor);
}

void aroma_code_editor_cut(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor || !ce_has_selection(editor))
        return;
    ce_copy_selection(editor);
    ce_delete_selection_internal(editor);
    ce_ensure_cursor_visible(editor);
}

void aroma_code_editor_paste(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    ce_paste_clipboard(editor);
}

static bool ce_keyboard_handler(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!event || event->event_type != EVENT_TYPE_KEY_PRESS || !event->target_node)
        return false;
    CeEditor *editor = AROMA_NODE_AS(event->target_node, CeEditor);
    if (!editor || !editor->focused)
        return false;

    uint32_t key = event->data.key.key_code;
    uint16_t mods = event->data.key.modifiers;
    bool ctrl = (mods & AROMA_KEY_MOD_CTRL) != 0;
    bool shift = (mods & AROMA_KEY_MOD_SHIFT) != 0;

    if (editor->find_visible)
    {
        if (key == 27)
        {
            aroma_code_editor_hide_find(event->target_node);
            return true;
        }
        if (key == 13 || key == 10 || key == 0xFF0D)
        {
            if (editor->find_total <= 0)
                ce_rebuild_find_matches(editor, true);
            else
                ce_find_next(editor, shift ? -1 : 1);
            return true;
        }
        if (key == 8 || key == 0x0008 || key == 0xFF08 || key == 0x007F)
        {
            size_t len = strlen(editor->find_query);
            if (len > 0)
                editor->find_query[len - 1] = '\0';
            ce_rebuild_find_matches(editor, true);
            aroma_node_invalidate(event->target_node);
            return true;
        }
        if (ctrl && key == 6)
        {
            aroma_code_editor_hide_find(event->target_node);
            return true;
        }
        if (!ctrl && key >= 32 && key <= 255)
        {
            size_t len = strlen(editor->find_query);
            if (len + 1 < sizeof(editor->find_query))
            {
                editor->find_query[len] = (char)key;
                editor->find_query[len + 1] = '\0';
                ce_rebuild_find_matches(editor, true);
                aroma_node_invalidate(event->target_node);
            }
            return true;
        }
        return true;
    }

    if (editor->completions_visible)
    {
        if (key == 27)
        {
            aroma_code_editor_hide_completions(event->target_node);
            return true;
        }
        if (key == 0xFF52)
        {
            editor->completion_index = ce_clamp_int(editor->completion_index - 1, 0,
                                                    editor->completion_count - 1);
            aroma_node_invalidate(event->target_node);
            return true;
        }
        if (key == 0xFF54)
        {
            editor->completion_index = ce_clamp_int(editor->completion_index + 1, 0,
                                                    editor->completion_count - 1);
            aroma_node_invalidate(event->target_node);
            return true;
        }
        if (ce_key_is_tab(key) || key == 13 || key == 10 || key == 0xFF0D)
        {
            ce_accept_completion(editor);
            return true;
        }
    }

    if (ctrl)
    {
        switch (key)
        {
        case 19: /* Ctrl+S */
            if (editor->on_save)
                editor->on_save(editor->node, editor->text, editor->on_save_data);
            return true;
        case 26: /* Ctrl+Z */
            aroma_code_editor_undo(event->target_node);
            return true;
        case 25: /* Ctrl+Y */
            aroma_code_editor_redo(event->target_node);
            return true;
        case 1: /* Ctrl+A */
            aroma_code_editor_select_all(event->target_node);
            return true;
        case 6: /* Ctrl+F */
            aroma_code_editor_show_find(event->target_node);
            return true;
        case 3: /* Ctrl+C */
            ce_copy_selection(editor);
            return true;
        case 24: /* Ctrl+X */
            if (ce_has_selection(editor))
            {
                ce_copy_selection(editor);
                ce_delete_selection_internal(editor);
                ce_ensure_cursor_visible(editor);
            }
            return true;
        case 22: /* Ctrl+V */
            ce_paste_clipboard(editor);
            return true;
        case 8: /* Ctrl+Backspace */
            ce_delete_word_backward(editor);
            ce_ensure_cursor_visible(editor);
            return true;
        case 127: /* Ctrl+Delete */
            ce_delete_word_forward(editor);
            ce_ensure_cursor_visible(editor);
            return true;
        case 11: /* Ctrl+K: kill to end of line */
            ce_delete_to_end_of_line(editor);
            ce_ensure_cursor_visible(editor);
            return true;
        case 21: /* Ctrl+U: kill to start of line */
            ce_delete_to_start_of_line(editor);
            ce_ensure_cursor_visible(editor);
            return true;
        case 4: /* Ctrl+D: delete forward */
            ce_delete_forward_internal(editor);
            ce_ensure_cursor_visible(editor);
            return true;
        case 32: /* Ctrl+Space */
            editor->hover_visible = false;
            ce_request_completion(editor);
            return true;
        case 13: /* Ctrl+Enter: definition (fallback when F12 is not delivered) */
        case 10:
            ce_request_definition(editor);
            return true;
        default:
            if (key == 0xFF51)
            {
                ce_move_word(editor, -1, shift);
                return true;
            }
            if (key == 0xFF53)
            {
                ce_move_word(editor, 1, shift);
                return true;
            }
            if (key == 0xFF50)
            {
                ce_move_home(editor, shift, true);
                return true;
            }
            if (key == 0xFF57)
            {
                ce_move_end(editor, shift, true);
                return true;
            }
            return true;
        }
    }

    editor->hover_visible = false;

    if (key == 8 || key == 0x0008 || key == 0xFF08 || key == 0x007F || key == 22)
    {
        editor->completions_visible = false;
        ce_backspace_internal(editor);
        ce_ensure_cursor_visible(editor);
        return true;
    }
    if (key == 0xFFFF || key == 0xFF9F || key == 127)
    {
        editor->completions_visible = false;
        ce_delete_forward_internal(editor);
        ce_ensure_cursor_visible(editor);
        return true;
    }
    if (key == 0xFF51)
    {
        ce_move_horizontal(editor, -1, shift);
        editor->completions_visible = false;
        return true;
    }
    if (key == 0xFF53)
    {
        ce_move_horizontal(editor, 1, shift);
        editor->completions_visible = false;
        return true;
    }
    if (key == 0xFF52)
    {
        ce_move_vertical(editor, -1, shift);
        editor->completions_visible = false;
        return true;
    }
    if (key == 0xFF54)
    {
        ce_move_vertical(editor, 1, shift);
        editor->completions_visible = false;
        return true;
    }
    if (key == 0xFF50)
    {
        ce_move_home(editor, shift, false);
        editor->completions_visible = false;
        return true;
    }
    if (key == 0xFF57)
    {
        ce_move_end(editor, shift, false);
        editor->completions_visible = false;
        return true;
    }
    if (ce_key_is_tab(key))
    {
        editor->completions_visible = false;
        ce_insert_tab(editor);
        return true;
    }
    if (key == 13 || key == 10 || key == 0xFF0D)
        {
            editor->completions_visible = false;
            ce_insert_newline(editor);
            return true;
        }
    if (key == 27)
    {
        editor->completions_visible = false;
        editor->hover_visible = false;
        aroma_node_invalidate(event->target_node);
        return true;
    }

    if (key >= 32 && key <= 255)
    {
        char ch = (char)(key & 0xFF);
        if (shift && ch >= 'a' && ch <= 'z')
            ch = (char)toupper((unsigned char)ch);
        ce_type_character(editor, ch);
        if (ce_should_auto_complete(editor, ch))
            ce_request_completion(editor);
        else if (editor->completions_visible)
            aroma_code_editor_hide_completions(editor->node);
        ce_ensure_cursor_visible(editor);
        return true;
    }

    return false;
}
typedef struct CeMetrics
{
    int line_height;
    int char_width;
    int gutter_width;
    int code_x;
    int code_y;
    int code_width;
    int code_height;
    int minimap_width;
    int minimap_x;
} CeMetrics;

static void ce_metrics(const CeEditor *editor, CeMetrics *out)
{
    CeMetrics m;
    memset(&m, 0, sizeof(m));
    ce_font_line_height(editor, &m.line_height);
    m.char_width = (int)(ce_measure_char((CeEditor *)editor) + 0.5f);
    if (m.char_width < 4)
        m.char_width = 8;

    int digits = 1;
    int lines = editor ? editor->line_count : 1;
    while (lines >= 10)
    {
        digits++;
        lines /= 10;
    }

    m.gutter_width = editor && editor->line_numbers ? 18 + digits * m.char_width + 8 : 8;
    m.minimap_width = editor && editor->minimap && editor->rect.width >= 620 ? 82 : 0;
    m.code_x = editor->rect.x + m.gutter_width;
    m.code_y = editor->rect.y + 4;
    m.code_width = editor->rect.width - m.gutter_width - m.minimap_width - 8;
    m.code_height = editor->rect.height - 8;
    if (m.code_width < 32)
        m.code_width = 32;
    if (m.code_height < m.line_height * 2)
        m.code_height = m.line_height * 2;
    m.minimap_x = editor->rect.x + editor->rect.width - m.minimap_width;
    if (out)
        *out = m;
}

static void ce_find_bar_rect(const CeEditor *editor, const CeMetrics *m,
                             int *x, int *y, int *w, int *h)
{
    int width = editor->rect.width - 24;
    if (width > 380)
        width = 380;
    if (width < 220)
        width = editor->rect.width - 8;
    int height = m->line_height + 12;
    if (height < 30)
        height = 30;
    *x = editor->rect.x + editor->rect.width - width - 12;
    *y = editor->rect.y + 8;
    *w = width;
    *h = height;
}

static void ce_completion_popup_rect(const CeEditor *editor, const CeMetrics *m,
                                     int *x, int *y, int *w, int *h)
{
    int rows = editor->completion_count;
    if (rows > CE_POPUP_MAX_ROWS)
        rows = CE_POPUP_MAX_ROWS;
    if (rows < 1)
        rows = 1;
    int width = editor->rect.width - 32;
    if (width > 420)
        width = 420;
    if (width < 240)
        width = editor->rect.width - 16;
    int row_height = m->line_height + 6;
    int header = m->line_height + 8;
    int height = header + rows * row_height + 6;

    int line = ce_line_of_offset(editor, editor->cursor);
    int column = ce_column_of_offset(editor, editor->cursor);
    int px = m->code_x + ce_pixel_x_from_column(editor, line, column) - editor->scroll_x + 8;
    int py = m->code_y + (line + 1) * m->line_height - editor->scroll_y + 4;
    if (py + height > editor->rect.y + editor->rect.height - 8)
        py = m->code_y + line * m->line_height - editor->scroll_y - height - 4;
    if (px + width > editor->rect.x + editor->rect.width - 8)
        px = editor->rect.x + editor->rect.width - width - 8;
    if (px < editor->rect.x + 8)
        px = editor->rect.x + 8;
    if (py < editor->rect.y + 8)
        py = editor->rect.y + 8;

    *x = px;
    *y = py;
    *w = width;
    *h = height;
}

static size_t ce_offset_at_point(CeEditor *editor, int x, int y)
{
    CeMetrics m;
    ce_metrics(editor, &m);
    int wrap_cols = ce_wrap_cols(editor, m.code_width, m.char_width);
    int row = (y - m.code_y + editor->scroll_y) / m.line_height;
    int line = row;
    int segment = 0;
    if (wrap_cols > 0)
        ce_visual_row_to_line(editor, row, wrap_cols, &line, &segment);
    line = ce_clamp_int(line, 0, editor->line_count - 1);
    int segment_start_px = wrap_cols > 0
                               ? ce_pixel_x_from_column(editor, line, segment * wrap_cols)
                               : 0;
    int col = ce_column_for_pixel(editor, line,
                                  x - m.code_x + editor->scroll_x + segment_start_px);
    return ce_offset_for_line_column(editor, line, col + 1);
}

static void ce_select_word_at(CeEditor *editor, size_t offset)
{
    if (!editor)
        return;
    offset = ce_clamp_offset(editor, offset);
    size_t start = offset;
    size_t end = offset;
    while (start > 0 && ce_is_word_char((unsigned char)editor->text[start - 1]))
        start--;
    while (end < editor->length && ce_is_word_char((unsigned char)editor->text[end]))
        end++;
    editor->anchor = start;
    editor->cursor = end;
    editor->show_cursor = true;
    editor->cursor_blink_ms = ce_now_ms();
    editor->desired_column = ce_column_of_offset(editor, editor->cursor);
    ce_notify_cursor(editor);
    ce_ensure_cursor_visible(editor);
    aroma_node_invalidate(editor->node);
}

static int ce_expand_line(const CeEditor *editor, int line, char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return 0;
    out[0] = '\0';
    if (!editor)
        return 0;
    size_t start = ce_line_start(editor, line);
    size_t end = ce_line_end_content(editor, line);
    size_t used = 0;
    int col = 0;
    int tab_width = editor->tab_width > 0 ? editor->tab_width : 4;
    for (size_t i = start; i < end && used + 1 < out_size; i++)
    {
        unsigned char ch = (unsigned char)editor->text[i];
        if (ch == '\t')
        {
            int spaces = tab_width - (col % tab_width);
            for (int j = 0; j < spaces && used + 1 < out_size; j++)
                out[used++] = ' ';
            col += spaces;
        }
        else if (ch >= 32 || ch == '\r')
        {
            out[used++] = (char)ch;
            col++;
        }
        else
        {
            out[used++] = ' ';
            col++;
        }
    }
    out[used] = '\0';
    return (int)used;
}

/* Convert an offset in the original text buffer to the corresponding position
 * in the tab-expanded display buffer. Accounts for tabs expanding to multiple
 * spaces. */
static size_t ce_offset_to_expanded(const CeEditor *editor, int line, size_t offset)
{
    size_t start = ce_line_start(editor, line);
    size_t end = ce_line_end_content(editor, line);
    if (offset > end)
        offset = end;
    size_t expanded_pos = 0;
    int col = 0;
    int tab_width = editor->tab_width > 0 ? editor->tab_width : 4;
    for (size_t i = start; i < offset && i < end; i++)
    {
        unsigned char ch = (unsigned char)editor->text[i];
        if (ch == '\t')
        {
            int spaces = tab_width - (col % tab_width);
            expanded_pos += (size_t)spaces;
            col += spaces;
        }
        else if (ch >= 32 || ch == '\r')
        {
            expanded_pos++;
            col++;
        }
        else
        {
            expanded_pos++;
            col++;
        }
    }
    return expanded_pos;
}

static void ce_draw_text(AromaGraphicsInterface *gfx, size_t window_id, AromaFont *font,
                         const char *text, int x, int y, uint32_t color)
{
    if (!gfx || !gfx->render_text || !font || !text || text[0] == '\0')
        return;
    gfx->render_text(window_id, font, text, x, y, color, 1.0f);
}

static int ce_diag_count_on_line(const CeEditor *editor, int line, uint32_t *worst_color)
{
    int count = 0;
    uint32_t color = 0;
    int worst = 4;
    for (int i = 0; i < editor->diagnostic_count; i++)
    {
        const AromaCodeDiagnostic *diag = &editor->diagnostics[i];
        if (diag->line == line)
        {
            count++;
            int severity = (int)diag->severity;
            if (severity < worst)
            {
                worst = severity;
                color = severity == AROMA_CODE_DIAGNOSTIC_ERROR ? editor->palette.error :
                        severity == AROMA_CODE_DIAGNOSTIC_WARNING ? editor->palette.warning :
                        severity == AROMA_CODE_DIAGNOSTIC_INFO ? editor->palette.info :
                        editor->palette.comment;
            }
        }
    }
    if (worst_color)
        *worst_color = color;
    return count;
}

static bool ce_bracket_open(char c)
{
    return c == '(' || c == '[' || c == '{';
}

static bool ce_bracket_close(char c)
{
    return c == ')' || c == ']' || c == '}';
}

static char ce_bracket_partner(char c)
{
    switch (c)
    {
    case '(': return ')';
    case '[': return ']';
    case '{': return '}';
    case ')': return '(';
    case ']': return '[';
    case '}': return '{';
    default:  return '\0';
    }
}

/* Return the offset of the bracket matching the one at `offset`, or
 * SIZE_MAX when the character is not a bracket / has no partner. */
static size_t ce_find_matching_bracket(const CeEditor *e, size_t offset)
{
    if (!e || offset >= e->length)
        return SIZE_MAX;
    char c = e->text[offset];
    if (!ce_bracket_open(c) && !ce_bracket_close(c))
        return SIZE_MAX;
    char partner = ce_bracket_partner(c);
    if (!partner)
        return SIZE_MAX;

    if (ce_bracket_open(c))
    {
        int depth = 0;
        for (size_t i = offset + 1; i < e->length; i++)
        {
            char t = e->text[i];
            if (t == c)
                depth++;
            else if (t == partner)
            {
                if (depth == 0)
                    return i;
                depth--;
            }
        }
    }
    else
    {
        int depth = 0;
        for (size_t i = offset; i > 0;)
        {
            i--;
            char t = e->text[i];
            if (t == partner)
                depth++;
            else if (t == c)
            {
                if (depth == 0)
                    return i;
                depth--;
            }
        }
    }
    return SIZE_MAX;
}

static void ce_draw_bracket_at(AromaGraphicsInterface *gfx, size_t window_id,
                               const CeEditor *editor, const CeMetrics *m,
                               size_t offset, uint32_t color, int wrap_cols)
{
    int line = ce_line_of_offset(editor, offset);
    int column = ce_column_of_offset(editor, offset);
    int visual_row = ce_visual_row_for_line(editor, line, wrap_cols);
    int x = m->code_x + ce_pixel_x_from_column(editor, line, column) - editor->scroll_x;
    if (wrap_cols > 0)
    {
        int segment = column / wrap_cols;
        visual_row += segment;
        x = m->code_x + ce_pixel_x_from_column(editor, line, column) -
            ce_pixel_x_from_column(editor, line, segment * wrap_cols) -
            editor->scroll_x;
    }
    int y = m->code_y + visual_row * m->line_height - editor->scroll_y;
    if (y + m->line_height < m->code_y || y > m->code_y + m->code_height)
        return;
    if (x + m->char_width < m->code_x || x > m->code_x + m->code_width)
        return;
    int w = m->char_width;
    if (w < 2)
        w = 2;
    gfx->fill_rectangle(window_id, x, y, w, m->line_height, color, false, 0.0f);
}

static void ce_draw_bracket_highlight(AromaGraphicsInterface *gfx, size_t window_id,
                                      const CeEditor *editor, const CeMetrics *m,
                                      int wrap_cols)
{
    if (!editor->bracket_match || editor->read_only)
        return;
    size_t offset = SIZE_MAX;
    if (editor->cursor < editor->length &&
        (ce_bracket_open(editor->text[editor->cursor]) ||
         ce_bracket_close(editor->text[editor->cursor])))
        offset = editor->cursor;
    else if (editor->cursor > 0 &&
             (ce_bracket_open(editor->text[editor->cursor - 1]) ||
              ce_bracket_close(editor->text[editor->cursor - 1])))
        offset = editor->cursor - 1;
    if (offset == SIZE_MAX)
        return;
    size_t match = ce_find_matching_bracket(editor, offset);
    if (match == SIZE_MAX)
        return;
    uint32_t color = editor->palette.selection;
    color = (color & 0x00FFFFFFu) | 0x44000000u;
    ce_draw_bracket_at(gfx, window_id, editor, m, offset, color, wrap_cols);
    ce_draw_bracket_at(gfx, window_id, editor, m, match, color, wrap_cols);
}

static void ce_draw_selection_line(AromaGraphicsInterface *gfx, size_t window_id,
                                   const CeEditor *editor, const CeMetrics *m, int line,
                                   int line_y, int wrap_start_col)
{
    if (!ce_has_selection(editor))
        return;
    size_t start = ce_sel_start(editor);
    size_t end = ce_sel_end(editor);
    size_t line_start = ce_line_start(editor, line);
    size_t content_end = ce_line_end_content(editor, line);
    size_t include_end = ce_line_end_include_newline(editor, line);
    if (end <= line_start || start >= include_end)
        return;
    size_t sel_start = start > line_start ? start : line_start;
    size_t sel_end = end < include_end ? end : include_end;
    if (sel_end < sel_start)
        return;
    int col_start = ce_visual_columns_for_text(editor->text, line_start, sel_start,
                                               editor->tab_width > 0 ? editor->tab_width : 4);
    int col_end = ce_visual_columns_for_text(editor->text, line_start, sel_end,
                                             editor->tab_width > 0 ? editor->tab_width : 4);
    bool newline_selected = end > content_end && content_end < editor->length;
    if (newline_selected)
        col_end = ce_line_display_columns(editor, line) + 1;
    if (col_end <= col_start)
        col_end = col_start + 1;
    size_t sel_start_expanded = ce_offset_to_expanded(editor, line, sel_start);
    size_t sel_end_expanded = ce_offset_to_expanded(editor, line, sel_end);
    int wrap_offset = wrap_start_col > 0
                          ? ce_pixel_x_from_column(editor, line, wrap_start_col)
                          : 0;
    int x = m->code_x + ce_pixel_x_from_column(editor, line, (int)sel_start_expanded) - editor->scroll_x - wrap_offset;
    int width = (m->code_x + ce_pixel_x_from_column(editor, line, (int)sel_end_expanded) - editor->scroll_x - wrap_offset) - x;
    if (x + width < m->code_x || x > m->code_x + m->code_width)
        return;
    gfx->fill_rectangle(window_id, x, line_y, width, m->line_height,
                        editor->palette.selection, false, 0.0f);
}

static void ce_draw_find_highlights(AromaGraphicsInterface *gfx, size_t window_id,
                                    const CeEditor *editor, const CeMetrics *m,
                                    int line, int line_y, int wrap_start_col)
{
    if (!editor->find_visible || editor->find_query[0] == '\0')
        return;
    size_t start = ce_line_start(editor, line);
    size_t end = ce_line_end_content(editor, line);
    size_t qlen = strlen(editor->find_query);
    if (qlen == 0 || end - start < qlen)
        return;
    size_t pos = start;
    while (pos + qlen <= end)
    {
        size_t match = ce_find_next_match(editor, pos, editor->find_query, false);
        if (match == SIZE_MAX || match + qlen > end)
            break;
        size_t match_expanded = ce_offset_to_expanded(editor, line, match);
        int width = ce_text_width(editor, editor->find_query, (int)qlen);
        int wrap_offset = wrap_start_col > 0
                              ? ce_pixel_x_from_column(editor, line, wrap_start_col)
                              : 0;
        int x = m->code_x + ce_pixel_x_from_column(editor, line, (int)match_expanded) - editor->scroll_x - wrap_offset;
        if (x + width >= m->code_x && x <= m->code_x + m->code_width)
            gfx->fill_rectangle(window_id, x, line_y, width, m->line_height,
                                editor->palette.find_match, false, 0.0f);
        pos = match + 1;
    }
}

static void ce_draw_line_tokens(AromaGraphicsInterface *gfx, size_t window_id,
                                CeEditor *editor, const CeMetrics *m, int line, int line_y,
                                int wrap_start_col)
{
    char display[4096];
    int display_len = ce_expand_line(editor, line, display, sizeof(display));
    if (display_len <= 0 || !editor->font)
        return;

    int font_height = aroma_font_get_line_height(editor->font);
    int text_y = line_y + (m->line_height - font_height) / 2;
    int wrap_offset = wrap_start_col > 0
                          ? ce_pixel_x_from_column(editor, line, wrap_start_col)
                          : 0;

    CeToken tokens[256];
    int token_count = ce_lex_line(editor, display, display_len, tokens,
                                  (int)(sizeof(tokens) / sizeof(tokens[0])));
    if (token_count <= 0)
    {
        int x = m->code_x - editor->scroll_x - wrap_offset;
        ce_draw_text(gfx, window_id, editor->font, display, x, text_y, editor->palette.text);
        return;
    }

    AromaGraphicsInterface *gfx_iface = aroma_backend_abi.get_graphics_interface();

    int cur_x = m->code_x - editor->scroll_x - wrap_offset;
    int cur_col = 0;

    for (int i = 0; i < token_count; i++)
    {
        CeToken *token = &tokens[i];

        while (cur_col < token->start && cur_col < display_len)
        {
            char ch = display[cur_col];
            char buf[2] = {ch, '\0'};
            int w = 8;
            if (gfx_iface && gfx_iface->measure_text)
            {
                w = (int)gfx_iface->measure_text((size_t)editor->last_window_id,
                                                 editor->font, buf, 1.0f);
                if (w < 1)
                    w = 1;
            }
            cur_x += w;
            cur_col++;
        }

        if (cur_x > m->code_x + m->code_width)
            break;

        int token_len = token->end - token->start;
        if (token_len <= 0)
            continue;
        if (token_len >= (int)sizeof(display))
            token_len = (int)sizeof(display) - 1;

        int token_w = ce_text_width(editor, display + token->start, token_len);
        if (cur_x + token_w < m->code_x)
        {
            cur_x += token_w;
            cur_col = token->end;
            continue;
        }

        char text[768];
        if (token_len >= (int)sizeof(text))
            token_len = (int)sizeof(text) - 1;
        memcpy(text, display + token->start, (size_t)token_len);
        text[token_len] = '\0';
        ce_draw_text(gfx, window_id, editor->font, text, cur_x, text_y,
                     ce_token_color(editor, token->kind));

        cur_x += token_w;
        cur_col = token->end;
    }
}

static void ce_draw_diagnostics(AromaGraphicsInterface *gfx, size_t window_id,
                                const CeEditor *editor, const CeMetrics *m, int line,
                                int line_y, int wrap_start_col)
{
    size_t line_start = ce_line_start(editor, line);
    size_t content_end = ce_line_end_content(editor, line);
    for (int i = 0; i < editor->diagnostic_count; i++)
    {
        const AromaCodeDiagnostic *diag = &editor->diagnostics[i];
        if (diag->line != line + 1)
            continue;
        uint32_t color = diag->severity == AROMA_CODE_DIAGNOSTIC_ERROR ? editor->palette.error :
                         diag->severity == AROMA_CODE_DIAGNOSTIC_WARNING ? editor->palette.warning :
                         diag->severity == AROMA_CODE_DIAGNOSTIC_INFO ? editor->palette.info :
                         editor->palette.comment;
        int column = diag->column > 0 ? diag->column : 1;
        size_t start = ce_offset_for_line_column(editor, line, column);
        if (start < line_start)
            start = line_start;
        if (start > content_end)
            start = content_end;
        int end_column = diag->end_column > 0 ? diag->end_column : column + 1;
        if (diag->end_line > 0 && diag->end_line != diag->line)
            end_column = ce_line_display_columns(editor, line) + 1;
        size_t end = ce_offset_for_line_column(editor, line, end_column);
        if (end < start)
            end = start;
        if (end > content_end)
            end = content_end;
        size_t start_expanded = ce_offset_to_expanded(editor, line, start);
        size_t end_expanded = ce_offset_to_expanded(editor, line, end);
        int wrap_offset = wrap_start_col > 0
                              ? ce_pixel_x_from_column(editor, line, wrap_start_col)
                              : 0;
        int x = m->code_x + ce_pixel_x_from_column(editor, line, (int)start_expanded) - editor->scroll_x - wrap_offset;
        int width = (m->code_x + ce_pixel_x_from_column(editor, line, (int)end_expanded) - editor->scroll_x - wrap_offset) - x;
        if (width < 6)
            width = 6;
        if (x + width < m->code_x || x > m->code_x + m->code_width)
            continue;
        int y = line_y + m->line_height - 3;
        gfx->fill_rectangle(window_id, x, y, width, 2, color, false, 0.0f);
    }
}

static void ce_draw_gutter(AromaGraphicsInterface *gfx, size_t window_id, CeEditor *editor,
                           const CeMetrics *m, int wrap_cols)
{
    gfx->fill_rectangle(window_id, editor->rect.x, editor->rect.y, m->gutter_width,
                        editor->rect.height, editor->palette.gutter_background, false, 0.0f);
    gfx->fill_rectangle(window_id, m->code_x - 1, editor->rect.y, 1, editor->rect.height,
                        editor->palette.popup_border, false, 0.0f);

    int current_line = ce_line_of_offset(editor, editor->cursor);
    int font_height = editor->font ? aroma_font_get_line_height(editor->font) : m->line_height;
    for (int line = 0; line < editor->line_count; line++)
    {
        int visual_row = ce_visual_row_for_line(editor, line, wrap_cols);
        int y = m->code_y + visual_row * m->line_height - editor->scroll_y;
        if (y + m->line_height < editor->rect.y || y > editor->rect.y + editor->rect.height)
            continue;
        uint32_t diag_color = 0;
        bool has_diag = ce_diag_count_on_line(editor, line + 1, &diag_color) > 0;
        if (has_diag)
        {
            int dot = m->char_width / 3;
            if (dot < 3)
                dot = 3;
            if (dot > 5)
                dot = 5;
            gfx->fill_rectangle(window_id, editor->rect.x + 8,
                                y + (m->line_height - dot) / 2, dot, dot,
                                diag_color, true, (float)dot * 0.5f);
        }
        if (editor->line_numbers)
        {
            char number[24];
            snprintf(number, sizeof(number), "%d", line + 1);
            int text_width = (int)((float)(int)strlen(number) * (float)m->char_width);
            int nx = m->code_x - text_width - 10;
            int ty = y + (m->line_height - font_height) / 2;
            ce_draw_text(gfx, window_id, editor->font, number, nx, ty,
                         line == current_line ? editor->palette.gutter_active :
                                                editor->palette.gutter_text);
        }
    }
}

static void ce_draw_minimap(AromaGraphicsInterface *gfx, size_t window_id, CeEditor *editor,
                            const CeMetrics *m)
{
    if (m->minimap_width <= 0)
        return;
    gfx->fill_rectangle(window_id, m->minimap_x, editor->rect.y, m->minimap_width,
                        editor->rect.height, editor->palette.minimap, false, 0.0f);
    gfx->fill_rectangle(window_id, m->minimap_x, editor->rect.y, 1, editor->rect.height,
                        editor->palette.popup_border, false, 0.0f);

    if (editor->line_count <= 0)
        return;
    float scale = (float)m->code_height / (float)(editor->line_count * m->line_height);
    int bar_height = (int)((float)m->line_height * scale);
    if (bar_height < 1)
        bar_height = 1;
    int max_cols = editor->max_columns > 0 ? editor->max_columns : 1;
    for (int line = 0; line < editor->line_count; line++)
    {
        int y = m->code_y + (int)((float)line * (float)m->line_height * scale);
        int indent = 0;
        size_t pos = ce_line_start(editor, line);
        size_t end = ce_line_end_content(editor, line);
        while (pos < end && editor->text[pos] == ' ' && indent < 16)
        {
            indent++;
            pos++;
        }
        if (pos >= end)
            continue;
        uint32_t color = editor->palette.text;
        unsigned char first = (unsigned char)editor->text[pos];
        if (first == '/' || first == '#')
            color = editor->palette.comment;
        else if (first == '"' || first == '\'' || first == '`')
            color = editor->palette.string;
        color = (color & 0x00FFFFFFu) | 0x99000000u;
        int line_cols = (int)(end - pos);
        int x = m->minimap_x + 8 + indent * 2;
        int width = line_cols * (m->minimap_width - 18) / max_cols;
        if (width < 2)
            width = 2;
        if (x + width > m->minimap_x + m->minimap_width - 4)
            width = m->minimap_x + m->minimap_width - 4 - x;
        if (width > 0)
            gfx->fill_rectangle(window_id, x, y, width, bar_height, color, false, 0.0f);
    }

    int content_height = editor->line_count * m->line_height;
    int viewport_height = content_height > 0
                              ? m->code_height * m->code_height / content_height
                              : m->code_height;
    if (viewport_height < 20)
        viewport_height = 20;
    if (viewport_height > m->code_height)
        viewport_height = m->code_height;
    int viewport_y = m->code_y;
    if (content_height > m->code_height)
        viewport_y += editor->scroll_y * (m->code_height - viewport_height) /
                      (content_height - m->code_height);
    gfx->fill_rectangle(window_id, m->minimap_x, viewport_y, m->minimap_width,
                        viewport_height, editor->palette.minimap_viewport, true, 3.0f);
}

static void ce_minimap_viewport(const CeEditor *editor, const CeMetrics *m,
                                int *viewport_y, int *viewport_height,
                                int *max_scroll)
{
    int content_height = editor->line_count * m->line_height;
    int height = content_height > 0
                     ? m->code_height * m->code_height / content_height
                     : m->code_height;
    if (height < 20)
        height = 20;
    if (height > m->code_height)
        height = m->code_height;
    int scroll_limit = content_height > m->code_height
                           ? content_height - m->code_height
                           : 0;
    int travel = m->code_height - height;
    int y = m->code_y;
    if (scroll_limit > 0 && travel > 0)
        y += editor->scroll_y * travel / scroll_limit;
    if (viewport_y)
        *viewport_y = y;
    if (viewport_height)
        *viewport_height = height;
    if (max_scroll)
        *max_scroll = scroll_limit;
}

static void ce_scroll_from_minimap(CeEditor *editor, const CeMetrics *m,
                                   int y, int drag_offset)
{
    int viewport_y;
    int viewport_height;
    int max_scroll;
    ce_minimap_viewport(editor, m, &viewport_y, &viewport_height, &max_scroll);
    int travel = m->code_height - viewport_height;
    if (max_scroll <= 0 || travel <= 0)
    {
        editor->scroll_y = 0;
        return;
    }
    int desired_y = y - drag_offset;
    int max_viewport_y = m->code_y + travel;
    if (desired_y < m->code_y)
        desired_y = m->code_y;
    if (desired_y > max_viewport_y)
        desired_y = max_viewport_y;
    editor->scroll_y = (desired_y - m->code_y) * max_scroll / travel;
    ce_clamp_scroll(editor);
}

static void ce_draw_scrollbars(AromaGraphicsInterface *gfx, size_t window_id, CeEditor *editor,
                               const CeMetrics *m)
{
    int content_height = editor->line_count * m->line_height;
    if (content_height > m->code_height)
    {
        int track_x = editor->rect.x + editor->rect.width - 7;
        int track_y = m->code_y;
        int track_h = m->code_height;
        int thumb_h = track_h * m->code_height / content_height;
        if (thumb_h < 24)
            thumb_h = 24;
        if (thumb_h > track_h)
            thumb_h = track_h;
        int max_scroll = content_height - m->code_height;
        int thumb_y = track_y;
        if (max_scroll > 0)
            thumb_y += editor->scroll_y * (track_h - thumb_h) / max_scroll;
        gfx->fill_rectangle(window_id, track_x, thumb_y, 5, thumb_h,
                            editor->palette.scrollbar, true, 2.5f);
    }

    int content_width = (int)((float)editor->max_columns * (float)m->char_width) + 24;
    if (content_width > m->code_width)
    {
        int track_y = m->code_y + m->code_height - 5;
        int thumb_w = m->code_width * m->code_width / content_width;
        if (thumb_w < 24)
            thumb_w = 24;
        if (thumb_w > m->code_width)
            thumb_w = m->code_width;
        int max_scroll = content_width - m->code_width;
        int thumb_x = m->code_x;
        if (max_scroll > 0)
            thumb_x += editor->scroll_x * (m->code_width - thumb_w) / max_scroll;
        gfx->fill_rectangle(window_id, thumb_x, track_y, thumb_w, 4,
                            editor->palette.scrollbar, true, 2.0f);
    }
}

static void ce_draw_find_bar(AromaGraphicsInterface *gfx, size_t window_id, CeEditor *editor,
                             const CeMetrics *m)
{
    if (!editor->find_visible)
        return;
    int x, y, w, h;
    ce_find_bar_rect(editor, m, &x, &y, &w, &h);
    gfx->fill_rectangle(window_id, x, y, w, h, editor->palette.find_bar, true, 5.0f);
    gfx->draw_hollow_rectangle(window_id, x, y, w, h, editor->palette.popup_border, 1,
                               true, 5.0f);
    int font_height = editor->font ? aroma_font_get_line_height(editor->font) : m->line_height;
    int ty = y + (h - font_height) / 2;
    const char *query = editor->find_query[0] ? editor->find_query : "Find";
    ce_draw_text(gfx, window_id, editor->font, query, x + 12, ty,
                 editor->find_query[0] ? editor->palette.text : editor->palette.gutter_text);
    char count[32];
    if (editor->find_query[0])
        snprintf(count, sizeof(count), "%d/%d", editor->find_total > 0 ? editor->find_index + 1 : 0,
                 editor->find_total);
    else
        count[0] = '\0';
    int count_width = (int)((float)(int)strlen(count) * (float)m->char_width);
    ce_draw_text(gfx, window_id, editor->font, count, x + w - 102 - count_width, ty,
                 editor->find_total > 0 ? editor->palette.gutter_text : editor->palette.error);
    ce_draw_text(gfx, window_id, editor->font, "<", x + w - 78, ty, editor->palette.text);
    ce_draw_text(gfx, window_id, editor->font, ">", x + w - 52, ty, editor->palette.text);
    ce_draw_text(gfx, window_id, editor->font, "x", x + w - 22, ty, editor->palette.text);
}

static uint32_t ce_completion_color(const CeEditor *editor, AromaCodeCompletionKind kind)
{
    switch (kind)
    {
    case AROMA_CODE_COMPLETION_METHOD:
    case AROMA_CODE_COMPLETION_FUNCTION: return editor->palette.function;
    case AROMA_CODE_COMPLETION_VARIABLE: return editor->palette.property;
    case AROMA_CODE_COMPLETION_CLASS:
    case AROMA_CODE_COMPLETION_INTERFACE:
    case AROMA_CODE_COMPLETION_MODULE: return editor->palette.type;
    case AROMA_CODE_COMPLETION_PROPERTY: return editor->palette.attribute;
    case AROMA_CODE_COMPLETION_KEYWORD: return editor->palette.keyword;
    case AROMA_CODE_COMPLETION_SNIPPET: return editor->palette.tag;
    case AROMA_CODE_COMPLETION_TEXT:
    default: return editor->palette.text;
    }
}

static char ce_completion_letter(AromaCodeCompletionKind kind)
{
    switch (kind)
    {
    case AROMA_CODE_COMPLETION_METHOD: return 'M';
    case AROMA_CODE_COMPLETION_FUNCTION: return 'f';
    case AROMA_CODE_COMPLETION_VARIABLE: return 'v';
    case AROMA_CODE_COMPLETION_CLASS: return 'C';
    case AROMA_CODE_COMPLETION_INTERFACE: return 'I';
    case AROMA_CODE_COMPLETION_MODULE: return 'M';
    case AROMA_CODE_COMPLETION_PROPERTY: return 'p';
    case AROMA_CODE_COMPLETION_KEYWORD: return 'K';
    case AROMA_CODE_COMPLETION_SNIPPET: return 'S';
    case AROMA_CODE_COMPLETION_TEXT:
    default: return 'T';
    }
}

static void ce_draw_completion_popup(AromaGraphicsInterface *gfx, size_t window_id,
                                     CeEditor *editor, const CeMetrics *m)
{
    if (!editor->completions_visible || editor->completion_count <= 0)
        return;
    int x, y, w, h;
    ce_completion_popup_rect(editor, m, &x, &y, &w, &h);
    gfx->fill_rectangle(window_id, x, y, w, h, editor->palette.popup_background, true, 6.0f);
    gfx->draw_hollow_rectangle(window_id, x, y, w, h, editor->palette.popup_border, 1,
                               true, 6.0f);

    int header = m->line_height + 8;
    int font_height = editor->font ? aroma_font_get_line_height(editor->font) : m->line_height;
    ce_draw_text(gfx, window_id, editor->font, "Suggestions", x + 12,
                 y + (header - font_height) / 2, editor->palette.gutter_text);
    gfx->fill_rectangle(window_id, x + 1, y + header, w - 2, 1,
                        editor->palette.popup_border, false, 0.0f);

    int row_height = m->line_height + 6;
    int rows = editor->completion_count > CE_POPUP_MAX_ROWS ? CE_POPUP_MAX_ROWS : editor->completion_count;
    int first = 0;
    if (editor->completion_index >= CE_POPUP_MAX_ROWS)
        first = editor->completion_index - CE_POPUP_MAX_ROWS + 1;
    for (int i = 0; i < rows; i++)
    {
        int index = first + i;
        if (index >= editor->completion_count)
            break;
        int ry = y + header + i * row_height;
        if (index == editor->completion_index)
        {
            gfx->fill_rectangle(window_id, x + 4, ry, w - 8, row_height,
                                editor->palette.popup_selection, true, 4.0f);
            gfx->fill_rectangle(window_id, x + 4, ry + 4, 3, row_height - 8,
                                editor->palette.info, true, 1.5f);
        }
        AromaCodeCompletion *completion = &editor->completions[index];
        char letter[2] = {ce_completion_letter(completion->kind), '\0'};
        ce_draw_text(gfx, window_id, editor->font, letter, x + 14,
                     ry + (row_height - font_height) / 2,
                     ce_completion_color(editor, completion->kind));
        int label_x = x + 34;
        int detail_width = 0;
        if (completion->detail[0])
        {
            detail_width = (int)((float)(int)strlen(completion->detail) * (float)m->char_width);
            if (detail_width > w / 2 - 20)
                detail_width = w / 2 - 20;
            char detail[128];
            ce_safe_copy(detail, sizeof(detail), completion->detail);
            size_t max_chars = (size_t)((w / 2 - 20) / (m->char_width > 0 ? m->char_width : 8));
            if (strlen(detail) > max_chars)
                detail[max_chars] = '\0';
            ce_draw_text(gfx, window_id, editor->font, detail,
                         x + w - 14 - (int)((float)(int)strlen(detail) * (float)m->char_width),
                         ry + (row_height - font_height) / 2, editor->palette.gutter_text);
        }
        int max_label_width = w - 34 - (detail_width > 0 ? detail_width + 28 : 14);
        char label[80];
        ce_safe_copy(label, sizeof(label), completion->label);
        size_t max_chars = (size_t)(max_label_width / (m->char_width > 0 ? m->char_width : 8));
        if (max_chars < 4)
            max_chars = 4;
        if (strlen(label) > max_chars)
            label[max_chars] = '\0';
        ce_draw_text(gfx, window_id, editor->font, label, label_x,
                     ry + (row_height - font_height) / 2, editor->palette.text);
    }
}

static int ce_count_hover_rows(const char *text, int max_chars, int max_rows)
{
    if (!text || !text[0])
        return 0;
    int rows = 0;
    int col = 0;
    for (const char *p = text; *p && rows < max_rows; p++)
    {
        if (*p == '\n')
        {
            rows++;
            col = 0;
            continue;
        }
        if (col >= max_chars)
        {
            rows++;
            col = 0;
            if (rows >= max_rows)
                break;
        }
        col++;
    }
    if (rows < max_rows)
        rows++;
    return rows;
}

static void ce_draw_hover_popup(AromaGraphicsInterface *gfx, size_t window_id,
                                CeEditor *editor, const CeMetrics *m)
{
    if (!editor->hover_visible || !editor->hover_text[0])
        return;
    int width = editor->rect.width - 32;
    if (width > 440)
        width = 440;
    if (width < 220)
        width = editor->rect.width - 16;
    int max_chars = (width - 24) / (m->char_width > 0 ? m->char_width : 8);
    if (max_chars < 12)
        max_chars = 12;
    int max_rows = 10;
    int rows = ce_count_hover_rows(editor->hover_text, max_chars, max_rows);
    if (rows <= 0)
        return;
    int font_height = editor->font ? aroma_font_get_line_height(editor->font) : m->line_height;
    int line_height = font_height + 3;
    int height = rows * line_height + 18;
    int line = editor->hover_line > 0 ? editor->hover_line - 1 : 0;
    int col = editor->hover_column > 0 ? editor->hover_column - 1 : 0;
    int x = m->code_x + ce_pixel_x_from_column(editor, line, col) - editor->scroll_x + 12;
    int y = m->code_y + (line + 1) * m->line_height - editor->scroll_y + 5;
    if (x + width > editor->rect.x + editor->rect.width - 8)
        x = editor->rect.x + editor->rect.width - width - 8;
    if (x < editor->rect.x + 8)
        x = editor->rect.x + 8;
    if (y + height > editor->rect.y + editor->rect.height - 8)
        y = m->code_y + line * m->line_height - editor->scroll_y - height - 4;
    if (y < editor->rect.y + 8)
        y = editor->rect.y + 8;

    gfx->fill_rectangle(window_id, x, y, width, height,
                        editor->palette.popup_background, true, 6.0f);
    gfx->draw_hollow_rectangle(window_id, x, y, width, height,
                               editor->palette.popup_border, 1, true, 6.0f);

    int row = 0;
    int col_pos = 0;
    char line_buffer[512];
    int line_used = 0;
    line_buffer[0] = '\0';
    const char *p = editor->hover_text;
    while (*p && row < max_rows)
    {
        if (*p == '\n' || col_pos >= max_chars)
        {
            line_buffer[line_used] = '\0';
            ce_draw_text(gfx, window_id, editor->font, line_buffer,
                         x + 12, y + 9 + row * line_height, editor->palette.text);
            row++;
            line_used = 0;
            line_buffer[0] = '\0';
            col_pos = 0;
            if (*p == '\n')
            {
                p++;
                continue;
            }
        }
        if (line_used + 1 < (int)sizeof(line_buffer))
            line_buffer[line_used++] = *p;
        p++;
        col_pos++;
    }
    if (row < max_rows && line_used > 0)
    {
        line_buffer[line_used] = '\0';
        ce_draw_text(gfx, window_id, editor->font, line_buffer,
                     x + 12, y + 9 + row * line_height, editor->palette.text);
    }
}

void aroma_code_editor_draw(AromaNode *node, size_t window_id)
{
    if (!node || !node->node_widget_ptr || aroma_node_is_hidden(node))
        return;
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return;
    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;

    editor->last_window_id = (int)window_id;
    if (editor->font && editor->last_font_window != (int)window_id)
    {
        aroma_ui_prepare_font_for_window(window_id, editor->font);
        editor->last_font_window = (int)window_id;
        editor->char_width = 0.0f;
    }
    if (editor->char_width <= 0.0f)
        ce_measure_char(editor);

    ce_clamp_scroll(editor);
    CeMetrics m;
    ce_metrics(editor, &m);

    gfx->fill_rectangle(window_id, editor->rect.x, editor->rect.y,
                        editor->rect.width, editor->rect.height,
                        editor->palette.background, false, 0.0f);

    int wrap_cols = ce_wrap_cols(editor, m.code_width, m.char_width);
    int total_rows = ce_total_visual_rows(editor, wrap_cols);
    int first_row = editor->scroll_y / m.line_height;
    int last_row = (editor->scroll_y + m.code_height) / m.line_height + 1;
    first_row = ce_clamp_int(first_row, 0, total_rows - 1);
    last_row = ce_clamp_int(last_row, first_row, total_rows - 1);
    int current_line = ce_line_of_offset(editor, editor->cursor);

    gfx->graphics_set_clip(m.code_x, m.code_y, m.code_width, m.code_height);
    for (int row = first_row; row <= last_row; row++)
    {
        int line = row;
        int segment = 0;
        if (wrap_cols > 0)
            ce_visual_row_to_line(editor, row, wrap_cols, &line, &segment);
        if (line < 0 || line >= editor->line_count)
            continue;
        int line_y = m.code_y + row * m.line_height - editor->scroll_y;
        if (line_y + m.line_height < m.code_y || line_y > m.code_y + m.code_height)
            continue;
        int wrap_start_col = wrap_cols > 0 ? segment * wrap_cols : 0;
        if (editor->focused && line == current_line)
            gfx->fill_rectangle(window_id, m.code_x, line_y, m.code_width,
                                m.line_height, editor->palette.current_line, false, 0.0f);
        ce_draw_find_highlights(gfx, window_id, editor, &m, line, line_y, wrap_start_col);
        ce_draw_selection_line(gfx, window_id, editor, &m, line, line_y, wrap_start_col);
        ce_draw_line_tokens(gfx, window_id, editor, &m, line, line_y, wrap_start_col);
        ce_draw_diagnostics(gfx, window_id, editor, &m, line, line_y, wrap_start_col);
    }

    ce_draw_bracket_highlight(gfx, window_id, editor, &m, wrap_cols);

    if (editor->focused && editor->show_cursor)
    {
        int line = ce_line_of_offset(editor, editor->cursor);
        int column = ce_column_of_offset(editor, editor->cursor);
        int cursor_row = ce_visual_row_for_line(editor, line, wrap_cols);
        int cx = m.code_x + ce_pixel_x_from_column(editor, line, column) - editor->scroll_x;
        if (wrap_cols > 0)
        {
            int segment = column / wrap_cols;
            cursor_row += segment;
            cx = m.code_x + ce_pixel_x_from_column(editor, line, column) -
                 ce_pixel_x_from_column(editor, line, segment * wrap_cols) -
                 editor->scroll_x;
        }
        int cy = m.code_y + cursor_row * m.line_height - editor->scroll_y;
        int cursor_width = m.char_width / 8;
        if (cursor_width < 2)
            cursor_width = 2;
        int font_height = editor->font ? aroma_font_get_line_height(editor->font) : m.line_height;
        if (font_height < m.line_height - 4)
            font_height = m.line_height - 4;
        if (cx >= m.code_x - cursor_width && cx <= m.code_x + m.code_width &&
            cy + m.line_height >= m.code_y && cy <= m.code_y + m.code_height)
            gfx->fill_rectangle(window_id, cx, cy + (m.line_height - font_height) / 2,
                                cursor_width, font_height, editor->palette.cursor, false, 0.0f);
    }
    gfx->graphics_clear_clip();

    ce_draw_gutter(gfx, window_id, editor, &m, wrap_cols);
    ce_draw_minimap(gfx, window_id, editor, &m);
    ce_draw_scrollbars(gfx, window_id, editor, &m);
    ce_draw_find_bar(gfx, window_id, editor, &m);
    ce_draw_completion_popup(gfx, window_id, editor, &m);
    ce_draw_hover_popup(gfx, window_id, editor, &m);
}
static void ce_request_hover_at(CeEditor *editor, int x, int y)
{
    if (!editor)
        return;
    CeMetrics m;
    ce_metrics(editor, &m);
    if (x < m.code_x || x >= m.code_x + m.code_width ||
        y < m.code_y || y >= m.code_y + m.code_height)
    {
        editor->hover_waiting = false;
        editor->hover_visible = false;
        return;
    }
    int wrap_cols = ce_wrap_cols(editor, m.code_width, m.char_width);
    int row = (y - m.code_y + editor->scroll_y) / m.line_height;
    int line = row;
    int segment = 0;
    if (wrap_cols > 0)
        ce_visual_row_to_line(editor, row, wrap_cols, &line, &segment);
    line = ce_clamp_int(line, 0, editor->line_count - 1);
    int segment_start_px = wrap_cols > 0
                               ? ce_pixel_x_from_column(editor, line, segment * wrap_cols)
                               : 0;
    int col = (x - m.code_x + editor->scroll_x + segment_start_px) / m.char_width;
    if (col < 0)
        col = 0;
    if (editor->hover_waiting && editor->hover_request_line == line &&
        editor->hover_request_column == col)
        return;
    editor->hover_request_line = line;
    editor->hover_request_column = col;
    editor->hover_request_ms = ce_now_ms();
    editor->hover_waiting = true;
    editor->hover_visible = false;
    aroma_node_invalidate(editor->node);
}

static bool ce_mouse_handler(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!event || !event->target_node)
        return false;
    CeEditor *editor = AROMA_NODE_AS(event->target_node, CeEditor);
    if (!editor)
        return false;

    AromaEventType type = event->event_type;
    int x = event->data.mouse.x;
    int y = event->data.mouse.y;
    if (type == EVENT_TYPE_TOUCH_DOWN || type == EVENT_TYPE_TOUCH_UP ||
        type == EVENT_TYPE_TOUCH_MOVE)
    {
        x = event->data.touch.x;
        y = event->data.touch.y;
        if (type == EVENT_TYPE_TOUCH_DOWN)
            type = EVENT_TYPE_MOUSE_CLICK;
        else if (type == EVENT_TYPE_TOUCH_UP)
            type = EVENT_TYPE_MOUSE_RELEASE;
        else
            type = EVENT_TYPE_MOUSE_MOVE;
    }

    CeMetrics m;
    ce_metrics(editor, &m);

    if (type == EVENT_TYPE_MOUSE_SCROLL)
    {
        float sy = event->data.mouse.scroll_y;
        float sx = event->data.mouse.scroll_x;
        if (sy != 0.0f)
            editor->scroll_y -= (int)(sy * 50.0f);
        if (sx != 0.0f)
            editor->scroll_x -= (int)(sx * 50.0f);
        ce_clamp_scroll(editor);
        editor->hover_visible = false;
        editor->hover_waiting = false;
        aroma_node_invalidate(event->target_node);
        return true;
    }

    if (type == EVENT_TYPE_MOUSE_CLICK)
    {
        aroma_code_editor_focus(event->target_node);
        editor->hover_visible = false;
        editor->hover_waiting = false;

        if (editor->completions_visible)
        {
            int px, py, pw, ph;
            ce_completion_popup_rect(editor, &m, &px, &py, &pw, &ph);
            if (x >= px && x < px + pw && y >= py && y < py + ph)
            {
                int header = m.line_height + 8;
                int row_height = m.line_height + 6;
                int row = (y - py - header) / row_height;
                if (row >= 0 && row < editor->completion_count)
                {
                    editor->completion_index = row;
                    ce_accept_completion(editor);
                }
                return true;
            }
        }

        if (editor->find_visible)
        {
            int fx, fy, fw, fh;
            ce_find_bar_rect(editor, &m, &fx, &fy, &fw, &fh);
            if (x >= fx && x < fx + fw && y >= fy && y < fy + fh)
            {
                if (x >= fx + fw - 30)
                {
                    aroma_code_editor_hide_find(event->target_node);
                }
                else if (x >= fx + fw - 92)
                {
                    if (editor->find_total <= 0)
                        ce_rebuild_find_matches(editor, true);
                    else
                        ce_find_next(editor, (x < fx + fw - 61) ? -1 : 1);
                }
                return true;
            }
        }

        if (m.minimap_width > 0 && x >= m.minimap_x)
        {
            int viewport_y;
            int viewport_height;
            ce_minimap_viewport(editor, &m, &viewport_y, &viewport_height, NULL);
            int drag_offset = viewport_height / 2;
            if (y >= viewport_y && y < viewport_y + viewport_height)
                drag_offset = y - viewport_y;
            editor->minimap_dragging = true;
            editor->minimap_drag_offset = drag_offset;
            ce_scroll_from_minimap(editor, &m, y, drag_offset);
            aroma_node_invalidate(event->target_node);
            return true;
        }

        size_t offset = ce_offset_at_point(editor, x, y);
        uint64_t now = ce_now_ms();
        bool double_click = (now - editor->last_click_ms < 360) &&
                            (offset == editor->last_click_offset ||
                             (offset > 0 && offset - 1 == editor->last_click_offset));
        editor->last_click_ms = now;
        editor->last_click_offset = offset;
        editor->dragging = true;

        if (double_click)
        {
            ce_select_word_at(editor, offset);
            ce_request_definition(editor);
            return true;
        }

        ce_update_cursor(editor, offset, false);
        ce_ensure_cursor_visible(editor);
        int line = ce_line_of_offset(editor, offset);
        (void)line;
        return true;
    }

    if (type == EVENT_TYPE_MOUSE_DOUBLE_CLICK)
    {
        size_t offset = ce_offset_at_point(editor, x, y);
        ce_update_cursor(editor, offset, false);
        ce_select_word_at(editor, offset);
        ce_request_definition(editor);
        return true;
    }

    if (type == EVENT_TYPE_MOUSE_MOVE)
    {
        if (editor->minimap_dragging)
        {
            ce_scroll_from_minimap(editor, &m, y,
                                   editor->minimap_drag_offset);
            aroma_node_invalidate(event->target_node);
            return true;
        }
        if (editor->dragging)
        {
            size_t offset = ce_offset_at_point(editor, x, y);
            ce_update_cursor(editor, offset, true);
            ce_ensure_cursor_visible(editor);
            return true;
        }
        if (!(x >= m.code_x && x < m.code_x + m.code_width &&
              y >= m.code_y && y < m.code_y + m.code_height))
        {
            editor->hover_visible = false;
            editor->hover_waiting = false;
        }
        else
        {
            ce_request_hover_at(editor, x, y);
        }
        return false;
    }

    if (type == EVENT_TYPE_MOUSE_RELEASE)
    {
        editor->dragging = false;
        editor->minimap_dragging = false;
        return true;
    }

    if (type == EVENT_TYPE_MOUSE_EXIT)
    {
        editor->hover_visible = false;
        editor->hover_waiting = false;
        aroma_node_invalidate(event->target_node);
        return false;
    }

    return false;
}

static bool ce_focus_handler(AromaEvent *event, void *user_data)
{
    (void)user_data;
    if (!event || !event->target_node)
        return false;
    CeEditor *editor = AROMA_NODE_AS(event->target_node, CeEditor);
    if (!editor)
        return false;
    if (event->event_type == EVENT_TYPE_FOCUS_LOST)
    {
        editor->focused = false;
        editor->show_cursor = false;
        editor->dragging = false;
        editor->minimap_dragging = false;
        editor->hover_visible = false;
        editor->hover_waiting = false;
        editor->completions_visible = false;
        aroma_node_invalidate(event->target_node);
        return true;
    }
    if (event->event_type == EVENT_TYPE_FOCUS_GAINED)
    {
        editor->focused = true;
        editor->show_cursor = true;
        editor->cursor_blink_ms = ce_now_ms();
        aroma_node_invalidate(event->target_node);
        return true;
    }
    return false;
}

bool aroma_code_editor_tick(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return false;
    bool changed = false;
    uint64_t now = ce_now_ms();

    if (editor->focused && now - editor->cursor_blink_ms >= CE_CURSOR_BLINK_MS)
    {
        editor->cursor_blink_ms = now;
        editor->show_cursor = !editor->show_cursor;
        aroma_node_invalidate(node);
        changed = true;
    }

    if (editor->hover_waiting &&
        now - editor->hover_request_ms >= CE_HOVER_DELAY_MS)
    {
        editor->hover_waiting = false;
        if (editor->on_hover_request)
        {
            int line = editor->hover_request_line + 1;
            int column = editor->hover_request_column + 1;
            editor->on_hover_request(node, line, column, editor->on_hover_data);
            changed = true;
        }
    }

    return changed;
}

bool aroma_code_editor_setup_events(AromaNode *node)
{
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    if (!editor)
        return false;

    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, ce_mouse_handler, NULL, 20);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, ce_mouse_handler, NULL, 20);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_MOVE, ce_mouse_handler, NULL, 20);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_EXIT, ce_mouse_handler, NULL, 20);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_DOUBLE_CLICK, ce_mouse_handler, NULL, 20);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_SCROLL, ce_mouse_handler, NULL, 20);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_DOWN, ce_mouse_handler, NULL, 20);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_UP, ce_mouse_handler, NULL, 20);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_TOUCH_MOVE, ce_mouse_handler, NULL, 20);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_KEY_PRESS, ce_keyboard_handler, NULL, 20);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_FOCUS_LOST, ce_focus_handler, NULL, 20);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_FOCUS_GAINED, ce_focus_handler, NULL, 20);
    return true;
}

void aroma_code_editor_destroy(AromaNode *node)
{
    if (!node)
        return;
    CeEditor *editor = AROMA_NODE_AS(node, CeEditor);
    AromaNode *focused = aroma_ui_get_focused_node();
    if (focused == node)
        aroma_ui_clear_focused_node(node);
    if (!editor)
        return;

    ce_clear_undo_redo(editor);
    free(editor->clipboard_text);
    free(editor->line_starts);
    free(editor->text);
    if (editor->owns_font && editor->font)
        aroma_font_destroy(editor->font);
    editor->font = NULL;
    editor->text = NULL;
    editor->line_starts = NULL;
    editor->clipboard_text = NULL;

    node->node_widget_ptr = NULL;
    aroma_widget_free(editor);
}
