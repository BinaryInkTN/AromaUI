#define _POSIX_C_SOURCE 200809L

#include "designer.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

                                                                         
                                                                           
                                                                            

enum
{
    WF_CONTAINER = 1,                                                     
    WF_FILL_W = 2,                                                        
    WF_FILL_H = 4,                                                        
    WF_DATA = 8,                                                          
    WF_PASS = 16                                                          
};

typedef struct WidgetInfo
{
    const char *tag;
    short w, h;
    unsigned flags;
                                                                          
    const char *attrs;
                                                                            
    const char *kids;
} WidgetInfo;

static const WidgetInfo WIDGETS[] = {
                             
    {"Window", 390, 844, WF_CONTAINER, "", ""},
    {"Container", 200, 160, WF_CONTAINER | WF_FILL_W, "layout=flex;direction=column", ""},
    {"Column", 200, 160, WF_CONTAINER | WF_FILL_W, "layout=flex;direction=column", ""},
    {"Row", 200, 160, WF_CONTAINER | WF_FILL_W, "layout=flex;direction=row", ""},
    {"ScrollView", 320, 200, WF_CONTAINER | WF_FILL_W, "direction=vertical", ""},
    {"Card", 220, 120, WF_CONTAINER, "type=filled;corner_radius=12;color=#2F2924", ""},
    {"Canvas", 240, 160, WF_CONTAINER | WF_FILL_W | WF_FILL_H, "", ""},
    {"Carousel", 320, 180, WF_CONTAINER, "", "Page:|Page:"},
    {"Page", 0, 0, WF_PASS | WF_CONTAINER, "", ""},
                      
    {"Label", 160, 24, 0, "text='Label;style=medium;color=#ECE0D9", ""},
    {"Icon", 32, 32, 0, "text='AROMA_ICON_STAR;size=24;color=#ECE0D9", ""},
    {"Image", 200, 120, 0, "src=\"\";scale=cover;corner_radius=0", ""},
    {"GIF", 200, 120, 0, "src=\"\";autoplay=true;corner_radius=0", ""},
    {"Video", 320, 180, 0, "src=\"\";autoplay=false;loop=false;muted=true;controls=true", ""},
    {"WebView", 320, 240, 0, "src=\"\"", ""},
               
    {"Button", 160, 44, 0, "text='Button;type=filled", ""},
    {"IconButton", 48, 48, 0, "icon='AROMA_ICON_FAVORITE;variant=standard;size=48;icon_size=24", ""},
    {"Textbox", 240, 44, 0, "placeholder='Type here...", ""},
    {"Switch", 56, 32, 0, "value=false", ""},
    {"Checkbox", 180, 32, 0, "label='Option;checked=false", ""},
    {"RadioButton", 180, 32, 0, "label='Option;group=1;selected=false", ""},
    {"Slider", 220, 32, 0, "min=0;max=100;value=50", ""},
    {"Stepper", 160, 40, 0, "min=0;max=10;value=0;step=1;wrap=false", ""},
    {"Dropdown", 200, 44, 0, "", "Option:text='Option 1|Option:text='Option 2|Option:text='Option 3"},
    {"Chip", 80, 32, 0, "label='Chip;type=filter;selected=false", ""},
    {"DatePicker", 280, 48, 0, "year=2026;month=1;day=1", ""},
    {"TimePicker", 280, 48, 0, "hour=12;minute=0", ""},
    {"Calendar", 300, 320, 0, "year=2026;month=1;day=1", ""},
                 
    {"ProgressBar", 220, 8, 0, "value=0.5;type=determinate", ""},
    {"Gauge", 140, 140, 0, "value=0.6;min=0;max=1", ""},
    {"Loading", 40, 40, 0, "radius=16;thickness=3;color=#007ACC", ""},
    {"Divider", 280, 2, WF_FILL_W, "orientation=horizontal", ""},
    {"Chart", 280, 140, 0, "data='0.3,0.6,0.9,0.7,0.5,0.8,1.0;y_min=0;y_max=1", ""},
    {"Tooltip", 120, 32, 0, "text='Tooltip text;position=top", ""},
    {"DebugOverlay", 200, 100, 0, "", ""},
                    
    {"Tabs", 320, 48, WF_FILL_W, "selected=0", "Tab:text='Tab 1|Tab:text='Tab 2|Tab:text='Tab 3"},
    {"SegmentedControl", 240, 36, 0, "selected=0", "Segment:text='One|Segment:text='Two"},
    {"Sidebar", 240, 400, WF_FILL_H, "", "Item:text='Home|Item:text='Settings"},
    {"Menu", 180, 120, 0, "visible=1", "MenuItem:text='Item 1|MenuItem:text='Item 2"},
              
    {"ListView", 280, 240, 0, "", "ListItem:text='Item 1|ListItem:text='Item 2|ListItem:text='Item 3"},
    {"Table", 320, 200, WF_FILL_W, "columns=3", "Column:header='Col 1|Column:header='Col 2|Column:header='Col 3"},
                  
    {"Snackbar", 320, 48, 0, "message='Message;duration=3000;show=1", ""},
    {"Dialog", 280, 200, 0, "title='Dialog Title;message='Dialog message;type=basic", ""},
                  
    {"Map", 320, 200, WF_FILL_W, "lat=48.8566;lon=2.3522;zoom=12;attribution=true",
     "Marker:lat=48.8566;lon=2.3522"},
    {"ThreeDViewer", 280, 220, 0, "theta=0.6;phi=0.8;radius=5;auto_rotate=true;interactive=true", ""},
                          
    {"Action", 0, 0, WF_DATA, "", ""},
    {"DialogAction", 0, 0, WF_DATA, "", ""},
    {"Header", 0, 0, WF_DATA, "", ""},
    {"HeaderCell", 0, 0, WF_DATA, "", ""},
    {"Item", 0, 0, WF_DATA, "", ""},
    {"ListItem", 0, 0, WF_DATA, "", ""},
    {"Marker", 0, 0, WF_DATA, "", ""},
    {"MenuItem", 0, 0, WF_DATA, "", ""},
    {"Option", 0, 0, WF_DATA, "", ""},
    {"Segment", 0, 0, WF_DATA, "", ""},
    {"Separator", 0, 0, WF_DATA, "", ""},
    {"Step", 0, 0, WF_DATA, "", ""},
    {"Tab", 0, 0, WF_DATA, "", ""},
                
    {"BottomNav", 320, 56, WF_FILL_W, "item_count=3", ""},
    {"AppBar", 320, 56, WF_FILL_W, "title='AppBar", ""},
};
#define WIDGET_COUNT ((int)(sizeof(WIDGETS) / sizeof(WIDGETS[0])))

static const WidgetInfo *info_find(const char *tag)
{
    static const char *const alias[][2] = {
        {"TextBox", "Textbox"}, {"Input", "Textbox"}, {"List", "ListView"},
        {"TabBar", "Tabs"}, {"Panel", "Card"}, {"Segmented", "SegmentedControl"}};
    if (!tag)
        return NULL;
    for (size_t i = 0; i < sizeof(alias) / sizeof(alias[0]); i++)
    {
        if (strcmp(tag, alias[i][0]) == 0)
        {
            tag = alias[i][1];
            break;
        }
    }
    for (int i = 0; i < WIDGET_COUNT; i++)
    {
        if (strcmp(WIDGETS[i].tag, tag) == 0)
            return &WIDGETS[i];
    }
    return NULL;
}

                                                                                 
static void design_frag_set_size(IdeDesignDocument *doc, int width, int height)
{
    if (width <= 0)
        width = 390;
    if (height <= 0)
        height = 844;
    doc->frag_width = width;
    doc->frag_height = height;
    snprintf(doc->frag_width_text, sizeof(doc->frag_width_text), "%d", width);
    snprintf(doc->frag_height_text, sizeof(doc->frag_height_text), "%d", height);
}

bool ide_design_is_fragment(const IdeDesignDocument *doc)
{
    return doc && doc->root >= 0 && doc->root < doc->node_count &&
           strcmp(doc->nodes[doc->root].tag, IDE_DESIGN_FRAGMENT_TAG) == 0;
}

bool ide_design_root_is_page(const IdeDesignDocument *doc)
{
    return doc && doc->root >= 0 && doc->root < doc->node_count &&
           (strcmp(doc->nodes[doc->root].tag, "Window") == 0 ||
            ide_design_is_fragment(doc));
}

static bool node_is_data(const IdeDesignDocument *doc, int index)
{
    const IdeDesignNode *node = &doc->nodes[index];
    const WidgetInfo *info = info_find(node->tag);
    if (strcmp(node->tag, "Column") == 0 && node->parent >= 0 &&
        node->parent < doc->node_count &&
        strcmp(doc->nodes[node->parent].tag, "Table") == 0)
        return true;
    return info && (info->flags & WF_DATA);
}

static bool node_is_pass(const IdeDesignDocument *doc, int index)
{
    const WidgetInfo *info = info_find(doc->nodes[index].tag);
    return info && (info->flags & WF_PASS);
}

                                                                   
static bool node_is_virtual(const IdeDesignDocument *doc, int index)
{
    return node_is_data(doc, index) || node_is_pass(doc, index);
}

static bool node_is_container(const IdeDesignDocument *doc, int index)
{
    if (index == doc->root)
        return true;
    if (node_is_virtual(doc, index))
        return false;
    const WidgetInfo *info = info_find(doc->nodes[index].tag);
    return info && (info->flags & WF_CONTAINER);
}

static void design_defaults(const char *tag, int parent_w, int parent_h,
                            int *width, int *height)
{
    const WidgetInfo *info = info_find(tag);
    if (!info)
    {
        *width = 120;
        *height = 40;
        return;
    }
    *width = info->w;
    *height = info->h;
    if ((info->flags & WF_FILL_W) && parent_w > 0)
        *width = parent_w;
    if ((info->flags & WF_FILL_H) && parent_h > 0)
        *height = parent_h;
}

                                                                         
                                                                                   
                                                                            

typedef struct DesignParser
{
    const char *source;
    size_t pos;
    int line;
    char error[192];
} DesignParser;

static void dp_set_error(DesignParser *p, const char *message)
{
    if (p->error[0] == '\0')
        snprintf(p->error, sizeof(p->error), "line %d: %s", p->line, message);
}

static void dp_ws(DesignParser *p)
{
    while (p->source[p->pos] == ' ' || p->source[p->pos] == '\t' ||
           p->source[p->pos] == '\r' || p->source[p->pos] == '\n')
    {
        if (p->source[p->pos] == '\n')
            p->line++;
        p->pos++;
    }
}

                                                                           
static void dp_skip(DesignParser *p)
{
    for (;;)
    {
        dp_ws(p);
        if (p->source[p->pos] == '/' && p->source[p->pos + 1] == '/')
        {
            while (p->source[p->pos] && p->source[p->pos] != '\n')
                p->pos++;
            continue;
        }
        if (p->source[p->pos] == '/' && p->source[p->pos + 1] == '*')
        {
            p->pos += 2;
            while (p->source[p->pos] &&
                   !(p->source[p->pos] == '*' && p->source[p->pos + 1] == '/'))
            {
                if (p->source[p->pos] == '\n')
                    p->line++;
                p->pos++;
            }
            if (p->source[p->pos])
                p->pos += 2;
            continue;
        }
        break;
    }
}

static bool dp_ident(DesignParser *p, char *out, size_t out_size)
{
    dp_skip(p);
    size_t start = p->pos;
    char c = p->source[p->pos];
    if (!(isalpha((unsigned char)c) || c == '_'))
    {
        dp_set_error(p, "expected an element or property name");
        return false;
    }
    p->pos++;
    while (isalnum((unsigned char)p->source[p->pos]) ||
           p->source[p->pos] == '_' || p->source[p->pos] == '-' ||
           p->source[p->pos] == '.')
        p->pos++;
    size_t length = p->pos - start;
    if (length >= out_size)
        length = out_size - 1;
    memcpy(out, p->source + start, length);
    out[length] = '\0';
    return true;
}

static bool dp_expect(DesignParser *p, char wanted)
{
    dp_skip(p);
    if (p->source[p->pos] != wanted)
    {
        char message[80];
        snprintf(message, sizeof(message), "expected '%c'", wanted);
        dp_set_error(p, message);
        return false;
    }
    p->pos++;
    return true;
}

static bool dp_string(DesignParser *p, char *out, size_t out_size, bool *quoted)
{
                                                                          
    dp_ws(p);
    size_t used = 0;
    if (p->source[p->pos] == '"')
    {
        *quoted = true;
        p->pos++;
        while (p->source[p->pos] && p->source[p->pos] != '"')
        {
            char c = p->source[p->pos++];
            if (c == '\\' && p->source[p->pos])
            {
                char escaped = p->source[p->pos++];
                switch (escaped)
                {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                default: c = escaped; break;
                }
            }
            else if (c == '\n')
                p->line++;
            if (used + 1 < out_size)
                out[used++] = c;
        }
        out[used] = '\0';
        if (p->source[p->pos] != '"')
        {
            dp_set_error(p, "unterminated string");
            return false;
        }
        p->pos++;
        return true;
    }

    *quoted = false;
    while (1)
    {
        char c = p->source[p->pos];
        if (!c || isspace((unsigned char)c) || c == '}' || c == '{' || c == ',')
            break;
        p->pos++;
        if (used + 1 < out_size)
            out[used++] = c;
    }
    out[used] = '\0';
    if (used == 0)
    {
        dp_set_error(p, "expected a property value");
        return false;
    }
    return true;
}

static int dp_int(const char *value, int fallback)
{
    if (!value || !value[0])
        return fallback;
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value)
        return fallback;
    return (int)parsed;
}

                                                                         
         
                                                                            

/* Flex flow.  Children of a flex container (Row, Column, or any container
 * with `layout: flex`) are placed one after another instead of at an
 * absolute x/y.  Only children that carry no explicit x/y take part, so
 * positions the designer wrote itself (drag/drop) stay where they are.
 * Without this every such child was stored at the container's top-left
 * corner, so clicks on the child the preview drew elsewhere hit the
 * container instead. */
typedef struct FlexFlow
{
    bool active;
    bool row;
    int gap;
    int cursor_x;
    int cursor_y;
} FlexFlow;

static void flex_begin(const IdeDesignDocument *doc, int index, FlexFlow *flow)
{
    memset(flow, 0, sizeof(*flow));
    if (node_is_virtual(doc, index))
        return;
    const char *tag = doc->nodes[index].tag;
    const char *layout = ide_design_attr(doc, index, "layout");
    bool is_row = strcmp(tag, "Row") == 0;
    bool is_col = strcmp(tag, "Column") == 0;
    bool flex = is_row || is_col || (layout && strcmp(layout, "flex") == 0);
    if (layout && strcmp(layout, "flex") != 0)
        flex = false;
    if (!flex)
        return;
    const char *dir = ide_design_attr(doc, index, "direction");
    flow->row = dir ? strcmp(dir, "row") == 0 : is_row;
    const char *gap = ide_design_attr(doc, index, "gap");
    if (!gap)
        gap = ide_design_attr(doc, index, "spacing");
    flow->gap = dp_int(gap, 0);
    int pad = dp_int(ide_design_attr(doc, index, "padding"), 0);
    if (flow->gap < 0)
        flow->gap = 0;
    if (pad < 0)
        pad = 0;
    flow->cursor_x = pad;
    flow->cursor_y = pad;
    flow->active = true;
}

static void layout_node(IdeDesignDocument *doc, int index, int parent_abs_x,
                        int parent_abs_y, const FlexFlow *place)
{
    if (index < 0 || index >= doc->node_count)
        return;
    IdeDesignNode *node = &doc->nodes[index];
    int parent_w = 0, parent_h = 0;
    if (node->parent >= 0 && node->parent < doc->node_count)
    {
        parent_w = doc->nodes[node->parent].abs_width;
        parent_h = doc->nodes[node->parent].abs_height;
    }

    if (node_is_data(doc, index))
    {
        node->x = node->y = node->width = node->height = 0;
        node->abs_x = parent_abs_x;
        node->abs_y = parent_abs_y;
        node->abs_width = node->abs_height = 0;
    }
    else if (node_is_pass(doc, index))
    {
        node->x = node->y = 0;
        node->width = parent_w;
        node->height = parent_h;
        node->abs_x = parent_abs_x;
        node->abs_y = parent_abs_y;
        node->abs_width = parent_w;
        node->abs_height = parent_h;
    }
    else if (index == doc->root && ide_design_is_fragment(doc))
    {
        node->x = node->y = 0;
        node->width = doc->frag_width > 0 ? doc->frag_width : 390;
        node->height = doc->frag_height > 0 ? doc->frag_height : 844;
        node->abs_x = node->abs_y = 0;
        node->abs_width = node->width;
        node->abs_height = node->height;
    }
    else
    {
        int default_w = 0, default_h = 0;
        design_defaults(node->tag, parent_w, parent_h, &default_w, &default_h);
        if (index == doc->root && !info_find(node->tag))
        {
            default_w = 390;
            default_h = 844;
        }
        node->x = dp_int(ide_design_attr(doc, index, "x"), 0);
        node->y = dp_int(ide_design_attr(doc, index, "y"), 0);
        if (place && place->active && !ide_design_attr(doc, index, "x") &&
            !ide_design_attr(doc, index, "y"))
        {
            node->x = place->cursor_x;
            node->y = place->cursor_y;
        }
        node->width = dp_int(ide_design_attr(doc, index, "width"), default_w);
        node->height = dp_int(ide_design_attr(doc, index, "height"), default_h);
        if (node->width <= 0)
            node->width = default_w;
        if (node->height <= 0)
            node->height = default_h;
        node->abs_x = index == doc->root ? 0 : parent_abs_x + node->x;
        node->abs_y = index == doc->root ? 0 : parent_abs_y + node->y;
        node->abs_width = node->width;
        node->abs_height = node->height;
    }

    FlexFlow flow;
    flex_begin(doc, index, &flow);
    for (int i = 0; i < doc->node_count; i++)
    {
        if (doc->nodes[i].parent != index || i == index)
            continue;
        layout_node(doc, i, node->abs_x, node->abs_y, &flow);
        const IdeDesignNode *kid = &doc->nodes[i];
        if (flow.active && !node_is_virtual(doc, i) &&
            !ide_design_attr(doc, i, "x") && !ide_design_attr(doc, i, "y"))
        {
            if (flow.row)
                flow.cursor_x += kid->width + flow.gap;
            else
                flow.cursor_y += kid->height + flow.gap;
        }
    }
}

void ide_design_layout(IdeDesignDocument *doc)
{
    if (!doc || !doc->valid || doc->node_count <= 0 || doc->root < 0 ||
        doc->root >= doc->node_count)
        return;
    if (ide_design_is_fragment(doc) && (!doc->frag_w_user || !doc->frag_h_user))
    {
        /* No Window and no size given: size the page to fit the content.
         * Start from the default size every time so the result is stable
         * (fill-width children would otherwise grow on each layout). */
        design_frag_set_size(doc, doc->frag_w_user ? doc->frag_width : 0,
                             doc->frag_h_user ? doc->frag_height : 0);
        layout_node(doc, doc->root, 0, 0, NULL);
        int right = 0, bottom = 0, count = 0;
        for (int i = 0; i < doc->node_count; i++)
        {
            if (i == doc->root || node_is_virtual(doc, i))
                continue;
            const IdeDesignNode *n = &doc->nodes[i];
            if (n->abs_x + n->abs_width > right)
                right = n->abs_x + n->abs_width;
            if (n->abs_y + n->abs_height > bottom)
                bottom = n->abs_y + n->abs_height;
            count++;
        }
        if (count > 0)
        {
            int w = doc->frag_w_user ? doc->frag_width : right + 16;
            int h = doc->frag_h_user ? doc->frag_height : bottom + 16;
            if (!doc->frag_w_user && w < 200)
                w = 200;
            if (!doc->frag_h_user && h < 120)
                h = 120;
            if (w > 4096)
                w = 4096;
            if (h > 4096)
                h = 4096;
            design_frag_set_size(doc, w, h);
        }
    }
    layout_node(doc, doc->root, 0, 0, NULL);
    doc->page_width = doc->nodes[doc->root].abs_width;
    doc->page_height = doc->nodes[doc->root].abs_height;
    if (doc->page_width <= 0)
        doc->page_width = 800;
    if (doc->page_height <= 0)
        doc->page_height = 600;
}

void ide_design_init(IdeDesignDocument *doc)
{
    if (!doc)
        return;
    memset(doc, 0, sizeof(*doc));
    doc->root = -1;
    doc->selected = -1;
    doc->page_width = 800;
    doc->page_height = 600;
    snprintf(doc->page_title, sizeof(doc->page_title), "Window");
    design_frag_set_size(doc, 390, 844);
}

                                                                         
             
                                                                            

const char *ide_design_attr(const IdeDesignDocument *doc, int index,
                            const char *key)
{
    if (!doc || index < 0 || index >= doc->node_count || !key)
        return NULL;
    if (index == doc->root && ide_design_is_fragment(doc))
    {
        if (strcmp(key, "width") == 0)
            return doc->frag_width_text;
        if (strcmp(key, "height") == 0)
            return doc->frag_height_text;
        if (strcmp(key, "x") == 0 || strcmp(key, "y") == 0)
            return NULL;
    }
    const IdeDesignNode *node = &doc->nodes[index];
    for (int i = 0; i < node->attr_count; i++)
    {
        if (strcmp(node->attrs[i].key, key) == 0)
            return node->attrs[i].value;
    }
    return NULL;
}

static int design_attr_index(const IdeDesignNode *node, const char *key)
{
    for (int i = 0; i < node->attr_count; i++)
    {
        if (strcmp(node->attrs[i].key, key) == 0)
            return i;
    }
    return -1;
}

                                                                             
static bool design_put_attr(IdeDesignNode *node, const char *key,
                            const char *value, bool quoted)
{
    int found = design_attr_index(node, key);
    if (found < 0)
    {
        if (node->attr_count >= IDE_DESIGN_MAX_ATTRS)
            return false;
        found = node->attr_count++;
        snprintf(node->attrs[found].key, sizeof(node->attrs[found].key), "%s", key);
        node->attrs[found].order = -1;
    }
    snprintf(node->attrs[found].value, sizeof(node->attrs[found].value), "%s",
             value ? value : "");
    node->attrs[found].quoted = quoted;
    return true;
}

static void design_put_int(IdeDesignNode *node, const char *key, int value)
{
    char number[32];
    snprintf(number, sizeof(number), "%d", value);
    design_put_attr(node, key, number, false);
}

bool ide_design_set_attr(IdeDesignDocument *doc, int index, const char *key,
                         const char *value, bool quoted)
{
    if (!doc || !doc->valid || index < 0 || index >= doc->node_count || !key ||
        !key[0])
        return false;
    if (index == doc->root && ide_design_is_fragment(doc) &&
        (strcmp(key, "x") == 0 || strcmp(key, "y") == 0 ||
         strcmp(key, "width") == 0 || strcmp(key, "height") == 0))
    {
        /* A fragment has no size of its own in the file; the page size is
         * only an editing aid, so it is kept out of the written markup. */
        bool set = value && dp_int(value, 0) > 0;
        if (strcmp(key, "width") == 0)
        {
            doc->frag_w_user = set; /* empty value = back to fit-to-content */
            design_frag_set_size(doc, dp_int(value, 0), doc->frag_height);
        }
        else if (strcmp(key, "height") == 0)
        {
            doc->frag_h_user = set;
            design_frag_set_size(doc, doc->frag_width, dp_int(value, 0));
        }
        ide_design_layout(doc);
        return true;
    }
                                                                           
                                            
    if ((!value || !value[0]) && !quoted && strcmp(key, "@embed") != 0)
    {
        IdeDesignNode *node = &doc->nodes[index];
        int at = design_attr_index(node, key);
        if (at >= 0)
        {
            for (int i = at; i + 1 < node->attr_count; i++)
                node->attrs[i] = node->attrs[i + 1];
            node->attr_count--;
        }
        if (strcmp(key, "x") == 0 || strcmp(key, "y") == 0 ||
            strcmp(key, "width") == 0 || strcmp(key, "height") == 0)
            ide_design_layout(doc);
        return true;
    }
    if (!design_put_attr(&doc->nodes[index], key, value, quoted))
        return false;
    if (strcmp(key, "x") == 0 || strcmp(key, "y") == 0 ||
        strcmp(key, "width") == 0 || strcmp(key, "height") == 0)
        ide_design_layout(doc);
    if (index == doc->root && strcmp(key, "title") == 0)
        snprintf(doc->page_title, sizeof(doc->page_title), "%s",
                 value && value[0] ? value : doc->nodes[index].tag);
    return true;
}

                                                                         
        
                                                                            

static int design_child_count(const IdeDesignDocument *doc, int index)
{
    int count = 0;
    for (int i = 0; i < doc->node_count; i++)
        if (doc->nodes[i].parent == index && i != index)
            count++;
    return count;
}

/* `@embed "path"` optionally followed by `{ key: value, ... }`.  The braces
 * are kept as raw text (the loader reads them itself) and are attached to the
 * directive as an "@embed_props" attribute.  `p->pos` is just after '@'. */
static bool parser_embed(DesignParser *p, IdeDesignDocument *doc, int index)
{
    IdeDesignNode *node = &doc->nodes[index];
    char word[IDE_DESIGN_KEY_MAX];
    if (!dp_ident(p, word, sizeof(word)) || strcmp(word, "embed") != 0)
    {
        dp_set_error(p, "unknown directive (only @embed is supported)");
        return false;
    }
    if (node->attr_count + 2 > IDE_DESIGN_MAX_ATTRS)
    {
        dp_set_error(p, "too many properties on one element");
        return false;
    }
    IdeDesignAttr *attr = &node->attrs[node->attr_count];
    memset(attr, 0, sizeof(*attr));
    snprintf(attr->key, sizeof(attr->key), "@embed");
    bool quoted = false;
    if (!dp_string(p, attr->value, sizeof(attr->value), &quoted))
        return false;
    attr->quoted = true;
    attr->order = design_child_count(doc, index);
    node->attr_count++;

    size_t save_pos = p->pos;
    int save_line = p->line;
    dp_ws(p);
    if (p->source[p->pos] != '{')
    {
        p->pos = save_pos;
        p->line = save_line;
        return true;
    }

    const char *src = p->source;
    size_t i = p->pos + 1;
    char raw[IDE_DESIGN_VALUE_MAX];
    size_t used = 0;
    bool pending_space = false;
    while (src[i] && src[i] != '}')
    {
        char c = src[i];
        if (c == '"')
        {
            if (pending_space && used > 0 && used + 1 < sizeof(raw))
                raw[used++] = ' ';
            pending_space = false;
            if (used + 1 < sizeof(raw))
                raw[used++] = '"';
            i++;
            while (src[i] && src[i] != '"')
            {
                char q = src[i];
                if (q == '\\' && src[i + 1])
                {
                    if (used + 1 < sizeof(raw))
                        raw[used++] = q;
                    i++;
                    q = src[i];
                }
                if (q == '\n')
                {
                    p->line++;
                    q = ' ';
                }
                if (used + 1 < sizeof(raw))
                    raw[used++] = q;
                i++;
            }
            if (src[i] != '"')
            {
                dp_set_error(p, "unterminated string in @embed properties");
                return false;
            }
            if (used + 1 < sizeof(raw))
                raw[used++] = '"';
            i++;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            if (c == '\n')
                p->line++;
            pending_space = true;
            i++;
            continue;
        }
        if (pending_space && used > 0 && used + 1 < sizeof(raw))
            raw[used++] = ' ';
        pending_space = false;
        if (used + 1 < sizeof(raw))
            raw[used++] = c;
        i++;
    }
    if (src[i] != '}')
    {
        dp_set_error(p, "unterminated @embed properties");
        return false;
    }
    raw[used] = '\0';
    p->pos = i + 1;
    if (used + 1 >= sizeof(raw))
    {
        dp_set_error(p, "@embed properties are too long");
        return false;
    }
    if (used > 0)
    {
        IdeDesignAttr *props = &node->attrs[node->attr_count++];
        memset(props, 0, sizeof(*props));
        snprintf(props->key, sizeof(props->key), "@embed_props");
        snprintf(props->value, sizeof(props->value), "%s", raw);
        props->order = -1;
    }
    return true;
}

/* A top-level `const name = "value"` line (used by `embed_name` references).
 * Kept verbatim so it survives a round trip through the designer. */
static bool parser_is_const_decl(const DesignParser *p)
{
    const char *s = p->source + p->pos;
    if (strncmp(s, "const", 5) != 0 || (s[5] != ' ' && s[5] != '\t'))
        return false;
    s += 5;
    while (*s == ' ' || *s == '\t')
        s++;
    return isalpha((unsigned char)*s) || *s == '_';
}

static bool parser_const_decl(DesignParser *p, IdeDesignNode *node)
{
    if (node->attr_count >= IDE_DESIGN_MAX_ATTRS)
    {
        dp_set_error(p, "too many top-level declarations");
        return false;
    }
    size_t start = p->pos;
    size_t end = start;
    while (p->source[end] && p->source[end] != '\n')
        end++;
    p->pos = end;
    while (end > start && (p->source[end - 1] == ' ' || p->source[end - 1] == '\t' ||
                           p->source[end - 1] == '\r'))
        end--;
    IdeDesignAttr *attr = &node->attrs[node->attr_count];
    memset(attr, 0, sizeof(*attr));
    snprintf(attr->key, sizeof(attr->key), "@pre");
    size_t length = end - start;
    if (length >= sizeof(attr->value))
    {
        dp_set_error(p, "declaration is too long");
        return false;
    }
    memcpy(attr->value, p->source + start, length);
    attr->value[length] = '\0';
    attr->order = -1;
    node->attr_count++;
    return true;
}

/* Parses properties, directives and child elements until the closing '}'
 * (or end of input when `top` is set, i.e. a file's top level). */
static bool parser_body(DesignParser *p, IdeDesignDocument *doc, int index,
                        bool top);

static bool parser_node(DesignParser *p, IdeDesignDocument *doc, int parent,
                        const char *tag)
{
    if (doc->node_count >= IDE_DESIGN_MAX_NODES)
    {
        dp_set_error(p, "too many elements");
        return false;
    }
    int index = doc->node_count++;
    IdeDesignNode *node = &doc->nodes[index];
    memset(node, 0, sizeof(*node));
    snprintf(node->tag, sizeof(node->tag), "%s", tag);
    node->parent = parent;
    node->depth = parent >= 0 ? doc->nodes[parent].depth + 1 : 0;

    if (!dp_expect(p, '{'))
        return false;
    return parser_body(p, doc, index, false);
}

static bool parser_body(DesignParser *p, IdeDesignDocument *doc, int index,
                        bool top)
{
    IdeDesignNode *node = &doc->nodes[index];
    while (1)
    {
        dp_skip(p);
        char c = p->source[p->pos];
        if (c == '}')
        {
            if (top)
            {
                dp_set_error(p, "unexpected '}'");
                return false;
            }
            p->pos++;
            return true;
        }
        if (!c)
        {
            if (top)
                return true;
            dp_set_error(p, "unterminated element");
            return false;
        }
        if (c == ',')
        {
            p->pos++;
            continue;
        }
        if (c == '@')
        {
            p->pos++;
            if (!parser_embed(p, doc, index))
                return false;
            continue;
        }
        if (top && parser_is_const_decl(p))
        {
            if (!parser_const_decl(p, node))
                return false;
            continue;
        }

        char name[IDE_DESIGN_KEY_MAX];
        if (!dp_ident(p, name, sizeof(name)))
            return false;

        dp_skip(p);
        if (p->source[p->pos] == ':')
        {
            p->pos++;
            if (node->attr_count >= IDE_DESIGN_MAX_ATTRS)
            {
                dp_set_error(p, "too many properties on one element");
                return false;
            }
            IdeDesignAttr *attr = &node->attrs[node->attr_count];
            memset(attr, 0, sizeof(*attr));
            snprintf(attr->key, sizeof(attr->key), "%s", name);
            attr->order = -1;
            if (!dp_string(p, attr->value, sizeof(attr->value), &attr->quoted))
                return false;
            node->attr_count++;
        }
        else if (p->source[p->pos] == '{')
        {
            if (!parser_node(p, doc, index, name))
                return false;
            /* parser_node may not move the array, but keep the pointer fresh */
            node = &doc->nodes[index];
        }
        else
        {
            dp_set_error(p, "expected ':' or '{' after element name");
            return false;
        }
    }
}

/* Removes the synthetic root of a file that is just one `Window { ... }`, so
 * ordinary screens keep their Window as the document root. */
static bool design_collapse_window(IdeDesignDocument *doc)
{
    IdeDesignNode *frag = &doc->nodes[0];
    int child = -1;
    for (int i = 1; i < doc->node_count; i++)
    {
        if (doc->nodes[i].parent == 0)
        {
            if (child >= 0)
                return true;
            child = i;
        }
    }
    if (child != 1 || strcmp(doc->nodes[1].tag, "Window") != 0)
        return true;
    int pre_count = 0;
    IdeDesignAttr pre[IDE_DESIGN_MAX_ATTRS];
    for (int i = 0; i < frag->attr_count; i++)
    {
        if (strcmp(frag->attrs[i].key, "@pre") != 0)
            return true; /* top-level props or embeds next to the Window */
        pre[pre_count++] = frag->attrs[i];
    }

    memmove(&doc->nodes[0], &doc->nodes[1],
            sizeof(IdeDesignNode) * (size_t)(doc->node_count - 1));
    doc->node_count--;
    for (int i = 0; i < doc->node_count; i++)
    {
        int parent = doc->nodes[i].parent;
        doc->nodes[i].parent = parent <= 0 ? -1 : parent - 1;
        doc->nodes[i].depth--;
    }
    IdeDesignNode *root = &doc->nodes[0];
    if (pre_count > 0)
    {
        if (root->attr_count + pre_count > IDE_DESIGN_MAX_ATTRS)
            return false;
        memmove(&root->attrs[pre_count], &root->attrs[0],
                sizeof(IdeDesignAttr) * (size_t)root->attr_count);
        memcpy(&root->attrs[0], pre, sizeof(IdeDesignAttr) * (size_t)pre_count);
        root->attr_count += pre_count;
    }
    return true;
}

bool ide_design_parse(IdeDesignDocument *doc, const char *source)
{
    if (!doc)
        return false;
    ide_design_init(doc);
    if (!source)
    {
        snprintf(doc->error, sizeof(doc->error), "The file is empty");
        return false;
    }

    DesignParser parser;
    memset(&parser, 0, sizeof(parser));
    parser.source = source;
    parser.line = 1;

    /* Index 0 is always the synthetic fragment root while parsing: a file may
     * hold one Window, or any number of nodes / @embed directives. */
    IdeDesignNode *frag = &doc->nodes[0];
    memset(frag, 0, sizeof(*frag));
    snprintf(frag->tag, sizeof(frag->tag), "%s", IDE_DESIGN_FRAGMENT_TAG);
    frag->parent = -1;
    doc->node_count = 1;

    if (!parser_body(&parser, doc, 0, true))
    {
        snprintf(doc->error, sizeof(doc->error), "%s", parser.error);
        doc->node_count = 0;
        return false;
    }

    if (!design_collapse_window(doc))
    {
        snprintf(doc->error, sizeof(doc->error), "too many top-level declarations");
        doc->node_count = 0;
        return false;
    }

    doc->valid = true;
    doc->root = 0;
    doc->selected = -1;
    ide_design_layout(doc);
    const char *title = ide_design_attr(doc, doc->root, "title");
    snprintf(doc->page_title, sizeof(doc->page_title), "%s",
             title && title[0] ? title
             : ide_design_is_fragment(doc) ? "Embedded file"
                                           : doc->nodes[doc->root].tag);
    return true;
}

                                                                         
              
                                                                            

static bool node_contains(const IdeDesignNode *node, int x, int y)
{
    return x >= node->abs_x && x < node->abs_x + node->abs_width &&
           y >= node->abs_y && y < node->abs_y + node->abs_height;
}

static bool is_descendant_or_self(const IdeDesignDocument *doc, int node, int ancestor)
{
    int guard = 0;
    while (node >= 0 && guard++ < IDE_DESIGN_MAX_NODES)
    {
        if (node == ancestor)
            return true;
        node = doc->nodes[node].parent;
    }
    return false;
}

                                                                        
int ide_design_hit_test(const IdeDesignDocument *doc, int page_x, int page_y)
{
    if (!doc || !doc->valid)
        return -1;
                                                                         
                                                                        
                                                    
    bool root_is_page = ide_design_root_is_page(doc);
    int best = -1;
    for (int i = 0; i < doc->node_count; i++)
    {
        if (root_is_page && i == doc->root)
            continue;
        if (node_is_virtual(doc, i))
            continue;
        if (!node_contains(&doc->nodes[i], page_x, page_y))
            continue;
        if (best < 0 || doc->nodes[i].depth >= doc->nodes[best].depth)
            best = i;
    }
    if (best >= 0)
        return best;
    if (!root_is_page && doc->root >= 0 && page_x >= 0 && page_y >= 0 &&
        page_x < doc->page_width && page_y < doc->page_height)
        return doc->root;
    return -1;
}

static int container_at_excluding(const IdeDesignDocument *doc, int page_x,
                                  int page_y, int exclude)
{
    int best = doc->root;
    for (int i = 0; i < doc->node_count; i++)
    {
        if (!node_is_container(doc, i) || !node_contains(&doc->nodes[i], page_x, page_y))
            continue;
        if (exclude >= 0 && is_descendant_or_self(doc, i, exclude))
            continue;
        if (best < 0 || doc->nodes[i].depth >= doc->nodes[best].depth)
            best = i;
    }
    return best;
}

int ide_design_container_at(const IdeDesignDocument *doc, int page_x, int page_y)
{
    if (!doc || !doc->valid)
        return -1;
    return container_at_excluding(doc, page_x, page_y, -1);
}

                                                                         
                 
                                                                            

static int design_new_node(IdeDesignDocument *doc, const char *tag, int parent)
{
    int index = doc->node_count++;
    IdeDesignNode *node = &doc->nodes[index];
    memset(node, 0, sizeof(*node));
    snprintf(node->tag, sizeof(node->tag), "%s", tag);
    node->parent = parent;
    node->depth = parent >= 0 ? doc->nodes[parent].depth + 1 : 0;
    return index;
}

                                                                              
static void design_apply_spec(IdeDesignNode *node, const char *spec, const char *end)
{
    const char *p = spec;
    while (p < end)
    {
        const char *stop = p;
        while (stop < end && *stop != ';')
            stop++;
        const char *eq = p;
        while (eq < stop && *eq != '=')
            eq++;
        if (eq < stop && eq > p)
        {
            char key[IDE_DESIGN_KEY_MAX];
            char value[IDE_DESIGN_VALUE_MAX];
            size_t klen = (size_t)(eq - p);
            if (klen >= sizeof(key))
                klen = sizeof(key) - 1;
            memcpy(key, p, klen);
            key[klen] = '\0';
            const char *v = eq + 1;
            bool quoted = false;
            if (v < stop && *v == '\'')
            {
                quoted = true;
                v++;
            }
            else if (v + 1 < stop && v[0] == '"' && v[1] == '"')
            {
                quoted = true;
                v = stop;
            }
            size_t vlen = (size_t)(stop - v);
            if (vlen >= sizeof(value))
                vlen = sizeof(value) - 1;
            memcpy(value, v, vlen);
            value[vlen] = '\0';
            design_put_attr(node, key, value, quoted);
        }
        p = stop + 1;
    }
}

static int design_count_kids(const char *kids)
{
    if (!kids || !kids[0])
        return 0;
    int n = 1;
    for (const char *c = kids; *c; c++)
        if (*c == '|')
            n++;
    return n;
}

static void design_make_kids(IdeDesignDocument *doc, int parent, const char *kids)
{
    const char *p = kids;
    while (p && *p)
    {
        const char *stop = strchr(p, '|');
        if (!stop)
            stop = p + strlen(p);
        const char *colon = p;
        while (colon < stop && *colon != ':')
            colon++;
        if (colon < stop || colon == stop)
        {
            char tag[IDE_DESIGN_TAG_MAX];
            size_t tlen = (size_t)(colon - p);
            if (tlen >= sizeof(tag))
                tlen = sizeof(tag) - 1;
            memcpy(tag, p, tlen);
            tag[tlen] = '\0';
            if (tag[0] && doc->node_count < IDE_DESIGN_MAX_NODES)
            {
                int kid = design_new_node(doc, tag, parent);
                if (colon < stop)
                    design_apply_spec(&doc->nodes[kid], colon + 1, stop);
            }
        }
        p = *stop ? stop + 1 : stop;
    }
}

int ide_design_add(IdeDesignDocument *doc, const char *tag, int page_x, int page_y)
{
    if (!doc || !doc->valid || !tag || !tag[0])
        return -1;
    const WidgetInfo *info = info_find(tag);
    int needed = 1 + (info ? design_count_kids(info->kids) : 0);
    if (doc->node_count + needed > IDE_DESIGN_MAX_NODES)
        return -1;
                                                    
    if (info && (info->flags & WF_DATA))
        return -1;

    int parent = ide_design_container_at(doc, page_x, page_y);
    if (parent < 0 || parent >= doc->node_count)
        parent = doc->root;
    if (parent < 0)
        return -1;

    int local_x = page_x - doc->nodes[parent].abs_x;
    int local_y = page_y - doc->nodes[parent].abs_y;
    int width = 0, height = 0;
    design_defaults(tag, doc->nodes[parent].abs_width, doc->nodes[parent].abs_height,
                    &width, &height);
    if (doc->nodes[parent].abs_width > 0 && width > doc->nodes[parent].abs_width)
        width = doc->nodes[parent].abs_width;
    if (doc->nodes[parent].abs_height > 0 && height > doc->nodes[parent].abs_height)
        height = doc->nodes[parent].abs_height;
    if (local_x + width > doc->nodes[parent].abs_width && doc->nodes[parent].abs_width > 0)
        local_x = doc->nodes[parent].abs_width - width;
    if (local_y + height > doc->nodes[parent].abs_height && doc->nodes[parent].abs_height > 0)
        local_y = doc->nodes[parent].abs_height - height;
    if (local_x < 0)
        local_x = 0;
    if (local_y < 0)
        local_y = 0;

    int index = design_new_node(doc, tag, parent);
    IdeDesignNode *node = &doc->nodes[index];
    design_put_int(node, "x", local_x);
    design_put_int(node, "y", local_y);
    design_put_int(node, "width", width);
    design_put_int(node, "height", height);
    if (info)
    {
        if (info->attrs[0])
            design_apply_spec(node, info->attrs, info->attrs + strlen(info->attrs));
        if (info->kids[0])
            design_make_kids(doc, index, info->kids);
    }

    doc->selected = index;
    ide_design_layout(doc);
    return index;
}

                                                                         
                    
                                                                            

static void design_shift_descendants(IdeDesignDocument *doc, int index, int dx, int dy)
{
    for (int i = 0; i < doc->node_count; i++)
    {
        if (doc->nodes[i].parent == index && i != index)
        {
            doc->nodes[i].abs_x += dx;
            doc->nodes[i].abs_y += dy;
            design_shift_descendants(doc, i, dx, dy);
        }
    }
}

static void design_set_depth(IdeDesignDocument *doc, int index)
{
    doc->nodes[index].depth = doc->nodes[index].parent >= 0
                                  ? doc->nodes[doc->nodes[index].parent].depth + 1
                                  : 0;
    for (int i = 0; i < doc->node_count; i++)
    {
        if (doc->nodes[i].parent == index && i != index)
            design_set_depth(doc, i);
    }
}

static void design_clamp_local(const IdeDesignDocument *doc, int index, int parent,
                               int *lx, int *ly)
{
    int pw = parent >= 0 ? doc->nodes[parent].abs_width : doc->page_width;
    int ph = parent >= 0 ? doc->nodes[parent].abs_height : doc->page_height;
    if (pw > 0 && *lx + doc->nodes[index].width > pw)
        *lx = pw - doc->nodes[index].width;
    if (ph > 0 && *ly + doc->nodes[index].height > ph)
        *ly = ph - doc->nodes[index].height;
    if (*lx < 0)
        *lx = 0;
    if (*ly < 0)
        *ly = 0;
}

/* An @embed remembers how many child nodes preceded it.  When a child leaves
 * or joins `parent`, directives located after it move by one slot. */
static void design_embed_shift(IdeDesignDocument *doc, int parent, int child,
                               int delta)
{
    if (parent < 0 || parent >= doc->node_count)
        return;
    int pos = 0;
    for (int i = 0; i < child && i < doc->node_count; i++)
        if (doc->nodes[i].parent == parent && i != parent)
            pos++;
    IdeDesignNode *node = &doc->nodes[parent];
    for (int i = 0; i < node->attr_count; i++)
    {
        if (strcmp(node->attrs[i].key, "@embed") == 0 &&
            node->attrs[i].order > pos)
            node->attrs[i].order += delta;
    }
}

static bool design_move(IdeDesignDocument *doc, int index, int page_x, int page_y,
                        bool allow_reparent, bool full_layout)
{
    if (!doc || !doc->valid || index <= 0 || index >= doc->node_count ||
        node_is_virtual(doc, index))
        return false;

    IdeDesignNode *node = &doc->nodes[index];
    int parent = node->parent;
    if (parent < 0 || parent >= doc->node_count || parent == index)
        return false;

    if (allow_reparent)
    {
                                                                       
        int target = container_at_excluding(doc, page_x + node->width / 2,
                                            page_y + node->height / 2, index);
        if (target >= 0 && target != parent && target != index &&
            !is_descendant_or_self(doc, target, index))
        {
            design_embed_shift(doc, parent, index, -1);
            node->parent = parent = target;
            design_embed_shift(doc, target, index, +1);
            design_set_depth(doc, index);
        }
    }

    int parent_x = parent >= 0 ? doc->nodes[parent].abs_x : 0;
    int parent_y = parent >= 0 ? doc->nodes[parent].abs_y : 0;
    int lx = page_x - parent_x;
    int ly = page_y - parent_y;
    design_clamp_local(doc, index, parent, &lx, &ly);

    design_put_int(node, "x", lx);
    design_put_int(node, "y", ly);

    if (full_layout)
    {
        ide_design_layout(doc);
    }
    else
    {
        int new_abs_x = parent_x + lx;
        int new_abs_y = parent_y + ly;
        int dx = new_abs_x - node->abs_x;
        int dy = new_abs_y - node->abs_y;
        node->x = lx;
        node->y = ly;
        node->abs_x = new_abs_x;
        node->abs_y = new_abs_y;
        design_shift_descendants(doc, index, dx, dy);
    }
    return true;
}

                                                             
bool ide_design_set_position(IdeDesignDocument *doc, int index, int page_x, int page_y)
{
    return design_move(doc, index, page_x, page_y, true, true);
}

                                                                         
bool ide_design_set_position_fast(IdeDesignDocument *doc, int index, int page_x,
                                  int page_y)
{
    return design_move(doc, index, page_x, page_y, false, false);
}

                                                                         
                             
                                                                            

static int design_subtree_size(const IdeDesignDocument *doc, int index)
{
    int total = 1;
    for (int i = 0; i < doc->node_count; i++)
    {
        if (doc->nodes[i].parent == index && i != index)
            total += design_subtree_size(doc, i);
    }
    return total;
}

                                                                               
static int design_copy_subtree(IdeDesignDocument *doc, int src, int new_parent,
                               int limit)
{
    int dst = doc->node_count++;
    doc->nodes[dst] = doc->nodes[src];
    doc->nodes[dst].parent = new_parent;
    doc->nodes[dst].depth = new_parent >= 0 ? doc->nodes[new_parent].depth + 1 : 0;
    for (int i = 0; i < limit; i++)
    {
        if (doc->nodes[i].parent == src && i != src)
            design_copy_subtree(doc, i, dst, limit);
    }
    return dst;
}

int ide_design_duplicate(IdeDesignDocument *doc, int index)
{
    if (!doc || !doc->valid || index < 0 || index >= doc->node_count ||
        index == doc->root || node_is_virtual(doc, index))
        return -1;
    if (doc->node_count + design_subtree_size(doc, index) > IDE_DESIGN_MAX_NODES)
        return -1;

    int ox = doc->nodes[index].x;
    int oy = doc->nodes[index].y;
    int target = design_copy_subtree(doc, index, doc->nodes[index].parent,
                                     doc->node_count);
    int parent = doc->nodes[target].parent;
    int lx = ox + 16, ly = oy + 16;
    design_clamp_local(doc, target, parent, &lx, &ly);
    design_put_int(&doc->nodes[target], "x", lx);
    design_put_int(&doc->nodes[target], "y", ly);
    doc->selected = target;
    ide_design_layout(doc);
    return target;
}

static void design_mark_removed(const IdeDesignDocument *doc, int index, bool *removed)
{
    removed[index] = true;
    for (int i = 0; i < doc->node_count; i++)
    {
        if (doc->nodes[i].parent == index && !removed[i])
            design_mark_removed(doc, i, removed);
    }
}

bool ide_design_remove(IdeDesignDocument *doc, int index)
{
    if (!doc || !doc->valid || index <= 0 || index >= doc->node_count)
        return false;

                                                                         
                                           
    bool removed[IDE_DESIGN_MAX_NODES];
    int map[IDE_DESIGN_MAX_NODES];
    memset(removed, 0, sizeof(removed));
    design_embed_shift(doc, doc->nodes[index].parent, index, -1);
    design_mark_removed(doc, index, removed);

    int new_count = 0;
    for (int i = 0; i < doc->node_count; i++)
        map[i] = removed[i] ? -1 : new_count++;

    IdeDesignNode *next = (IdeDesignNode *)malloc(sizeof(IdeDesignNode) * (size_t)new_count);
    if (!next)
        return false;
    int old_parent = doc->nodes[index].parent;
    for (int i = 0; i < doc->node_count; i++)
    {
        if (removed[i])
            continue;
        next[map[i]] = doc->nodes[i];
        if (next[map[i]].parent >= 0)
            next[map[i]].parent = map[next[map[i]].parent];
    }
    memcpy(doc->nodes, next, sizeof(IdeDesignNode) * (size_t)new_count);
    free(next);
    doc->node_count = new_count;
    doc->root = 0;
    doc->selected = (old_parent >= 0 && map[old_parent] >= 0) ? map[old_parent] : 0;
    ide_design_layout(doc);
    return true;
}

                                                                         
                
                                                                            

typedef struct DesignWriter
{
    char *out;
    size_t cap;
    size_t used;
    bool failed;
} DesignWriter;

static void dw_printf(DesignWriter *writer, const char *format, ...)
{
    if (writer->failed || writer->used >= writer->cap)
    {
        writer->failed = true;
        return;
    }
    va_list args;
    va_start(args, format);
    int written = vsnprintf(writer->out + writer->used, writer->cap - writer->used,
                            format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= writer->cap - writer->used)
    {
        writer->failed = true;
        return;
    }
    writer->used += (size_t)written;
}

static void dw_quoted(DesignWriter *writer, const char *value)
{
    dw_printf(writer, "\"");
    for (const char *p = value; *p; p++)
    {
        if (*p == '"' || *p == '\\')
            dw_printf(writer, "\\%c", *p);
        else if (*p == '\n')
            dw_printf(writer, "\\n");
        else if (*p == '\t')
            dw_printf(writer, "\\t");
        else if (*p == '\r')
            dw_printf(writer, "\\r");
        else
            dw_printf(writer, "%c", *p);
    }
    dw_printf(writer, "\"");
}

static int design_scale_int(int value, int scale256)
{
    int scaled = (value * scale256 + (value >= 0 ? 128 : -128)) / 256;
    return scaled;
}

                                                                            
                                                                           
                        
static bool design_scale_attr(const char *key, const char *value, int scale256,
                              char *out, size_t cap)
{
    static const char *const scale_keys[] = {
        "x", "y", "width", "height", "corner_radius", "radius", "size",
        "icon_size", "font_size", "thickness", "length", "gap", "padding",
        "spacing", "margin", "blur_radius", "stroke_width", "line_width",
        "track_height", "thumb_size", "handle_size", "dot_size", "bar_width",
        "bar_height", "item_height", "item_width", "offset_x", "offset_y",
        "animation_start_x", "animation_start_y", "animation_end_x",
        "animation_end_y", "start_x", "start_y", "end_x", "end_y",
        "shadow_radius", "shadow_offset_x", "shadow_offset_y", "border_width",
        "indicator_size", "thumb_radius"};
    bool scale = false;
    for (size_t i = 0; i < sizeof(scale_keys) / sizeof(scale_keys[0]); i++)
    {
        if (strcmp(key, scale_keys[i]) == 0)
        {
            scale = true;
            break;
        }
    }
    if (!scale)
        return false;

    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || !end || *end != '\0')
        return false;

    int scaled = design_scale_int((int)parsed, scale256);
    bool font_size = strcmp(key, "size") == 0 || strcmp(key, "icon_size") == 0 ||
                     strcmp(key, "font_size") == 0;
    bool zero_ok = strcmp(key, "corner_radius") == 0 ||
                   strcmp(key, "radius") == 0 ||
                   strcmp(key, "thickness") == 0 || strcmp(key, "gap") == 0 ||
                   strcmp(key, "padding") == 0 || strcmp(key, "spacing") == 0 ||
                   strcmp(key, "margin") == 0 || strcmp(key, "offset_x") == 0 ||
                   strcmp(key, "offset_y") == 0 ||
                   strcmp(key, "shadow_offset_x") == 0 ||
                   strcmp(key, "shadow_offset_y") == 0;
    if (font_size && scaled < 8)
        scaled = 8;
    else if (!zero_ok && parsed > 0 && scaled < 1)
        scaled = 1;
    snprintf(out, cap, "%d", scaled);
    return true;
}

static void dw_pre(const IdeDesignDocument *doc, DesignWriter *writer)
{
    if (doc->root < 0 || doc->root >= doc->node_count)
        return;
    const IdeDesignNode *root = &doc->nodes[doc->root];
    for (int i = 0; i < root->attr_count; i++)
    {
        if (strcmp(root->attrs[i].key, "@pre") == 0)
            dw_printf(writer, "%s\n", root->attrs[i].value);
    }
}

static void dw_embed(DesignWriter *writer, int indent, const IdeDesignNode *node,
                     int at)
{
    dw_printf(writer, "%*s@embed ", indent, "");
    dw_quoted(writer, node->attrs[at].value);
    if (at + 1 < node->attr_count &&
        strcmp(node->attrs[at + 1].key, "@embed_props") == 0 &&
        node->attrs[at + 1].value[0])
        dw_printf(writer, " { %s }", node->attrs[at + 1].value);
    dw_printf(writer, "\n");
}

static void design_write_node(const IdeDesignDocument *doc, int index, int depth,
                              int scale256, DesignWriter *writer);

/* Writes the properties, @embed directives and children of a node at `depth`
 * (without the `Tag {` / `}` lines), so it also serves a fragment's top level. */
static void design_write_body(const IdeDesignDocument *doc, int index, int depth,
                              int scale256, DesignWriter *writer)
{
    const IdeDesignNode *node = &doc->nodes[index];
    int indent = depth * 4;
    bool emitted[IDE_DESIGN_MAX_ATTRS];
    memset(emitted, 0, sizeof(emitted));

    for (int i = 0; i < node->attr_count; i++)
    {
        const IdeDesignAttr *attr = &node->attrs[i];
        if (attr->key[0] == '@')
        {
            /* directives before the first child stay among the properties */
            if (strcmp(attr->key, "@embed") == 0 && attr->order == 0)
            {
                dw_embed(writer, indent, node, i);
                emitted[i] = true;
            }
            continue;
        }
        char scaled_value[IDE_DESIGN_VALUE_MAX];
        const char *attr_value = attr->value;
        if (scale256 != 256 && !attr->quoted &&
            design_scale_attr(attr->key, attr->value, scale256, scaled_value,
                              sizeof(scaled_value)))
            attr_value = scaled_value;

        dw_printf(writer, "%*s%s: ", indent, "", attr->key);
        bool safe = !attr->quoted && attr_value[0] != '\0';
        for (const char *c = attr_value; *c && safe; c++)
        {
            if (isspace((unsigned char)*c) || *c == '{' || *c == '}' ||
                *c == ',' || *c == '"')
                safe = false;
        }
        if (safe)
            dw_printf(writer, "%s", attr_value);
        else
            dw_quoted(writer, attr_value);
        dw_printf(writer, "\n");
    }

    int kid = 0;
    for (int i = 0; i < doc->node_count; i++)
    {
        if (doc->nodes[i].parent != index || i == index)
            continue;
        for (int a = 0; a < node->attr_count; a++)
        {
            if (!emitted[a] && strcmp(node->attrs[a].key, "@embed") == 0 &&
                node->attrs[a].order >= 0 && node->attrs[a].order <= kid)
            {
                dw_embed(writer, indent, node, a);
                emitted[a] = true;
            }
        }
        design_write_node(doc, i, depth, scale256, writer);
        kid++;
    }
    for (int a = 0; a < node->attr_count; a++)
    {
        if (!emitted[a] && strcmp(node->attrs[a].key, "@embed") == 0)
        {
            dw_embed(writer, indent, node, a);
            emitted[a] = true;
        }
    }
}

static void design_write_node(const IdeDesignDocument *doc, int index, int depth,
                              int scale256, DesignWriter *writer)
{
    const IdeDesignNode *node = &doc->nodes[index];
    dw_printf(writer, "%*s%s {\n", depth * 4, "", node->tag);
    design_write_body(doc, index, depth + 1, scale256, writer);
    dw_printf(writer, "%*s}\n", depth * 4, "");
}

static bool design_serialize_from(const IdeDesignDocument *doc, int index,
                                  int scale256, char *out, size_t cap)
{
    out[0] = '\0';
    DesignWriter writer = {out, cap, 0, false};
    if (index == doc->root)
        dw_pre(doc, &writer);
    if (index == doc->root && ide_design_is_fragment(doc))
        design_write_body(doc, index, 0, scale256, &writer);
    else
        design_write_node(doc, index, 0, scale256, &writer);
    if (writer.failed)
    {
        out[0] = '\0';
        return false;
    }
    return true;
}

bool ide_design_serialize(const IdeDesignDocument *doc, char *out, size_t cap)
{
    if (!doc || !doc->valid || !out || cap == 0 || doc->root < 0)
        return false;
    return design_serialize_from(doc, doc->root, 256, out, cap);
}

bool ide_design_serialize_node(const IdeDesignDocument *doc, int index, char *out,
                               size_t cap)
{
    if (!doc || !doc->valid || !out || cap == 0 || index < 0 ||
        index >= doc->node_count)
        return false;
    return design_serialize_from(doc, index, 256, out, cap);
}

/* Markup for the live preview.  The preview loader needs a `Window` root, so a
 * fragment is wrapped in one (the wrapper is never written to the file). */
bool ide_design_serialize_scaled(const IdeDesignDocument *doc, int scale256,
                                 char *out, size_t cap)
{
    if (!doc || !doc->valid || !out || cap == 0 || doc->root < 0 ||
        scale256 <= 0)
        return false;

    out[0] = '\0';
    DesignWriter writer = {out, cap, 0, false};
    dw_pre(doc, &writer);
    if (strcmp(doc->nodes[doc->root].tag, "Window") == 0)
    {
        design_write_node(doc, doc->root, 0, scale256, &writer);
    }
    else
    {
        int page_w = design_scale_int(doc->page_width > 0 ? doc->page_width : 390,
                                      scale256);
        int page_h = design_scale_int(doc->page_height > 0 ? doc->page_height : 844,
                                      scale256);
        if (page_w < 1)
            page_w = 1;
        if (page_h < 1)
            page_h = 1;
        dw_printf(&writer, "Window {\n    width: %d\n    height: %d\n", page_w,
                  page_h);
        if (ide_design_is_fragment(doc))
            design_write_body(doc, doc->root, 1, scale256, &writer);
        else
            design_write_node(doc, doc->root, 1, scale256, &writer);
        dw_printf(&writer, "}\n");
    }
    if (writer.failed)
    {
        out[0] = '\0';
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------- paste */

static int design_import_subtree(IdeDesignDocument *doc, const IdeDesignDocument *src,
                                 int index, int new_parent)
{
    int dst = doc->node_count++;
    doc->nodes[dst] = src->nodes[index];
    doc->nodes[dst].parent = new_parent;
    doc->nodes[dst].depth = doc->nodes[new_parent].depth + 1;
    /* top-level declarations belong to the source file, not to a pasted node */
    IdeDesignNode *node = &doc->nodes[dst];
    int kept = 0;
    for (int i = 0; i < node->attr_count; i++)
    {
        if (strcmp(node->attrs[i].key, "@pre") != 0)
            node->attrs[kept++] = node->attrs[i];
    }
    node->attr_count = kept;
    for (int i = 0; i < src->node_count; i++)
    {
        if (src->nodes[i].parent == index && i != index)
            design_import_subtree(doc, src, i, dst);
    }
    return dst;
}

int ide_design_paste(IdeDesignDocument *doc, const char *source)
{
    if (!doc || !doc->valid || !source || !source[0])
        return -1;

    IdeDesignDocument *temp = (IdeDesignDocument *)malloc(sizeof(IdeDesignDocument));
    if (!temp)
        return -1;
    int result = -1;
    /* Copied markup parses as a fragment holding the copied element(s); a
     * whole Window is not something that can be pasted into a page. */
    if (ide_design_parse(temp, source) && temp->root >= 0 &&
        ide_design_is_fragment(temp) &&
        doc->node_count + temp->node_count - 1 <= IDE_DESIGN_MAX_NODES)
    {
        int parent = doc->selected;
        if (parent < 0 || parent >= doc->node_count)
            parent = doc->root;
        while (parent > 0 && !node_is_container(doc, parent))
            parent = doc->nodes[parent].parent;
        if (parent < 0)
            parent = doc->root;

        for (int i = 0; i < temp->node_count; i++)
        {
            if (temp->nodes[i].parent != temp->root || i == temp->root ||
                strcmp(temp->nodes[i].tag, "Window") == 0 ||
                node_is_data(temp, i))
                continue;
            int index = design_import_subtree(doc, temp, i, parent);
            int lx = temp->nodes[i].x + 20;
            int ly = temp->nodes[i].y + 20;
            ide_design_layout(doc);
            design_clamp_local(doc, index, parent, &lx, &ly);
            design_put_int(&doc->nodes[index], "x", lx);
            design_put_int(&doc->nodes[index], "y", ly);
            ide_design_layout(doc);
            doc->selected = index;
            result = index;
        }
    }
    free(temp);
    return result;
}