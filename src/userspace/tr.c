/*
 * tr.c - Translate or delete characters
 *
 * Usage: tr [-d] SET1 SET2 FILE
 *        tr -d SET1 FILE
 *        -d  delete characters in SET1
 *
 * Reads FILE, translates/deletes characters, writes to stdout.
 */

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    int delete_mode = 0;
    const char *set1 = NULL;
    const char *set2 = NULL;
    const char *filename = NULL;

    /* Parse arguments */
    int arg_idx = 1;
    if (arg_idx < argc && strcmp(argv[arg_idx], "-d") == 0) {
        delete_mode = 1;
        arg_idx++;
    }

    if (arg_idx < argc) {
        set1 = argv[arg_idx++];
    }
    if (arg_idx < argc) {
        if (delete_mode) {
            filename = argv[arg_idx++];
        } else {
            set2 = argv[arg_idx++];
        }
    }
    if (arg_idx < argc && !delete_mode) {
        filename = argv[arg_idx++];
    }

    if (set1 == NULL || (!delete_mode && set2 == NULL) || filename == NULL) {
        fprintf(stderr, "Usage: tr [-d] SET1 SET2 FILE\n");
        fprintf(stderr, "       tr -d SET1 FILE\n");
        return 1;
    }

    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "tr: cannot open '%s'\n", filename);
        return 1;
    }

    /* Build translation table */
    int translate[256];
    for (int i = 0; i < 256; i++) {
        translate[i] = i;
    }

    if (delete_mode) {
        /* Mark characters for deletion (-1 means delete) */
        for (const char *p = set1; *p; p++) {
            translate[(unsigned char)*p] = -1;
        }
    } else {
        /* Build translation mapping */
        size_t len1 = strlen(set1);
        size_t len2 = strlen(set2);

        for (size_t i = 0; i < len1; i++) {
            unsigned char c1 = set1[i];
            /* If set2 is shorter, use last char of set2 */
            unsigned char c2 = (i < len2) ? set2[i] : set2[len2 - 1];
            translate[c1] = c2;
        }
    }

    /* Process input */
    int c;
    while ((c = fgetc(fp)) != EOF) {
        int out = translate[(unsigned char)c];
        if (out >= 0) {
            putchar(out);
        }
        /* If out == -1, character is deleted (not printed) */
    }

    fclose(fp);
    return 0;
}
