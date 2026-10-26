HEADERS = {
"stdio.h":"Input and output: streams, files, formatted input and output, character and block transfer.",
"stdlib.h":"General utilities: memory allocation, string conversion, random numbers, sorting, searching, program control, integer arithmetic and multibyte conversion.",
"string.h":"Memory blocks and strings: copying, concatenation, comparison, searching and splitting.",
"ctype.h":"Character classification and case conversion. Arguments are unsigned char values or EOF.",
"math.h":"Mathematical functions on double values: trigonometry, exponentials, logarithms, powers, rounding and remainders.",
"time.h":"Dates and times: reading the clock, converting and formatting.",
"conio.h":"Console input and output: text windows, colours, cursor control and unbuffered keyboard input.",
"dos.h":"DOS-style date, time and delay functions and a few system calls.",
"bios.h":"The few BIOS-style services that make sense on a terminal.",
"misc":"assert.h (assert), setjmp.h (setjmp, longjmp), signal.h (signal, raise), stdarg.h (va_start, va_arg, va_end), stddef.h, locale.h, errno.h, limits.h and float.h.",
}
def P(title, text, keys="", alias=""): return (title, text, keys, alias)
PAGES = [
P("printf formats",
""" printf formats

 A conversion specification has the form

   %[flags][width][.precision][length]conversion

## Flags

   -    left-justify in the field
   +    always print a sign
   space  a space in front of a positive number
   #    alternate form: 0 before octal, 0x before hex, always a
        decimal point for floating types
   0    pad with zeros instead of spaces

## Width and precision

 Width and precision are numbers or * (taken from the arguments).

 Precision: minimum digits for integers, digits after the point for
 e, E and f, significant digits for g and G, maximum characters for s.

## Length

 h (short), l (long, or wide for c and s), L (long double)

## Conversions

   d i  signed decimal        o  octal          u  unsigned decimal
   x X  hexadecimal           c  character      s  string
   f    fixed point           e E  exponent     g G  shortest of e/f
   p    pointer               n  stores the count so far
   %    a percent sign

 See {printf}, {fprintf}, {sprintf}, {scanf formats}""", "format specifier conversion %d %s %f", "format specifiers"),
P("scanf formats",
""" scanf formats

 A conversion specification has the form

   %[*][width][length]conversion

 * suppresses assignment. White space in the format matches any amount
 of white space in the input; other characters must match exactly.

## Conversions

   d    decimal int            i  int, base from prefix
   o    octal                  u  unsigned decimal
   x X  hexadecimal            c  characters (no skip of white space)
   s    word (stops at white space)
   e f g  floating point       p  pointer
   n    characters read so far
   [set]  characters from the set; [^set] characters not in it
   %    a percent sign

 Length h (short), l (long, or double for floating types), L (long double).
 Arguments must be pointers. See {scanf}, {sscanf}, {fscanf}, {printf formats}""", "scanf conversion input", ""),
P("Keywords and operators",
""" Keywords and operators

## Keywords

   auto break case char const continue default do double else enum
   extern float for goto if int long register return short signed
   sizeof static struct switch typedef union unsigned void volatile
   while

 The words near, far, huge, cdecl and pascal are accepted and ignored.

## Operators, highest precedence first

   () [] -> .                       left to right
   ! ~ ++ -- + - * & (type) sizeof  right to left
   * / %                            left to right
   + -
   << >>
   < <= > >=
   == !=
   &    ^    |
   &&   ||
   ?:                               right to left
   = += -= *= /= %= &= ^= |= <<= >>=
   ,                                left to right

 See {Preprocessor}, {Language reference}""",
"keyword operator precedence if else for while do switch case default break continue goto return sizeof struct union enum typedef static extern register volatile const signed unsigned void char short int long float double auto", "C keywords"),
P("Preprocessor",
""" Preprocessor

## Directives

   #include <file>   search the include directories
   #include "file"   search the current directory first
   #define NAME text / #define NAME(args) text
   #undef  NAME
   #if expr / #ifdef / #ifndef / #elif / #else / #endif
   #line number "file"
   #error text
   #pragma ...       startup and exit functions, others ignored

 Operators in macros: # makes a string, ## joins tokens.
 Defined in expressions: defined(NAME).

 See {Predefined macros}""", "include define undef ifdef ifndef elif endif pragma line error defined", "Preprocessor directives"),
P("Predefined macros",
""" Predefined macros

## Macros

   __FILE__   the name of the source file as a string
   __LINE__   the current line number
   __DATE__   the compilation date, "Mmm dd yyyy"
   __TIME__   the compilation time, "hh:mm:ss"
   __STDC__   1

 See {Preprocessor}""", "__FILE__ __LINE__ __DATE__ __TIME__ __STDC__", ""),
]
