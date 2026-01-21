/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "test_aroma_slab_alloc.h"
#include "test_aroma_node.h"
#include "test_aroma_event_system.h"
#include <stdio.h>

int main(void) {
    
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