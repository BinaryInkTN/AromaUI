#include "test_aroma_slab_alloc.h"
#include "test_aroma_node.h"
#include "test_aroma_event_system.h"
#include <stdio.h>

int main(void) {
    
    printf("=== Aroma Multi-Cache Test Suite ===\n\n");
    
    int slab_passed, slab_failed;
    int node_passed, node_failed;
    int event_passed, event_failed;
    
    run_slab_allocator_tests(&slab_passed, &slab_failed);
    
    run_node_tests(&node_passed, &node_failed);
    
    run_event_tests(&event_passed, &event_failed);
    
    int total_passed = slab_passed + node_passed + event_passed;
    int total_failed = slab_failed + node_failed + event_failed;
    
    printf("\n=== Summary ===\n");
    printf("Slab Allocator: %d passed, %d failed\n", slab_passed, slab_failed);
    printf("Node System:    %d passed, %d failed\n", node_passed, node_failed);
    printf("Event System:   %d passed, %d failed\n", event_passed, event_failed);
    printf("Total:          %d passed, %d failed\n", total_passed, total_failed);
    
    if (total_failed == 0) {
        printf("\nAll tests passed!\n");
    } else {
        printf("\nSome tests failed!\n");
    }
    
    return total_failed > 0 ? 1 : 0;
}