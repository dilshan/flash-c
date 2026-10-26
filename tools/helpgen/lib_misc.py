H = "misc"
F = [
("assert","void assert(int expression);","Checks a condition while the program runs. (assert.h)","If the expression is 0 a message with the expression, file and line is written to stderr and abort is called. Define NDEBUG before including assert.h to remove the checks.","Nothing.","abort",None),
("longjmp","void longjmp(jmp_buf env, int val);","Jumps back to a setjmp. (setjmp.h)","Execution continues as if setjmp had returned val (1 if val is 0). The function that called setjmp must still be active.","Does not return.","setjmp",None),
("setjmp","int setjmp(jmp_buf env);","Saves the calling environment. (setjmp.h)","Returns 0 when called directly; when longjmp is used it returns the value given to longjmp.","0, or the value passed to longjmp.","longjmp","#include <setjmp.h>\n#include <stdio.h>\n\nstatic jmp_buf env;\n\nstatic void fail(void)\n{\n    longjmp(env, 2);\n}\n\nint main(void)\n{\n    int r = setjmp(env);\n    if (r == 0) {\n        fail();\n    } else {\n        printf(\"recovered, code %d\\n\", r);\n    }\n    return 0;\n}"),
("raise","int raise(int sig);","Sends a signal to the program. (signal.h)","","0 on success.","signal",None),
("signal","void (*signal(int sig, void (*func)(int)))(int);","Sets the handler for a signal. (signal.h)","func is a function, SIG_DFL (default action) or SIG_IGN (ignore). Signals: SIGABRT, SIGFPE, SIGILL, SIGINT, SIGSEGV, SIGTERM.","The previous handler, or SIG_ERR.","raise",None),
("va_arg","type va_arg(va_list ap, type);","Gets the next variable argument. (stdarg.h)","","The value, of the given type.","va_start va_end",None),
("va_end","void va_end(va_list ap);","Ends the use of a variable argument list. (stdarg.h)","","Nothing.","va_start",None),
("va_start","void va_start(va_list ap, parmN);","Starts a variable argument list. (stdarg.h)","parmN is the last named parameter of the function.","Nothing.","va_arg va_end vprintf","#include <stdarg.h>\n#include <stdio.h>\n\nstatic int sum(int count, ...)\n{\n    va_list ap;\n    int total = 0;\n    va_start(ap, count);\n    while (count-- > 0)\n        total += va_arg(ap, int);\n    va_end(ap);\n    return total;\n}\n\nint main(void)\n{\n    printf(\"%d\\n\", sum(3, 10, 20, 30));\n    return 0;\n}"),
("setlocale","char *setlocale(int category, const char *locale);","Selects the locale. (locale.h)","Categories: LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY, LC_NUMERIC, LC_TIME. Only the \"C\" locale (and \"\") is provided.","The locale name, or NULL.","localeconv",None),
("localeconv","struct lconv *localeconv(void);","Gets the numeric formatting of the locale. (locale.h)","","A pointer to a structure.","setlocale",None),
("offsetof","size_t offsetof(type, member);","Offset of a structure member in bytes. (stddef.h)","","The offset.","sizeof",None),
]
TYPES = [
("errno","errno.h: a modifiable int that library functions set on errors. Values: EDOM (domain error), ERANGE (result out of range), and system codes such as ENOENT. Test it right after the failing call."),
("limits.h","Limits of integer types: CHAR_BIT (8), SCHAR_MIN/MAX, UCHAR_MAX, CHAR_MIN/MAX, SHRT_MIN/MAX, USHRT_MAX, INT_MIN/MAX, UINT_MAX, LONG_MIN/MAX, ULONG_MAX, MB_LEN_MAX."),
("float.h","Characteristics of floating types: FLT_/DBL_/LDBL_ with DIG, EPSILON, MANT_DIG, MAX, MAX_EXP, MIN, MIN_EXP and so on, FLT_RADIX, FLT_ROUNDS."),
("stddef.h","Common definitions: ptrdiff_t, size_t, wchar_t, NULL, offsetof."),
]
