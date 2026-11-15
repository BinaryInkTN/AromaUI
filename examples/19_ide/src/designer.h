#ifndef IDE_DESIGNER_H
#define IDE_DESIGNER_H

#include <stdbool.h>
#include <stddef.h>

#define IDE_DESIGN_MAX_NODES 160
#define IDE_DESIGN_MAX_ATTRS 20
#define IDE_DESIGN_KEY_MAX 32
#define IDE_DESIGN_VALUE_MAX 192
#define IDE_DESIGN_TAG_MAX 32

typedef struct IdeDesignAttr
{
    char key[IDE_DESIGN_KEY_MAX];
    char value[IDE_DESIGN_VALUE_MAX];
    bool quoted;
} IdeDesignAttr;

typedef struct IdeDesignNode
{
    char tag[IDE_DESIGN_TAG_MAX];
    IdeDesignAttr attrs[IDE_DESIGN_MAX_ATTRS];
    int attr_count;
    int parent;
    int depth;
    int x;
    int y;
    int width;
    int height;
    int abs_x;
    int abs_y;
    int abs_width;
    int abs_height;
} IdeDesignNode;

typedef struct IdeDesignDocument
{
    bool valid;
    char error[192];
    int node_count;
    int root;
    int selected;
    int page_width;
    int page_height;
    char page_title[128];
    IdeDesignNode nodes[IDE_DESIGN_MAX_NODES];
} IdeDesignDocument;

void ide_design_init(IdeDesignDocument *doc);
bool ide_design_parse(IdeDesignDocument *doc, const char *source);
bool ide_design_serialize(const IdeDesignDocument *doc, char *out, size_t cap);
                                                                     
                                                                          
                                                               
bool ide_design_serialize_scaled(const IdeDesignDocument *doc, int scale256,
                                 char *out, size_t cap);
void ide_design_layout(IdeDesignDocument *doc);

const char *ide_design_attr(const IdeDesignDocument *doc, int index,
                            const char *key);
bool ide_design_set_attr(IdeDesignDocument *doc, int index, const char *key,
                         const char *value, bool quoted);
int ide_design_add(IdeDesignDocument *doc, const char *tag, int page_x,
                   int page_y);
int ide_design_duplicate(IdeDesignDocument *doc, int index);
bool ide_design_remove(IdeDesignDocument *doc, int index);
bool ide_design_set_position(IdeDesignDocument *doc, int index, int page_x,
                             int page_y);
bool ide_design_set_position_fast(IdeDesignDocument *doc, int index, int page_x,
                                  int page_y);
int ide_design_hit_test(const IdeDesignDocument *doc, int page_x, int page_y);
int ide_design_container_at(const IdeDesignDocument *doc, int page_x,
                            int page_y);
bool ide_design_serialize_node(const IdeDesignDocument *doc, int index,
                               char *out, size_t cap);
int ide_design_paste(IdeDesignDocument *doc, const char *source);

#endif