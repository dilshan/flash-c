#!/usr/bin/env python3
"""Reproducible evidence extraction from the reference IDE binaries.

Usage:  python3 -I extract_evidence.py /path/to/TC/BIN  [outdir]

Reads TC.EXE (IDE), TCC.EXE (command-line compiler) and TCCONFIG.TC (settings) and writes plain-text
dumps that back the statements made in EVIDENCE.md.  Brand names of the reference product are
replaced by <brand> in every dump, so the files can sit next to the Flash C sources.
"""
import os, re, struct, sys

SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/research/TC/BIN')
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dumps')
os.makedirs(OUT, exist_ok=True)
BRAND = re.compile('(' + 'Bor' + 'land' + '|' + 'Tur' + 'bo' + r'(?:\s+(?:C\+\+|C|Vision|Help|Debugger|Profiler|Assembler|Librarian))?' + ')', re.I)
def redact(s): return BRAND.sub('<brand>', s)

def rd(name): return open(os.path.join(SRC, name), 'rb').read()

def ascii_strings(data, minlen=4):
    return [m.group().decode('latin1') for m in re.finditer(rb'[\x20-\x7e\t]{%d,}' % minlen, data)]

# ---------------------------------------------------------------- NE header / segment table
def ne_report(name):
    d = rd(name)
    lines = ['file: %s  size: %d' % (name, len(d)), 'MZ signature: %r' % d[:2]]
    o = struct.unpack_from('<I', d, 0x3c)[0]
    lines.append('e_lfanew: 0x%x  new-header signature: %r' % (o, d[o:o + 2]))
    if d[o:o + 2] != b'NE':
        return lines
    h = d[o:]
    (_, ld, lv, entoff, entlen, crc, flags, autods, heap, stack, csip, sssp, nseg, nmod, nonres, segoff, resoff,
     resnoff, modoff, impoff, nonresoff, nmov, shift, nres, os_) = struct.unpack_from('<2sBBHHIHHHHIIHHHHHHHHIHHHB', h, 0)
    lines += ['linker version: %d.%d' % (ld, lv), 'flags: 0x%x (protected mode: %s)' % (flags, bool(flags & 0x8)),
              'target OS byte: %d (5 = DOS 4.x / extender)' % os_, 'segments: %d  alignment shift: %d' % (nseg, shift),
              'entry CS:IP = 0x%08x   SS:SP = 0x%08x' % (csip, sssp)]
    nc = nd = 0; csz = dsz = 0
    seglist = []
    for i in range(nseg):
        so, sl, sf, sm = struct.unpack_from('<HHHH', h, segoff + i * 8)
        isdata = sf & 1
        if isdata: nd += 1; dsz += sl or 65536
        else: nc += 1; csz += sl or 65536
        seglist.append('%3d  file-offset 0x%06x  length %5d  flags 0x%04x  %s' % (i + 1, so << shift, sl or 65536, sf, 'DATA' if isdata else 'CODE'))
    lines += ['code segments: %d (%d bytes)   data segments: %d (%d bytes)' % (nc, csz, nd, dsz), '']
    lines += seglist
    p = nonresoff
    n = d[p]
    lines += ['', 'non-resident name table: %r' % redact(d[p + 1:p + 1 + n].decode('latin1'))]
    return lines

for nm in ('TC.EXE', 'TCC.EXE'):
    open(os.path.join(OUT, nm.lower().replace('.exe', '') + '_ne_header.txt'), 'w').write('\n'.join(ne_report(nm)) + '\n')

# ---------------------------------------------------------------- strings
tc = [redact(s) for s in ascii_strings(rd('TC.EXE'))]
tcc = [redact(s) for s in ascii_strings(rd('TCC.EXE'))]
open(os.path.join(OUT, 'tc_strings_all.txt'), 'w').write('\n'.join(tc) + '\n')
open(os.path.join(OUT, 'tcc_strings_all.txt'), 'w').write('\n'.join(tcc) + '\n')

def between(lst, a, b, inclusive_b=True):
    i = next(k for k, s in enumerate(lst) if s.startswith(a))
    j = next(k for k in range(i, len(lst)) if lst[k].startswith(b))
    return lst[i:j + (1 if inclusive_b else 0)]

sections = {
    'menus_items.txt': ('Open...      F3', ' Help '),                 # every pulldown item with its shortcut, then the bar titles
    'dialog_controls.txt': ('Break Make On', ' Information '),        # option dialog labels (~x~ marks the hot key)
    'dialog_search_etc.txt': ('~Files', 'Include files for'),
    'color_groups_items.txt': ('Unused', 'Illegal Char'),             # Options|Environment|Colors lists
    'statusline_and_progress.txt': (' Help ', ' Inspect'),
    'hint_texts.txt': ('System commands and Transfer programs', 'Force virtual tables to be public'),
}
for fn, (a, b) in sections.items():
    try:
        sec = between(tc, a, b)
    except StopIteration:
        sec = between(tc, a, tc[-1])
    open(os.path.join(OUT, fn), 'w').write('\n'.join(sec) + '\n')

# compile / link pipeline strings
pipe_keys = re.compile(r'Compiling|Linking|Building|Making|Checking dependencies|Lines compiled|Linker|Fatal|Warning|Error|PASS|Success|up to date|Press any key|\.OBJ|\.EXE|\.MAP|\.LIB|\.ASM|\.PRJ|\.DSK|\.DPR|TCCONFIG|TLINK|TASM|%f %n|exit code', re.I)
open(os.path.join(OUT, 'pipeline_strings.txt'), 'w').write('\n'.join(s for s in tc if pipe_keys.search(s) and len(s) < 90) + '\n')
usage = between(tcc, 'Syntax is:', 'operator=')
open(os.path.join(OUT, 'tcc_usage_and_options.txt'), 'w').write('\n'.join(usage) + '\n')
msgs = [s for s in tcc if re.match(r"^[A-Z'].{8,70}$", s) and not s.startswith('Syntax')]
open(os.path.join(OUT, 'tcc_diagnostic_samples.txt'), 'w').write('\n'.join(m for m in msgs if re.search(r'syntax|Undefined|Unterminated|Statement missing|prototype|Declaration|Too (many|few)|Lvalue|Type mismatch|Cannot (convert|cast)|should return|never used|no effect|Unreachable', m, re.I)) + '\n')

# ---------------------------------------------------------------- settings file + colour bytes
cfg = rd('TCCONFIG.TC')
pal = cfg[0x2c28:0x2c28 + 0x40]
lines = ['file: TCCONFIG.TC  size %d' % len(cfg), 'header text: %r' % redact(cfg[:cfg.index(b'\x1a')].decode('latin1')),
         '', 'attribute bytes found at 0x2c28 (bg<<4 | fg, CGA colour numbers):', ' '.join('%02x' % b for b in pal), '',
         'decoded:']
names = ['black', 'blue', 'green', 'cyan', 'red', 'magenta', 'brown', 'lightgray', 'darkgray', 'lightblue', 'lightgreen', 'lightcyan', 'lightred', 'lightmagenta', 'yellow', 'white']
for b in pal[:48]:
    lines.append('  0x%02x = %s on %s' % (b, names[b & 15], names[(b >> 4) & 7]))
open(os.path.join(OUT, 'config_palette.txt'), 'w').write('\n'.join(lines) + '\n')
print('wrote dumps to', OUT)
