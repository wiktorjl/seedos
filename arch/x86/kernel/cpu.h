/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * x86-64 CPU primitives
 */

#ifndef _CPU_H
#define _CPU_H

#include "types.h"

extern uint64_t stack_size;
extern char stack_bottom[];
extern char stack_top[];

/**
 * cpu_get_stack_top - Get current stack pointer
 *
 * Return: current RSP value
 */
static inline uint64_t cpu_get_stack_top(void)
{
	uint64_t rsp;
	__asm__ volatile("movq %%rsp, %0" : "=r"(rsp));
	return rsp;
}

/**
 * cpu_enable_interrupts - Enable interrupts (STI)
 */
static inline void cpu_enable_interrupts(void)
{
	__asm__ volatile("sti");
}

/**
 * cpu_disable_interrupts - Disable interrupts (CLI)
 */
static inline void cpu_disable_interrupts(void)
{
	__asm__ volatile("cli");
}

/**
 * cpu_halt - Halt CPU until next interrupt (HLT)
 */
static inline void cpu_halt(void)
{
	__asm__ volatile("hlt");
}

#endif /* _CPU_H */
