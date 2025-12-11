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
struct pool
{
    struct lock lock;        /* Mutual exclusion. */
    struct bitmap *used_map; /* Bitmap of free pages. */
    uint8_t *base;           /* Base of pool. */

    size_t next_fit_start_idx; /* Next Fit의 검색 시작 지점 */

/* Buddy System을 위한 추가 필드 */
    struct list free_list[MAX_ORDER + 1]; /* 예를 들어, 0 ~ MAX_ORDER 크기의 자유 리스트 */
    size_t max_order; /* 최대 블록 크기의 order */
    size_t *block_order; /* 각 페이지의 order를 저장하는 배열 (선택) */
};

/* Two pools: one for kernel data, one for user pages. */
static struct pool kernel_pool, user_pool;

static enum palloc_mode current_palloc_mode = PAL_FIRST_FIT;
/* 기본값은 First Fit */

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

/* First Fit: 처음부터 검색하여 첫 번째로 찾은 적합한 공간 할당 */
static size_t
find_first_fit (struct pool *pool, size_t page_cnt)
{
    return bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
}

/* Next Fit: 이전 할당 위치부터 검색 시작 */
static size_t
find_next_fit (struct pool *pool, size_t page_cnt)
{
    size_t page_idx = bitmap_scan_and_flip (pool->used_map, 
                                             pool->next_fit_start_idx, 
                                             page_cnt, false);
    
    /* 끝까지 찾았는데 없으면 처음부터 다시 검색 */
    if (page_idx == BITMAP_ERROR && pool->next_fit_start_idx > 0)
        page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
    
    if (page_idx != BITMAP_ERROR)
        pool->next_fit_start_idx = page_idx + page_cnt;
    
    return page_idx;
}

/* Best Fit: 전체를 검색하여 가장 적합한 (가장 작은) 공간 할당 */
static size_t
find_best_fit (struct pool *pool, size_t page_cnt)
{
    size_t best_idx = BITMAP_ERROR;
    size_t best_size = SIZE_MAX;
    size_t idx = 0;
    
    /* 모든 가능한 위치를 검색 */
    while (idx < bitmap_size(pool->used_map))
    {
        size_t free_start = bitmap_scan (pool->used_map, idx, 1, false);
        if (free_start == BITMAP_ERROR)
            break;
        
        /* 연속된 free page 개수 계산 */
        size_t free_cnt = 0;
        while (free_start + free_cnt < bitmap_size(pool->used_map) &&
               !bitmap_test(pool->used_map, free_start + free_cnt))
            free_cnt++;
        
        /* 요구하는 크기 이상이고, 지금까지 찾은 것보다 작으면 갱신 */
        if (free_cnt >= page_cnt && free_cnt < best_size)
        {
            best_idx = free_start;
            best_size = free_cnt;
            
            /* 정확히 맞는 크기를 찾으면 즉시 반환 */
            if (free_cnt == page_cnt)
                break;
        }
        
        idx = free_start + free_cnt;
    }
    
    /* 찾았으면 할당 */
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

    lock_release (&pool->lock);
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

    if (current_palloc_mode == PAL_BUDDY) {
       palloc_free_multiple (pool, pages);
    } else {
       bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
    }
   lock_release (&pool->lock);
}

/* Frees the page at PAGE. */
void
palloc_free_page (void *page)
{
    palloc_free_multiple (page, 1);
}

static void
buddy_system_free_pages (struct pool *pool, void *pages, size_t page_cnt)
{
    size_t page_idx = pg_no (pages) - pg_no (pool->base);
    
    /* 현재는 기본 bitmap 방식으로 처리 (TODO: 실제 buddy system 구현) */
    bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
    
    /* TODO: Buddy system 로직 추가
       - 블록 병합 (coalescing)
       - free_list에 추가
       - buddy 찾기 및 병합
    */
}

void
buddy_system_free (struct pool *pool, void *pages)
{
    if (pages == NULL || pool == NULL)
        return;
        
    size_t page_idx = pg_no (pages) - pg_no (pool->base);
    
    lock_acquire (&pool->lock);
    
    /* 할당된 페이지 수 계산 */
    size_t page_cnt = 1;
    while (page_idx + page_cnt < bitmap_size(pool->used_map) &&
           bitmap_test (pool->used_map, page_idx + page_cnt))
    {
        page_cnt++;
    }
    
    /* 비트맵에서 해제 */
    bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
    
    lock_release (&pool->lock);
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
    p->page_cnt = page_cnt;
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

