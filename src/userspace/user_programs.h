/*
 * user_programs.h - Embedded user program binaries
 *
 * This header declares the embedded binary data for all user programs.
 * Each program is compiled from .s -> .bin -> .c with xxd.
 */

#ifndef USER_PROGRAMS_H
#define USER_PROGRAMS_H

/* info - prints information about the user program interface */
extern unsigned char info_bin[];
extern unsigned int info_bin_len;

/* heap - prints message then allocates and frees heap memory */
extern unsigned char heap_bin[];
extern unsigned int heap_bin_len;

/* hello - prints "Hello from USERSPACE!" and exits */
extern unsigned char hello_bin[];
extern unsigned int hello_bin_len;

/* loop - prints message then loops forever */
extern unsigned char loop_bin[];
extern unsigned int loop_bin_len;

/* crash - prints message then triggers page fault */
extern unsigned char crash_bin[];
extern unsigned int crash_bin_len;

/* count - prints digits 0-9 */
extern unsigned char count_bin[];
extern unsigned int count_bin_len;

/* alpha - prints A-Z alphabet */
extern unsigned char alpha_bin[];
extern unsigned int alpha_bin_len;

/* stars - prints 20 asterisks */
extern unsigned char stars_bin[];
extern unsigned int stars_bin_len;

/* input - tests keyboard input via sys_read */
extern unsigned char input_bin[];
extern unsigned int input_bin_len;

/* ctest - C program test (argc/argv demo) */
extern unsigned char ctest_bin[];
extern unsigned int ctest_bin_len;

#endif /* USER_PROGRAMS_H */
