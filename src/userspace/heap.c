/*
 * heap.c - Test malloc/free for heap allocation
 *
 * Demonstrates dynamic memory allocation via malloc():
 * 1. Allocate memory
 * 2. Write data to allocated memory
 * 3. Read it back
 * 4. Free the memory
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    printf("Allocating 16 bytes with malloc...\n");

    /* Allocate 16 bytes */
    char *ptr = malloc(16);
    if (ptr == NULL) {
        printf("Failed!\n");
        return 1;
    }

    /* Write 'ABCD' to allocated memory */
    ptr[0] = 'A';
    ptr[1] = 'B';
    ptr[2] = 'C';
    ptr[3] = 'D';
    ptr[4] = '\0';

    printf("Write OK\n");

    /* Read back and print */
    printf("Data: %s\n", ptr);

    /* Free the memory */
    printf("Freeing memory...\n");
    free(ptr);

    printf("Free OK\n");
    printf("Success!\n");

    return 0;
}
