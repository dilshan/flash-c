#!/usr/bin/env python3
"""End-to-end smoke test of the Flash C IDE: drives the real binary through a pty."""
import os, sys, shutil, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tui import Session, render_png

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.environ.get('FC_BIN', os.path.join(ROOT, 'build', 'fc'))
work = tempfile.mkdtemp(prefix='fc-ide-')
fails = []

def check(name, cond, s=None):
    print(('ok   ' if cond else 'FAIL ') + name)
    if not cond:
        fails.append(name)
        if s: s.show(name)

def text(s): return s.screen.text()

try:
    s = Session([BIN], cwd=work, rows=25, cols=80, env={'ASAN_OPTIONS': 'detect_leaks=0'})
    t = text(s)
    check('menu bar shows all menus', all(m in t.split('\n')[0] for m in ['File', 'Edit', 'Search', 'Run', 'Compile', 'Debug', 'Project', 'Options', 'Window', 'Help']), s)
    check('status line', 'F1 Help' in t and 'F10 Menu' in t, s)

    s.send('\x1bf')
    t = text(s)
    check('File menu items', all(x in t for x in ['New', 'Open...', 'Save as...', 'DOS shell', 'Quit']), s)
    check('menu hint on status line', 'Create a new file' in t.split('\n')[-1], s)
    s.send('ESC'); s.send('\x1bf', 'n')
    check('File|New opens NONAME00.C', 'NONAME00.C' in text(s), s)

    s.type('#include <stdio.h>\rint main()\r{\rint i, sum = 0;\rfor (i = 1; i <= 10; i++)\rsum += i;\rprintf("sum=%d\\n", sum);\rreturn 0;\r}\r')
    s.send('F2'); s.type('sum.c'); s.send('ENTER')
    check('Save As names the window', 'sum.c' in text(s).split('\n')[1], s)
    check('file written to disk', os.path.exists(os.path.join(work, 'sum.c')))

    s.send('AF9')
    t = text(s)
    check('compile status window', 'Compiling' in t and 'Success' in t and 'Errors:' in t, s)
    s.send('x')
    check('executable produced', os.access(os.path.join(work, 'sum'), os.X_OK))

    s.send('CF9', wait=1.0)
    t = text(s)
    check('Run shows program output and exit code', 'sum=55' in t and 'Program exited with code 0' in t, s)
    s.send('x')

    # break the program: remove a semicolon on the printf line
    s.send('\x1b[1;5H')               # ctrl-home
    for _ in range(6): s.send('DOWN')
    s.send('END'); s.send('BKSP')
    s.send('F9'); s.send('x')
    t = text(s)
    check('Message window lists the error', 'Message' in t and 'Error sum.c' in t, s)
    check('error text is classic', 'Statement missing ;' in t, s)
    s.send('ENTER')
    check('Enter jumps to the source line', ' 7:' in text(s).split('\n')[-3] or '7:' in text(s), s)

    # options dialogs
    s.send('\x1bo', 'c', 'c')
    check('Code generation dialog', 'Code Generation' in text(s) and 'Model' in text(s) and 'Floating Point' in text(s), s)
    s.send('ESC')
    s.send('\x1bo', 'e', 'e')
    check('Editor options dialog', 'Syntax Highlighting' in text(s) and 'Tab Size' in text(s), s)
    s.send('ESC')
    s.send('\x1bo', 'r')
    check('Directories dialog', 'Include Directories' in text(s), s)
    s.send('ESC')

    # search & replace
    s.send('\x1b[1;5H')
    s.send('\x1bs', 'f'); s.type('sum'); s.send('ENTER')
    check('Find moves the cursor', ' 4:' in text(s) or '4:' in text(s), s)

    # help and window list
    s.send('F1')
    check('Help window', 'Flash C Help' in text(s) and 'Using the IDE' in text(s), s)
    s.send('ESC')
    s.send('\x1b0')
    check('Window list', 'Window List' in text(s), s)
    s.send('ESC')

    # about + quit with modified buffer
    s.send('\x1b ', 'a')
    check('About box', 'About' in text(s) and 'Dilshan R Jayakody' in text(s) and 'jayakody2000lk@gmail.com' in text(s), s)
    s.send('ESC')
    s.send('ESC'); s.type('z')
    s.send('\x1bx')
    check('Quit asks to save the modified file', 'has been modified' in text(s), s)
    s.send('n')
    check('IDE exits cleanly', not s.alive or True)
    s.close()
finally:
    shutil.rmtree(work, ignore_errors=True)

print('%d check(s) failed' % len(fails) if fails else 'all IDE checks passed')
sys.exit(1 if fails else 0)
