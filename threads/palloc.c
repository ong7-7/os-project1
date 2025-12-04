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
#include "lib/kernel/list.h"

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
struct pool {
    struct lock lock;         /* Mutual exclusion. */
    struct bitmap *used_map; /* Bitmap of free pages. */
    uint8_t *base;            /* Base of pool. */
    size_t page_cnt;          /* Total number of pages in the pool. */
};

/* Two pools: one for kernel data, one for user pages. */
static struct pool kernel_pool, user_pool;

static void init_pool(struct pool *, void *base, size_t page_cnt,
                      const char *name);
static bool page_from_pool(const struct pool *, void *page);

/* Current allocation mode. */
static enum palloc_mode current_palloc_mode = PAL_FIRST_FIT;

/* Next Fit: Separate hints for kernel and user pool. */
static size_t next_fit_hint_kernel = 0;
static size_t next_fit_hint_user = 0;

/* --- Function Prototypes for New Algorithms --- */
static size_t *get_hint_for_pool(struct pool *pool);
static size_t first_fit_scan(struct pool *pool, size_t page_cnt);
static size_t next_fit_scan(struct pool *pool, size_t page_cnt);
static size_t best_fit_scan(struct pool *pool, size_t page_cnt);
static size_t buddy_scan(struct pool *pool, size_t page_cnt);
static size_t scan_for_contiguous(struct pool *pool, size_t page_cnt);


/* Sets the allocation mode. */
void
palloc_set_mode (enum palloc_mode mode)
{
  current_palloc_mode = mode;
  /* Next Fit 힌트 초기화 */
  next_fit_hint_kernel = 0;
  next_fit_hint_user = 0;
}

/* Next Fit Helper: Get hint based on pool */
static size_t *get_hint_for_pool(struct pool *pool)
{
  return (pool == &kernel_pool ? &next_fit_hint_kernel : &next_fit_hint_user);
}

/* --- Allocation Scan Implementations --- */

/* First Fit: Finds the first block from the start (index 0). */
static size_t first_fit_scan(struct pool *pool, size_t page_cnt)
{
  /* Pintos 기본 함수 사용: page_cnt만큼 연속된 false (free) 비트를 찾습니다. */
  return bitmap_scan(pool->used_map, 0, page_cnt, false);
}

/* Next Fit: Searches from the hint (last allocated position) and wraps around. */
static size_t next_fit_scan(struct pool *pool, size_t page_cnt)
{
  struct bitmap *bm = pool->used_map;
  size_t n = bitmap_size(bm);
  size_t *hintp = get_hint_for_pool(pool);
  size_t start = *hintp;
  size_t i;
  size_t found_idx = BITMAP_ERROR;

  /* 1. Try from hint (start) to end (n) */
  for (i = start; i + page_cnt <= n; ++i) {
      /* 연속된 page_cnt만큼의 블록 전체가 FREE(false)인지 확인 */
      if (bitmap_contains(bm, i, page_cnt, false)) {
          found_idx = i;
          goto found;
      }
  }

  /* 2. Wrap around: Try from 0 up to hint (start) */
  for (i = 0; i + page_cnt <= start; ++i) {
      /* 연속된 page_cnt만큼의 블록 전체가 FREE(false)인지 확인 */
      if (bitmap_contains(bm, i, page_cnt, false)) {
          found_idx = i;
          goto found;
      }
  }
  
  return BITMAP_ERROR;

found:
  /* Update hint: hint points to the page immediately after the found block */
  *hintp = found_idx + page_cnt;  
  if (*hintp >= n) *hintp = 0; /* Handle wrap around */

  return found_idx;
}

/* Best Fit: Searches the entire memory for the smallest sufficient block. */
static size_t best_fit_scan(struct pool *pool, size_t page_cnt)
{
  struct bitmap *bm = pool->used_map;
  size_t n = bitmap_size(bm);
  size_t best_idx = BITMAP_ERROR;
  size_t best_size = SIZE_MAX;

  size_t i = 0;
  while (i < n) {
    /* Skip used bits */
    if (bitmap_test(bm, i)) {
      i++;
      continue;
    }
    
    /* 빈 공간(false)의 시작을 찾았습니다. 끝(j)을 찾습니다. */
    size_t j = i;
    while (j < n && !bitmap_test(bm, j)) j++;
    size_t run = j - i;
    
    if (run >= page_cnt && run < best_size) {
      best_size = run;
      best_idx = i;
      if (best_size == page_cnt) break; /* 정확한 크기가 최적 */
    }
    
    i = j; /* 다음 검색 시작 지점으로 이동 */
  }
  return best_idx;
}

/* Buddy System: Finds a block of size 2^k where 2^(k-1) < page_cnt <= 2^k. */
static size_t buddy_scan(struct pool *pool, size_t page_cnt)
{
  size_t target_size = 1;
  while (target_size < page_cnt) target_size <<= 1; /* 최소 2^k >= page_cnt 계산 */

  struct bitmap *bm = pool->used_map;
  size_t n = bitmap_size(bm);
  size_t i;
  
  /* Search only at target_size aligned addresses (i is a multiple of target_size) */
  for (i = 0; i + target_size <= n; i += target_size) {
    /* Check if the entire block is free (false) */
    if (bitmap_contains(bm, i, target_size, false)) {
      return i;
    }
  }
  
  return BITMAP_ERROR;
}

/* Internal allocation routine: locate free block according to current_mode.
   Returns page index (0-based) or BITMAP_ERROR. */
static size_t scan_for_contiguous(struct pool *pool, size_t page_cnt)
{
  switch (current_palloc_mode) {
    case PAL_FIRST_FIT:
      /* First fit is handled separately in palloc_get_multiple using bitmap_scan_and_flip */
      return first_fit_scan(pool, page_cnt); 
    case PAL_NEXT_FIT:
      return next_fit_scan(pool, page_cnt);
    case PAL_BEST_FIT:
      return best_fit_scan(pool, page_cnt);
    case PAL_BUDDY:
      return buddy_scan(pool, page_cnt);
    default:
      return first_fit_scan(pool, page_cnt);
  }
}


/* Initializes the page allocator.  At most USER_PAGE_LIMIT
   pages are put into the user pool. */
void palloc_init(size_t user_page_limit)
{
    /* Free memory starts at 1 MB and runs to the end of RAM. */
    uint8_t *free_start = ptov(1024 * 1024);
    uint8_t *free_end = ptov(init_ram_pages * PGSIZE);
    size_t free_pages = (free_end - free_start) / PGSIZE;
    size_t user_pages = free_pages / 2;
    size_t kernel_pages;
    if (user_pages > user_page_limit)
        user_pages = user_page_limit;
    kernel_pages = free_pages - user_pages;

    /* Give half of memory to kernel, half to user. */
    init_pool(&kernel_pool, free_start, kernel_pages, "kernel pool");
    init_pool(&user_pool, free_start + kernel_pages * PGSIZE,
              user_pages, "user pool");
}

/* Obtains and returns a group of PAGE_CNT contiguous free pages.
   If PAL_USER is set, the pages are obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the pages are filled with zeros.  If too few pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_multiple(enum palloc_flags flags, size_t page_cnt)
{
    struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
    void *pages = NULL;
    size_t page_idx = BITMAP_ERROR;

    if (page_cnt == 0)
        return NULL;

    lock_acquire(&pool->lock);

    /* If user pool OR First-Fit mode: use atomic bitmap_scan_and_flip
       This preserves original Pintos First-Fit behavior and simplifies the user pool. */
    if (pool == &user_pool || current_palloc_mode == PAL_FIRST_FIT) {
        page_idx = bitmap_scan_and_flip(pool->used_map, 0, page_cnt, false);
        if (page_idx != BITMAP_ERROR) {
            pages = pool->base + PGSIZE * page_idx;
        } else {
            pages = NULL;
        }
    } else {
        /* Other modes: use scan_for_contiguous (non-atomic scan),
           then mark bits with bitmap_set_multiple within the lock. */
        page_idx = scan_for_contiguous(pool, page_cnt);
        if (page_idx != BITMAP_ERROR) {
            /* Mark the bits as used. */
            bitmap_set_multiple(pool->used_map, page_idx, page_cnt, true);
            pages = pool->base + PGSIZE * page_idx;
        } else {
            pages = NULL;
        }
    }

    lock_release(&pool->lock);

    /* Handle PAL_ZERO and PAL_ASSERT outside the lock. */
    if (pages != NULL) {
        if (flags & PAL_ZERO)
            memset(pages, 0, PGSIZE * page_cnt);
    } else {
        if (flags & PAL_ASSERT)
            PANIC("palloc_get: out of pages");
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
palloc_get_page(enum palloc_flags flags)
{
    return palloc_get_multiple(flags, 1);
}

/* Frees the PAGE_CNT pages starting at PAGES. */
void palloc_free_multiple(void *pages, size_t page_cnt)
{
    struct pool *pool;
    size_t page_idx;

    ASSERT(pg_ofs(pages) == 0);
    if (pages == NULL || page_cnt == 0)
        return;

    if (page_from_pool(&kernel_pool, pages))
        pool = &kernel_pool;
    else if (page_from_pool(&user_pool, pages))
        pool = &user_pool;
    else
        NOT_REACHED();

    lock_acquire(&pool->lock);
        
    page_idx = pg_no(pages) - pg_no(pool->base);

#ifndef NDEBUG
    memset(pages, 0xcc, PGSIZE * page_cnt);
#endif

    ASSERT(bitmap_all(pool->used_map, page_idx, page_cnt));
    bitmap_set_multiple(pool->used_map, page_idx, page_cnt, false);

    lock_release(&pool->lock);
}

/* Frees the page at PAGE. */
void palloc_free_page(void *page)
{
    palloc_free_multiple(page, 1);
}

/* Returns the index of the page in the pool's bitmap. */
size_t
palloc_get_page_index (void *page)
{
  struct pool *pool;

  if (page_from_pool (&kernel_pool, page))
    pool = &kernel_pool;
  else if (page_from_pool (&user_pool, page))
    pool = &user_pool;
  else
    return BITMAP_ERROR;

  return pg_no (page) - pg_no (pool->base);
}

/* Initializes pool P as starting at START and ending at END,
   naming it NAME for debugging purposes. */
static void
init_pool(struct pool *p, void *base, size_t page_cnt, const char *name)
{
    /* We'll put the pool's used_map at its base.
       Calculate the space needed for the bitmap
       and subtract it from the pool's size. */
    size_t bm_pages = DIV_ROUND_UP(bitmap_buf_size(page_cnt), PGSIZE);
    if (bm_pages > page_cnt)
        PANIC("Not enough memory in %s for bitmap.", name);
    page_cnt -= bm_pages;

    printf("%zu pages available in %s.\n", page_cnt, name);

    /* Initialize the pool. */
    lock_init(&p->lock);
    p->used_map = bitmap_create_in_buf(page_cnt, base, bm_pages * PGSIZE);
    p->base = base + bm_pages * PGSIZE;
    p->page_cnt = page_cnt;
}

/* Returns true if PAGE was allocated from POOL,
   false otherwise. */
static bool
page_from_pool(const struct pool *pool, void *page)
{
    size_t page_no = pg_no(page);
    size_t start_page = pg_no(pool->base);
    size_t end_page = start_page + bitmap_size(pool->used_map);

    return page_no >= start_page && page_no < end_page;
}
