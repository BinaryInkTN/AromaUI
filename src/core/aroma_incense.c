#include "aroma_incense.h"
#include "utils/mpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

typedef struct IncludeStackEntry
{
    char path[4096];
} IncludeStackEntry;

static IncludeStackEntry include_stack[64];
static size_t include_depth = 0;

static char *incense_strip_comments(const char *src)
{
    size_t len = strlen(src);
    char *out = malloc(len + 1);
    if (!out)
        return NULL;

    size_t i = 0, j = 0;
    while (i < len)
    {
        if (src[i] == '/' && i + 1 < len && src[i + 1] == '/')
        {
            while (i < len && src[i] != '\n')
                i++;
            continue;
        }
        if (src[i] == '/' && i + 1 < len && src[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < len)
            {
                if (src[i] == '*' && src[i + 1] == '/')
                {
                    i += 2;
                    break;
                }
                if (src[i] == '\n')
                    out[j++] = '\n';
                i++;
            }
            continue;
        }
        if (src[i] == '"')
        {
            out[j++] = src[i++];
            while (i < len && src[i] != '"')
            {
                if (src[i] == '\\' && i + 1 < len)
                    out[j++] = src[i++];
                out[j++] = src[i++];
            }
            if (i < len)
                out[j++] = src[i++];
            continue;
        }
        out[j++] = src[i++];
    }
    out[j] = '\0';
    return out;
}

static char *incense_strdup(const char *s)
{
    if (!s)
        return NULL;
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (copy)
        memcpy(copy, s, len + 1);
    return copy;
}

static IncenseNode *incense_node_new(IncenseNodeType type, const char *name, const char *value, int line, int column)
{
    IncenseNode *n = calloc(1, sizeof(IncenseNode));
    if (!n)
        return NULL;
    n->type = type;
    n->line = line;
    n->column = column;
    n->name = incense_strdup(name);
    n->value = incense_strdup(value);
    return n;
}

static void incense_node_append_child(IncenseNode *parent, IncenseNode *child)
{
    if (!parent->first_child)
    {
        parent->first_child = child;
        return;
    }
    IncenseNode *cur = parent->first_child;
    while (cur->next_sibling)
        cur = cur->next_sibling;
    cur->next_sibling = child;
}

static void incense_node_destroy(IncenseNode *node)
{
    if (!node)
        return;
    IncenseNode *child = node->first_child;
    while (child)
    {
        IncenseNode *next = child->next_sibling;
        incense_node_destroy(child);
        child = next;
    }
    free(node->name);
    free(node->value);
    free(node);
}

static char *incense_resolve_includes(const char *source, const char *base_path)
{
    if (!source)
        return NULL;

    size_t cap = strlen(source) * 2 + 4096;
    char *result = malloc(cap);
    if (!result)
        return NULL;
    result[0] = '\0';
    size_t pos = 0;
    const char *p = source;

    while (*p)
    {
        if (*p == '@')
        {
            const char *embed_check = p + 1;

            while (*embed_check == ' ' || *embed_check == '\t')
                embed_check++;

            if (strncmp(embed_check, "embed", 5) == 0)
            {
                p = embed_check + 5;
                while (*p == ' ' || *p == '\t')
                    p++;

                if (*p != '"')
                {
                    while (*p && *p != '\n')
                        p++;
                    const char *err = "// ERROR: Invalid embed syntax\n";
                    size_t err_len = strlen(err);
                    if (pos + err_len + 1 >= cap)
                    {
                        cap = cap * 2 + err_len;
                        char *new_result = realloc(result, cap);
                        if (!new_result)
                        {
                            free(result);
                            return NULL;
                        }
                        result = new_result;
                    }
                    memcpy(result + pos, err, err_len);
                    pos += err_len;
                    continue;
                }

                p++;
                const char *path_start = p;
                while (*p && *p != '"')
                    p++;

                if (*p != '"')
                {
                    while (*p && *p != '\n')
                        p++;
                    const char *err = "// ERROR: Unterminated embed path\n";
                    size_t err_len = strlen(err);
                    if (pos + err_len + 1 >= cap)
                    {
                        cap = cap * 2 + err_len;
                        char *new_result = realloc(result, cap);
                        if (!new_result)
                        {
                            free(result);
                            return NULL;
                        }
                        result = new_result;
                    }
                    memcpy(result + pos, err, err_len);
                    pos += err_len;
                    continue;
                }

                size_t path_len = p - path_start;
                char *inc_path = malloc(path_len + 1);
                if (!inc_path)
                {
                    free(result);
                    return NULL;
                }
                memcpy(inc_path, path_start, path_len);
                inc_path[path_len] = '\0';
                p++;

                while (*p == ' ' || *p == '\t' || *p == '\n')
                    p++;

                if (*p == '{')
                {
                    p++;
                    int brace_depth = 1;
                    while (*p && brace_depth > 0)
                    {
                        if (*p == '"')
                        {
                            p++;
                            while (*p && *p != '"')
                            {
                                if (*p == '\\' && *(p + 1))
                                    p++;
                                p++;
                            }
                            if (*p == '"')
                                p++;
                        }
                        else if (*p == '{')
                        {
                            brace_depth++;
                            p++;
                        }
                        else if (*p == '}')
                        {
                            brace_depth--;
                            if (brace_depth > 0)
                                p++;
                        }
                        else
                        {
                            p++;
                        }
                    }
                    if (*p == '}')
                        p++;
                }

                char full_path[4096];
                if (base_path)
                {
                    const char *last_sep = strrchr(base_path, '/');
                    if (!last_sep)
                        last_sep = strrchr(base_path, '\\');
                    if (last_sep)
                    {
                        size_t dir_len = last_sep - base_path + 1;
                        if (dir_len < sizeof(full_path))
                        {
                            memcpy(full_path, base_path, dir_len);
                            snprintf(full_path + dir_len, sizeof(full_path) - dir_len, "%s", inc_path);
                        }
                        else
                        {
                            snprintf(full_path, sizeof(full_path), "%s", inc_path);
                        }
                    }
                    else
                    {
                        snprintf(full_path, sizeof(full_path), "%s", inc_path);
                    }
                }
                else
                {
                    snprintf(full_path, sizeof(full_path), "%s", inc_path);
                }

                if (include_depth >= 64)
                {
                    const char *err = "// ERROR: Maximum embed depth exceeded\n";
                    size_t err_len = strlen(err);
                    if (pos + err_len + 1 >= cap)
                    {
                        cap = cap * 2 + err_len;
                        char *new_result = realloc(result, cap);
                        if (!new_result)
                        {
                            free(result);
                            free(inc_path);
                            return NULL;
                        }
                        result = new_result;
                    }
                    memcpy(result + pos, err, err_len);
                    pos += err_len;
                    free(inc_path);
                    continue;
                }

                bool circular = false;
                for (size_t i = 0; i < include_depth; i++)
                {
                    if (strcmp(include_stack[i].path, full_path) == 0)
                    {
                        circular = true;
                        break;
                    }
                }

                if (circular)
                {
                    char *err_buf = malloc(64 + strlen(full_path) + 1);
                    if (!err_buf)
                    {
                        free(result);
                        free(inc_path);
                        return NULL;
                    }
                    sprintf(err_buf, "// ERROR: Circular embed detected: %s\n", full_path);
                    size_t err_len = strlen(err_buf);
                    if (pos + err_len + 1 >= cap)
                    {
                        cap = cap * 2 + err_len;
                        char *new_result = realloc(result, cap);
                        if (!new_result)
                        {
                            free(result);
                            free(err_buf);
                            free(inc_path);
                            return NULL;
                        }
                        result = new_result;
                    }
                    memcpy(result + pos, err_buf, err_len);
                    pos += err_len;
                    free(err_buf);
                    free(inc_path);
                    continue;
                }

                FILE *fp = fopen(full_path, "rb");
                char *file_content = NULL;

                if (fp)
                {
                    fseek(fp, 0, SEEK_END);
                    long fsize = ftell(fp);
                    rewind(fp);

                    if (fsize > 0 && fsize <= 10 * 1024 * 1024)
                    {
                        file_content = malloc(fsize + 1);
                        if (file_content)
                        {
                            size_t read = fread(file_content, 1, fsize, fp);
                            file_content[read] = '\0';
                        }
                    }
                    fclose(fp);
                }

                if (!file_content)
                {
                    char *err_buf = malloc(64 + strlen(full_path) + 1);
                    if (!err_buf)
                    {
                        free(result);
                        free(inc_path);
                        return NULL;
                    }
                    sprintf(err_buf, "// ERROR: Failed to embed file: %s\n", full_path);
                    size_t err_len = strlen(err_buf);
                    if (pos + err_len + 1 >= cap)
                    {
                        cap = cap * 2 + err_len;
                        char *new_result = realloc(result, cap);
                        if (!new_result)
                        {
                            free(result);
                            free(err_buf);
                            free(inc_path);
                            return NULL;
                        }
                        result = new_result;
                    }
                    memcpy(result + pos, err_buf, err_len);
                    pos += err_len;
                    free(err_buf);
                    free(inc_path);
                    continue;
                }

                char *processed = NULL;

                strncpy(include_stack[include_depth].path, full_path, sizeof(include_stack[0].path) - 1);
                include_stack[include_depth].path[sizeof(include_stack[0].path) - 1] = '\0';
                include_depth++;

                processed = incense_resolve_includes(file_content, full_path);
                include_depth--;

                if (processed)
                {
                    size_t proc_len = strlen(processed);
                    if (pos + proc_len + 1 >= cap)
                    {
                        cap = (pos + proc_len) * 2 + 4096;
                        char *new_result = realloc(result, cap);
                        if (!new_result)
                        {
                            free(result);
                            free(processed);
                            free(file_content);
                            free(inc_path);
                            return NULL;
                        }
                        result = new_result;
                    }
                    memcpy(result + pos, processed, proc_len);
                    pos += proc_len;
                    free(processed);
                }
                else
                {
                    if (pos + strlen(file_content) + 1 >= cap)
                    {
                        cap = (pos + strlen(file_content)) * 2 + 4096;
                        char *new_result = realloc(result, cap);
                        if (!new_result)
                        {
                            free(result);
                            free(file_content);
                            free(inc_path);
                            return NULL;
                        }
                        result = new_result;
                    }
                    memcpy(result + pos, file_content, strlen(file_content));
                    pos += strlen(file_content);
                }

                free(file_content);
                free(inc_path);
                continue;
            }
        }

        if (pos + 1 >= cap)
        {
            cap = cap * 2 + 1;
            char *new_result = realloc(result, cap);
            if (!new_result)
            {
                free(result);
                return NULL;
            }
            result = new_result;
        }
        result[pos++] = *p++;
    }

    result[pos] = '\0';
    return result;
}

static IncenseNode *build_node(mpc_ast_t *ast, const char *source);

static IncenseNode *build_object(mpc_ast_t *ast, const char *source)
{
    const char *type_name = NULL;
    int line = 0, column = 0;

    for (int i = 0; i < ast->children_num; i++)
    {
        mpc_ast_t *ch = ast->children[i];
        if (strstr(ch->tag, "typename") && ch->contents && ch->contents[0])
        {
            type_name = ch->contents;
            if (ch->state.pos)
            {
                line = ch->state.row;
                column = ch->state.col;
            }
            break;
        }
        if (!type_name && ch->contents && ch->contents[0] >= 'A' && ch->contents[0] <= 'Z')
        {
            type_name = ch->contents;
            if (ch->state.pos)
            {
                line = ch->state.row;
                column = ch->state.col;
            }
        }
    }
    if (!type_name)
        type_name = "(unknown)";

    if (line == 0)
    {
        for (int i = 0; i < ast->children_num; i++)
        {
            mpc_ast_t *ch = ast->children[i];
            if (ch->state.pos)
            {
                line = ch->state.row;
                column = ch->state.col;
                break;
            }
        }
    }

    IncenseNode *obj = incense_node_new(INCENSE_OBJECT, type_name, NULL, line, column);
    if (!obj)
        return NULL;

    for (int i = 0; i < ast->children_num; i++)
    {
        mpc_ast_t *ch = ast->children[i];
        if (strstr(ch->tag, "item") || strstr(ch->tag, "object") || strstr(ch->tag, "property") || strstr(ch->tag, "embed"))
        {
            IncenseNode *child_node = build_node(ch, source);
            if (child_node)
                incense_node_append_child(obj, child_node);
        }
    }
    return obj;
}

static IncenseNode *build_property(mpc_ast_t *ast, const char *source)
{
    const char *key = NULL;
    const char *value = NULL;
    int line = 0, column = 0;

    for (int i = 0; i < ast->children_num; i++)
    {
        mpc_ast_t *ch = ast->children[i];
        if (strstr(ch->tag, "ident") && !key)
        {
            key = ch->contents;
            if (ch->state.pos)
            {
                line = ch->state.row;
                column = ch->state.col;
            }
        }
        else if (strstr(ch->tag, "value") && !value)
        {
            if (ch->children_num > 0 && ch->children[0]->contents)
            {
                value = ch->children[0]->contents;
            }
            else if (ch->contents && ch->contents[0])
            {
                value = ch->contents;
            }
        }
    }
    if (!key)
        return NULL;
    if (line == 0)
    {
        for (int i = 0; i < ast->children_num; i++)
        {
            mpc_ast_t *ch = ast->children[i];
            if (ch->state.pos)
            {
                line = ch->state.row;
                column = ch->state.col;
                break;
            }
        }
    }
    return incense_node_new(INCENSE_PROPERTY, key, value ? value : "", line, column);
}

static IncenseNode *build_node(mpc_ast_t *ast, const char *source)
{
    if (!ast)
        return NULL;
    if (strstr(ast->tag, "object"))
        return build_object(ast, source);
    if (strstr(ast->tag, "property"))
        return build_property(ast, source);
    if (strstr(ast->tag, "item"))
    {
        for (int i = 0; i < ast->children_num; i++)
        {
            IncenseNode *n = build_node(ast->children[i], source);
            if (n)
                return n;
        }
    }
    if (strstr(ast->tag, "embed"))
    {
        char *filename = NULL;
        int line = 0, column = 0;

        for (int i = 0; i < ast->children_num; i++)
        {
            mpc_ast_t *ch = ast->children[i];
            if (strstr(ch->tag, "string"))
            {
                filename = ch->contents;
                line = ch->state.row;
                column = ch->state.col;
                break;
            }
        }

        if (filename)
        {
            size_t len = strlen(filename);
            if (len >= 2 && filename[0] == '"' && filename[len - 1] == '"')
            {
                filename[len - 1] = '\0';
                filename++;
                IncenseNode *embed_node = incense_node_new(INCENSE_PROPERTY, "@embed", filename, line, column);
                if (embed_node)
                {
                    embed_node->is_embed = 1;
                }
                return embed_node;
            }
        }
    }
    return NULL;
}

static mpc_parser_t *p_Ident = NULL;
static mpc_parser_t *p_TypeName = NULL;
static mpc_parser_t *p_Integer = NULL;
static mpc_parser_t *p_Float_ = NULL;
static mpc_parser_t *p_Boolean = NULL;
static mpc_parser_t *p_Color = NULL;
static mpc_parser_t *p_String = NULL;
static mpc_parser_t *p_Embed = NULL;
static mpc_parser_t *p_Value = NULL;
static mpc_parser_t *p_Property = NULL;
static mpc_parser_t *p_Object = NULL;
static mpc_parser_t *p_Item = NULL;
static mpc_parser_t *p_Document = NULL;
static int g_parsers_ready = 0;

static int incense_parsers_init(void)
{
    if (g_parsers_ready)
        return 1;

    p_Ident = mpc_new("ident");
    p_TypeName = mpc_new("typename");
    p_Integer = mpc_new("integer");
    p_Float_ = mpc_new("float_");
    p_Boolean = mpc_new("boolean");
    p_Color = mpc_new("color");
    p_String = mpc_new("string");
    p_Embed = mpc_new("embed");
    p_Value = mpc_new("value");
    p_Property = mpc_new("property");
    p_Object = mpc_new("object");
    p_Item = mpc_new("item");
    p_Document = mpc_new("document");

    mpc_err_t *err = mpca_lang(MPCA_LANG_DEFAULT,
                               " ident    : /[a-z_][A-Za-z0-9_]*/ ;                      "
                               " typename : /[A-Z][A-Za-z0-9_]*/ ;                        "
                               " integer  : /-?[0-9]+/ ;                                   "
                               " float_   : /-?[0-9]+\\.[0-9]+/ ;                         "
                               " boolean  : \"true\" | \"false\" ;                         "
                               " color    : /#[0-9A-Fa-f]+/ ;                              "
                               " string   : /\\\"(\\\\\\\\.|[^\\\"])*\\\"/ ;               "
                               " embed    : \"@embed\" <string> ;                          "
                               " value    : <float_> | <integer> | <boolean>               "
                               "           | <color>  | <string> ;                         "
                               " property : <ident> \":\" <value> ;                        "
                               " object   : <typename> \"{\" <item>* \"}\" ;              "
                               " item     : <object> | <property> | <embed> ;              "
                               " document : /^/ <object> /$/ ;                             ",
                               p_Ident, p_TypeName, p_Integer, p_Float_, p_Boolean, p_Color, p_String,
                               p_Embed, p_Value, p_Property, p_Object, p_Item, p_Document, NULL);

    if (err)
    {
        fprintf(stderr, "incense: grammar error:\n");
        mpc_err_print(err);
        mpc_err_delete(err);
        return 0;
    }
    g_parsers_ready = 1;
    return 1;
}

static IncenseDocument *incense_from_result(mpc_result_t *r, int ok, const char *source, const char *base_path)
{
    if (!ok)
    {
        mpc_err_print(r->error);
        mpc_err_delete(r->error);
        return NULL;
    }
    mpc_ast_t *ast = (mpc_ast_t *)r->output;
    mpc_ast_t *obj_ast = NULL;
    for (int i = 0; i < ast->children_num; i++)
    {
        if (strstr(ast->children[i]->tag, "object"))
        {
            obj_ast = ast->children[i];
            break;
        }
    }
    if (!obj_ast && strstr(ast->tag, "object"))
        obj_ast = ast;

    IncenseDocument *doc = NULL;
    if (obj_ast)
    {
        IncenseNode *root = build_object(obj_ast, source);
        if (root)
        {
            doc = calloc(1, sizeof(IncenseDocument));
            if (doc)
            {
                doc->root = root;
                doc->base_path = incense_strdup(base_path);
            }
            else
                incense_node_destroy(root);
        }
    }
    mpc_ast_delete(ast);
    return doc;
}

static IncenseDocument *incense_parse_with_embeds(const char *source, const char *base_path)
{
    if (!source)
        return NULL;
    if (!incense_parsers_init())
        return NULL;

    char *processed = incense_resolve_includes(source, base_path);
    if (!processed)
        return NULL;

    char *clean = incense_strip_comments(processed);
    free(processed);
    if (!clean)
        return NULL;

    mpc_result_t r;
    int ok = mpc_parse("<string>", clean, p_Document, &r);
    IncenseDocument *doc = incense_from_result(&r, ok, clean, base_path);
    free(clean);
    return doc;
}

IncenseDocument *IncenseParseString(const char *source)
{
    return incense_parse_with_embeds(source, NULL);
}

IncenseDocument *IncenseParseFile(const char *path)
{
    if (!path)
        return NULL;
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        fprintf(stderr, "incense: cannot open '%s'\n", path);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);
    char *buf = malloc((size_t)size + 1);
    if (!buf)
    {
        fclose(fp);
        return NULL;
    }
    fread(buf, 1, (size_t)size, fp);
    buf[size] = '\0';
    fclose(fp);

    char *base_path = incense_strdup(path);
    IncenseDocument *doc = incense_parse_with_embeds(buf, base_path);
    free(buf);
    if (doc)
    {
        free(doc->base_path);
        doc->base_path = base_path;
    }
    else
    {
        free(base_path);
    }
    return doc;
}

static void incense_print_node(const IncenseNode *node, int depth)
{
    for (int i = 0; i < depth; i++)
        printf("  ");
    if (node->type == INCENSE_OBJECT)
    {
        printf("%s [%d:%d] {\n", node->name, node->line, node->column);
    }
    else
    {
        if (node->is_embed)
        {
            printf("@embed \"%s\" [%d:%d]\n", node->value ? node->value : "", node->line, node->column);
        }
        else
        {
            printf("%s [%d:%d]: %s\n", node->name, node->line, node->column, node->value ? node->value : "");
        }
    }
    const IncenseNode *child = node->first_child;
    while (child)
    {
        incense_print_node(child, depth + 1);
        child = child->next_sibling;
    }
    if (node->type == INCENSE_OBJECT)
    {
        for (int i = 0; i < depth; i++)
            printf("  ");
        printf("}\n");
    }
}

void IncensePrintTree(const IncenseDocument *doc)
{
    if (!doc || !doc->root)
        return;
    incense_print_node(doc->root, 0);
}

void IncenseDestroy(IncenseDocument *doc)
{
    if (!doc)
        return;
    incense_node_destroy(doc->root);
    free(doc->base_path);
    free(doc);
}