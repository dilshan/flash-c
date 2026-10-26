#!/usr/bin/env python3
# File:    extract_libs.py
# Purpose: Static study of the reference run-time libraries (OMF .LIB archives
#          and .OBJ start-up files).
# Author:  Dilshan R Jayakody
# License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
# Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux

"""Static study of the reference run-time libraries (OMF .LIB archives and .OBJ start-up files).

Usage:  python3 -I extract_libs.py /path/to/TC  [outdir]

Parses the Intel OMF records of  LIB/*.LIB  and  LIB/*.OBJ  (no execution, no disassembly) and
writes plain-text dumps:
  lib_inventory.txt   one line per library: size, page size, module count, public symbol count
  lib_modules_<X>.txt every module of one library with its segments, publics and externals
  lib_dependencies.txt  module -> modules it pulls in (derived from EXTDEF/PUBDEF) for the small model
  startup_objects.txt   the C0x.OBJ start-up modules: segments, publics, externals, comments
  headers_inventory.txt every public header with the function prototypes it declares
Brand names are replaced by <brand>.
"""
import os, re, struct, sys, glob, collections

SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/research/TC')
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dumps')
os.makedirs(OUT, exist_ok=True)
BRAND = re.compile('(' + 'Bor' + 'land' + '|' + 'Tur' + 'bo' + r'(?:\s+(?:C\+\+|C|Vision|Help|Debugger|Profiler|Assembler|Librarian))?' + ')', re.I)
def redact(s): return BRAND.sub('<brand>', s)

RT = {0x80: 'THEADR', 0x88: 'COMENT', 0x8A: 'MODEND', 0x8B: 'MODEND', 0x8C: 'EXTDEF', 0x90: 'PUBDEF', 0x91: 'PUBDEF',
      0x96: 'LNAMES', 0x98: 'SEGDEF', 0x99: 'SEGDEF', 0x9A: 'GRPDEF', 0x9C: 'FIXUPP', 0x9D: 'FIXUPP',
      0xA0: 'LEDATA', 0xA1: 'LEDATA', 0xA2: 'LIDATA', 0xB0: 'COMDEF', 0xB4: 'LEXTDEF', 0xB6: 'LPUBDEF', 0xF0: 'LIBHDR', 0xF1: 'LIBEND'}

def pstr(b, p):
    n = b[p]
    return b[p + 1:p + 1 + n].decode('latin1'), p + 1 + n

def index(b, p):
    v = b[p]
    if v & 0x80: return ((v & 0x7f) << 8) | b[p + 1], p + 2
    return v, p + 1

class Module:
    def __init__(self):
        self.name = ''; self.lnames = []; self.segs = []; self.groups = []
        self.pub = []; self.ext = []; self.comments = []; self.datasize = 0; self.fixups = 0

def parse_module(d, pos):
    """parse records from pos up to and including MODEND; returns (Module, endpos)"""
    m = Module()
    while pos < len(d):
        t = d[pos]; ln = struct.unpack_from('<H', d, pos + 1)[0]
        body = d[pos + 3:pos + 3 + ln - 1]
        pos += 3 + ln
        wide = t & 1 if t in (0x91, 0x99, 0xA1, 0x9D, 0x8B) else 0
        k = RT.get(t)
        if k == 'THEADR': m.name = pstr(body, 0)[0]
        elif k == 'LNAMES':
            p = 0
            while p < len(body):
                s, p = pstr(body, p); m.lnames.append(s)
        elif k == 'SEGDEF':
            p = 0; acbp = body[p]; p += 1
            if (acbp >> 5) == 0: p += 3
            seglen = struct.unpack_from('<I' if wide else '<H', body + b'\0\0', p)[0]; p += 4 if wide else 2
            sn, p = index(body, p); cn, p = index(body, p)
            nm = m.lnames[sn - 1] if 0 < sn <= len(m.lnames) else '?'
            cl = m.lnames[cn - 1] if 0 < cn <= len(m.lnames) else '?'
            m.segs.append((nm, cl, seglen, (acbp >> 2) & 7))
        elif k == 'GRPDEF':
            gn, _ = index(body, 0); m.groups.append(m.lnames[gn - 1] if 0 < gn <= len(m.lnames) else '?')
        elif k == 'PUBDEF':
            p = 0; g, p = index(body, p); s, p = index(body, p)
            if g == 0 and s == 0: p += 2
            while p < len(body):
                n, p = pstr(body, p); p += 4 if wide else 2; _, p = index(body, p)
                m.pub.append(n)
        elif k == 'EXTDEF':
            p = 0
            while p < len(body):
                n, p = pstr(body, p); _, p = index(body, p); m.ext.append(n)
        elif k == 'COMDEF':
            p = 0
            while p < len(body):
                n, p = pstr(body, p); _, p = index(body, p); m.ext.append(n + ' (communal)')
                tt = body[p]; p += 1
                if tt == 0x61:
                    for _ in range(2):
                        v = body[p]; p += 1
                        if v == 0x81: p += 2
                        elif v == 0x84: p += 3
                        elif v == 0x88: p += 4
                else:
                    v = body[p]; p += 1
                    if v == 0x81: p += 2
                    elif v == 0x84: p += 3
                    elif v == 0x88: p += 4
        elif k == 'COMENT':
            cls = body[1] if len(body) > 1 else 0
            txt = body[2:]
            if cls in (0xA1, 0xA4, 0x9F, 0x9E, 0xE9, 0xA0): pass
            m.comments.append((cls, redact(''.join(chr(c) if 32 <= c < 127 else '.' for c in txt))))
        elif k == 'LEDATA':
            m.datasize += len(body)
        elif k == 'FIXUPP': m.fixups += 1
        if k == 'MODEND': break
    return m, pos

def parse_lib(path):
    d = open(path, 'rb').read()
    if d[0] != 0xF0: return None
    ln = struct.unpack_from('<H', d, 1)[0]
    page = ln + 3
    dict_off, dict_blocks = struct.unpack_from('<IH', d, 3)
    mods = []; pos = page
    while pos < dict_off:
        m, end = parse_module(d, pos)
        m.offset = pos; m.size = end - pos
        mods.append(m)
        pos = (end + page - 1) // page * page
        if pos < len(d) and d[pos] == 0xF1: break
    return d, page, dict_off, dict_blocks, mods

lib_files = sorted(glob.glob(os.path.join(SRC, 'LIB', '*.LIB')))
inv = ['%-12s %8s %6s %8s %8s %8s' % ('library', 'bytes', 'page', 'modules', 'publics', 'code B')]
alls = {}
for f in lib_files:
    nm = os.path.basename(f)
    r = parse_lib(f)
    if not r: continue
    d, page, doff, dblk, mods = r
    alls[nm] = mods
    npub = sum(len(m.pub) for m in mods)
    code = sum(m.datasize for m in mods)
    inv.append('%-12s %8d %6d %8d %8d %8d' % (nm, len(d), page, len(mods), npub, code))
    out = ['library %s: %d bytes, page size %d, dictionary at 0x%x (%d blocks), %d modules' % (nm, len(d), page, doff, dblk, len(mods)), '']
    for m in mods:
        out.append('MODULE %s   (lib offset 0x%x, %d bytes, %d data bytes, %d fixup records)' % (m.name, m.offset, m.size, m.datasize, m.fixups))
        if m.segs: out.append('  segments : ' + ', '.join('%s/%s:%d' % (s[0], s[1], s[2]) for s in m.segs))
        if m.groups: out.append('  groups   : ' + ', '.join(m.groups))
        if m.pub: out.append('  publics  : ' + ' '.join(m.pub))
        if m.ext: out.append('  externals: ' + ' '.join(m.ext))
        out.append('')
    open(os.path.join(OUT, 'lib_modules_%s.txt' % nm[:-4]), 'w').write(redact('\n'.join(out)))
open(os.path.join(OUT, 'lib_inventory.txt'), 'w').write('\n'.join(inv) + '\n')
print('\n'.join(inv))

# ------------------------------------------------------------ dependency view for the small model
dep = []
for libname in ('CS.LIB', 'MATHS.LIB'):
    mods = alls.get(libname, [])
    owner = {}
    for m in mods:
        for p in m.pub: owner[p] = m.name
    dep.append('== %s: module -> modules referenced (external symbol resolved inside the same library) ==' % libname)
    for m in mods:
        refs = sorted({owner[e] for e in m.ext if e in owner and owner[e] != m.name})
        unres = sorted(e for e in m.ext if e not in owner)
        dep.append('%-12s -> %s' % (m.name, ' '.join(refs) or '-') + ('    [outside: %s]' % ' '.join(unres) if unres else ''))
    dep.append('')
open(os.path.join(OUT, 'lib_dependencies.txt'), 'w').write(redact('\n'.join(dep)))

# ------------------------------------------------------------ start-up objects
st = []
for f in sorted(glob.glob(os.path.join(SRC, 'LIB', '*.OBJ'))):
    d = open(f, 'rb').read()
    m, _ = parse_module(d, 0)
    st.append('OBJECT %s  (%d bytes) module "%s"' % (os.path.basename(f), len(d), m.name))
    st.append('  segments : ' + ', '.join('%s/%s:%d' % (s[0], s[1], s[2]) for s in m.segs))
    st.append('  groups   : ' + ', '.join(m.groups))
    st.append('  publics  : ' + ' '.join(m.pub))
    st.append('  externals: ' + ' '.join(m.ext))
    for c in m.comments:
        if len(c[1].strip('.')) > 3: st.append('  comment %02X: %s' % c)
    st.append('')
open(os.path.join(OUT, 'startup_objects.txt'), 'w').write(redact('\n'.join(st)))

# ------------------------------------------------------------ headers: declared functions
hd = []
proto = re.compile(r'^\s*(?:extern\s+)?[A-Za-z_][\w\s\*]*?\b(_?[A-Za-z]\w*)\s*\(', re.M)
for h in sorted(glob.glob(os.path.join(SRC, 'INCLUDE', '*.H')) + glob.glob(os.path.join(SRC, 'INCLUDE', 'SYS', '*.H'))):
    t = open(h, encoding='latin1').read()
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
    names = []
    for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\)?\s*\(\s*(?:[^()]|\([^()]*\))*\)\s*;', t):
        n = m.group(1)
        if n not in ('defined', 'sizeof', 'void', 'if', 'while', 'return') and n not in names: names.append(n)
    hd.append('%-14s %4d lines  %3d declarations: %s' % (h[len(SRC) + 9:], t.count('\n'), len(names), ' '.join(names)))
open(os.path.join(OUT, 'headers_inventory.txt'), 'w').write(redact('\n'.join(hd)) + '\n')
print('wrote', OUT)
