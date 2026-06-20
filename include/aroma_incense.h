
#ifndef AROMA_INCENSE_H
#define AROMA_INCENSE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
    INCENSE_OBJECT   = 0,
    INCENSE_PROPERTY = 1
} IncenseNodeType;


typedef struct IncenseNode {
    IncenseNodeType    type;
      int               id; 
    int line;
    int column;
    char              *name;
    char              *value;
    struct IncenseNode *first_child;
    struct IncenseNode *next_sibling;
} IncenseNode;


typedef struct {
    IncenseNode *root;
} IncenseDocument;


IncenseDocument *IncenseParseString(const char *source);

IncenseDocument *IncenseParseFile(const char *path);

void IncensePrintTree(const IncenseDocument *doc);

void IncenseDestroy(IncenseDocument *doc);

#ifdef __cplusplus
}
#endif

#endif