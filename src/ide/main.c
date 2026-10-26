/* fc - Flash C IDE */
#include "ide.h"
#include <string.h>

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("Flash C %s - C compiler and IDE\nUsage: fc [file.c ...] [project.fcp]\n", FC_VERSION);
            return 0;
        }
    ide_run(argc, argv);
    return 0;
}
