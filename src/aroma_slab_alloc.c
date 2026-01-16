#include "aroma_slab_alloc.h"
#include "aroma_logger.h"
#include <stdlib.h>
#include <string.h>

static const size_t WIDGET_BUCKET_SIZES[AROMA_WIDGET_BUCKET_COUNT] = {32, 64, 128, 256, 512};
AromaMemorySystem global_memory_system = {0};

void __slab_pool_init(AromaSlabAllocator* pool, size_t object_size) {
    if (!pool) {
        LOG_CRITICAL("Slab pool pointer is NULL.");
        return;
    }

    memset(pool, 0, sizeof(AromaSlabAllocator));
    pool->object_size = object_size;
    pool->free_list = NULL;
    pool->pages = NULL;

    LOG_INFO("Initialized slab pool with object size: %zu", object_size);
}

void __slab_pool_destroy(AromaSlabAllocator* pool) {
    if (!pool) return;

    AromaSlabAllocatorPage* page = pool->pages;
    while (page) {
        AromaSlabAllocatorPage* next = page->next_page;
        free(page);
        page = next;
    }

    memset(pool, 0, sizeof(AromaSlabAllocator));
    LOG_INFO("Destroyed slab pool");
}

void* __slab_pool_alloc(AromaSlabAllocator* pool) {
    if (!pool) {
        LOG_ERROR("Slab pool is NULL.");
        return NULL;
    }

    if (!pool->free_list) {
        AromaSlabAllocatorPage* new_page = malloc(sizeof(AromaSlabAllocatorPage));
        if (!new_page) {
            LOG_CRITICAL("Failed to allocate new slab page!");
            return NULL;
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

        LOG_INFO("Allocated new page for pool (object size: %zu), total pages: %zu", 
                  pool->object_size, pool->total_pages);
    }

    void* object = pool->free_list;
    pool->free_list = pool->free_list->next;
    pool->total_allocated++;

    LOG_INFO("Allocated object from pool (size: %zu), total allocated: %zu", 
              pool->object_size, pool->total_allocated);

    return object;
}

void __slab_pool_free(AromaSlabAllocator* pool, void* object) {
    if (!pool || !object) {
        LOG_WARNING("Invalid parameters to slab_pool_free.");
        return;
    }

    AromaFreeSlot* slot = (AromaFreeSlot*)object;
    slot->next = pool->free_list;
    pool->free_list = slot;
    pool->total_freed++;

    LOG_INFO("Freed object to pool (size: %zu), total freed: %zu", 
              pool->object_size, pool->total_freed);
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
    if (size == 0) {
        LOG_WARNING("Attempted to allocate widget of size 0");
        return NULL;
    }

    uint8_t bucket_index = __find_bucket_index(size);
    size_t bucket_size = WIDGET_BUCKET_SIZES[bucket_index];

    void* allocation = __slab_pool_alloc(&global_memory_system.widget_pools[bucket_index]);
    if (!allocation) {
        LOG_CRITICAL("Failed to allocate widget (size: %zu, bucket: %u)", size, bucket_index);
        return NULL;
    }

    uint8_t* tagged_ptr = (uint8_t*)allocation;
    *tagged_ptr = bucket_index;

    void* widget_ptr = tagged_ptr + 1;

    LOG_INFO("Allocated widget: size=%zu, bucket=%u(%zu), ptr=%p", 
              size, bucket_index, bucket_size, widget_ptr);

    return widget_ptr;
}

void aroma_widget_free(void* widget) {
    if (!widget) {
        LOG_WARNING("Attempted to free NULL widget");
        return;
    }

    uint8_t* tagged_ptr = (uint8_t*)widget - 1;
    uint8_t bucket_index = *tagged_ptr;

    if (bucket_index >= AROMA_WIDGET_BUCKET_COUNT) {
        LOG_ERROR("Invalid bucket index %u found in widget header", bucket_index);
        return;
    }

    LOG_INFO("Freeing widget: bucket=%u, ptr=%p", bucket_index, widget);

    __slab_pool_free(&global_memory_system.widget_pools[bucket_index], tagged_ptr);
}

void aroma_memory_system_init(void) {

    __slab_pool_init(&global_memory_system.node_pool, sizeof(AromaNode));

    for (int i = 0; i < AROMA_WIDGET_BUCKET_COUNT; i++) {
        __slab_pool_init(&global_memory_system.widget_pools[i], WIDGET_BUCKET_SIZES[i] + 1); 

    }

    LOG_INFO("Memory system initialized with %d widget buckets", AROMA_WIDGET_BUCKET_COUNT);
}

void aroma_memory_system_destroy(void) {

    for (int i = 0; i < AROMA_WIDGET_BUCKET_COUNT; i++) {
        __slab_pool_destroy(&global_memory_system.widget_pools[i]);
    }

    __slab_pool_destroy(&global_memory_system.node_pool);

    LOG_INFO("Memory system destroyed");
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