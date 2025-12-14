/*
 * test_vmm.c - Virtual Memory Manager Tests
 *
 * Tests for 4-level paging and address space management.
 */

#include "test_framework.h"
#include "vmm.h"
#include "pmm.h"

/* =============================================================================
 * VMM Tests
 * =============================================================================
 */

/*
 * Test creating a new address space.
 */
static int test_vmm_create_space(void) {
    uint64_t pml4 = vmm_create_address_space();
    TEST_ASSERT_NOT_NULL(pml4);

    /* PML4 should be page-aligned */
    TEST_ASSERT_EQ(pml4 & 0xFFF, 0);

    return TEST_PASS;
}

/*
 * Test mapping a single page.
 */
static int test_vmm_map_page(void) {
    uint64_t pml4 = vmm_create_address_space();
    TEST_ASSERT_NOT_NULL(pml4);

    uint64_t phys = pmm_alloc();
    TEST_ASSERT_NOT_NULL(phys);

    /* Map at user code base address */
    int result = vmm_map_page(pml4, USER_CODE_BASE, phys, PTE_PRESENT | PTE_USER);
    TEST_ASSERT_EQ(result, 0);

    pmm_free(phys);
    return TEST_PASS;
}

/*
 * Test mapping multiple pages.
 */
static int test_vmm_map_multiple(void) {
    uint64_t pml4 = vmm_create_address_space();
    TEST_ASSERT_NOT_NULL(pml4);

    uint64_t phys1 = pmm_alloc();
    uint64_t phys2 = pmm_alloc();
    TEST_ASSERT_NOT_NULL(phys1);
    TEST_ASSERT_NOT_NULL(phys2);

    /* Map code and stack pages */
    int r1 = vmm_map_page(pml4, USER_CODE_BASE, phys1, PTE_PRESENT | PTE_USER);
    int r2 = vmm_map_page(pml4, USER_STACK_BASE, phys2, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    TEST_ASSERT_EQ(r1, 0);
    TEST_ASSERT_EQ(r2, 0);

    pmm_free(phys1);
    pmm_free(phys2);
    return TEST_PASS;
}

/*
 * Test address space switching.
 */
static int test_vmm_switch_space(void) {
    uint64_t kernel_pml4 = vmm_get_kernel_pml4();
    TEST_ASSERT_NOT_NULL(kernel_pml4);

    uint64_t user_pml4 = vmm_create_address_space();
    TEST_ASSERT_NOT_NULL(user_pml4);

    /* Switch to user address space */
    vmm_switch_address_space(user_pml4);

    /* Switch back to kernel - should not crash */
    vmm_switch_address_space(kernel_pml4);

    return TEST_PASS;
}

/*
 * Test kernel PML4 retrieval.
 */
static int test_vmm_kernel_pml4(void) {
    uint64_t pml4 = vmm_get_kernel_pml4();
    TEST_ASSERT_NOT_NULL(pml4);

    /* Should be consistent */
    uint64_t pml4_2 = vmm_get_kernel_pml4();
    TEST_ASSERT_EQ(pml4, pml4_2);

    return TEST_PASS;
}

/*
 * Test writing to mapped memory via HHDM.
 */
static int test_vmm_write_mapped(void) {
    uint64_t hhdm = test_get_hhdm_offset();
    if(hhdm == 0) {
        return TEST_SKIP;
    }

    uint64_t pml4 = vmm_create_address_space();
    TEST_ASSERT_NOT_NULL(pml4);

    uint64_t phys = pmm_alloc();
    TEST_ASSERT_NOT_NULL(phys);

    int result = vmm_map_page(pml4, USER_CODE_BASE, phys, PTE_PRESENT | PTE_USER);
    TEST_ASSERT_EQ(result, 0);

    /* Write via HHDM */
    uint64_t *ptr = (uint64_t *)(phys + hhdm);
    ptr[0] = 0xCAFEBABEDEADBEEF;

    /* Read back */
    TEST_ASSERT_EQ(ptr[0], 0xCAFEBABEDEADBEEF);

    pmm_free(phys);
    return TEST_PASS;
}

/*
 * Test that kernel stays mapped in user address space.
 */
static int test_vmm_kernel_mapped(void) {
    uint64_t kernel_pml4 = vmm_get_kernel_pml4();
    uint64_t user_pml4 = vmm_create_address_space();
    TEST_ASSERT_NOT_NULL(user_pml4);

    /* Switch to user space */
    vmm_switch_address_space(user_pml4);

    /* We can still execute kernel code (this test is running!) */
    /* If kernel wasn't mapped, we would have crashed */

    /* Switch back */
    vmm_switch_address_space(kernel_pml4);

    return TEST_PASS;
}

/* =============================================================================
 * Test Registration
 * =============================================================================
 */

void test_vmm_register(void) {
    test_register("vmm", "create_space", "Create address space", test_vmm_create_space);
    test_register("vmm", "map_page", "Map a single page", test_vmm_map_page);
    test_register("vmm", "map_multiple", "Map multiple pages", test_vmm_map_multiple);
    test_register("vmm", "switch_space", "Switch address spaces", test_vmm_switch_space);
    test_register("vmm", "kernel_pml4", "Kernel PML4 retrieval", test_vmm_kernel_pml4);
    test_register("vmm", "write_mapped", "Write to mapped memory", test_vmm_write_mapped);
    test_register("vmm", "kernel_mapped", "Kernel mapped in user space", test_vmm_kernel_mapped);
}
