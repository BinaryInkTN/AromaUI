

#ifndef AROMA_SLAB_ALLOC_H
#define AROMA_SLAB_ALLOC_H

#include <stddef.h>
#include <stdlib.h>
#include "aroma_node.h"
#ifdef __cplusplus
extern "C" {
#endif


#define AROMA_GENERIC_PAGE_SIZE 2048

/* Every slot handed out by a pool must satisfy the alignment of any
 * object it can hold (AromaNode needs 8). The page base (static array
 * or malloc) only guarantees pointer/max alignment on some targets
 * (4 on wasm32), so align the storage and round object sizes up.
 * Without this, is_valid_node_ptr() rejects legitimately allocated
 * nodes depending on link order, blanking all rendering. */
#define AROMA_SLAB_ALIGN _Alignof(max_align_t)

#define AROMA_MAX_PAGES 32

#define AROMA_NODE_POOL_INDEX 0xFF

#define AROMA_WIDGET_BUCKET_COUNT 7

#define AROMA_WIDGET_BUCKET_SIZES {32, 64, 128, 256, 512, 1024, 2048}


typedef struct  AromaFreeSlot {
    struct AromaFreeSlot* next;
} AromaFreeSlot;


typedef struct  AromaSlabAllocatorPage {
    _Alignas(AROMA_SLAB_ALIGN) uint8_t data[AROMA_GENERIC_PAGE_SIZE];
    struct AromaSlabAllocatorPage* next_page;
    uint8_t is_stack_page;
} AromaSlabAllocatorPage;


typedef struct  AromaSlabAllocator {
    size_t object_size;
    AromaFreeSlot* free_list;
    AromaSlabAllocatorPage* pages;
    size_t total_pages;
    size_t total_allocated;
    size_t total_freed;
} AromaSlabAllocator;


typedef struct  AromaMemorySystem {
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

#ifdef __cplusplus
}
#endif
#endif

