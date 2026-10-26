#!/usr/bin/env python3
# File:    helpexamples.py
# Purpose: Compiles and runs every example program of help/language.fch (they are
#          what Edit|Copy example copies).
# Author:  Dilshan R Jayakody
# License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
# Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux

"""Compiles and runs every example program of help/language.fch (they are what Edit|Copy example copies)."""
import os, re, subprocess, sys, tempfile
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fcc = os.path.join(root, 'build', 'fcc')
text = open(os.path.join(root, 'help', 'language.fch')).read().split('\n')
examples, title, cur = [], None, None
for line in text:
    if line.startswith('@topic '): title = line[7:]
    elif line == '@example': cur = []
    elif line == '@end' and cur is not None: examples.append((title, '\n'.join(cur) + '\n')); cur = None
    elif cur is not None: cur.append(line[1:] if line.startswith('@@') else line)
fails = 0
tmp = tempfile.mkdtemp()
for title, src in examples:
    f = os.path.join(tmp, 'ex.c'); open(f, 'w').write(src)
    exe = os.path.join(tmp, 'ex')
    r = subprocess.run([fcc, '-w-', '-e' + exe, f], capture_output=True, text=True)
    if r.returncode:
        print('FAIL compile', title); print(r.stdout[-300:], r.stderr[-300:]); fails += 1; continue
    try:
        r = subprocess.run([exe, 'a', 'b'], capture_output=True, text=True, timeout=10, stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        print('FAIL timeout', title); fails += 1; continue
    if r.returncode not in (0, 1) or ('Segmentation' in r.stderr):
        print('FAIL run', title, r.returncode); fails += 1
    elif not r.stdout.strip():
        print('FAIL no output', title); fails += 1
over = [l for l in text if len(l) > 76 and not l.startswith((';;',))]
print('%d examples, %d failed; %d lines over 76 columns' % (len(examples), fails, len(over)))
for l in over[:5]: print('  ', len(l), l)
sys.exit(1 if fails or over else 0)
