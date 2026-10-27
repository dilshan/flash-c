#!/usr/bin/env python3
# File:    screenshots.py
# Purpose: Regenerates the pictures in docs/screenshots by driving the real IDE through a pty.
#          Usage: tools/screenshots.py [output-dir]   (needs build/fc and the Pillow package)
# Author:  Dilshan R Jayakody
# License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
# Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux

"""Regenerates docs/screenshots/*.png from the current build of the IDE."""
import os, sys, shutil, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tests'))
from tui import Session, render_png

BIN = os.environ.get('FC_BIN', os.path.join(ROOT, 'build', 'fc'))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'docs', 'screenshots')
os.makedirs(OUT, exist_ok=True)

DEMO = '''#include <stdio.h>
#include <math.h>

/* Area of a circle */
double area(double r)
{
    return M_PI * r * r;
}

int main()
{
    int i;
    for (i = 1; i <= 3; i++)
        printf("r=%d area=%.2f\\n", i, area(i));
    return 0;
}
'''

BROKEN = '''#include <stdio.h>

int main()
{
    int count = 3;
    printf("count=%d\\n", cnt);
    return 0
}
'''

DEBUG = '''#include <stdio.h>

int total;

int add(int a, int b)
{
    int s = a + b;
    return s;
}

int main(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        int k = i * 2;
        total += add(k, 1);
    }
    printf("total=%d\\n", total);
    return 0;
}
'''

CF8, CF7, F7 = '\x1b[19;5~', '\x1b[18;5~', 'F7'
work = tempfile.mkdtemp(prefix='fc-shots-')
for name, src in (('demo.c', DEMO), ('broken.c', BROKEN), ('sum.c', DEBUG)):
    open(os.path.join(work, name), 'w').write(src)
env = {'ASAN_OPTIONS': 'detect_leaks=0', 'HOME': work}


def shot(s, name):
    render_png(s.screen, os.path.join(OUT, name + '.png'))
    print('wrote', name + '.png')


def ide(*files):
    return Session([BIN, *files], cwd=work, rows=25, cols=80, env=env)


try:
    # the editor
    s = ide('demo.c')
    shot(s, 'editor')
    s.send('\x1bc')
    shot(s, 'compile-menu')
    s.send('ESC')
    s.close()

    # error messages
    s = ide('broken.c')
    s.send('F9', wait=1.0)
    s.send(' ', wait=0.5)
    shot(s, 'messages')
    s.close()

    # option dialogs
    s = ide('demo.c')
    s.send('\x1bo', 'c', 'c')
    shot(s, 'code-generation-dialog')
    s.send('ESC')
    s.send('\x1bo', 'c', 'o')
    s.send('DOWN', 'DOWN', 'DOWN')
    shot(s, 'optimization-dialog')
    s.send('F1', wait=0.6)
    shot(s, 'optimization-help')
    s.close()

    # the help system
    s = ide('demo.c')
    s.send('F1', wait=0.6)
    shot(s, 'help')
    s.close()

    # the source-level debugger: a breakpoint, a watch and the current statement
    s = ide('sum.c')
    s.send('\x1b[1;5H')
    for _ in range(15):
        s.send('DOWN')
    s.send(CF8)
    s.send(CF7)
    s.type('total')
    s.send('ENTER')
    s.send('CF9', wait=1.5)
    s.send(F7, wait=0.6)
    s.send(F7, wait=0.6)
    shot(s, 'debugger')
    s.close()
finally:
    shutil.rmtree(work, ignore_errors=True)
