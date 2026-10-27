#!/usr/bin/env python3
# File:    optfuzz.py
# Purpose: Differential test of the optimizer: random C programs are compiled without and with
#          optimizations and must print the same output.
# Author:  Dilshan R Jayakody
# License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
# Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
#
# usage: optfuzz.py [count] [seed]      (FC_BIN_DIR selects the directory with fcc)
import os, random, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FCC = os.path.join(os.environ.get('FC_BIN_DIR', os.path.join(ROOT, 'build')), 'fcc')

TYPES = ['signed char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned',
         'long', 'unsigned long']
CONSTS = ['0', '1', '2', '3', '4', '5', '7', '8', '16', '100', '255', '256', '-1', '-2', '65535',
          '2147483647', '12345', '0x7f', '3000000000u']


class Gen:
    def __init__(self, rnd):
        self.r = rnd
        self.vars = []          # (name, type)
        self.loopvars = 0
        self.labels = 0
        self.depth_limit = 4

    def var(self):
        return self.r.choice(self.vars)[0]

    def const(self):
        return self.r.choice(CONSTS)

    def atom(self):
        k = self.r.random()
        if k < 0.45:
            return self.var()
        if k < 0.75:
            return self.const()
        if k < 0.85:
            return 'arr[(%s) & 7]' % self.var()
        return 'g'

    def cast(self):
        return '(%s)' % self.r.choice(TYPES)

    def expr(self, d=0):
        r = self.r
        if d >= self.depth_limit or r.random() < 0.18:
            return self.atom()
        k = r.random()
        e = lambda: self.expr(d + 1)
        if k < 0.30:
            op = r.choice(['+', '-', '*', '&', '|', '^'])
            return '(%s %s %s)' % (e(), op, e())
        if k < 0.38:
            return '(%s %s ((%s & 15) + 1))' % (e(), r.choice(['/', '%']), e())
        if k < 0.45:
            return '(%s %s (%s & 15))' % (e(), r.choice(['<<', '>>']), e())
        if k < 0.58:
            return '(%s %s %s)' % (e(), r.choice(['==', '!=', '<', '<=', '>', '>=']), e())
        if k < 0.66:
            return '(%s %s %s)' % (e(), r.choice(['&&', '||']), e())
        if k < 0.70:
            return '(! %s)' % e()
        if k < 0.74:
            return '(~ %s)' % e()
        if k < 0.78:
            return '(- %s)' % e()
        if k < 0.84:
            return '(%s ? %s : %s)' % (e(), e(), e())
        if k < 0.91:
            return '%s%s' % (self.cast(), e())
        if k < 0.93:
            return 'f%d(%s, %s)' % (r.randint(1, 3), e(), e())
        if k < 0.95:
            return 'tr((%s) & 15, %s)' % (e(), e())
        if k < 0.97:
            return r.choice(['acc((%s) & 7, %s)', 'facc((%s) & 5) + 0 * (%s)', 'sr(%s, %s)',
                             'ip(%s, 5) + 0 * (%s)', 'sideeff(%s) + 0 * (%s)']) % (e(), e())
        return '(%s, %s)' % (e(), e())

    def lvalue(self):
        if self.r.random() < 0.12:
            return 'arr[(%s) & 7]' % self.var()
        return self.var()

    def simple_stmt(self):
        r = self.r
        k = r.random()
        lv = self.lvalue()
        if k < 0.35:
            return '%s = %s;' % (lv, self.expr())
        if k < 0.45:
            return '%s = %s;' % (lv, self.const())         # constants feed propagation
        if k < 0.52:
            return '%s = %s;' % (lv, self.var())           # copies
        if k < 0.62:
            return '%s %s= %s;' % (lv, r.choice(['+', '-', '*', '&', '|', '^']), self.expr(2))
        if k < 0.67:
            return '%s%s;' % (lv, r.choice(['++', '--']))
        if k < 0.71:
            return '%s = %s = %s;' % (self.var(), self.var(), self.expr(2))
        if k < 0.75:
            return '%s = %s %s %s;' % (lv, lv, r.choice(['+', '*', '|']), r.choice(['0', '1', '1']))
        if k < 0.80:
            # the same subexpression twice
            a, b = self.var(), self.var()
            return '%s = (%s * %s) + (%s * %s) + %s;' % (lv, a, b, a, b, self.const())
        if k < 0.88:
            return '%s = %s;' % (lv, self.const())
        if k < 0.94:
            return r.choice(['sideeff(%s);', 'purefn(%s, 3);', 'ip(%s, 5);']) % self.expr(2)
        return 'g = %s;' % self.expr(2)

    def block(self, depth, loop):
        n = self.r.randint(1, 4)
        return ' '.join(self.stmt(depth + 1, loop) for _ in range(n))

    def stmt(self, depth, loop):
        r = self.r
        if depth > 3 or r.random() < 0.55:
            return self.simple_stmt()
        k = r.random()
        if k < 0.30:
            s = 'if (%s) { %s }' % (self.expr(2), self.block(depth, loop))
            if r.random() < 0.5:
                s += ' else { %s }' % self.block(depth, loop)
            return s
        if k < 0.38:
            return 'if (%s) { %s } else { %s }' % (r.choice(['0', '1', '2']), self.block(depth, loop),
                                                  self.block(depth, loop))
        if k < 0.58:
            i = 'i%d' % self.loopvars
            self.loopvars += 1
            return 'for (%s = 0; %s < %d; %s++) { %s }' % (i, i, r.randint(1, 5), i,
                                                           self.block(depth, True))
        if k < 0.62:
            # loops over the array with an invariant part and a scaled counter
            i = 'i%d' % self.loopvars
            self.loopvars += 1
            a, b = self.var(), self.var()
            step = r.choice(['%s++', '%s += 2', '%s += 3'])
            return ('for (%s = %d; %s < %d; %s) { arr[(%s * 3 + (%s)) & 7] += (%s * %s) + (%s << 1) + %s * 8; %s }'
                    % (i, r.randint(0, 2), i, r.randint(3, 12), step % i, i, a, a, b, b, i, self.simple_stmt()))
        if k < 0.66:
            i = 'i%d' % self.loopvars
            self.loopvars += 1
            return '%s = %d; while (%s > 0) { %s %s--; }' % (i, r.randint(0, 4), i,
                                                              self.block(depth, True), i)
        if k < 0.74:
            return 'switch ((%s) & 3) { case 0: %s break; case 1: %s case 2: %s break; default: %s }' % (
                self.expr(2), self.simple_stmt(), self.simple_stmt(), self.simple_stmt(),
                self.simple_stmt())
        if k < 0.80 and loop:
            return 'if (%s) %s;' % (self.expr(2), r.choice(['break', 'continue']))
        if k < 0.78:
            self.labels += 1
            n = self.labels
            return 'if (%s) goto L%d; %s L%d: ;' % (self.expr(2), n, self.block(depth, loop), n)
        if k < 0.82:
            self.labels += 1
            n = self.labels
            i = 'i%d' % self.loopvars
            self.loopvars += 1
            return '%s = %d; L%d: %s %s--; if (%s > 0) goto L%d;' % (i, r.randint(1, 4), n,
                                                                    self.block(depth, False), i, i, n)
        if k < 0.84:
            return 'while (1) { %s break; }' % self.block(depth, False)
        if k < 0.86:
            return 'do { %s } while (0);' % self.block(depth, loop)
        if k < 0.90:
            return 'if (0) { %s }' % self.block(depth, loop)
        if k < 0.94:
            return 'while (0) { %s }' % self.block(depth, loop)
        return '{ %s }' % self.block(depth, loop)

    def program(self):
        r = self.r
        nv = r.randint(3, 7)
        for i in range(nv):
            self.vars.append(('v%d' % i, r.choice(TYPES)))
        body = []
        for _ in range(r.randint(4, 12)):
            body.append(self.stmt(0, False))
            body.append('dump();')
        nloops = self.loopvars
        out = ['#include <stdio.h>', 'int g = 3; int arr[8] = {1,2,3,4,5,6,7,8};']
        out.append('static int tr(int n, int a) { if (n <= 0) return a; return tr(n - 1, a + n * 3); }')
        out.append('static int acc(int n, int k) { if (n <= 0) return k; return n * 3 + acc(n - 1, k + 1); }')
        out.append('static long facc(int n) { if (n < 2) return 1; return n * facc(n - 1); }')
        out.append('struct P { int a; int b; };')
        out.append('static int sr(int x, int y) { struct P p; int t[3]; p.a = x; p.b = y; t[0] = p.a; '
                   't[1] = p.b; t[2] = t[0] + t[1]; return t[2] * 2 + p.a; }')
        out.append('static int ip(int a, int k) { return a * k + 3; }')
        out.append('static int sideeff(int a) { g += a; return a; }')
        out.append('static int purefn(int a, int b) { return a * b + 1; }')
        for i in (1, 2, 3):
            out.append('static int f%d(int a, int b) { return %s; }' % (
                i, ['a * 2 + b', '(a ^ b) + 1', 'a - b * 3'][i - 1]))
        for n, t in self.vars:
            out.append('%s %s;' % (t, n))
        out.append('static void dump(void)\n{ printf("%s\\n", %s g, arr[0], arr[3], arr[7]); }' % (
            ' '.join(['%ld'] * len(self.vars)) + ' %d %d %d %d',
            ' '.join('(long)%s,' % n for n, _ in self.vars)))
        out.append('int main(void)\n{')
        out.append('    int %s;' % ', '.join(['i%d' % i for i in range(max(nloops, 1))]) if nloops else '')
        for n, t in self.vars:
            out.append('    %s = %s;' % (n, self.const()))
        for b in body:
            out.append('    ' + b)
        out.append('    return 0;\n}')
        return '\n'.join(out) + '\n'


def run(cmd, timeout=30, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, **kw)


def build_run(src, flags, tmp, name):
    exe = os.path.join(tmp, name)
    p = run([FCC, '-w-'] + flags + ['-e' + exe, src])
    if p.returncode != 0:
        return None, p.stdout + p.stderr
    try:
        q = run([exe], timeout=5)
    except subprocess.TimeoutExpired:
        return 'timeout', ''
    return q.stdout + ('\nexit %d' % q.returncode), ''


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    singles = ['fold', 'algebra', 'compare', 'constprop', 'copyprop', 'deadstore', 'unreachable',
               'casts', 'canon', 'cse', 'strength', 'bool', 'gcse', 'pre', 'licm', 'iv', 'inline',
               'regalloc', 'isel', 'layout', 'tailcall', 'ssa', 'sccp', 'domtree', 'adce',
               'alias', 'ipcp', 'ipdce', 'unroll', 'unswitch', 'recur', 'sroa', 'size']
    bad = 0
    with tempfile.TemporaryDirectory(prefix='fc-optfuzz-') as tmp:
        for n in range(count):
            rnd = random.Random(seed * 100003 + n)
            src = os.path.join(tmp, 'p.c')
            code = Gen(rnd).program()
            open(src, 'w').write(code)
            want, err = build_run(src, [], tmp, 'base')
            if want is None:
                print('seed %d/%d: baseline does not compile:\n%s' % (seed, n, err[:400]))
                bad += 1
                continue
            if want == 'timeout':   # a generated loop that never ends (continue skips the decrement)
                continue
            for flags in (['-O', '-Z'], ['-O'], ['-Z'], ['-Os'], ['-O:unroll,unswitch,sroa,adce,sccp,ssa,domtree,cfg,fold,constprop']):
                got, err = build_run(src, flags, tmp, 'opt')
                if got != want:
                    culprit = []
                    for s in singles + ['jumps', 'peephole']:
                        g, _ = build_run(src, ['-O:' + s], tmp, 'one')
                        if g != want:
                            culprit.append(s)
                    keep = os.path.join(tempfile.gettempdir(), 'optfuzz-fail-%d-%d.c' % (seed, n))
                    open(keep, 'w').write(code)
                    print('MISMATCH seed %d case %d flags %s -> passes %s (program kept in %s)' % (
                        seed, n, ' '.join(flags), culprit or '?', keep))
                    bad += 1
                    break
    print('%d programs, %d failures' % (count, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
