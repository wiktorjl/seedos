/*
 * hexdump.c - Display file contents in hexadecimal
 *
 * Usage: hexdump FILE [FILE ...]
 */

#include <stdio.h>
#include <ctype.h>

int main(int argc, char **argv) {
    if(argc < 2) {
        fprintf(stderr, "Usage: hexdump FILE [FILE ...]\n");
        return 1;
    }

    int ret = 0;

    for(int i = 1; i < argc; i++) {
        FILE *fp = fopen(argv[i], "r");
        if(fp == NULL) {
            fprintf(stderr, "hexdump: cannot open '%s'\n", argv[i]);
            ret = 1;
            continue;
        }

        if(argc > 2) {
            if(i > 1) putchar('\n');
            printf("==> %s <==\n", argv[i]);
        }

        unsigned char buf[16];
        size_t offset = 0;
        size_t n;

        while((n = fread(buf, 1, 16, fp)) > 0) {
            /* Print offset */
            printf("%08lx  ", (unsigned long)offset);

            /* Print hex bytes */
            for(size_t j = 0; j < 16; j++) {
                if(j == 8) putchar(' ');
                if(j < n) {
                    printf("%02x ", buf[j]);
                }else {
                    printf("   ");
                }
            }

            /* Print ASCII */
            printf(" |");
            for(size_t j = 0; j < n; j++) {
                if(isprint(buf[j])) {
                    putchar(buf[j]);
                }else {
                    putchar('.');
                }
            }
            printf("|\n");

            offset += n;
        }

        /* Print final offset */
        printf("%08lx\n", (unsigned long)offset);

        fclose(fp);
    }

    return ret;
}
