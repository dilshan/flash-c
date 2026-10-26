#ifndef _FC_ASSERT_H
#define _FC_ASSERT_H
void __assertfail(const char *fmt, const char *expr, const char *file, int line);
#undef assert
#ifdef NDEBUG
#define assert(p) ((void)0)
#else
#define assert(p) ((p) ? (void)0 : __assertfail("Assertion failed: %s, file %s, line %d\n", #p, __FILE__, __LINE__))
#endif
#endif
