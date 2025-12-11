#include "threads/init.h"
#include <console.h>
#include <debug.h>
#include <inttypes.h>
#include <limits.h>
#include <random.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices/kbd.h"
#include "devices/input.h"
#include "devices/serial.h"
#include "devices/shutdown.h"
#include "devices/timer.h"
#include "devices/vga.h"
#include "devices/rtc.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/thread.h"
#include "tests/threads/tests.h"

/* Page directory with kernel mappings only. */
uint32_t *init_page_dir;



/* -ul: Maximum number of pages to put into palloc's user pool. */
static size_t user_page_limit = SIZE_MAX;

static void bss_init(void);
static void paging_init(void);

static char **read_command_line(void);
static char **parse_options(char **argv);
static void run_actions(char **argv);
static void usage(void);



int main(void) NO_RETURN;

/* Pintos main program. */
int main(void)
{
    char **argv;

    /* Clear BSS. */
    bss_init();

    /* Break command line into arguments and parse options. */
    argv = read_command_line();
    argv = parse_options(argv);

    /* Initialize ourselves as a thread so we can use locks,
     then enable console locking. */
    thread_init();
    console_init();

    /* Greet user. */
    printf("Pintos booting with %'" PRIu32 " kB RAM...\n",
           init_ram_pages * PGSIZE / 1024);

    /* Initialize memory system. */
    palloc_init(user_page_limit);
    malloc_init();
    paging_init();

    /* Segmentation. */


    /* Initialize interrupt handlers. */
    intr_init();
    timer_init();
    kbd_init();
    input_init();


    /* Start thread scheduler and enable interrupts. */
    thread_start();
    serial_init_queue();
    timer_calibrate();



    printf("Boot complete.\n");

    /* Run actions specified on kernel command line. */
    run_actions(argv);

    /* Finish up. */
    shutdown();
    thread_exit();
}

/* Clear the "BSS", a segment that should be initialized to
   zeros.  It isn't actually stored on disk or zeroed by the
   kernel loader, so we have to zero it ourselves.

   The start and end of the BSS segment is recorded by the
   linker as _start_bss and _end_bss.  See kernel.lds. */
static void
bss_init(void)
{
    extern char _start_bss, _end_bss;
    memset(&_start_bss, 0, &_end_bss - &_start_bss);
}

/* Populates the base page directory and page table with the
   kernel virtual mapping, and then sets up the CPU to use the
   new page directory.  Points init_page_dir to the page
   directory it creates. */
static void
paging_init(void)
{
    uint32_t *pd, *pt;
    size_t page;
    extern char _start, _end_kernel_text;

    pd = init_page_dir = palloc_get_page(PAL_ASSERT | PAL_ZERO);
    pt = NULL;
    for (page = 0; page < init_ram_pages; page++) {
        uintptr_t paddr = page * PGSIZE;
        char *vaddr = ptov(paddr);
        size_t pde_idx = pd_no(vaddr);
        size_t pte_idx = pt_no(vaddr);
        bool in_kernel_text = &_start <= vaddr && vaddr < &_end_kernel_text;

        if (pd[pde_idx] == 0) {
            pt = palloc_get_page(PAL_ASSERT | PAL_ZERO);
            pd[pde_idx] = pde_create(pt);
        }

        pt[pte_idx] = pte_create_kernel(vaddr, !in_kernel_text);
    }

    /* Store the physical address of the page directory into CR3
     aka PDBR (page directory base register).  This activates our
     new page tables immediately.  See [IA32-v2a] "MOV--Move
     to/from Control Registers" and [IA32-v3a] 3.7.5 "Base Address
     of the Page Directory". */
    asm volatile("movl %0, %%cr3" : : "r"(vtop(init_page_dir)));
}

/* Breaks the kernel command line into words and returns them as
   an argv-like array. */
static char **
read_command_line(void)
{
    static char *argv[LOADER_ARGS_LEN / 2 + 1];
    char *p, *end;
    int argc;
    int i;

    argc = *(uint32_t *)ptov(LOADER_ARG_CNT);
    p = ptov(LOADER_ARGS);
    end = p + LOADER_ARGS_LEN;
    for (i = 0; i < argc; i++) {
        if (p >= end)
            PANIC("command line arguments overflow");

        argv[i] = p;
        p += strnlen(p, end - p) + 1;
    }
    argv[argc] = NULL;

    /* Print kernel command line. */
    printf("Kernel command line:");
    for (i = 0; i < argc; i++)
        if (strchr(argv[i], ' ') == NULL)
            printf(" %s", argv[i]);
        else
            printf(" '%s'", argv[i]);
    printf("\n");

    return argv;
}

/* Parses options in ARGV[]
   and returns the first non-option argument. */
static char **
parse_options(char **argv)
{
    for (; *argv != NULL && **argv == '-'; argv++) {
        char *save_ptr;
        char *name = strtok_r(*argv, "=", &save_ptr);
        /* char *value = strtok_r(NULL, "", &save_ptr); */

        if (!strcmp(name, "-h"))
            usage();
        else if (!strcmp(name, "-q"))
            shutdown_configure(SHUTDOWN_POWER_OFF);
        else if (!strcmp(name, "-r"))
            shutdown_configure(SHUTDOWN_REBOOT);

        else
            PANIC("unknown option `%s' (use -h for help)", name);
    }

    /* Initialize the random number generator based on the system
     time.  This has no effect if an "-rs" option was specified.

     When running under Bochs, this is not enough by itself to
     get a good seed value, because the pintos script sets the
     initial time to a predictable value, not to the local time,
     for reproducibility.  To fix this, give the "-r" option to
     the pintos script to request real-time execution. */
    random_init(rtc_get_time());

    return argv;
}

/* Runs the task specified in ARGV[1]. */
static void
run_task(char **argv)
{
    const char *task = argv[1];

    printf("Executing '%s':\n", task);
    run_test(task);
    printf("Execution of '%s' complete.\n", task);
}

/* Executes all of the actions specified in ARGV[]
   up to the null pointer sentinel. */
static void
run_actions(char **argv)
{
    /* An action. */
    struct action {
        char *name;                    /* Action name. */
        int argc;                      /* # of args, including action name. */
        void (*function)(char **argv); /* Function to execute action. */
    };

    /* Table of supported actions. */
    static const struct action actions[] = {
        {"run", 2, run_task},

        {NULL, 0, NULL},
    };

    while (*argv != NULL) {
        const struct action *a;
        int i;

        /* Find action name. */
        for (a = actions;; a++)
            if (a->name == NULL)
                PANIC("unknown action `%s' (use -h for help)", *argv);
            else if (!strcmp(*argv, a->name))
                break;

        /* Check for required arguments. */
        for (i = 1; i < a->argc; i++)
            if (argv[i] == NULL)
                PANIC("action `%s' requires %d argument(s)", *argv, a->argc - 1);

        /* Invoke action and advance. */
        a->function(argv);
        argv += a->argc;
    }
}

/* Prints a kernel command line help message and powers off the
   machine. */
static void
usage(void)
{
    printf("\nCommand line syntax: [OPTION...] [ACTION...]\n"
           "Options must precede actions.\n"
           "Actions are executed in the order specified.\n"
           "\nAvailable actions:\n"
           "  run TEST           Run TEST.\n"
    );
    shutdown_power_off();
}


