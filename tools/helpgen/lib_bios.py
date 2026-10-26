# File:    lib_bios.py
# Purpose: Help text for the functions of <bios.h>, used by gen.py.
# Author:  Dilshan R Jayakody
# License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
# Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux

H = "bios.h"
F = [
("biosequip","int biosequip(void);","Returns the equipment list.","A fixed value (0x0021: a display and one floppy-style drive entry) is returned; the real hardware is not examined.","The equipment word.","biosmemory",None),
("bioskey","int bioskey(int cmd);","Keyboard services.","cmd 0 reads a key and waits (an extended key is returned as scan code << 8); cmd 1 tells whether a key is waiting; cmd 2 (shift flags) always returns 0.","The key, or the result of the command.","getch kbhit",None),
("biosmemory","int biosmemory(void);","Returns the memory size in kilobytes.","Always 640; the real memory size is not reported.","640.","biosequip",None),
("biostime","long biostime(int cmd, long newtime);","Reads the BIOS clock.","cmd 0 returns the number of clock ticks (18.2 per second) since midnight. Setting the clock (cmd 1) is not supported and does nothing.","The tick count, or 0 for cmd 1.","time clock",None),
]
TYPES = []
