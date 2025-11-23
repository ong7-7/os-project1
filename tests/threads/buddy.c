#include "tests/threads/tests.h"
#include "threads/palloc.h"
#include <stdio.h>

void test_buddy (void) 
{
  palloc_set_mode (PAL_BUDDY);
}
