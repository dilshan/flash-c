/* rand - pseudo-random numbers.  The generator and its constants are the ones of the reference
 * library, so  srand(n)  produces the same sequence of values. */
#include <stdlib.h>

static unsigned long __seed = 1;

void srand(unsigned int s) { __seed = s; }

int rand(void)
{
    __seed = (__seed * 22695477UL + 1) & 0xFFFFFFFFUL;
    return (int)((__seed >> 16) & 0x7FFF);
}
