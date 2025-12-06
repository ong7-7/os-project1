/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Forward declarations for comparator used when inserting semaphore_elem
   into condition variable's waiters list. */
static bool semaphore_elem_priority_cmp (const struct list_elem *a,
                                         const struct list_elem *b,
                                         void *aux UNUSED);

/* Initializes semaphore SEMA to VALUE. */
void
sema_init (struct semaphore *sema, unsigned value)
{
    ASSERT (sema != NULL);

    sema->value = value;
    list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore. */
void
sema_down (struct semaphore *sema)
{
    enum intr_level old_level;

    ASSERT (sema != NULL);
    ASSERT (!intr_context ());

    old_level = intr_disable ();
    while (sema->value == 0)
    {
        /* 요구사항 1: 세마포어 대기열에 우선순위 순으로 삽입 */
        list_insert_ordered (&sema->waiters, &thread_current ()->elem,
                             thread_cmp_priority, NULL);
        thread_block ();
    }
    sema->value--;
    /* Unblocked 스레드가 현재 스레드보다 우선순위가 높으면 선점 검사 */
    thread_check_preemption ();
    intr_set_level (old_level);
}

/* Try down operation. */
bool
sema_try_down (struct semaphore *sema)
{
    enum intr_level old_level;
    bool success;

    ASSERT (sema != NULL);

    old_level = intr_disable ();
    if (sema->value > 0)
    {
        sema->value--;
        success = true;
    }
    else
        success = false;
    intr_set_level (old_level);

    return success;
}

/* Up or "V" operation on a semaphore. */
void
sema_up (struct semaphore *sema)
{
    enum intr_level old_level;

    ASSERT (sema != NULL);

    old_level = intr_disable ();
    if (!list_empty (&sema->waiters))
    {
        /* 요구사항 1: 대기열의 가장 높은 우선순위 스레드를 unblock */
        struct list_elem *e = list_pop_front (&sema->waiters);
        struct thread *t = list_entry (e, struct thread, elem);
        thread_unblock (t);
    }
    sema->value++;

    /* thread_unblock 내부에서 선점 체크를 수행하므로 여기선 생략 */
    intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores making control "ping-pong" between threads. */
void
sema_self_test (void)
{
    struct semaphore sema[2];
    int i;

    printf ("Testing semaphores...");
    sema_init (&sema[0], 0);
    sema_init (&sema[1], 0);
    thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
    for (i = 0; i < 10; i++)
    {
        sema_up (&sema[0]);
        sema_down (&sema[1]);
    }
    printf ("done.\n");
}

static void
sema_test_helper (void *sema_)
{
    struct semaphore *sema = sema_;
    int i;

    for (i = 0; i < 10; i++)
    {
        sema_down (&sema[0]);
        sema_up (&sema[1]);
    }
}

/* Initializes LOCK. */
void
lock_init (struct lock *lock)
{
    ASSERT (lock != NULL);

    lock->holder = NULL;
    sema_init (&lock->semaphore, 1);
    list_init (&lock->waiters);
}

/* Acquires LOCK, sleeping until it becomes available if necessary. */
void
lock_acquire (struct lock *lock)
{
    ASSERT (lock != NULL);
    ASSERT (!intr_context ());
    ASSERT (!lock_held_by_current_thread (lock));

    sema_down (&lock->semaphore);
    lock->holder = thread_current ();
}

/* Tries to acquires LOCK and returns true if successful. */
bool
lock_try_acquire (struct lock *lock)
{
    bool success;

    ASSERT (lock != NULL);
    ASSERT (!lock_held_by_current_thread (lock));

    success = sema_try_down (&lock->semaphore);
    if (success)
        lock->holder = thread_current ();
    return success;
}

/* Releases LOCK, which must be owned by the current thread. */
void
lock_release (struct lock *lock)
{
    ASSERT (lock != NULL);
    ASSERT (lock_held_by_current_thread (lock));

    lock->holder = NULL;
    sema_up (&lock->semaphore);
}

/* Returns true if current thread holds LOCK. */
bool
lock_held_by_current_thread (const struct lock *lock)
{
    ASSERT (lock != NULL);

    return lock->holder == thread_current ();
}

/* One semaphore in a list.  We add 'priority' to record the waiting
   thread's priority at the time of wait so condition-variable waiters
   can be ordered correctly. */
struct semaphore_elem
{
    struct list_elem elem;      /* List element. */
    struct semaphore semaphore; /* This semaphore. */
    int priority;               /* Priority of the waiting thread (snapshot). */
};

/* Initializes condition variable COND. */
void
cond_init (struct condition *cond)
{
    ASSERT (cond != NULL);

    list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled. */
void
cond_wait (struct condition *cond, struct lock *lock)
{
    struct semaphore_elem waiter;
    enum intr_level old_level;

    ASSERT (cond != NULL);
    ASSERT (lock != NULL);
    ASSERT (!intr_context ());
    ASSERT (lock_held_by_current_thread (lock));

    /* Initialize waiter semaphore and record current thread priority as snapshot. */
    sema_init (&waiter.semaphore, 0);
    waiter.priority = thread_get_priority ();

    old_level = intr_disable ();
    /* 요구사항 1: 조건 변수 대기열에 우선순위 순으로 삽입 (priority snapshot 사용) */
    list_insert_ordered (&cond->waiters, &waiter.elem,
                         semaphore_elem_priority_cmp, NULL);
    intr_set_level (old_level);

    lock_release (lock);
    sema_down (&waiter.semaphore);
    lock_acquire (lock);
}

/* If any threads are waiting on COND, signal one of them to wake up. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED)
{
    enum intr_level old_level;

    ASSERT (cond != NULL);
    ASSERT (lock != NULL);
    ASSERT (!intr_context ());
    ASSERT (lock_held_by_current_thread (lock));

    old_level = intr_disable ();
    if (!list_empty (&cond->waiters))
    {
        /* 요구사항 1: 대기열의 가장 높은 우선순위 semaphore_elem을 선택하여 semaphore_up */
        struct list_elem *e = list_pop_front (&cond->waiters);
        struct semaphore_elem *se = list_entry (e, struct semaphore_elem, elem);
        sema_up (&se->semaphore);
    }
    intr_set_level (old_level);
}

/* Wakes up all threads waiting on COND. */
void
cond_broadcast (struct condition *cond, struct lock *lock)
{
    ASSERT (cond != NULL);
    ASSERT (lock != NULL);

    enum intr_level old_level = intr_disable ();
    /* 요구사항 1: 모든 대기 스레드를 우선순위 순으로 unblock (list는 이미 정렬되어 있음) */
    while (!list_empty (&cond->waiters))
    {
        struct list_elem *e = list_pop_front (&cond->waiters);
        struct semaphore_elem *se = list_entry (e, struct semaphore_elem, elem);
        sema_up (&se->semaphore);
    }
    intr_set_level (old_level);
}

/* Comparator for semaphore_elem list insertion.
   Compare by saved priority (higher priority first).
   If equal priority, preserve FIFO order: because insertion uses
   list_insert_ordered which inserts *before* the first element where
   cmp returns true, we define cmp such that it returns true when
   a->priority > b->priority; when equal, return false so new waiter
   is placed after existing equal-priority waiters (FIFO). */
static bool
semaphore_elem_priority_cmp (const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux UNUSED)
{
    const struct semaphore_elem *sa = list_entry (a, struct semaphore_elem, elem);
    const struct semaphore_elem *sb = list_entry (b, struct semaphore_elem, elem);

    if (sa->priority > sb->priority)
        return true;
    else
        return false; /* equal or lower -> inserted after (preserve FIFO on equal) */
}

/* 리스트 내 semaphore_elem들의 우선순위에 따라 다시 정렬 */
void
list_reorder_by_priority(struct list *waiters)
{
    struct list_elem *e;
    if (list_empty(waiters))
        return;

    struct list sorted_list;
    list_init(&sorted_list);

    while (!list_empty(waiters))
    {
        e = list_pop_front(waiters);
        struct list_elem *inserted = list_begin(&sorted_list);
        while (inserted != list_end(&sorted_list))
        {
            struct semaphore_elem *se1 = list_entry(e, struct semaphore_elem, elem);
            struct semaphore_elem *se2 = list_entry(inserted, struct semaphore_elem, elem);
            if (se1->priority > se2->priority)
                break;
            inserted = list_next(inserted);
        }
        list_insert(inserted, e);
    }

    /* 원래 waiters 리스트에 다시 연결 */
    while (!list_empty(&sorted_list))
    {
        list_push_back(waiters, list_pop_front(&sorted_list));
    }
}
