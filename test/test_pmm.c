/*
 * test_pmm.c - Physical Memory Manager Tests
 *
 * Tests for the bitmap-based physical page allocator.
 */

#include "test_framework.h"
#include "pmm.h"

/* =============================================================================
 * PMM Tests
 * =============================================================================
 */

/*
 * Test basic page allocation.
 */
static int test_pmm_alloc_basic(void) {
    uint64_t page = pmm_alloc();
    TEST_ASSERT_NOT_NULL(page);

    /* Page should be aligned to 4KB */
    TEST_ASSERT_EQ(page & 0xFFF, 0);

    pmm_free(page);
    return TEST_PASS;
}

/*
 * Test allocating and freeing a page.
 */
static int test_pmm_alloc_free(void) {
    uint64_t free_before = pmm_get_free_pages();

    /* Allocate a page */
    uint64_t page = pmm_alloc();
    TEST_ASSERT_NOT_NULL(page);

    uint64_t free_after_alloc = pmm_get_free_pages();
    TEST_ASSERT_EQ(free_after_alloc, free_before - 1);

    /* Free the page */
    pmm_free(page);

    uint64_t free_after_free = pmm_get_free_pages();
    TEST_ASSERT_EQ(free_after_free, free_before);

    return TEST_PASS;
}

/*
 * Test allocating multiple pages.
 */
static int test_pmm_alloc_multiple(void) {
    uint64_t pages[8];
    int i;

    /* Allocate 8 pages */
    for (i = 0; i < 8; i++) {
        pages[i] = pmm_alloc();
        TEST_ASSERT_NOT_NULL(pages[i]);
    }

    /* All pages should be unique */
    for (i = 0; i < 8; i++) {
        for (int j = i + 1; j < 8; j++) {
            TEST_ASSERT_NEQ(pages[i], pages[j]);
        }
    }

    /* Free all pages */
    for (i = 0; i < 8; i++) {
        pmm_free(pages[i]);
    }

    return TEST_PASS;
}

/*
 * Test that freed pages can be reallocated.
 */
static int test_pmm_realloc(void) {
    /* Allocate a page */
    uint64_t page1 = pmm_alloc();
    TEST_ASSERT_NOT_NULL(page1);

    /* Free it */
    pmm_free(page1);

    /* Allocate again - should get the same page back (or another free one) */
    uint64_t page2 = pmm_alloc();
    TEST_ASSERT_NOT_NULL(page2);

    pmm_free(page2);
    return TEST_PASS;
}

/*
 * Test page count consistency.
 */
static int test_pmm_counts(void) {
    uint64_t total = pmm_get_total_pages();
    uint64_t free = pmm_get_free_pages();

    /* Free pages should never exceed total */
    TEST_ASSERT(free <= total);

    /* Total should be non-zero */
    TEST_ASSERT(total > 0);

    return TEST_PASS;
}

/*
 * Test memory write/read via HHDM after allocation.
 */
static int test_pmm_write_read(void) {
    uint64_t hhdm = test_get_hhdm_offset();
    if (hhdm == 0) {
        return TEST_SKIP;  /* HHDM not available */
    }

    uint64_t page = pmm_alloc();
    TEST_ASSERT_NOT_NULL(page);

    /* Write test pattern via HHDM */
    uint64_t *ptr = (uint64_t *)(page + hhdm);
    ptr[0] = 0xDEADBEEFCAFEBABE;
    ptr[1] = 0x1234567890ABCDEF;

    /* Read back and verify */
    TEST_ASSERT_EQ(ptr[0], 0xDEADBEEFCAFEBABE);
    TEST_ASSERT_EQ(ptr[1], 0x1234567890ABCDEF);

    pmm_free(page);
    return TEST_PASS;
}

/* =============================================================================
 * Test Registration
 * =============================================================================
 */

void test_pmm_register(void) {
    test_register("pmm", "alloc_basic", "Basic page allocation", test_pmm_alloc_basic);
    test_register("pmm", "alloc_free", "Allocate and free cycle", test_pmm_alloc_free);
    test_register("pmm", "alloc_multiple", "Allocate multiple pages", test_pmm_alloc_multiple);
    test_register("pmm", "realloc", "Reallocation of freed pages", test_pmm_realloc);
    test_register("pmm", "counts", "Page count consistency", test_pmm_counts);
    test_register("pmm", "write_read", "Write/read via HHDM", test_pmm_write_read);
}
