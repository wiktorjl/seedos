/*
 * shell.h - Simple kernel shell
 */

#ifndef SHELL_H
#define SHELL_H

/*
 * Initialize the shell and print welcome message
 */
void shell_init(void);

/*
 * Print the shell prompt
 */
void shell_prompt(void);

/*
 * Process a character of input
 */
void shell_input(char c);

#endif /* SHELL_H */