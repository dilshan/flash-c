#!/usr/bin/env python3
"""End-to-end test of the Flash C source-level debugger: drives the real IDE through a pty."""
import os, sys, shutil, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tui import Session

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.environ.get('FC_BIN', os.path.join(ROOT, 'build', 'fc'))
work = tempfile.mkdtemp(prefix='fc-dbg-')
fails = []
CF2, CF3, CF8, SF7 = '\x1b[12;5~', '\x1b[13;5~', '\x1b[19;5~', '\x1b[18;2~'

def check(name, cond, s=None):
    print(('ok   ' if cond else 'FAIL ') + name)
    if not cond:
        fails.append(name)
        if s: s.show(name)

SRC = '''#include <stdio.h>
struct pt { int x, y; };
int total;
int add(int a, int b)
{
    int s = a + b;
    return s;
}
int main(void)
{
    int i;
    struct pt p;
    for (i = 0; i < 3; i++) {
        int k = i * 2;
        total += add(k, 1);
    }
    p.x = total;
    printf("total=%d\\n", p.x);
    return 0;
}
'''
open(os.path.join(work, 'dd.c'), 'w').write(SRC)
def text(s): return s.screen.text()
def goto(s, line):
    s.send('\x1b[1;5H')
    for _ in range(line - 1): s.send('DOWN')

try:
    s = Session([BIN, 'dd.c'], cwd=work, rows=30, cols=100, env={'ASAN_OPTIONS': 'detect_leaks=0'})
    check('file opened', 'DD.C' in text(s).upper(), s)
    goto(s, 15)
    s.send(CF8)
    row = [i for i, l in enumerate(text(s).split('\n')) if 'total += add' in l][0]
    check('Toggle breakpoint marks the line', s.screen.at[row][10] != s.screen.at[row - 1][10], s)
    s.send('CF7'.replace('CF7', '\x1b[18;5~')); s.type('total'); s.send('ENTER')
    check('watch added', 'total:' in text(s), s)

    s.send('CF9', wait=1.5)
    t = text(s)
    check('Run stops at the breakpoint (no program exit)', 'Program terminated' not in t and 'total:' in t, s)
    check('watch shows the live value', 'total: 0' in t, s)
    row2 = [i for i, l in enumerate(text(s).split('\n')) if 'total += add' in l][0]
    check('execution bar on the breakpoint line', s.screen.at[row2][10] != s.screen.at[row2 - 1][10], s)

    s.send('F7', wait=0.8)
    t = text(s)
    check('Trace into enters add()', 'int s = a + b;' in t and '═6:' in t, s)
    s.send(CF3, wait=0.5)
    t = text(s)
    check('Call stack lists add and main', 'Call Stack' in t and 'add(a=0, b=1)' in t and 'main()' in t, s)
    s.send('ESC')

    s.send('CF4'); s.type('a+b'); s.send('ENTER')
    check('Evaluate shows result', ' 1' in text(s).split('Result')[1][:200] if 'Result' in text(s) else False, s)
    s.send('ESC')

    s.send('F8', wait=0.6)
    s.send('F8', wait=0.6)
    t = text(s)
    check('Step over returns to the caller', 'total += add' in t, s)

    s.send('F8', wait=0.6)
    s.send(CF2)
    t = text(s)
    check('Program reset ends the session', 'total: Undefined symbol' in t, s)

    s.send('\x1b[1;5H')
    s.send('CF9'.replace('CF9', '\x1b[20;5~'), wait=1.5)
    t = text(s)
    check('Run again stops at the breakpoint', 'total += add' in t, s)
    goto(s, 15); s.send(CF8)      # clear the breakpoint
    goto(s, 18)
    s.send('F4', wait=1.5)
    t = text(s)
    check('Go to cursor runs to the line', 'total: 9' in t, s)
    s.send('\x1b[20;5~', wait=1.5)
    t = text(s)
    check('Run to the end shows the program output', 'total=9' in t and ('exited with code 0' in t or 'Program terminated' in t), s)
    s.send('x', wait=0.6)
    t = text(s)
    check('exit message', 'Program exit code: 0' in t or 'Program terminated' in t, s)
    s.send('ENTER')
    s.send('\x1bfq')
    s.pump(0.5)
finally:
    try: s.close()
    except Exception: pass
    shutil.rmtree(work, ignore_errors=True)

if fails:
    print('FAILED:', ', '.join(fails)); sys.exit(1)
print('all debugger checks passed')
