#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/palloc.h"

void test_buddy (void) 
{
    void *a, *b;
    
    /* Buddy System 모드로 전환 */
    palloc_set_mode (PAL_BUDDY);
    
    /* 4 페이지 할당 */
    a = palloc_get_multiple (PAL_USER, 4);
    if (a != NULL) {
        size_t index = palloc_get_page_index (a);
        msg ("Allocated A at index %zu", index);
    }
    
    /* 4 페이지 할당 */
    b = palloc_get_multiple (PAL_USER, 4);
    if (b != NULL) {
        size_t index = palloc_get_page_index (b);
        msg ("Allocated B at index %zu", index);
    }
    
    /* 해제 */
    if (a != NULL)
        palloc_free_multiple (a, 4);
    if (b != NULL)
        palloc_free_multiple (b, 4);
    
}
