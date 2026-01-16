#ifndef TEST_AROMA_SLAB_ALLOC_H
#define TEST_AROMA_SLAB_ALLOC_H

#include "aroma_slab_alloc.h"
#include "aroma_node.h"

extern AromaSlabAllocator global_slab_allocator;

void run_slab_allocator_tests(int* passed, int* failed);

#endif