#!/bin/sh
# File:    libtest.sh
# Purpose: Tests of the run-time library build options: link map, library
#          selection, start-up/exit pragmas, wildcard arguments.
# Author:  Dilshan R Jayakody
# License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
# Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux

cd "$(dirname "$0")" || exit 1
FCC=$(pwd)/../build/fcc
T=${TMPDIR:-/tmp}/fc-lib.$$
mkdir -p "$T" && cd "$T" || exit 1
fail=0
check() { if [ "$2" = "$3" ]; then echo "ok   lib/$1"; else echo "FAIL lib/$1: want [$3] got [$2]"; fail=1; fi; }

# --- hello world with a link map: only the functions that are used are linked
cat > hello.c <<'EOF'
#include <stdio.h>
int main(void) { printf("hi\n"); return 0; }
EOF
$FCC -w- -Mp hello.c > /dev/null 2>&1
check "map file written" "$([ -f hello.map ] && echo yes)" yes
check "map has printf" "$(grep -c '_printf' hello.map | sed 's/^[1-9].*/some/')" some
check "map omits unused strtol" "$(grep -c '_strtol' hello.map)" 0
check "map omits math" "$(grep -c 'libm' hello.map)" 0
check "hello runs" "$(./hello)" hi

# --- #pragma startup / exit with priorities
cat > prag.c <<'EOF'
#include <stdio.h>
static void a(void) { puts("start 20"); }
static void b(void) { puts("start 5"); }
static void x(void) { puts("exit 20"); }
static void y(void) { puts("exit 5"); }
#pragma startup a 20
#pragma startup b 5
#pragma exit x 20
#pragma exit y 5
int main(void) { puts("main"); return 0; }
EOF
$FCC -w- prag.c > /dev/null 2>&1
check "pragma order" "$(./prag | tr '\n' ',')" "start 5,start 20,main,exit 20,exit 5,"

# --- no standard library: the printf reference cannot be resolved
$FCC -w- -ln hello.c 2>&1 | grep -q "Undefined symbol" && r=yes || r=no
check "-ln leaves printf undefined" "$r" yes

# --- start-up code only
cat > bare.c <<'EOF'
int main(void) { return 7; }
EOF
$FCC -w- -ln bare.c > /dev/null 2>&1; ./bare; check "-ln program without library calls" "$?" 7

# --- floating point: formats are linked only when needed
cat > fp.c <<'EOF'
#include <stdio.h>
int main(void) { double d = 1.5; printf("%.1f\n", d * 2); return 0; }
EOF
$FCC -w- fp.c > /dev/null 2>&1; check "floating point works" "$(./fp)" "3.0"
$FCC -w- -Mp fp.c > /dev/null 2>&1; check "fp map shows the float converter" "$(grep -c '___realcvt' fp.map | sed 's/^[1-9].*/some/')" some
$FCC -w- -lnm fp.c > /dev/null 2>&1; ./fp > fp.out 2>&1; check "no math library -> format error" "$(head -1 fp.out)" "printf : floating point formats not linked"
$FCC -w- -f- fp.c > /dev/null 2>&1; ./fp > fp.out 2>&1; check "-f- -> format error" "$(head -1 fp.out)" "printf : floating point formats not linked"

# --- wildcard arguments
cat > wild.c <<'EOF'
#include <stdio.h>
int main(int argc, char **argv) { int i; for (i = 1; i < argc; i++) printf("%s ", argv[i]); printf("\n"); return 0; }
EOF
mkdir w && : > w/a.txt && : > w/b.txt && : > w/c.dat
$FCC -w- wild.c > /dev/null 2>&1; check "no wildargs" "$(./wild 'w/*.txt')" "w/*.txt "
$FCC -w- wild.c wildargs.obj > /dev/null 2>&1; check "wildargs expands" "$(./wild 'w/*.txt')" "w/a.txt w/b.txt "
check "wildargs keeps plain" "$(./wild plain 'nomatch*')" "plain nomatch* "

# --- exit codes, abort, atexit
cat > ex.c <<'EOF'
#include <stdlib.h>
#include <stdio.h>
static void bye(void) { puts("bye"); }
int main(int argc, char **argv) { atexit(bye); if (argc > 1) abort(); exit(4); }
EOF
$FCC -w- ex.c > /dev/null 2>&1
./ex > ex.out 2>&1; check "exit code" "$?" 4
check "atexit runs" "$(cat ex.out)" bye
./ex x > ex.out 2>&1; check "abort exit code" "$?" 3
check "abort message" "$(head -1 ex.out)" "Abnormal program termination"

# --- environment and command line
cat > env.c <<'EOF'
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv, char **envp) { printf("%d %s %d\n", argc, getenv("FC_PROBE"), envp[0] != 0); return 0; }
EOF
$FCC -w- env.c > /dev/null 2>&1; check "argc, getenv, envp" "$(FC_PROBE=zz ./env a b)" "3 zz 1"

# --- every public header compiles on its own
for h in assert conio ctype dir direct dirent dos errno fcntl float io limits locale malloc math mem memory process search setjmp share signal stdarg stddef stdio stdlib string sys/stat sys/timeb sys/types time utime values alloc bios locking stdbool; do
    printf '#include <%s.h>\nint main(void) { return 0; }\n' "$h" > hdr.c
    $FCC -w- hdr.c > hdr.log 2>&1 || { echo "FAIL lib/header $h"; head -3 hdr.log; fail=1; }
done
echo "ok   lib/headers compile"

cd /; rm -rf "$T"
exit $fail
