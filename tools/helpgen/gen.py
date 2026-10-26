#!/usr/bin/env python3
"""Generates help/*.fch (the extended Flash C help) from the tables in lib_*.py and pages.py.
Usage: gen.py <output-dir>"""
import sys, os, re, textwrap, importlib
sys.dont_write_bytecode = True
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, here)
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "..", "help")
os.makedirs(out, exist_ok=True)

BUILTIN = {"Contents","Using the IDE","Keyboard shortcuts","The editor","Compiling and running","Compiler options",
 "Messages and errors","Projects","Language reference","Differences from classic compilers","About Flash C","Source-level debugger",
 "Libraries and linking","Library: io and dir","Library: process"}
mods = ["stdio","stdlib","string","ctype","math","time","misc","conio","dos","bios"]
libs = {m: importlib.import_module("lib_"+m) for m in mods}
pages = importlib.import_module("pages")

topics = {}     # title -> dict
order = []
def add(title, body, keys="", alias="", example=None, first=False):
    topics[title] = dict(body=body, keys=keys, alias=alias, example=example); order.append(title)
def wrap(s, ind=" "):
    return "\n".join(textwrap.fill(p, 70, initial_indent=ind, subsequent_indent=ind) for p in s.split("\n")) if s else ""

MISC_PAGE = "assert.h setjmp.h signal.h stdarg.h stddef.h locale.h"
def hlink(hdr, summ):
    if hdr != "misc": return "{%s}" % hdr
    m = re.search(r"\((\w+\.h)\)", summ)
    return "{%s|%s}" % (m.group(1), MISC_PAGE) if m else "{%s}" % MISC_PAGE
funcs = {}
for m in mods:
    for f in libs[m].F: funcs[f[0]] = (m, f)
for name, (m, f) in sorted(funcs.items()):
    _, proto, summ, detail, ret, see, ex = f
    hdr = libs[m].H
    b = [" %s" % name, "", " %s" % summ, "", " Header: " + hlink(hdr, summ),
         "", " Syntax", "   %s" % proto, ""]
    if detail: b += [wrap(detail), ""]
    b += [" Return value", wrap(ret), ""]
    if see:
        links = " ".join("{%s}" % s for s in see.split() if s in funcs or s in topics or s in BUILTIN or any(s == t[0] for l in libs.values() for t in l.TYPES))
        if links: b += [" See also: " + links, ""]
    b += [" Library: {Library: %s}" % m if False else " Back to {Library functions}"]
    add(name, "\n".join(b), keys=name, alias="", example=ex)

# data/type topics
for m in mods:
    for t in libs[m].TYPES:
        if t[0] in topics: continue
        add(t[0], " %s\n\n%s\n\n Back to {Library functions}" % (t[0], wrap(t[1])), keys=t[0])

# header pages
HDRS = {}
for name,(m,f) in funcs.items():
    h = libs[m].H
    if h != "misc": HDRS.setdefault(h, []).append(name)
for h, names in sorted(HDRS.items()):
    desc = pages.HEADERS.get(h, "")
    ts = [t[0] for t in libs[[mm for mm in mods if libs[mm].H == h][0]].TYPES]
    b = [" <%s>" % h, "", wrap(desc), "", " Functions", ""]
    b += [textwrap.fill(" ".join("{%s}" % n for n in sorted(names)), 72, initial_indent="  ", subsequent_indent="  "), ""]
    if ts: b += [" Types, macros and objects", "", textwrap.fill(" ".join("{%s}" % t for t in ts), 72, initial_indent="  ", subsequent_indent="  "), ""]
    b += [" Back to {Library functions}"]
    add(h, "\n".join(b), keys=h)
# misc header (setjmp, signal ...) page
b = [" Other headers", "", wrap(pages.HEADERS["misc"]), "", " Functions and macros", "",
     textwrap.fill(" ".join("{%s}" % n for n,_ in [(f[0],0) for f in libs["misc"].F]), 72, initial_indent="  ", subsequent_indent="  "), "",
     " Other topics", "", textwrap.fill(" ".join("{%s}" % t[0] for t in libs["misc"].TYPES), 72, initial_indent="  ", subsequent_indent="  "), "", " Back to {Library functions}"]
add("assert.h setjmp.h signal.h stdarg.h stddef.h locale.h", "\n".join(b), keys="assert.h setjmp.h signal.h stdarg.h stddef.h locale.h errno.h limits.h float.h", alias="assert.h; setjmp.h; signal.h; stdarg.h; stddef.h; locale.h; errno.h; limits.h; float.h")
for p in pages.PAGES:
    add(p[0], p[1], keys=p[2] if len(p) > 2 else "", alias=p[3] if len(p) > 3 else "")

# category lists
cats = {"Library: stdio":"stdio","Library: string":"string","Library: stdlib":"stdlib","Library: ctype":"ctype","Library: math":"math","Library: time":"time", "Library: conio and dos":None}
for title, m in cats.items():
    if m is None: continue
    names = sorted(f[0] for f in libs[m].F)
    b = [" %s  (<%s>)" % (title, libs[m].H), "", wrap(pages.HEADERS.get(libs[m].H, "")), ""]
    b += [textwrap.fill(" ".join("{%s}" % n for n in names), 72, initial_indent=" ", subsequent_indent=" "), "", " Back to {Library functions}"]
    add(title, "\n".join(b), keys="")
allf = sorted(funcs)
b = [" Library functions", "", " Header files of the library:", ""]
b += [" {%s}" % h for h in sorted(HDRS)] + [" {%s|assert.h setjmp.h signal.h stdarg.h stddef.h locale.h}" % x for x in "assert.h setjmp.h signal.h stdarg.h stddef.h locale.h errno.h limits.h float.h".split()] + [""]
b += [" Formatting and conversion", "", " {printf formats}", " {scanf formats}", " {Library: conio and dos}", " {Not supported DOS features}", ""]
b += [" All functions", "", textwrap.fill(" ".join("{%s}" % n for n in allf), 72, initial_indent=" ", subsequent_indent=" "), "",
      " Press Shift+F1 for the Index, or Ctrl+F1 on a function name in the editor."]
add("Library functions", "\n".join(b), keys="library standard functions reference")

names = lambda m: sorted(f[0] for f in libs[m].F)
b = [" Library: conio and dos", "", " Console input and output (<conio.h>), DOS-style time and delay functions", " (<dos.h>) and a few BIOS-style functions (<bios.h>).", " Console output uses ANSI terminal sequences; keyboard input is unbuffered.", "",
     " <conio.h>", textwrap.fill(" ".join("{%s}" % n for n in names("conio")), 72, initial_indent="  ", subsequent_indent="  "), "",
     " <dos.h>", textwrap.fill(" ".join("{%s}" % n for n in names("dos")), 72, initial_indent="  ", subsequent_indent="  "), "",
     " <bios.h>", textwrap.fill(" ".join("{%s}" % n for n in names("bios")), 72, initial_indent="  ", subsequent_indent="  "), "",
     " Types and constants", "  {text_info} {Console colors} {directvideo}", "  {struct date} {struct time} {struct dfree}", "",
     " DOS interrupts, ports, far memory and graphics are not available: see", " {Not supported DOS features}.", "", " Back to {Library functions}"]
add("Library: conio and dos", "\n".join(b), keys="conio dos bios console")
b = [" Not supported DOS features", "", wrap("Flash C produces programs for 64-bit Linux, so features that depend on DOS, the PC BIOS or the hardware cannot be provided. Declarations for these are not in the headers, so a program that uses them gets an undefined-function error. Use the portable replacements where one exists."), "",
 " Not provided", "",
 "   int86, int86x, intdos, intdosx, bdos, geninterrupt  (software interrupts)",
 "   inportb, outportb, inport, outport, inp, outp        (I/O ports)",
 "   peek, poke, peekb, pokeb, MK_FP, FP_SEG, FP_OFF      (far memory)",
 "   getvect, setvect, keep, harderr, ctrlbrk              (interrupt handling)",
 "   farmalloc, farfree, allocmem, coreleft                 (far heap)",
 "   gettext, puttext, movetext                             (screen buffer)",
 "   graphics.h: initgraph, line, circle and so on          (graphics)",
 "   bioscom, biosdisk, bios_*, biosprint                   (BIOS devices)", "",
 " Replacements", "",
 "   screen text      {cprintf}, {gotoxy}, {window}, {textcolor}",
 "   keyboard         {getch}, {kbhit}, {bioskey}",
 "   timing           {delay}, {sleep}, {time}, {clock}",
 "   files            {Library: stdio}", "",
 " The keywords near, far, huge, cdecl and pascal are accepted and ignored.", "",
 " See {Differences from classic compilers}"]
add("Not supported DOS features", "\n".join(b), keys="int86 intdos inportb outportb outp inp peek poke MK_FP FP_SEG FP_OFF getvect setvect gettext puttext movetext graphics initgraph farmalloc far near interrupt unsupported")

# check links
allt = set(topics) | BUILTIN
bad = []
for t, d in topics.items():
    for l in re.findall(r"\{([^}]*)\}", d["body"]):
        tgt = l.split("|")[-1]
        if tgt not in allt and not any(tgt.lower()==x.lower() for x in allt): bad.append((t, tgt))
if bad:
    print("unresolved links:", bad[:20], len(bad)); sys.exit(1)

# contents addendum
with open(os.path.join(out, "library.fch"), "w") as f:
    f.write(";; Flash C extended help - generated by tools/helpgen/gen.py, do not edit by hand.\n")
    f.write("@append Contents\n\n Extended topics\n\n {Library functions}\n {Keywords and operators}\n {Preprocessor}\n {printf formats}\n {scanf formats}\n {Predefined macros}\n\n Press Shift+F1 for the Index, Ctrl+F1 for the word at the cursor.\n")
    for t in sorted(topics):
        d = topics[t]
        f.write("\n@topic %s\n" % t)
        if d["keys"]: f.write("@keys %s\n" % d["keys"])
        if d["alias"]: f.write("@alias %s\n" % d["alias"])
        for line in d["body"].split("\n"):
            f.write(("@" + line if line.startswith("@") else line) + "\n")
        if d["example"]:
            f.write("\n Example\n\n@example\n")
            for line in d["example"].split("\n"): f.write(("@" + line if line.startswith("@") else line) + "\n")
            f.write("@end\n")
print("wrote", len(topics), "topics,", len(funcs), "functions")
