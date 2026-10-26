# File:    lib_ctype.py
# Purpose: Help text for the functions of <ctype.h>, used by gen.py.
# Author:  Dilshan R Jayakody
# License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
# Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux

H = "ctype.h"
def cf(name, what, cls):
    return (name,"int %s(int c);"%name,"Tests whether a character is %s."%what,"The argument must be an unsigned char value or EOF. %s"%cls,"Nonzero if the test is true, otherwise 0.","tolower toupper",None)
F = [
cf("isalnum","a letter or a digit","Same as isalpha(c) || isdigit(c)."),
cf("isalpha","a letter","In the \"C\" locale: A-Z and a-z."),
cf("iscntrl","a control character","In the \"C\" locale: codes 0 to 31 and 127."),
cf("isdigit","a decimal digit","The characters 0 to 9."),
cf("isgraph","a printing character other than space","Codes 33 to 126."),
cf("islower","a lowercase letter","a-z in the \"C\" locale."),
cf("isprint","a printing character including space","Codes 32 to 126."),
cf("ispunct","a punctuation character","A printing character that is neither space nor alphanumeric."),
cf("isspace","white space","Space, form feed, newline, carriage return, horizontal tab and vertical tab."),
cf("isupper","an uppercase letter","A-Z in the \"C\" locale."),
cf("isxdigit","a hexadecimal digit","0-9, A-F and a-f."),
("tolower","int tolower(int c);","Converts an uppercase letter to lowercase.","Other characters are returned unchanged.","The converted character.","toupper islower",None),
("toupper","int toupper(int c);","Converts a lowercase letter to uppercase.","Other characters are returned unchanged.","The converted character.","tolower isupper",None),
]
TYPES = []
