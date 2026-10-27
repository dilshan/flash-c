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

rm -rf "$TMP"
[ $fail -eq 0 ] && echo "all optimization checks passed"
exit $fail
