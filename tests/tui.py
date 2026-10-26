#!/usr/bin/env python3
# File:    tui.py
# Purpose: Tiny pty driver + VT emulator used to test the Flash C IDE headlessly.
# Author:  Dilshan R Jayakody
# License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
# Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux

"""Tiny pty driver + VT emulator used to test the Flash C IDE headlessly."""
import os, pty, sys, time, select, fcntl, termios, struct, re, signal

VGA = [(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),(170,85,0),(170,170,170),
       (85,85,85),(85,85,255),(85,255,85),(85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]

def vga_index(rgb):
    best, bd = 0, 1 << 30
    for i, c in enumerate(VGA):
        d = sum((a - b) ** 2 for a, b in zip(c, rgb))
        if d < bd: best, bd = i, d
    return best

KEYS = {
 'F1': '\x1bOP', 'F2': '\x1bOQ', 'F3': '\x1bOR', 'F4': '\x1bOS', 'F5': '\x1b[15~', 'F6': '\x1b[17~',
 'F7': '\x1b[18~', 'F8': '\x1b[19~', 'F9': '\x1b[20~', 'F10': '\x1b[21~',
 'AF9': '\x1b[20;3~', 'CF9': '\x1b[20;5~', 'AF3': '\x1b[13;3~', 'AF5': '\x1b[15;3~', 'AF7': '\x1b[18;3~', 'AF8': '\x1b[19;3~',
 'CF1': '\x1b[11;5~', 'SF1': '\x1b[11;2~', 'AF1': '\x1b[11;3~', 'CF4': '\x1b[14;5~', 'CF7': '\x1b[18;5~',
 'UP': '\x1b[A', 'DOWN': '\x1b[B', 'RIGHT': '\x1b[C', 'LEFT': '\x1b[D', 'HOME': '\x1b[H', 'END': '\x1b[F',
 'PGUP': '\x1b[5~', 'PGDN': '\x1b[6~', 'DEL': '\x1b[3~', 'INS': '\x1b[2~',
 'SUP': '\x1b[1;2A', 'SDOWN': '\x1b[1;2B', 'SRIGHT': '\x1b[1;2C', 'SLEFT': '\x1b[1;2D',
 'ENTER': '\r', 'TAB': '\t', 'STAB': '\x1b[Z', 'ESC': '\x1b', 'BKSP': '\x7f', 'ABKSP': '\x1b\x7f',
 'CINS': '\x1b[2;5~', 'SINS': '\x1b[2;2~', 'SDEL': '\x1b[3;2~',
}

class Screen:
    def __init__(self, rows, cols):
        self.rows, self.cols = rows, cols
        self.ch = [[' '] * cols for _ in range(rows)]
        self.at = [[7] * cols for _ in range(rows)]
        self.cx = self.cy = 0; self.attr = 7; self.alt = False
        self.buf = ''
        import codecs
        self.dec = codecs.getincrementaldecoder('utf-8')(errors='replace')
    def feed(self, data):
        text = self.buf + self.dec.decode(data)
        # keep incomplete utf-8 / escape tail
        i = 0; n = len(text)
        while i < n:
            c = text[i]
            if c == '\x1b':
                m = re.compile(r'\x1b\[([?<>0-9;]*)([@-~])').match(text, i)
                if m:
                    self.csi(m.group(1), m.group(2)); i = m.end(); continue
                m = re.compile(r'\x1b\][^\x07\x1b]*(\x07|\x1b\\)').match(text, i)
                if m: i = m.end(); continue
                if text[i:i+2] in ('\x1b7', '\x1b8'): i += 2; continue
                if i + 1 >= n or text[i+1] in '[]': break   # incomplete
                i += 2; continue
            if c == '\r': self.cx = 0
            elif c == '\n': self.cy = min(self.cy + 1, self.rows - 1)
            elif c == '\a': pass
            else:
                if self.cy < self.rows and self.cx < self.cols:
                    self.ch[self.cy][self.cx] = c; self.at[self.cy][self.cx] = self.attr
                self.cx += 1
            i += 1
        self.buf = text[i:]
    def csi(self, p, f):
        nums = [int(x) if x.isdigit() else 0 for x in p.lstrip('?<>').split(';')] if p else []
        if f == 'H':
            self.cy = (nums[0] if nums and nums[0] else 1) - 1; self.cx = (nums[1] if len(nums) > 1 and nums[1] else 1) - 1
        elif f == 'J':
            for r in range(self.rows): self.ch[r] = [' '] * self.cols; self.at[r] = [7] * self.cols
        elif f == 'K':
            for x in range(self.cx, self.cols): self.ch[self.cy][x] = ' '
        elif f == 'm':
            fg, bg = self.attr & 15, self.attr >> 4
            k = 0
            if not nums: nums = [0]
            while k < len(nums):
                v = nums[k]
                if v == 0: fg, bg = 7, 0
                elif v == 38 and k + 4 < len(nums) + 0 and nums[k+1] == 2: fg = vga_index(tuple(nums[k+2:k+5])); k += 4
                elif v == 48 and k + 4 < len(nums) + 0 and nums[k+1] == 2: bg = vga_index(tuple(nums[k+2:k+5])); k += 4
                elif 30 <= v <= 37: fg = [0,4,2,6,1,5,3,7][v-30]
                elif 90 <= v <= 97: fg = [0,4,2,6,1,5,3,7][v-90] + 8
                elif 40 <= v <= 47: bg = [0,4,2,6,1,5,3,7][v-40]
                k += 1
            self.attr = (bg << 4) | fg
    def text(self):
        return '\n'.join(''.join(r).rstrip() for r in self.ch)

class Session:
    def __init__(self, argv, rows=30, cols=100, cwd=None, env=None):
        self.screen = Screen(rows, cols)
        pid, fd = pty.fork()
        if pid == 0:
            if cwd: os.chdir(cwd)
            e = dict(os.environ); e['TERM'] = 'xterm-256color'; e['COLORTERM'] = 'truecolor'
            if env: e.update(env)
            os.execvpe(argv[0], argv, e)
        self.pid, self.fd = pid, fd
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
        self.alive = True
        self.pump(0.5)
    def pump(self, t=0.3):
        end = time.time() + t
        while True:
            r, _, _ = select.select([self.fd], [], [], max(0, end - time.time()))
            if not r: break
            try: d = os.read(self.fd, 65536)
            except OSError: self.alive = False; break
            if not d: self.alive = False; break
            self.screen.feed(d)
            end = max(end, time.time() + 0.05) if time.time() < end else end
            if time.time() >= end: break
    def send(self, *keys, wait=0.25):
        for k in keys:
            s = KEYS.get(k, k)
            os.write(self.fd, s.encode('latin1', errors='ignore') if isinstance(s, str) else s)
            self.pump(0.06)
        self.pump(wait)
    def type(self, s, wait=0.2):
        for ch in s:
            os.write(self.fd, ch.encode()); 
        self.pump(wait)
    def show(self, title=''):
        print('=' * 20, title)
        print(self.screen.text())
    def close(self):
        try: os.kill(self.pid, signal.SIGKILL)
        except Exception: pass
        try: os.waitpid(self.pid, 0)
        except Exception: pass

def render_html(screen, path):
    cols = ['#000','#00a','#0a0','#0aa','#a00','#a0a','#a50','#aaa','#555','#55f','#5f5','#5ff','#f55','#f5f','#ff5','#fff']
    out = ['<pre style="background:#000;font:14px/1.0 monospace;letter-spacing:0">']
    for r in range(screen.rows):
        for c in range(screen.cols):
            a = screen.at[r][c]; ch = screen.ch[r][c].replace('&', '&amp;').replace('<', '&lt;')
            out.append('<span style="color:%s;background:%s">%s</span>' % (cols[a & 15], cols[(a >> 4) & 7], ch))
        out.append('\n')
    out.append('</pre>')
    open(path, 'w').write(''.join(out))

if __name__ == '__main__':
    s = Session(['./build/fc'] + sys.argv[1:], cwd=os.path.join(os.path.dirname(__file__), '..'))
    s.show('startup'); s.close()

def render_png(screen, path, cw=10, ch=20):
    """Render the emulated screen to a PNG using DejaVu Sans Mono."""
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', 16)
    img = Image.new('RGB', (screen.cols * cw, screen.rows * ch), (0, 0, 0))
    d = ImageDraw.Draw(img)
    for r in range(screen.rows):
        for c in range(screen.cols):
            a = screen.at[r][c]
            fg, bg = VGA[a & 15], VGA[(a >> 4) & 7]
            x, y = c * cw, r * ch
            d.rectangle([x, y, x + cw - 1, y + ch - 1], fill=bg)
            g = screen.ch[r][c]
            if g != ' ':
                d.text((x, y), g, font=font, fill=fg)
    img.save(path)
