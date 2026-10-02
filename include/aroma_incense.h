#ifndef AROMA_INCENSE_H
#define AROMA_INCENSE_H

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
typedef enum {
    INCENSE_OBJECT,
    INCENSE_PROPERTY
} IncenseNodeType;

typedef struct IncenseNode {
    IncenseNodeType type;
    char *name;
    char *value;
    int line;
    int column;
    int is_embed;
    struct IncenseNode *first_child;
    struct IncenseNode *next_sibling;
    uint64_t id;
} IncenseNode;

typedef struct {
    IncenseNode *root;
    char *base_path;
} IncenseDocument;

IncenseDocument *IncenseParseString(const char *source);
IncenseDocument *IncenseParseFile(const char *path);
void IncensePrintTree(const IncenseDocument *doc);
void IncenseDestroy(IncenseDocument *doc);

#ifdef __cplusplus
}
#endif

#endif