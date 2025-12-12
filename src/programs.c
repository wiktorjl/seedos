#include <stddef.h>
#include <stdint.h>
#include "programs.h"
#include "context.h"
#include "memory.h"
#include "pmm.h"
#include "user_program.h"
#include "string.h"
#include "vmm.h"

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
    register_program("hello", "A simple hello world program", 
                     (unsigned char *)user_bin, user_bin_len);
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
    
    if(prog != NULL) {
        // Set up address space for the user program
        uint64_t memory = vmm_create_address_space();
        uint64_t code_page_phys = pmm_alloc();
        uint64_t stack_page_phys = pmm_alloc();

        if(memory != 0 && code_page_phys != 0 && stack_page_phys != 0) {
            struct user_context ctx;
            ctx.pml4 = memory;
            ctx.entry = USER_CODE_BASE;
            ctx.stack = USER_STACK_BASE + USER_STACK_SIZE;

            vmm_map_page(memory, USER_CODE_BASE, code_page_phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE);
            vmm_map_page(memory, USER_STACK_BASE, stack_page_phys, PTE_PRESENT | PTE_USER | PTE_WRITABLE);
            
            void * code_page_virt = phys_to_virt(code_page_phys);
            memcpy(code_page_virt, prog->code, prog->code_len);

            context_switch_to_user(&ctx);

            pmm_free(code_page_phys);
            pmm_free(stack_page_phys);
            return 0;
        }
    }
    return -1;
}

