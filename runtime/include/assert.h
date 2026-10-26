#ifndef _FC_ASSERT_H
#define _FC_ASSERT_H
#include <stdio.h>
#include <stdlib.h>
#define assert(e) ((e) ? (void)0 : (fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", #e, __FILE__, __LINE__), abort()))
#endif
