/*
 * File:    env.c
 * Purpose: env - the process environment
 * Author:  Dilshan R Jayakody
 * License: MIT (SPDX-License-Identifier: MIT); see the LICENSE file
 * Project: Flash C, a C compiler and text-mode IDE for 64-bit Linux
 */
#include <stdlib.h>
#include <string.h>

char *getenv(const char *name)
{
    char **e;
    size_t l = strlen(name);
    if (!environ || !l) return NULL;
    for (e = environ; *e; e++)
        if (!strncmp(*e, name, l) && (*e)[l] == '=') return *e + l + 1;
    return NULL;
}

static int __env_own;                  /* environ has been copied to the heap */

int putenv(const char *entry)
{
    char *eq = strchr(entry, '=');
    size_t nl, n = 0;
    char **e, *copy;
    if (!eq) {                         /* "NAME" alone removes the variable */
        nl = strlen(entry);
        if (!environ) return 0;
        for (e = environ; *e; e++)
            if (!strncmp(*e, entry, nl) && (*e)[nl] == '=') { char **q = e; while (*q) { *q = q[1]; q++; } break; }
        return 0;
    }
    nl = eq - entry;
    copy = strdup(entry);
    if (!copy) return -1;
    if (environ) {
        for (e = environ; *e; e++) {
            if (!strncmp(*e, entry, nl) && (*e)[nl] == '=') { *e = copy; return 0; }
            n++;
        }
    }
    {
        char **ne = malloc((n + 2) * sizeof(char *));
        if (!ne) return -1;
        if (n) memcpy(ne, environ, n * sizeof(char *));
        ne[n] = copy; ne[n + 1] = NULL;
        if (__env_own) free(environ);
        environ = ne; __env_own = 1;
    }
    return 0;
}
