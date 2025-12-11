#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stddef.h>

#define MAX_ORDER 10
struct pool;

/* How to allocate pages. */
enum palloc_flags {
    PAL_ASSERT = 001, /* Panic on failure. */
    PAL_ZERO = 002,   /* Zero page contents. */
    PAL_USER = 004    /* User page. */
};

void palloc_init(size_t user_page_limit);
void *palloc_get_page(enum palloc_flags);
void *palloc_get_multiple(enum palloc_flags, size_t page_cnt);
void palloc_free_page(void *);
void palloc_free_multiple(void *, size_t page_cnt);
size_t palloc_get_page_index(void *page);
void buddy_system_free (struct pool *pool, void *pages);
size_t buddy_system_alloc (struct pool *pool, size_t page_cnt);




/* Contiguous allocation mode selector */
enum palloc_mode {
    PAL_FIRST_FIT,
    PAL_NEXT_FIT,
    PAL_BEST_FIT,
    PAL_BUDDY
};
void palloc_set_mode(enum palloc_mode mode);

#endif /* threads/palloc.h */
