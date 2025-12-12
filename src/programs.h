#ifndef PROGRAMS_H
#define PROGRAMS_H

#define MAX_PROGRAMS 16

struct user_program {
    const char *name;
    const char *description;
    const unsigned char *code;
    unsigned int code_len;
};

void programs_init(void);
int programs_count(void);
struct user_program *programs_get(int index);
struct user_program *programs_find(const char *name);
int programs_run(const char *name);

#endif
