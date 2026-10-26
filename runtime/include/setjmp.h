#ifndef _FC_SETJMP_H
#define _FC_SETJMP_H
/* frame pointer, stack pointer and resume address of the setjmp call */
typedef struct __jmp_buf_tag { long bp, sp, ip, pad; } jmp_buf[1];
int __setjmp(jmp_buf env);
void __longjmp(jmp_buf env, int val);
#define setjmp(env) __setjmp(env)
#define longjmp(env, val) __longjmp(env, val)
#endif
