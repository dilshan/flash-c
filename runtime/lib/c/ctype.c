/*
 * File:    ctype.c
 * Purpose: ctype - character classification and conversion
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <ctype.h>

/*
 * Tests whether a character is a decimal digit. Returns nonzero if the test is true, otherwise
 * 0.
 */
int isdigit(int c)
{
    return c >= '0' && c <= '9';
}

/*
 * Tests whether a character is an uppercase letter. Returns nonzero if the test is true,
 * otherwise 0.
 */
int isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}

/*
 * Tests whether a character is a lowercase letter. Returns nonzero if the test is true,
 * otherwise 0.
 */
int islower(int c)
{
    return c >= 'a' && c <= 'z';
}

/* Tests whether a character is a letter. Returns nonzero if the test is true, otherwise 0. */
int isalpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/*
 * Tests whether a character is a letter or a digit. Returns nonzero if the test is true,
 * otherwise 0.
 */
int isalnum(int c)
{
    return isalpha(c) || isdigit(c);
}

/* Tests whether a character is white space. Returns nonzero if the test is true, otherwise 0. */
int isspace(int c)
{
    return c == ' ' || (c >= 9 && c <= 13);
}

/*
 * Tests whether a character is a hexadecimal digit. Returns nonzero if the test is true,
 * otherwise 0.
 */
int isxdigit(int c)
{
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/*
 * Tests whether a character is a printing character including space. Returns nonzero if the
 * test is true, otherwise 0.
 */
int isprint(int c)
{
    return c >= 32 && c < 127;
}

/*
 * Tests whether a character is a printing character other than space. Returns nonzero if the
 * test is true, otherwise 0.
 */
int isgraph(int c)
{
    return c > 32 && c < 127;
}

/*
 * Tests whether a character is a control character. Returns nonzero if the test is true,
 * otherwise 0.
 */
int iscntrl(int c)
{
    return (c >= 0 && c < 32) || c == 127;
}

/*
 * Tests whether a character is a punctuation character. Returns nonzero if the test is true,
 * otherwise 0.
 */
int ispunct(int c)
{
    return c > 32 && c < 127 && !isalnum(c);
}

/* Tests whether c is in the range 0 to 127. */
int isascii(int c)
{
    return c >= 0 && c < 128;
}

/* Clears all but the low seven bits of c. */
int toascii(int c)
{
    return c & 0x7f;
}

/* Converts a lowercase letter to uppercase. Returns the converted character. */
int toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

/* Converts an uppercase letter to lowercase. Returns the converted character. */
int tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

/* Converts a lower-case letter to upper case without testing it first (macro form). */
int _toupper(int c)
{
    return c - 32;
}

/* Converts an upper-case letter to lower case without testing it first (macro form). */
int _tolower(int c)
{
    return c + 32;
}
