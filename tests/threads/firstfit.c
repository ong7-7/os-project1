#include "tests/threads/tests.h"
#include "threads/palloc.h"
#include <stdio.h>

void
test_firstfit (void) 
{
  void *a, *b, *c;
    
    palloc_set_mode (PAL_FIRST_FIT);
    
    a = palloc_get_multiple (PAL_USER, 4);
    if (a != NULL) {
        size_t index = palloc_get_page_index (a);
        msg ("Allocated A at index %zu", index);
    }
    
    b = palloc_get_multiple (PAL_USER, 4);
    if (b != NULL) {
        size_t index = palloc_get_page_index (b);
        msg ("Allocated B at index %zu", index);
    }
    
    if (a != NULL) {
        palloc_free_multiple (a, 4);
        msg ("Freed A");
    }
    
    c = palloc_get_multiple (PAL_USER, 4);
    if (c != NULL) {
        size_t index = palloc_get_page_index (c);
        msg ("Allocated C at index %zu", index);
    }
    
    if (b != NULL)
        palloc_free_multiple (b, 4);
    if (c != NULL)
        palloc_free_multiple (c, 4);

}
