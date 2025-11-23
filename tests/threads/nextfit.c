#include "tests/threads/tests.h"
#include "threads/palloc.h"
#include <stdio.h>

void test_nextfit (void) 
{
  palloc_set_mode (PAL_NEXT_FIT);
}

