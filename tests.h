/*
 * tests.h - Kernel Test Suite
 *
 * This module provides test functions for various kernel subsystems.
 * Tests can be invoked from the shell to verify system functionality.
 *
 * Usage from shell:
 *   test memmap  - Display memory map from bootloader
 *   test vmm     - Test virtual memory manager
 *   test user    - Run userspace "Hello World" program
 */

#ifndef TESTS_H
#define TESTS_H

#include <stdint.h>

/* =============================================================================
 * Test Initialization
 *
 * Must be called during kernel startup to capture bootloader info.
 * =============================================================================
 */

/*
 * tests_init - Initialize the test subsystem.
 *
 * @memmap: Pointer to Limine memory map response.
 * @hhdm_offset: Higher Half Direct Map offset from bootloader.
 *
 * Stores references needed by test functions (memory map, HHDM offset).
 */
struct limine_memmap_response;
void tests_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);

/* =============================================================================
 * Test Functions
 *
 * Each test prints results to the console.
 * =============================================================================
 */

/*
 * test_memmap - Display the memory map from the bootloader.
 *
 * Shows all memory regions with their types, addresses, and sizes.
 * Useful for understanding the system's physical memory layout.
 */
void test_memmap(void);

/*
 * test_vmm - Test virtual memory manager functionality.
 *
 * Creates a new address space, maps pages, writes test patterns,
 * switches address spaces, and verifies everything works.
 */
void test_vmm(void);

/*
 * test_user - Run the userspace "Hello World" program.
 *
 * Allocates pages, copies the user binary, enters ring 3,
 * and returns when the user program calls sys_exit.
 */
void test_user(void);

#endif /* TESTS_H */
