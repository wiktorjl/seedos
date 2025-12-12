#include <stddef.h>
#include "programs.h"
#include "process.h"
#include "user_programs.h"
#include "string.h"

static struct user_program programs[MAX_PROGRAMS];
static int program_count = 0;

static void register_program(const char *name, const char *desc, 
                               unsigned char *code, unsigned int len) {

    if(program_count < MAX_PROGRAMS) {
        programs[program_count].name = name;
        programs[program_count].description = desc;
        programs[program_count].code = code;
        programs[program_count].code_len = len;
        program_count++;

    }
                                
}

void programs_init(void) {
    register_program("hello", "Prints hello and exits",
                     hello_bin, hello_bin_len);
    register_program("count", "Prints digits 0-9",
                     count_bin, count_bin_len);
    register_program("alpha", "Prints A-Z alphabet",
                     alpha_bin, alpha_bin_len);
    register_program("stars", "Prints 20 asterisks",
                     stars_bin, stars_bin_len);
    register_program("loop", "Loops forever (test preemption)",
                     loop_bin, loop_bin_len);
    register_program("crash", "Deliberately crashes (test exception)",
                     crash_bin, crash_bin_len);
}

int programs_count(void) {
    
    return program_count;
}

struct user_program *programs_get(int index) {
    if(index >= 0 && index < program_count) {
        return &programs[index];
    } else {
        return NULL;
    }
}

struct user_program *programs_find(const char *name) {
    for(int i = 0; i < program_count; i++) {
        if(programs[i].name != NULL && strcmp(programs[i].name, name) == 0) {
            return &programs[i];
        }
    }
    return NULL;
}

int programs_run(const char *name) {
    struct user_program *prog = programs_find(name);
    if (prog == NULL) {
        return -1;
    }

    struct process *p = process_create();
    if (p == NULL) {
        return -1;
    }

    if (process_load(p, prog->code, prog->code_len) != 0) {
        process_destroy(p);
        return -1;
    }

    int exit_code = process_run(p);
    process_destroy(p);
    return exit_code;
}

