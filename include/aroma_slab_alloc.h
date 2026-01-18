#ifndef AROMA_SLAB_ALLOC_H
#define AROMA_SLAB_ALLOC_H

#include <stdlib.h>
#include "aroma_node.h"

#define AROMA_GENERIC_PAGE_SIZE 2048
#define AROMA_MAX_PAGES 32
#define AROMA_NODE_POOL_INDEX 0xFF
#define AROMA_WIDGET_BUCKET_COUNT 7
#define AROMA_WIDGET_BUCKET_SIZES {32, 64, 128, 256, 512, 1024, 2048}


typedef struct AromaFreeSlot {
    struct AromaFreeSlot* next;
} AromaFreeSlot;

typedef struct AromaSlabAllocatorPage {
    uint8_t data[AROMA_GENERIC_PAGE_SIZE];
    struct AromaSlabAllocatorPage* next_page;
    uint8_t is_stack_page;
} AromaSlabAllocatorPage;

typedef struct AromaSlabAllocator {
    size_t object_size;
    AromaFreeSlot* free_list;
    AromaSlabAllocatorPage* pages;
    size_t total_pages;
    size_t total_allocated;
    size_t total_freed;
} AromaSlabAllocator;

typedef struct AromaMemorySystem {
    AromaSlabAllocator node_pool;
    AromaSlabAllocator widget_pools[AROMA_WIDGET_BUCKET_COUNT];
    AromaSlabAllocatorPage preallocated_pages[AROMA_MAX_PAGES];
    uint8_t page_used[AROMA_MAX_PAGES];
} AromaMemorySystem;

void __slab_pool_init(AromaSlabAllocator* pool, size_t object_size);
void __slab_pool_destroy(AromaSlabAllocator* pool);
void* __slab_pool_alloc(AromaSlabAllocator* pool);
void __slab_pool_free(AromaSlabAllocator* pool, void* object);

void* aroma_widget_alloc(size_t size);
void aroma_widget_free(void* widget);

void aroma_memory_system_init(void);
void aroma_memory_system_destroy(void);
void aroma_memory_system_stats(void);

extern AromaMemorySystem global_memory_system;

#endif