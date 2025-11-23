#include "tests/threads/tests.h"
#include "threads/palloc.h"
#include <stdio.h>

void
test_firstfit (void) 
{
  palloc_set_mode (PAL_FIRST_FIT);

}
