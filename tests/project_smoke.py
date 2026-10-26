#!/usr/bin/env python3
# File:    project_smoke.py
# Purpose: End-to-end test of Project|Local options: command line switches, output path,
#          exclude from link and exclude debug information, driven through the real IDE.
# Author:  Dilshan R Jayakody
# License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
# Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux

import os, subprocess, sys, shutil, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tui import Session

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.environ.get('FC_BIN', os.path.join(ROOT, 'build', 'fc'))
work = tempfile.mkdtemp(prefix='fc-proj-')
fails = []

def check(name, cond, s=None):
    print(('ok   ' if cond else 'FAIL ') + name)
    if not cond:
        fails.append(name)
        if s: s.show(name)

def write(name, text):
    with open(os.path.join(work, name), 'w') as f: f.write(text)

try:
    write('main.c', '#include <stdio.h>\nint helper(void);\nint main(void)\n{\n    printf("value=%d helper=%d\\n", VALUE, helper());\n    return 0;\n}\n')
    write('helper.c', 'int helper(void) { return 42; }\n')
    write('broken.c', 'this is not C at all\n')
    # settings of an item follow its name, indented
    write('demo.fcp', '# Flash C project\nmain.c\n\tcmd=-DVALUE=7 -O\n\tout=build/out/prog\nhelper.c\nbroken.c\n\tnolink=1\n')

    s = Session([BIN], cwd=work, rows=25, cols=80, env={'ASAN_OPTIONS': 'detect_leaks=0'})
    s.send('\x1bp', 'o'); s.type('demo.fcp'); s.send('ENTER')
    s.send('F9'); s.send('x')
    prog = os.path.join(work, 'build', 'out', 'prog')
    check('executable is written to the Output Path (new directories are created)', os.access(prog, os.X_OK), s)
    check('the default location is not used', not os.path.exists(os.path.join(work, 'demo')))
    out = subprocess.run([prog], capture_output=True, text=True).stdout if os.path.exists(prog) else ''
    check('command line options of the item reach the compiler (-DVALUE=7)', 'value=7 helper=42' in out, s)
    check('an item excluded from the link is not compiled (its errors do not stop the build)', 'Error' not in s.screen.text() and os.path.exists(prog), s)

    # the settings are kept in the project file and shown again by the dialog
    s.send('\x1bp', 'l')
    t = s.screen.text()
    check('Local Options dialog shows the stored settings', 'Local Options' in t and '-DVALUE=7' in t and 'build/out/prog' in t, s)
    check('Local Options has no overlay option', 'Overlay' not in t, s)
    s.send('ESC')

    # changing and saving through the dialog
    s.send('\x1bp', 'l'); s.send('TAB'); s.send('TAB'); s.send(' '); s.send('TAB'); s.send('TAB'); s.send('ENTER')
    saved = open(os.path.join(work, 'demo.fcp')).read()
    check('changes made in the dialog are saved in the project file', 'nodebug=1' in saved and 'cmd=-DVALUE=7 -O' in saved, s)
    s.close()
finally:
    shutil.rmtree(work, ignore_errors=True)

if fails:
    print('%d check(s) failed' % len(fails)); sys.exit(1)
print('all project checks passed')
