/**
 * @file aroma_slab_alloc.h
 * @brief Slab-based memory allocator for UI nodes and widgets.
 *
 * Provides a custom memory allocator to efficiently manage creation and destruction
 * of UI nodes and widget data structures, reducing fragmentation and malloc overhead.
 */
#ifndef AROMA_SLAB_ALLOC_H
#define AROMA_SLAB_ALLOC_H

#include <stdlib.h>
#include "aroma_node.h"
#ifdef __cplusplus
extern "C" {
#endif

/** @brief Size of an individual memory page. */
#define AROMA_GENERIC_PAGE_SIZE 2048
/** @brief Maximum number of preallocated pages. */
#define AROMA_MAX_PAGES 32
/** @brief Index identifier for the main node pool. */
#define AROMA_NODE_POOL_INDEX 0xFF
/** @brief Number of size buckets for widget-specific allocations. */
#define AROMA_WIDGET_BUCKET_COUNT 7
/** @brief Sizes defining the widget allocation buckets (bytes). */
#define AROMA_WIDGET_BUCKET_SIZES {32, 64, 128, 256, 512, 1024, 2048}

/**
 * @brief Internal linked list for tracking free slots.
 */
typedef struct  AromaFreeSlot {
    struct AromaFreeSlot* next; /**< Pointer to next free slot. */
} AromaFreeSlot;

/**
 * @brief Represents a page of memory used by the allocator.
 */
typedef struct  AromaSlabAllocatorPage {
    uint8_t data[AROMA_GENERIC_PAGE_SIZE]; /**< Raw data block. */
    struct AromaSlabAllocatorPage* next_page; /**< Next page in the chain. */
    uint8_t is_stack_page; /**< True if page is from preallocated stack (no free needed). */
} AromaSlabAllocatorPage;

/**
 * @brief A slab allocator instance for a specific object size.
 */
typedef struct  AromaSlabAllocator {
    size_t object_size; /**< Size of objects in this pool. */
    AromaFreeSlot* free_list; /**< Head of free list. */
    AromaSlabAllocatorPage* pages; /**< Head of pages list. */
    size_t total_pages; /**< Total pages allocated. */
    size_t total_allocated; /**< Total objects allocated. */
    size_t total_freed; /**< Total objects freed. */
} AromaSlabAllocator;

/**
 * @brief Global memory system state.
 */
typedef struct  AromaMemorySystem {
    AromaSlabAllocator node_pool; /**< Pool for AromaNode objects. */
    AromaSlabAllocator widget_pools[AROMA_WIDGET_BUCKET_COUNT]; /**< Pools for widget user data. */
    AromaSlabAllocatorPage preallocated_pages[AROMA_MAX_PAGES]; /**< Static memory pages. */
    uint8_t page_used[AROMA_MAX_PAGES]; /**< Usage flags for static pages. */
} AromaMemorySystem;

/** @internal Initialize a slab pool. */
void __slab_pool_init(AromaSlabAllocator* pool, size_t object_size);
/** @internal Destroy a slab pool. */
void __slab_pool_destroy(AromaSlabAllocator* pool);
/** @internal Allocate an object from a pool. */
void* __slab_pool_alloc(AromaSlabAllocator* pool);
/** @internal Free an object back to a pool. */
void __slab_pool_free(AromaSlabAllocator* pool, void* object);

/**
 * @brief Allocate memory for a widget's data.
 * Picks the appropriate pool based on size.
 * @param size Requested size in bytes.
 * @return Pointer to allocated memory, or NULL if too large (falls back to malloc).
 */
void* aroma_widget_alloc(size_t size);

/**
 * @brief Free memory for a widget.
 * @param widget Pointer to widget data.
 */
void aroma_widget_free(void* widget);

/**
 * @brief Initialize the global memory system.
 * Must be called before creating any nodes/widgets.
 */
void aroma_memory_system_init(void);

/**
 * @brief Destroy the global memory system.
 */
void aroma_memory_system_destroy(void);

/**
 * @brief Print memory usage statistics to the log.
 */
void aroma_memory_system_stats(void);

/** @brief Global memory system instance. */
extern AromaMemorySystem global_memory_system;

#ifdef __cplusplus
}
#endif
#endif

