#!/bin/sh
# File:    options.sh
# Purpose: Checks the code-generation / linker options of fcc by compiling one
#          program with different switches.
# Author:  Dilshan R Jayakody
# License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
# Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux

cd "$(dirname "$0")/options" || exit 1
FCC=../../build/fcc
TMP=${TMPDIR:-/tmp}/fc-opt.$$
mkdir -p "$TMP"
fail=0
ok()   { echo "ok   options/$1"; }
bad()  { echo "FAIL options/$1"; fail=1; }
run()  { $FCC -w- "$@" -e"$TMP/p" opt.c > "$TMP/log" 2>&1 || { cat "$TMP/log"; return 1; }; "$TMP/p" ${ARG:+$ARG}; }

run > "$TMP/base" || bad compile
grep -q '^enum 4 4 4' "$TMP/base" && grep -q '^struct 24' "$TMP/base" && grep -q 'strings separate' "$TMP/base" && ok defaults || bad defaults

run -b- | grep -q '^enum 1 4 1' && ok enums-narrow || bad enums-narrow
run -d | grep -q 'strings merged' && ok merge-strings || bad merge-strings
run -a- | grep -q '^struct 14' && ok pack-structs || bad pack-structs

# the optimizer must not change behaviour
for f in "-O" "-Z" "-O -Z" "-O -Z -lw -lt -v"; do
    run $f > "$TMP/out"
    if diff -q "$TMP/base" "$TMP/out" > /dev/null; then ok "same-output ($f)"; else bad "same-output ($f)"; fi
done
$FCC -w- -e"$TMP/p0" opt.c > /dev/null; $FCC -w- -O -Z -e"$TMP/p1" opt.c > /dev/null
[ "$(wc -c < "$TMP/p1")" -lt "$(wc -c < "$TMP/p0")" ] && ok optimized-smaller || bad optimized-smaller

# stack overflow test and stack size
$FCC -w- -N -ls256 -e"$TMP/p" opt.c > /dev/null
"$TMP/p" 100 | grep -q '^depth' && ok stack-check-fits || bad stack-check-fits
"$TMP/p" 3000000 > "$TMP/out" 2>&1; rc=$?
{ grep -q 'Stack overflow!' "$TMP/out" && [ $rc -eq 1 ]; } && ok stack-overflow || bad stack-overflow

# ELF layout (needs binutils)
if command -v readelf > /dev/null; then
    $FCC -w- -lw -lt -e"$TMP/p" opt.c > /dev/null
    readelf -lW "$TMP/p" | grep LOAD | grep -q ' R E ' && readelf -lW "$TMP/p" | grep LOAD | grep -q ' RW ' && ok wx-segments || bad wx-segments
    readelf -lW "$TMP/p" | grep LOAD | grep -q 'RWE' && bad wx-no-rwx || ok wx-no-rwx
    if command -v nm > /dev/null; then nm "$TMP/p" | grep -q ' T main$' && ok symtab-main || bad symtab-main; fi
    $FCC -w- -e"$TMP/p" opt.c > /dev/null
    readelf -SW "$TMP/p" 2>&1 | grep -q '\.symtab' && bad no-symtab-by-default || ok no-symtab-by-default
fi
rm -rf "$TMP"
exit $fail
