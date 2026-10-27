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
check gcse cfg,avail,gcse,cse smaller <<'X'
#include <stdio.h>
int main(int c, char **v) { int a = c * 7 + 3, b; if (c > 1) b = c * 7 + 3 + a; else b = c * 7 + 3; printf("%d %d\n", a, b + c * 7 + 3); return 0; }
X
check pre cfg,avail,pre,gcse same <<'X'
#include <stdio.h>
int main(int c, char **v) { int a = c + 5, x = 0, y; if (c > 1) x = c * 9 + 1; y = c * 9 + 1; printf("%d %d %d\n", a, x, y); return 0; }
X
check licm cfg,loops,licm,rd,live same <<'X'
#include <stdio.h>
int main(int c, char **v) { int i, k = c + 3, t, s = 0; for (i = 0; i < 20; i++) { t = k * 9 + 2; s += t + i * (k + 7); } printf("%d %d\n", s, t); return 0; }
X
check iv cfg,loops,iv same <<'X'
#include <stdio.h>
int a[64];
int main(int c, char **v) { int i; long s = 0; for (i = 0; i < 40; i += 3) { a[i] = i * 12; s += (long)i * 8 + c; } printf("%ld %d %d\n", s, a[9], i); return 0; }
X
check inline inline same <<'X'
#include <stdio.h>
static int sq(int x) { return x * x; }
static int mix(int a, int b) { return sq(a) + sq(b) + a * b; }
int main(int c, char **v) { printf("%d %d\n", mix(c, 3), mix(c + 1, c)); return 0; }
X
check regalloc regalloc smaller <<'X'
#include <stdio.h>
int main(int c, char **v) { int i, s = 0; for (i = 0; i < 1000; i++) s += i ^ c; printf("%d\n", s); return 0; }
X
check isel isel smaller <<'X'
#include <stdio.h>
int main(int c, char **v) { int i, s = 0; for (i = 0; i < 100; i++) { if (i > 5 && c < 9) s += i * 3; s = s + 7; } printf("%d\n", s); return 0; }
X
check layout layout smaller <<'X'
#include <stdio.h>
int main(int c, char **v) { int i = 0, s = 0; while (i < 50) { s += i; i++; } for (i = 0; i < c + 9; i++) s -= i; printf("%d\n", s); return 0; }
X
check sccp cfg,ssa,domtree,sccp,fold,algebra,compare,bool smaller <<'X'
#include <stdio.h>
int main(int c, char **v) { int k = 4, m, i, z = 0; if (k == 4) m = 10; else m = 20; while (z < 3) z++; for (i = 0; i < 5; i++) m += k; printf("%d %d\n", m + c, z); return 0; }
X
check adce cfg,ssa,domtree,adce smaller <<'X'
#include <stdio.h>
int main(int c, char **v) { int i, s = 0, dead = 0; for (i = 0; i < 10; i++) { dead = dead + i * 2; s += i; } printf("%d\n", s + c); return 0; }
X
check alias cfg,avail,gcse,alias same <<'X'
#include <stdio.h>
int a[16], b[16];
int main(int c, char **v) { int i = c, r; a[3] = 5; b[i] = 7; r = a[i + 2] * 3; b[1] = 9; r += a[i + 2] * 5; *(&a[0] + c) = 1; r += a[i + 2] * 7; printf("%d\n", r); return 0; }
X
check ipcp ipcp,fold,constprop smaller <<'X'
#include <stdio.h>
int scale(int v, int k) { if (k == 3) return v * 7; return v * k + 100; }
int main(int c, char **v) { printf("%d %d\n", scale(c, 3), scale(c + 1, 3)); return 0; }
X
check ipdce ipdce,inline smaller <<'X'
#include <stdio.h>
static int twice(int x) { return x * 2; }
static int unused_arg(int a, int b) { return a + 1; }
int main(int c, char **v) { printf("%d\n", unused_arg(c, c * 77) + twice(c)); return 0; }
X
check unroll unroll,fold same <<'X'
#include <stdio.h>
int main(int c, char **v) { int i, s = 0; for (i = 0; i < 6; i++) s += i * c; for (i = 20; i > 0; i -= 3) s ^= i; printf("%d %d\n", s, i); return 0; }
X
check unswitch unswitch same <<'X'
#include <stdio.h>
int main(int c, char **v) { int i, s = 0, f = c > 1; for (i = 0; i < 30; i++) { if (f) s += i; else s -= i; s ^= 3; } printf("%d\n", s); return 0; }
X
check sroa sroa,regalloc,fold,constprop smaller <<'X'
#include <stdio.h>
struct P { int x, y; };
int main(int c, char **v) { struct P p = {1, 2}; int t[3]; t[0] = c; t[1] = p.x + p.y; t[2] = t[0] + t[1]; p.x = t[2]; printf("%d %d\n", p.x, t[1]); return 0; }
X
check size size,inline,unroll same <<'X'
#include <stdio.h>
static int sq(int x) { return x * x; }
int main(int c, char **v) { int i, s = 0; for (i = 0; i < 4; i++) s += sq(i + c); printf("%d\n", s); return 0; }
X

# tail recursion: 20 million nested calls only work when the frame is reused
cat > "$TMP/tail.c" <<'X'
#include <stdio.h>
long sum(long n, long s) { if (n == 0) return s; return sum(n - 1, s + n); }
int main(void) { printf("%ld\n", sum(20000000L, 0)); return 0; }
X
$FCC -w- -O:tailrec -e"$TMP/tail" "$TMP/tail.c" > /dev/null 2>&1
if [ "$("$TMP/tail" 2>&1)" = "200000010000000" ]; then echo "ok   tailrec (20000000 calls in constant stack)"; else echo "FAIL tailrec"; fail=1; fi

# tail calls between different functions: only a reused frame survives 3 million nested calls
cat > "$TMP/tc.c" <<'X'
#include <stdio.h>
static long b(long n, long s);
static long a(long n, long s) { if (n == 0) return s; return b(n - 1, s + 1); }
static long b(long n, long s) { if (n == 0) return s; return a(n - 1, s + 2); }
int main(void) { printf("%ld\n", a(3000000L, 0)); return 0; }
X
$FCC -w- -O:tailcall,regalloc -e"$TMP/tc" "$TMP/tc.c" > /dev/null 2>&1
if [ "$("$TMP/tc" 2>&1)" = "4500000" ]; then echo "ok   tailcall (3000000 calls in constant stack)"; else echo "FAIL tailcall"; fail=1; fi

# accumulator introduction: n * f(n - 1) and f(n - 1) + n run in constant stack
cat > "$TMP/acc.c" <<'X'
#include <stdio.h>
int sumto(int n) { if (n == 0) return 0; return sumto(n - 1) + n; }
long fact(long n) { if (n <= 1) return 1; return n * fact(n - 1); }
int main(void) { printf("%d %ld\n", sumto(10000000), fact(15)); return 0; }
X
$FCC -w- -O:recur,tailrec -e"$TMP/acc" "$TMP/acc.c" > /dev/null 2>&1
if [ "$("$TMP/acc" 2>&1)" = "-2004260032 1307674368000" ]; then echo "ok   recur (10000000 nested calls in constant stack)"; else echo "FAIL recur: $("$TMP/acc" 2>&1)"; fail=1; fi

# profile-guided optimization: -Pg writes a profile, -Pu reads it and the output stays the same
cat > "$TMP/pg.c" <<'X'
#include <stdio.h>
static int hot(int x) { return x * 3 + 1; }
static int cold(int x) { int i, s = 0; for (i = 0; i < x; i++) s += i; return s; }
int main(void) { int i, s = 0; for (i = 0; i < 1000; i++) { if (i % 50 == 0) s += cold(3); else s += hot(i); } printf("%d\n", s); return 0; }
X
(cd "$TMP" && "$OLDPWD/$FCC" -w- -Pg -epg.gen pg.c > /dev/null 2>&1 && ./pg.gen > pg.want && "$OLDPWD/$FCC" -w- -O -Puprof.none -epg.nouse pg.c > /dev/null 2>&1)
ls "$TMP/fcprof.dat" > /dev/null 2>&1 && grep -q "^hot/F/0/E 980$" "$TMP/fcprof.dat" && echo "ok   pgo profile written (hot called 980 times)" || { echo "FAIL pgo profile"; fail=1; }
$FCC -w- -O -Pu"$TMP/fcprof.dat" -e"$TMP/pg.use" "$TMP/pg.c" > /dev/null 2>&1
if [ "$("$TMP/pg.use" 2>&1)" = "$(cat "$TMP/pg.want")" ]; then echo "ok   pgo (-Pu output equal)"; else echo "FAIL pgo -Pu"; fail=1; fi

rm -rf "$TMP"
[ $fail -eq 0 ] && echo "all optimization checks passed"
exit $fail
