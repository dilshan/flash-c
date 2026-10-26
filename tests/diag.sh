#!/bin/sh
# Checks the text of the diagnostics produced by fcc for known-bad programs.
cd "$(dirname "$0")/diag" || exit 1
FCC=../../build/fcc
fail=0
for f in *.c; do
    n=${f%.c}
    $FCC -e/dev/null "$f" 2>&1 >/dev/null | grep -E '^(Error|Warning|Fatal|Linker)' > "/tmp/fc-diag.$$"
    if [ -f "$n.expected" ]; then
        if diff -u "$n.expected" "/tmp/fc-diag.$$" > /dev/null; then echo "ok   diag/$n"; else echo "FAIL diag/$n"; diff -u "$n.expected" "/tmp/fc-diag.$$" | head -20; fail=1; fi
    else cp "/tmp/fc-diag.$$" "$n.expected"; echo "new  diag/$n (expected output recorded)"; fi
done
rm -f "/tmp/fc-diag.$$"
exit $fail
