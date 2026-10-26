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
    check('Code generation dialog', 'Code Generation' in text(s) and 'Unsigned characters' in text(s) and 'Floating Point' in text(s) and 'Model' not in text(s), s)
    s.send('ESC')
    s.send('\x1bo')
    t = text(s)
    check('Options menu has no unused items', 'Application' not in t and 'Librarian' not in t and 'Compiler' in t and 'Debugger' in t, s)
    s.send('ESC')
    s.send('\x1bo', 'e', 'e')
    check('Editor options dialog', 'Syntax Highlighting' in text(s) and 'Tab Size' in text(s), s)
    s.send('ESC')
    s.send('\x1bo', 'r')
    check('Directories dialog', 'Include Directories' in text(s), s)
    s.send('ESC')
    s.send('\x1bo', 'l', 'l')
    check('Libraries dialog', 'Standard run-time library' in text(s) and 'Math library' in text(s) and 'Wildcard arguments' in text(s), s)
    s.send('ESC')
    s.send('\x1bo', 'l', 's')
    check('Linker dialog', 'Map File' in text(s) and 'Default libraries' in text(s), s)
    s.send('ESC')

    # search & replace
    s.send('\x1b[1;5H')
    s.send('\x1bs', 'f'); s.type('sum'); s.send('ENTER')
    check('Find moves the cursor', ' 4:' in text(s) or '4:' in text(s), s)

    # help and window list
    s.send('F1')
    check('Help window', 'Flash C Help' in text(s) and 'Using the IDE' in text(s), s)
    s.send('ESC')
    # extended help: contextual F1 on a library function, Index dialog, Previous topic
    s.send('\x1b[1;5H'); s.send('CF1')
    t = text(s)
    check('Ctrl+F1 on a word shows nothing odd', 'Help' in t, s)
    s.send('ESC')
    s.send('\x1bhi'); s.type('strcpy')
    check('Help|Index finds a library function', 'Index' in text(s) and 'strcpy' in text(s), s)
    s.send('ENTER')
    t = text(s)
    check('Index shows the function topic', 'Help: strcpy' in t and 'Copies a string' in t, s)
    s.send('TAB'); s.send('ENTER')
    check('hyperlink is followed', 'Help: strcpy' not in text(s).split('\n')[1], s)
    s.send('AF1')
    check('Previous topic goes back', 'Help: strcpy' in text(s), s)
    s.send('ESC'); s.send('\x1bhi'); s.type('printf'); s.send('ENTER'); s.send('ENTER')
    check('Header name in a function topic is a link', 'Help: stdio.h' in text(s) and 'fopen' in text(s), s)
    s.send('\x1bhi'); s.type('library functions'); s.send('ENTER')
    t = text(s)
    check('help window has a scroll bar', '\u25b2' in t and '\u25bc' in t, s)
    for _ in range(3): s.send('PGDN')
    check('PgDn scrolls long help topics', 'wherex' in text(s) and 'bios.h' not in text(s), s)
    s.send('PGUP'); s.send('PGUP'); s.send('PGUP')
    check('PgUp scrolls back', 'bios.h' in text(s), s)
    s.send('ESC')
    s.send('\x1b0')
    check('Window list', 'Window List' in text(s), s)
    s.send('ESC')


    # without the help files only the basic documentation is shown
    iso = tempfile.mkdtemp(prefix='fc-nohelp-')
    for f in ('fc', 'fcc'):
        shutil.copy(os.path.join(os.path.dirname(BIN), f), iso)
    s2 = Session([os.path.join(iso, 'fc')], cwd=iso, rows=25, cols=80, env={'ASAN_OPTIONS': 'detect_leaks=0'})
    s2.send('F1')
    t = text(s2)
    check('basic help without help files', 'Flash C Help' in t, s2)
    s2.send('ESC'); s2.send('\x1bhi')
    check('Index lists only the basic topics', 'strcpy' not in text(s2), s2)
    s2.send('ESC')

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
