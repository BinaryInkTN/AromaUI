#include "aroma_slab_alloc.h"
#include "aroma_logger.h"
#include <stdlib.h>
#include <string.h>

static const size_t WIDGET_BUCKET_SIZES[AROMA_WIDGET_BUCKET_COUNT] = {32, 64, 128, 256, 512};
AromaMemorySystem global_memory_system = {0};

void __slab_pool_init(AromaSlabAllocator* pool, size_t object_size) {
    if (!pool) return;
    memset(pool, 0, sizeof(AromaSlabAllocator));
    pool->object_size = object_size;
}

void __slab_pool_destroy(AromaSlabAllocator* pool) {
    if (!pool) return;
    
    AromaSlabAllocatorPage* page = pool->pages;
    while (page) {
        AromaSlabAllocatorPage* next = page->next_page;
        if (!page->is_stack_page) {
            free(page);
        }
        page = next;
    }
    
    pool->free_list = NULL;
    pool->pages = NULL;
    memset(pool, 0, sizeof(AromaSlabAllocator));
}

void* __slab_pool_alloc(AromaSlabAllocator* pool) {
    if (!pool) return NULL;

    if (!pool->free_list) {
        AromaSlabAllocatorPage* new_page = NULL;
        
        for (int i = 0; i < AROMA_MAX_PAGES; i++) {
            if (!global_memory_system.page_used[i]) {
                new_page = &global_memory_system.preallocated_pages[i];
                global_memory_system.page_used[i] = 1;
                new_page->is_stack_page = 1;
                new_page->next_page = NULL;
                break;
            }
        }
        
        if (!new_page) {
            new_page = malloc(sizeof(AromaSlabAllocatorPage));
            if (!new_page) return NULL;
            new_page->is_stack_page = 0;
            new_page->next_page = NULL;
        }

        size_t objects_per_page = AROMA_GENERIC_PAGE_SIZE / pool->object_size;
        if (objects_per_page == 0) objects_per_page = 1;

        uint8_t* current = new_page->data;
        for (size_t i = 0; i < objects_per_page; i++) {
            AromaFreeSlot* slot = (AromaFreeSlot*)current;
            slot->next = pool->free_list;
            pool->free_list = slot;
            current += pool->object_size;
        }

        new_page->next_page = pool->pages;
        pool->pages = new_page;
        pool->total_pages++;
    }

    void* object = pool->free_list;
    pool->free_list = pool->free_list->next;
    pool->total_allocated++;
    return object;
}

void __slab_pool_free(AromaSlabAllocator* pool, void* object) {
    if (!pool || !object) return;
    AromaFreeSlot* slot = (AromaFreeSlot*)object;
    slot->next = pool->free_list;
    pool->free_list = slot;
    pool->total_freed++;
}

static uint8_t __find_bucket_index(size_t size) {
    for (uint8_t i = 0; i < AROMA_WIDGET_BUCKET_COUNT; i++) {
        if (size <= WIDGET_BUCKET_SIZES[i]) {
            return i;
        }
    }
    return AROMA_WIDGET_BUCKET_COUNT - 1;
}

void* aroma_widget_alloc(size_t size) {
    if (size == 0) return NULL;
    uint8_t bucket_index = __find_bucket_index(size);
    size_t bucket_size = WIDGET_BUCKET_SIZES[bucket_index];
    void* allocation = __slab_pool_alloc(&global_memory_system.widget_pools[bucket_index]);
    if (!allocation) return NULL;
    uint8_t* tagged_ptr = (uint8_t*)allocation;
    *tagged_ptr = bucket_index;
    void* widget_ptr = tagged_ptr + 1;
    return widget_ptr;
}

void aroma_widget_free(void* widget) {
    if (!widget) return;
    uint8_t* tagged_ptr = (uint8_t*)widget - 1;
    uint8_t bucket_index = *tagged_ptr;
    if (bucket_index >= AROMA_WIDGET_BUCKET_COUNT) return;
    __slab_pool_free(&global_memory_system.widget_pools[bucket_index], tagged_ptr);
}

void aroma_memory_system_init(void) {
    memset(&global_memory_system, 0, sizeof(AromaMemorySystem));
    __slab_pool_init(&global_memory_system.node_pool, sizeof(AromaNode));
    for (int i = 0; i < AROMA_WIDGET_BUCKET_COUNT; i++) {
        __slab_pool_init(&global_memory_system.widget_pools[i], WIDGET_BUCKET_SIZES[i] + 1);
    }
}

void aroma_memory_system_destroy(void) {
    for (int i = 0; i < AROMA_WIDGET_BUCKET_COUNT; i++) {
        __slab_pool_destroy(&global_memory_system.widget_pools[i]);
    }
    __slab_pool_destroy(&global_memory_system.node_pool);
    memset(&global_memory_system, 0, sizeof(AromaMemorySystem));
}

void aroma_memory_system_stats(void) {
    LOG_INFO("=== Memory System Statistics ===");
    AromaSlabAllocator* node_pool = &global_memory_system.node_pool;
    LOG_INFO("Node Pool:");
    LOG_INFO("  Object Size: %zu bytes", node_pool->object_size);
    LOG_INFO("  Total Pages: %zu", node_pool->total_pages);
    LOG_INFO("  Total Allocated: %zu", node_pool->total_allocated);
    LOG_INFO("  Total Freed: %zu", node_pool->total_freed);
    size_t available_nodes = 0;
    AromaFreeSlot* slot = node_pool->free_list;
    while (slot) {
        available_nodes++;
        slot = slot->next;
    }
    LOG_INFO("  Available Nodes: %zu", available_nodes);
    LOG_INFO("Widget Pools:");
    for (int i = 0; i < AROMA_WIDGET_BUCKET_COUNT; i++) {
        AromaSlabAllocator* pool = &global_memory_system.widget_pools[i];
        LOG_INFO("  Bucket %d (%zu bytes):", i, WIDGET_BUCKET_SIZES[i]);
        LOG_INFO("    Total Pages: %zu", pool->total_pages);
        LOG_INFO("    Total Allocated: %zu", pool->total_allocated);
        LOG_INFO("    Total Freed: %zu", pool->total_freed);
        size_t available_widgets = 0;
        slot = pool->free_list;
        while (slot) {
            available_widgets++;
            slot = slot->next;
        }
        LOG_INFO("    Available Widgets: %zu", available_widgets);
    }
    LOG_INFO("=== End Statistics ===");
}