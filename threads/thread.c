#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h" // timer_ticks 사용을 위해 추가
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
/* 요구사항 1: ready_list는 항상 우선순위 순으로 정렬됩니다. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* List of process in sleep */
static struct list sleep_list;
static int64_t next_tick_to_wakeup = INT64_MAX;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame
{
    void *eip;             /* Return address. */
    thread_func *function; /* Function to call. */
    void *aux;             /* Auxiliary data for function. */
};

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4          /* # of timer ticks to give each thread (RR fallback). */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
/* init.c/h에 전역 선언되어 있으므로 제거하거나 extern으로 선언해야 함.
   사용자의 템플릿에 bool thread_mlfqs; 가 포함되어 있으므로 유지 */
bool thread_mlfqs; 

/* 요구사항 3: MLFQS 시간 관리 */
/* 현재 스레드가 큐에서 실행된 틱 수 */
static int mlfqs_current_quantum_ticks;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Function to compare two list elements based on thread priority. */
/* 요구사항 1: ready_list 및 동기화 대기열 정렬에 사용 */
bool
thread_cmp_priority (const struct list_elem *a, const struct list_elem *b,
                     void *aux UNUSED)
{
  struct thread *t_a = list_entry (a, struct thread, elem);
  struct thread *t_b = list_entry (b, struct thread, elem);
  /* 우선순위가 높은 스레드를 앞에 두기 위해 내림차순으로 비교합니다. */
  return t_a->priority > t_b->priority;
}


/* Initializes the threading system by transforming the code
   that's currently running into a thread. */
void
thread_init (void)
{
    ASSERT (intr_get_level () == INTR_OFF);

    lock_init (&tid_lock);
    list_init (&ready_list);
    list_init (&all_list);
    list_init (&sleep_list);

    /* Set up a thread structure for the running thread. */
    initial_thread = running_thread ();
    init_thread (initial_thread, "main", PRI_DEFAULT);
    initial_thread->status = THREAD_RUNNING;
    initial_thread->tid = allocate_tid ();
    
    /* 요구사항 2 & 3: age, mlfqs_level 초기화 */
    initial_thread->age = 0;
    initial_thread->mlfqs_level = 0; // Q0 시작
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void)
{
    /* Create the idle thread. */
    struct semaphore idle_started;
    sema_init (&idle_started, 0);
    thread_create ("idle", PRI_MIN, idle, &idle_started);

    /* Start preemptive thread scheduling. */
    intr_enable ();

    /* Wait for the idle thread to initialize idle_thread. */
    sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick. */
void
thread_tick (void)
{
    struct thread *t = thread_current ();

    /* Update statistics. */
    if (t == idle_thread)
        idle_ticks++;
#ifdef USERPROG
    else if (t->pagedir != NULL)
        user_ticks++;
#endif
    else
        kernel_ticks++;

    /* 요구사항 2 & 3: 매 틱마다 에이징 및 MLFQS 관리 로직 실행 */
    if (!thread_mlfqs)
      thread_aging (); // 요구사항 2: Priority + Aging
    else
      mlfqs_demote_or_promote(); // 요구사항 3: MLFQS

    /* Enforce preemption (Round-robin or MLFQS time slice). */
    if (!thread_mlfqs)
      {
        /* Non-MLFQS (Priority + Aging) 모드에서는 일반적인 라운드 로빈 시간 슬라이스 사용 */
        if (++thread_ticks >= TIME_SLICE)
          intr_yield_on_return ();
      }
    else
      {
        /* MLFQS 모드에서는 mlfqs_demote_or_promote에서 강등 시 yield를 호출합니다.
           여기서는 단순히 thread_ticks를 증가시켜도 되지만, MLFQS 퀀텀 관리는 
           mlfqs_demote_or_promote에서만 수행하도록 합니다.
           (단, thread_schedule_tail에서 thread_ticks = 0 되므로, RR 기반의 thread_ticks는 무시됩니다.) */
      }
}

/* Prints thread statistics. */
void
thread_print_stats (void)
{
    printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
             idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux)
{
    struct thread *t;
    struct kernel_thread_frame *kf;
    struct switch_entry_frame *ef;
    struct switch_threads_frame *sf;
    tid_t tid;
    enum intr_level old_level;

    ASSERT (function != NULL);

    /* Allocate thread. */
    t = palloc_get_page (PAL_ZERO);
    if (t == NULL)
        return TID_ERROR;

    /* Initialize thread. */
    init_thread (t, name, priority);
    tid = t->tid = allocate_tid ();

    /* Prepare thread for first run by initializing its stack. */
    old_level = intr_disable ();

    /* Stack frame for kernel_thread(). */
    kf = alloc_frame (t, sizeof *kf);
    kf->eip = NULL;
    kf->function = function;
    kf->aux = aux;

    /* Stack frame for switch_entry(). */
    ef = alloc_frame (t, sizeof *ef);
    ef->eip = (void (*) (void))kernel_thread;

    /* Stack frame for switch_threads(). */
    sf = alloc_frame (t, sizeof *sf);
    sf->eip = switch_entry;
    sf->ebp = 0;

    intr_set_level (old_level);

    /* Add to run queue. */
    thread_unblock (t);
    
    /* 요구사항 1: 새로운 스레드 생성 시 선점 체크 */
    thread_check_preemption ();

    return tid;
}

/* Puts the current thread to sleep. */
void
thread_block (void)
{
    ASSERT (!intr_context ());
    ASSERT (intr_get_level () == INTR_OFF);

    thread_current ()->status = THREAD_BLOCKED;
    schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state. */
void
thread_unblock (struct thread *t)
{
    enum intr_level old_level;

    ASSERT (is_thread (t));

    old_level = intr_disable ();
    ASSERT (t->status == THREAD_BLOCKED);
    
    /* 요구사항 1: ready_list에 우선순위 순으로 삽입 */
    list_insert_ordered (&ready_list, &t->elem, thread_cmp_priority, NULL);
    t->status = THREAD_READY;

    /* 요구사항 2 & 3: 준비 큐에 들어갈 때 age 초기화 */
    t->age = 0; 
    
    /* 요구사항 1: Unblock 시 선점 체크 */
    thread_check_preemption ();
    
    intr_set_level (old_level);
}

static void
update_next_tick_to_wakeup (int64_t tick)
{
    next_tick_to_wakeup = 
        (next_tick_to_wakeup > tick) ? tick : next_tick_to_wakeup;
}

int64_t
get_next_tick_to_wakeup (void)
{
    return next_tick_to_wakeup;
}

/* Wakes up this thread after ticks */
void
thread_sleep (int64_t tick)
{
    struct thread *cur;
    enum intr_level old_level;

    old_level = intr_disable ();
    cur = thread_current ();

    ASSERT (cur != idle_thread);

    update_next_tick_to_wakeup (cur->wakeup_tick = tick);
    list_push_back (&sleep_list, &cur->elem);

    thread_block ();

    intr_set_level (old_level);
}

void
thread_wakeup (int64_t current_tick)
{
    struct list_elem *e;

    next_tick_to_wakeup = INT64_MAX;

    e = list_begin (&sleep_list);
    while (e != list_end (&sleep_list))
    {
        struct thread *t = list_entry (e, struct thread, elem);
        if (current_tick >= t->wakeup_tick)
        {
            e = list_remove (&t->elem);
            thread_unblock (t);
        }
        else
        {
            e = list_next (e);
            update_next_tick_to_wakeup (t->wakeup_tick);
        }
    }
}

/* Returns the name of the running thread. */
const char *
thread_name (void)
{
    return thread_current ()->name;
}

/* Returns the running thread. */
struct thread *
thread_current (void)
{
    struct thread *t = running_thread ();

    ASSERT (is_thread (t));
    ASSERT (t->status == THREAD_RUNNING);

    return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void)
{
    return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it. */
void
thread_exit (void)
{
    ASSERT (!intr_context ());

#ifdef USERPROG
    process_exit ();
#endif

    /* Remove thread from all threads list, set our status to dying,
       and schedule another process. */
    intr_disable ();
    list_remove (&thread_current ()->allelem);
    thread_current ()->status = THREAD_DYING;
    schedule ();
    NOT_REACHED ();
}

/* Yields the CPU. */
void
thread_yield (void)
{
    struct thread *cur = thread_current ();
    enum intr_level old_level;

    ASSERT (!intr_context ());

    old_level = intr_disable ();
    if (cur != idle_thread)
    {
        /* 요구사항 1: ready_list에 우선순위 순으로 삽입 */
        list_insert_ordered (&ready_list, &cur->elem, thread_cmp_priority, NULL);
        
        /* 요구사항 2 & 3: 준비 큐에 들어갈 때 age 초기화 */
        cur->age = 0;
    }
    cur->status = THREAD_READY;
    schedule ();
    intr_set_level (old_level);
}

/* Checks if the head of the ready_list has a higher priority than the current thread.
   If so, yields the CPU immediately (preemption). */
/* 요구사항 1: 선점 체크 함수 */
void
thread_set_priority (int new_priority)
{
    enum intr_level old_level = intr_disable ();
    struct thread *cur = thread_current ();

    cur->priority = new_priority;

    /* ready_list의 최고 우선순위가 현재 스레드보다 높으면 선점 */
    thread_check_preemption ();

    intr_set_level (old_level);
}

void
thread_check_preemption (void)
{
    /* 이미 인터럽트 비활성화 상태에서 호출된다고 가정 */
    if (list_empty (&ready_list))
        return;

    struct thread *next = list_entry (list_front (&ready_list), struct thread, elem);

    if (next->priority > thread_current ()->priority)
        thread_yield ();
}
/* Invoke function 'func' on all threads, passing along 'aux'. */
void
thread_foreach (thread_action_func *func, void *aux)
{
    struct list_elem *e;

    ASSERT (intr_get_level () == INTR_OFF);

    for (e = list_begin (&all_list); e != list_end (&all_list);
         e = list_next (e))
        {
            struct thread *t = list_entry (e, struct thread, allelem);
            func (t, aux);
        }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void)
{
    return thread_current ()->priority;
}

/* Initializes thread T with basic properties. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
    ASSERT (t != NULL);
    ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT (name != NULL);

    memset (t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy (t->name, name, sizeof t->name);
    t->stack = (uint8_t *)t + PGSIZE;
    t->priority = priority;
    t->magic = THREAD_MAGIC;
    list_push_back (&all_list, &t->allelem);

    /* 요구사항 2 & 3: age, mlfqs_level 초기화 */
    t->age = 0;
    t->mlfqs_level = 0; // Q0 시작
    
    if (thread_mlfqs) {
      /* MLFQS 모드에서는 생성 시 Q0 우선순위 티어로 설정 */
      t->priority = PRI_MLFQS_Q0;
    }
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size)
{
    /* Stack data is always allocated in word-size units. */
    ASSERT (is_thread (t));
    ASSERT (size % sizeof (uint32_t) == 0);

    t->stack -= size;
    return t->stack;
}

/* Chooses and returns the next thread to be scheduled. */
static struct thread *
next_thread_to_run (void)
{
    /* 요구사항 1: ready_list는 이미 우선순위 순으로 정렬되어 있으므로, 단순히 head를 선택 */
    if (list_empty (&ready_list))
        return idle_thread;
    else
        return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it. */
void
thread_schedule_tail (struct thread *prev)
{
    struct thread *cur = running_thread ();

    ASSERT (intr_get_level () == INTR_OFF);

    /* Mark us as running. */
    cur->status = THREAD_RUNNING;

    /* Start new time slice. */
    thread_ticks = 0;
    
    /* 요구사항 3: MLFQS 퀀텀 초기화 */
    mlfqs_current_quantum_ticks = 0;

#ifdef USERPROG
    /* Activate the new address space. */
    process_activate ();
#endif

    /* If the thread we switched from is dying, destroy its struct
       thread. */
    if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
        {
            ASSERT (prev != cur);
            palloc_free_page (prev);
        }
}

/* Schedules a new process. */
static void
schedule (void)
{
    struct thread *cur = running_thread ();
    struct thread *next = next_thread_to_run ();
    struct thread *prev = NULL;

    ASSERT (intr_get_level () == INTR_OFF);
    ASSERT (cur->status != THREAD_RUNNING);
    ASSERT (is_thread (next));

    if (cur != next)
        prev = switch_threads (cur, next);
    thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void)
{
    static tid_t next_tid = 1;
    tid_t tid;

    lock_acquire (&tid_lock);
    tid = next_tid++;
    lock_release (&tid_lock);

    return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);


/* 요구사항 2: 비-MLFQS 모드에서의 에이징 로직 (Starvation 방지) */
void
thread_aging (void)
{
  struct list_elem *e;
  enum intr_level old_level = intr_disable ();

  /* ready_list에 있는 모든 스레드에 대해 반복 */
  e = list_begin (&ready_list);
  while (e != list_end (&ready_list))
    {
      struct thread *t = list_entry (e, struct thread, elem);
      e = list_next (e);
      
      /* age 증가 */
      t->age++;

      /* age가 임계값에 도달하면 우선순위 상승 및 age 초기화 */
      if (t->age >= AGING_THRESHOLD)
        {
          t->age = 0;
          /* 우선순위는 PRI_MAX를 넘지 않도록 제한 */
          if (t->priority < PRI_MAX)
            {
              t->priority++;
              
              /* 우선순위가 변경되었으므로 ready_list에서 제거 후 재삽입하여 정렬 */
              list_remove (&t->elem);
              list_insert_ordered (&ready_list, &t->elem, thread_cmp_priority, NULL);
            }
        }
    }
  
  /* 에이징으로 인해 우선순위가 변경되었을 수 있으므로 선점 체크 */
  thread_check_preemption ();

  intr_set_level (old_level);
}


/* 요구사항 3: MLFQS 모드에서의 강등(Demotion) 및 승급(Promotion, Aging) 로직 */
void
mlfqs_demote_or_promote (void)
{
  struct thread *cur = thread_current();
  enum intr_level old_level = intr_disable ();

  /* --- 1. Promotion (Aging) Logic: ready_list에 있는 스레드만 처리 --- */
  struct list_elem *e;
  e = list_begin (&ready_list);
  while (e != list_end (&ready_list))
    {
      struct thread *waiting_t = list_entry (e, struct thread, elem);
      e = list_next (e);

      /* age 증가 */
      waiting_t->age++;

      /* age가 임계값에 도달하면 상위 큐로 승급 및 age 초기화 */
      if (waiting_t->age >= AGING_THRESHOLD)
        {
          waiting_t->age = 0;
          
          if (waiting_t->mlfqs_level > 0)
            {
              /* 상위 큐로 승급 (Q2 -> Q1 -> Q0) */
              waiting_t->mlfqs_level--;
              
              /* 승급에 따른 우선순위 티어 재설정 */
              if (waiting_t->mlfqs_level == 0)
                waiting_t->priority = PRI_MLFQS_Q0;
              else if (waiting_t->mlfqs_level == 1)
                waiting_t->priority = PRI_MLFQS_Q1;
              
              /* ready_list 재정렬을 위해 일시적으로 제거 후 재삽입 (우선순위 변경) */
              list_remove (&waiting_t->elem);
              list_insert_ordered (&ready_list, &waiting_t->elem, thread_cmp_priority, NULL);
              
              /* 승급으로 인해 선점 발생 가능성 체크 (thread_unblock에서도 체크됨) */
              thread_check_preemption ();
            }
        }
    }
  
  /* --- 2. Demotion (Time Slice) Logic: 현재 실행 중인 스레드만 처리 --- */
  int quantum = 0;
  
  /* 큐 레벨에 따른 Time Slice 설정 (Q0=2, Q1=4, Q2=8) */
  if (cur != idle_thread)
    {
      if (cur->mlfqs_level == 0)
        quantum = 2;
      else if (cur->mlfqs_level == 1)
        quantum = 4;
      else if (cur->mlfqs_level == 2)
        quantum = 8;
      
      mlfqs_current_quantum_ticks++;

      /* 현재 퀀텀이 소진되면 강등 */
      if (mlfqs_current_quantum_ticks >= quantum)
        {
          if (cur->mlfqs_level < 2)
            {
              /* 하위 큐로 강등 (Q0 -> Q1 -> Q2) */
              cur->mlfqs_level++;

              /* 강등에 따른 우선순위 티어 재설정 */
              if (cur->mlfqs_level == 1)
                cur->priority = PRI_MLFQS_Q1;
              else if (cur->mlfqs_level == 2)
                cur->priority = PRI_MLFQS_Q2;
            }
          
          /* 퀀텀 소진 및 강등 후 스케줄링 필요 */
          intr_yield_on_return ();
        }
    }

  intr_set_level (old_level);
}


/* Not yet implemented functions */
void thread_set_nice (int nice UNUSED) { /* Not yet implemented. */ }
int thread_get_nice (void) { return 0; }
int thread_get_load_avg (void) { return 0; }
int thread_get_recent_cpu (void) { return 0; }
