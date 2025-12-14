#include "threads/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* Page allocator.  Hands out memory in page-size (or
   page-multiple) chunks.  See malloc.h for an allocator that
   hands out smaller chunks.

   System memory is divided into two "pools" called the kernel
   and user pools.  The user pool is for user (virtual) memory
   pages, the kernel pool for everything else.  The idea here is
   that the kernel needs to have memory for its own operations
   even if user processes are swapping like mad.

   By default, half of system RAM is given to the kernel pool and
   half to the user pool.  That should be huge overkill for the
   kernel pool, but that's just fine for demonstration purposes. */

/* A memory pool. */

/* Two pools: one for kernel data, one for user pages. */
struct pool kernel_pool;
struct pool user_pool;

static enum palloc_mode current_palloc_mode = PAL_FIRST_FIT;

static void init_pool (struct pool *, void *base, size_t page_cnt,
                       const char *name);
static bool page_from_pool (const struct pool *, void *page);

static size_t find_first_fit (struct pool *pool, size_t page_cnt);
static size_t find_next_fit (struct pool *pool, size_t page_cnt);
static size_t find_best_fit (struct pool *pool, size_t page_cnt);

void palloc_set_mode (enum palloc_mode mode);

void palloc_set_mode (enum palloc_mode mode) {
    current_palloc_mode = mode;
}

/* Initializes the page allocator.  At most USER_PAGE_LIMIT
   pages are put into the user pool. */
void
palloc_init (size_t user_page_limit)
{
    /* Free memory starts at 1 MB and runs to the end of RAM. */
    uint8_t *free_start = ptov (1024 * 1024);
    uint8_t *free_end = ptov (init_ram_pages * PGSIZE);
    size_t free_pages = (free_end - free_start) / PGSIZE;
    size_t user_pages = free_pages / 2;
    size_t kernel_pages;
    if (user_pages > user_page_limit)
        user_pages = user_page_limit;
    kernel_pages = free_pages - user_pages;

    /* Give half of memory to kernel, half to user. */
    init_pool (&kernel_pool, free_start, kernel_pages, "kernel pool");
    init_pool (&user_pool, free_start + kernel_pages * PGSIZE,
               user_pages, "user pool");
}

static size_t
find_first_fit (struct pool *pool, size_t page_cnt)
{
    return bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
}

static size_t
find_next_fit (struct pool *pool, size_t page_cnt)
{
    size_t page_idx = BITMAP_ERROR;
    size_t bitmap_len = bitmap_size(pool->used_map);
    size_t start_idx = pool->next_fit_start_idx;

    page_idx = bitmap_scan (pool->used_map, start_idx, page_cnt, false);

    if (page_idx == BITMAP_ERROR) {
        page_idx = bitmap_scan (pool->used_map, 0, page_cnt, false);
    }
    
    if (page_idx != BITMAP_ERROR) {

        bitmap_set_multiple (pool->used_map, page_idx, page_cnt, true);
        
        size_t next_start = page_idx + page_cnt;
        if (next_start >= bitmap_len) {
            pool->next_fit_start_idx = 0;
        } else {
            pool->next_fit_start_idx = next_start;
        }
    }
    
    return page_idx;
}

static size_t
find_best_fit (struct pool *pool, size_t page_cnt)
{
    size_t best_idx = BITMAP_ERROR;
    size_t best_size = SIZE_MAX;
    size_t idx = 0;

    while (idx < bitmap_size(pool->used_map))
    {
        size_t free_start = bitmap_scan (pool->used_map, idx, 1, false);
        if (free_start == BITMAP_ERROR)
            break;
        
        /* 연속된 free page 개수 계산 */
        size_t free_cnt = 0;
        size_t current_idx = free_start;
        while (current_idx < bitmap_size(pool->used_map) &&
                !bitmap_test(pool->used_map, current_idx))
        {
            free_cnt++;
            current_idx++;
        }
        
        if (free_cnt >= page_cnt && free_cnt < best_size)
        {
            best_idx = free_start;
            best_size = free_cnt;
            
            if (free_cnt == page_cnt)
                break;
        }
        
        idx = free_start + free_cnt;
    }
    
    if (best_idx != BITMAP_ERROR)
        bitmap_set_multiple (pool->used_map, best_idx, page_cnt, true);
    
    return best_idx;
}

/* Obtains and returns a group of PAGE_CNT contiguous free pages.
   If PAL_USER is set, the pages are obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the pages are filled with zeros.  If too few pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt)
{
    struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
    void *pages;
    size_t page_idx;

    if (page_cnt == 0)
        return NULL;

    lock_acquire (&pool->lock);

   switch (current_palloc_mode)
    {
        case PAL_NEXT_FIT:
            page_idx = find_next_fit (pool, page_cnt);
            break;
        case PAL_BEST_FIT:
            page_idx = find_best_fit (pool, page_cnt);
            break;
        case PAL_BUDDY:
            /* TODO: Buddy System 구현 */
            page_idx = find_first_fit (pool, page_cnt);
            break;
        case PAL_FIRST_FIT:
        default:
            page_idx = find_first_fit (pool, page_cnt);
            break;
    }

    lock_release (&pool->lock);
   
    if (page_idx != BITMAP_ERROR)
        pages = pool->base + PGSIZE * page_idx;
    else
        pages = NULL;

    if (pages != NULL)
        {
            if (flags & PAL_ZERO)
                memset (pages, 0, PGSIZE * page_cnt);
        }
    else
        {
            if (flags & PAL_ASSERT)
                PANIC ("palloc_get: out of pages");
        }
    return pages;
}

/* Obtains a single free page and returns its kernel virtual
   address.
   If PAL_USER is set, the page is obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the page is filled with zeros.  If no pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_page (enum palloc_flags flags)
{
    return palloc_get_multiple (flags, 1);
}

/* Frees the PAGE_CNT pages starting at PAGES. */
void
palloc_free_multiple (void *pages, size_t page_cnt)
{
    struct pool *pool;
    size_t page_idx;

    ASSERT (pg_ofs (pages) == 0);
    if (pages == NULL || page_cnt == 0)
        return;

    if (page_from_pool (&kernel_pool, pages))
       pool = &kernel_pool;
    else if (page_from_pool (&user_pool, pages))
       pool = &user_pool;
    else
       NOT_REACHED ();

    page_idx = pg_no (pages) - pg_no (pool->base);

   #ifndef NDEBUG
      memset (pages, 0xcc, PGSIZE * page_cnt);
   #endif

    lock_acquire (&pool->lock);
    
    ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));
    bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
   
    lock_release (&pool->lock);
}

/* Frees the page at PAGE. */
void
palloc_free_page (void *page)
{
    palloc_free_multiple (page, 1);
}

void
buddy_system_free (struct pool *pool, void *pages)
{
    if (pages == NULL || pool == NULL)
        return;
        
    size_t page_idx = pg_no (pages) - pg_no (pool->base);
    
    lock_acquire (&pool->lock);
    
    size_t page_cnt = 1;
    while (page_idx + page_cnt < bitmap_size(pool->used_map) &&
           bitmap_test (pool->used_map, page_idx + page_cnt))
    {
        page_cnt++;
    }
    
    ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));
    bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
    
    lock_release (&pool->lock);
}

size_t
palloc_get_page_index (void *page)
{
    struct pool *pool;
    
    if (page_from_pool (&kernel_pool, page))
        pool = &kernel_pool;
    else if (page_from_pool (&user_pool, page))
        pool = &user_pool;
    else
        return 0;
    
    return pg_no (page) - pg_no (pool->base);
}

/* Initializes pool P as starting at START and ending at END,
   naming it NAME for debugging purposes. */
static void
init_pool (struct pool *p, void *base, size_t page_cnt, const char *name)
{
    /* We'll put the pool's used_map at its base.
       Calculate the space needed for the bitmap
       and subtract it from the pool's size. */
    size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (page_cnt), PGSIZE);
    if (bm_pages > page_cnt)
        PANIC ("Not enough memory in %s for bitmap.", name);
    page_cnt -= bm_pages;

    printf ("%zu pages available in %s.\n", page_cnt, name);

    /* Initialize the pool. */
    lock_init (&p->lock);
    p->used_map = bitmap_create_in_buf (page_cnt, base, bm_pages * PGSIZE);
    p->base = base + bm_pages * PGSIZE;

    p->next_fit_start_idx = 0;
}

/* Returns true if PAGE was allocated from POOL,
   false otherwise. */
static bool
page_from_pool (const struct pool *pool, void *page)
{
    size_t page_no = pg_no (page);
    size_t start_page = pg_no (pool->base);
    size_t end_page = start_page + bitmap_size (pool->used_map);

    return page_no >= start_page && page_no < end_page;
}

