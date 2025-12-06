#ifndef THREADS_INIT_H
#define THREADS_INIT_H

#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "threads/thread.h"

#define PRI_DEFAULT 31

/* Page directory with kernel mappings only. */
extern uint32_t *init_page_dir;

void power_of_2 (int *base, int *exp);

#endif /* threads/init.h */
