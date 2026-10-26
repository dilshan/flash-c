#!/bin/sh
# Compiles every tests/cases/*.c with Flash C and with the host compiler, and compares the output.
cd "$(dirname "$0")/cases" || exit 1
FCC=../../build/fcc
TMP=${TMPDIR:-/tmp}/fc-tests.$$
mkdir -p "$TMP"
pass=0; fail=0
for f in *.c; do
    n=${f%.c}
    if ! $FCC -w- -e"$TMP/$n.fc" "$f" > "$TMP/$n.log" 2>&1; then
        echo "FAIL $n (compile)"; cat "$TMP/$n.log" | head -8; fail=$((fail+1)); continue
    fi
    if [ -f "$n.expected" ]; then cp "$n.expected" "$TMP/$n.want"
    else
        gcc -w -m64 -o "$TMP/$n.ref" "$f" -lm 2>/dev/null || { echo "SKIP $n (no reference)"; continue; }
        INP=/dev/null; [ -f "$n.in" ] && INP="$n.in"
        "$TMP/$n.ref" < "$INP" > "$TMP/$n.want" 2>&1
    fi
    INP=/dev/null; [ -f "$n.in" ] && INP="$n.in"
    "$TMP/$n.fc" < "$INP" > "$TMP/$n.got" 2>&1
    if cmp -s "$TMP/$n.want" "$TMP/$n.got"; then echo "ok   $n"; pass=$((pass+1));
    else echo "FAIL $n (output differs)"; diff "$TMP/$n.want" "$TMP/$n.got" | head -8; fail=$((fail+1)); fi
done
rm -rf "$TMP"
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
