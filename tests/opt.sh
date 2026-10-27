#!/bin/sh
# File:    opt.sh
# Purpose: Checks every optimization on its own: the program must print the same with the pass
#          on, and the generated file must be smaller than without it.
# Author:  Dilshan R Jayakody
# License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
# Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux

cd "$(dirname "$0")/.." || exit 1
FCC=build/fcc
TMP=${TMPDIR:-/tmp}/fc-opt.$$
mkdir -p "$TMP"
fail=0

# check NAME PASSES MODE  (the source is read from stdin); MODE smaller needs a smaller file,
# MODE same only compares the output
check() {
    cat > "$TMP/$1.c"
    $FCC -w- -e"$TMP/$1.base" "$TMP/$1.c" > /dev/null 2>&1 || { echo "FAIL $1 (compile)"; fail=1; return; }
    $FCC -w- "-O:$2" -e"$TMP/$1.opt" "$TMP/$1.c" > /dev/null 2>&1 || { echo "FAIL $1 (compile with -O:$2)"; fail=1; return; }
    "$TMP/$1.base" > "$TMP/$1.want"; "$TMP/$1.opt" > "$TMP/$1.got"
    if ! cmp -s "$TMP/$1.want" "$TMP/$1.got"; then echo "FAIL $1 (output differs)"; fail=1; return; fi
    a=$(wc -c < "$TMP/$1.base"); b=$(wc -c < "$TMP/$1.opt")
    if [ "${3:-smaller}" = same ]; then echo "ok   $1 (output equal)"; elif [ "$b" -lt "$a" ]; then echo "ok   $1 ($a -> $b bytes)"; else echo "FAIL $1 (not smaller: $a -> $b)"; fail=1; fi
}

check fold fold same <<'X'
#include <stdio.h>
int main(void) { int a = (char)300 + (short)70000 - (int)3.9; double d = (double)7 / 2; printf("%d %g\n", a, d); return 0; }
X
check algebra algebra <<'X'
#include <stdio.h>
int main(int c, char **v) { int x = c + 4; printf("%d\n", (x + 0) * 1 + (x - x) + (x | 0) + ((x + 1) + 2)); return 0; }
X
check compare compare <<'X'
#include <stdio.h>
int main(int c, char **v) { unsigned u = c; char ch = c; if (u >= 0) puts("a"); if (ch == 300) puts("b"); if (c == c) puts("c"); return 0; }
X
check constprop fold,constprop <<'X'
#include <stdio.h>
int main(void) { int a = 6, b; b = a * 7; printf("%d %d\n", a, b + a); return 0; }
X
check copyprop copyprop,deadstore <<'X'
#include <stdio.h>
int main(int c, char **v) { int a = c, b = a, d = b; printf("%d\n", d + b + a); return 0; }
X
check deadstore deadstore <<'X'
#include <stdio.h>
int main(int c, char **v) { int a; a = c * 3; a = 5; printf("%d\n", a); return 0; }
X
check unreachable unreachable <<'X'
#include <stdio.h>
int main(void) { if (0) printf("never %d\n", 1); while (0) { puts("x"); } puts("y"); return 0; puts("z"); }
X
check casts casts same <<'X'
#include <stdio.h>
int main(int c, char **v) { long a = c; long b = (long)(long)a; int i = (int)(int)c; printf("%ld %d\n", b + (long)a, i); return 0; }
X
check canon canon same <<'X'
#include <stdio.h>
int main(int c, char **v) { int x = c; printf("%d\n", (x - 3) + (5 + x)); return 0; }
X
check cse cse <<'X'
#include <stdio.h>
int main(int c, char **v) { int a = c + 2, b = c + 3; int r = (a * b + 1) + (a * b + 2) + (a * b + 3) + (a * b + 4); printf("%d\n", r); return 0; }
X
check strength strength same <<'X'
#include <stdio.h>
int main(int c, char **v) { unsigned u = c + 100; printf("%u %u %u\n", u * 8, u / 4, u % 16); return 0; }
X
check bool bool <<'X'
#include <stdio.h>
int main(int c, char **v) { int x = c; if (!!x) puts("a"); if (x == 0) puts("b"); printf("%d\n", x ? 1 : 0); return 0; }
X

check gconst cfg,fold,gconst smaller <<'X'
#include <stdio.h>
int main(int c, char **v) { int k = 4, r = 0; if (c > 1) r = 1; else r = 2; while (c < 3) { r += k * 5; c++; } printf("%d %d\n", r, k + 1); return 0; }
X
check ccp cfg,fold,gconst,ccp,ublock smaller <<'X'
#include <stdio.h>
int main(int c, char **v) { int mode = 2; if (mode == 1) { puts("one"); puts("uno"); } else if (mode == 2) puts("two"); else puts("other"); return 0; }
X
check gcopy cfg,gcopy,gdce,live smaller <<'X'
#include <stdio.h>
int main(int c, char **v) { int a = c, b; if (c > 5) goto L; puts("small"); L: b = a; printf("%d\n", b + a); return 0; }
X
check ublock cfg,ublock,fold,gconst,ccp smaller <<'X'
#include <stdio.h>
int main(void) { int n = 1; goto skip; puts("never"); n = 5; skip: if (n > 3) puts("big"); printf("%d\n", n); return 0; }
X
check gdce cfg,gdce,live smaller <<'X'
#include <stdio.h>
int main(int c, char **v) { int a = c * 3, b = c * 5; int i; for (i = 0; i < 3; i++) { a = i; b = a + 1; } printf("%d\n", b); return 0; }
X
check thread cfg,thread,merge,unreachable smaller <<'X'
#include <stdio.h>
int f0(int c) { if (c) goto A; puts("x"); return 1; A: goto B; B: return 0; }
int f1(int c) { if (c) goto A; puts("x"); return 1; A: goto B; B: return 1; }
int f2(int c) { if (c) goto A; puts("x"); return 1; A: goto B; B: return 2; }
int f3(int c) { if (c) goto A; puts("x"); return 1; A: goto B; B: return 3; }
int f4(int c) { if (c) goto A; puts("x"); return 1; A: goto B; B: return 4; }
int f5(int c) { if (c) goto A; puts("x"); return 1; A: goto B; B: return 5; }
int f6(int c) { if (c) goto A; puts("x"); return 1; A: goto B; B: return 6; }
int f7(int c) { if (c) goto A; puts("x"); return 1; A: goto B; B: return 7; }
int main(int c, char **v) { printf("%d %d\n", f0(c) + f1(c) + f2(c) + f3(c), f4(0) + f5(0) + f6(c) + f7(c)); return 0; }
X
check merge cfg,thread,merge,cfs smaller <<'X'
#include <stdio.h>
int main(int c, char **v) { int s = 0; goto A; A: s += c; goto B; B: s += 2; printf("%d\n", s); return 0; }
X
check branch branch,cfs smaller <<'X'
#include <stdio.h>
int main(int c, char **v) { if (c > 5) goto L; L: puts("done"); return 0; }
X
check cfs cfg,cfs same <<'X'
#include <stdio.h>
int main(void) { int i; for (i = 0; i < 3; i++) { if (i == 1) continue; printf("%d\n", i); } return 0; }
X

# tail recursion: 20 million nested calls only work when the frame is reused
cat > "$TMP/tail.c" <<'X'
#include <stdio.h>
long sum(long n, long s) { if (n == 0) return s; return sum(n - 1, s + n); }
int main(void) { printf("%ld\n", sum(20000000L, 0)); return 0; }
X
$FCC -w- -O:tailrec -e"$TMP/tail" "$TMP/tail.c" > /dev/null 2>&1
if [ "$("$TMP/tail" 2>&1)" = "200000010000000" ]; then echo "ok   tailrec (20000000 calls in constant stack)"; else echo "FAIL tailrec"; fail=1; fi

rm -rf "$TMP"
[ $fail -eq 0 ] && echo "all optimization checks passed"
exit $fail
