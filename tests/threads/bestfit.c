#include "tests/threads/tests.h"
#include "threads/palloc.h"
#include <stdio.h>

void test_bestfit (void) 
{
   void *a, *b, *c, *d;
    
    palloc_set_mode (PAL_BEST_FIT);
    
    a = palloc_get_multiple (PAL_USER, 10);
    if (a != NULL) {
        size_t index = palloc_get_page_index (a);
        msg ("Allocated A (10 pages) at index %zu", index);
    }
    
    b = palloc_get_multiple (PAL_USER, 2);
    if (b != NULL) {
        size_t index = palloc_get_page_index (b);
        msg ("Allocated B (2 pages) at index %zu", index);
    }
    
    c = palloc_get_multiple (PAL_USER, 5);
    if (c != NULL) {
        size_t index = palloc_get_page_index (c);
        msg ("Allocated C (5 pages) at index %zu", index);
    }
    
    if (a != NULL) {
        palloc_free_multiple (a, 10);
        msg ("Freed A (10 pages)");
    }

    d = palloc_get_multiple (PAL_USER, 3);
    if (d != NULL) {
        size_t index = palloc_get_page_index (d);
        msg ("Allocated D (3 pages) at index %zu - Best Fit test", index);
    }
    
    if (b != NULL)
        palloc_free_multiple (b, 2);
    if (c != NULL)
        palloc_free_multiple (c, 5);
    if (d != NULL)
        palloc_free_multiple (d, 3);
}
