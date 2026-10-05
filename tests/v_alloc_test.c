// v_alloc_test.c -- core arena tests for the standalone v_alloc package.
//
// Covers arena reservation and initial state, commit alignment and page-aligned
// growth across boundaries, zero/exact/over-reservation commit behavior,
// implicit reservation, reset semantics, mark/release LIFO nesting and reuse,
// decommit behavior and rejection of invalid input, plus free/null handling.
@import("mverse-libs/mtest/mtest.h")
@import("v_alloc/v_alloc.h")
#include <stdint.h>
#include <string.h>

@test(reserve_initial_state) {
    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 4));
    @check(info.base != NULL);
    @check(info.ptr == info.base);
    @check(info.end == info.base);
    @check_eq(info.reserved_size, 4096 * 4);
    @check(info.page_size > 0);
    @check_eq(info.mark_depth, 0);
    @check(v_alloc_free(&info));
}

@test(commit_initial_state_and_alignment) {
    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 4));
    size_t page = info.page_size;

    void *p = v_alloc_commit(&info, 1);
    @check(p == info.base);
    @check(((uintptr_t)p % V_ALLOC_ALIGNMENT) == 0);
    @check_eq(info.ptr - info.base, 16);
    @check_eq(info.end - info.base, (long long)page);
    @check(v_alloc_free(&info));
}

@test(commit_growth_across_page_boundaries) {
    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 4));
    size_t page = info.page_size;

    void *p1 = v_alloc_commit(&info, 1);
    @check(p1 == info.base);
    @check(((uintptr_t)p1 % V_ALLOC_ALIGNMENT) == 0);
    @check_eq(info.ptr - info.base, 16);
    @check_eq(info.end - info.base, (long long)page);

    void *p2 = v_alloc_commit(&info, page);
    @check(p2 == info.base + 16);
    @check_eq(info.end - info.base, (long long)(2 * page));
    @check_eq(info.ptr - info.base, (long long)(page + 16));

    void *p3 = v_alloc_commit(&info, page - 16);
    @check(p3 == info.base + page + 16);
    @check_eq(info.end - info.base, (long long)(2 * page));

    void *p4 = v_alloc_commit(&info, 1);
    @check(p4 == info.base + 2 * page);
    @check_eq(info.end - info.base, (long long)(3 * page));
    @check_eq(info.ptr - info.base, (long long)(2 * page + 16));

    @check(((uintptr_t)p1 % V_ALLOC_ALIGNMENT) == 0);
    @check(((uintptr_t)p2 % V_ALLOC_ALIGNMENT) == 0);
    @check(((uintptr_t)p4 % V_ALLOC_ALIGNMENT) == 0);
    @check(v_alloc_free(&info));
}

@test(commit_zero_rejected) {
    AllocInfo zeroed = {0};
    @check(v_alloc_commit(&zeroed, 0) == NULL);
    @check(zeroed.base == NULL);

    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 4));
    char *ptr_before = info.ptr;
    char *end_before = info.end;
    @check(v_alloc_commit(&info, 0) == NULL);
    @check(info.ptr == ptr_before);
    @check(info.end == end_before);
    @check(v_alloc_free(&info));
}

@test(commit_exact_reservation_boundary) {
    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 2));
    size_t page = info.page_size;

    void *p = v_alloc_commit(&info, 2 * page);
    @check(p == info.base);
    @check(info.ptr == info.end);
    @check_eq(info.end - info.base, (long long)(2 * page));
    @check(v_alloc_free(&info));
}

@test(commit_out_of_reservation_fails_preserving_state) {
    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 2));
    size_t page = info.page_size;

    void *p1 = v_alloc_commit(&info, 16);
    @check(p1 == info.base);

    char *ptr_before = info.ptr;
    char *end_before = info.end;
    @check(v_alloc_commit(&info, 2 * page) == NULL);
    @check(info.ptr == ptr_before);
    @check(info.end == end_before);

    void *p2 = v_alloc_commit(&info, page - 16);
    @check(p2 == info.base + 16);
    @check(info.ptr == info.end);
    @check(v_alloc_free(&info));
}

@test(commit_auto_reserve_when_unreserved) {
    AllocInfo info = {0};
    void *p = v_alloc_commit(&info, 32);
    @check(p != NULL);
    @check(p == info.base);
    @check_eq(info.reserved_size, MAX_ARENA_CAPACITY);
    @check_eq(info.end - info.base, (long long)info.page_size);
    @check(v_alloc_free(&info));
}

@test(reset_rewinds_cursor_and_mark_depth) {
    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 4));
    size_t page = info.page_size;

    @check(v_alloc_commit(&info, 32) == info.base);
    VAllocMark m1 = v_alloc_mark(&info);
    @check_eq(m1.depth, 1);
    @check(v_alloc_commit(&info, 64) == info.base + 32);
    VAllocMark m2 = v_alloc_mark(&info);
    @check_eq(m2.depth, 2);
    @check(v_alloc_commit(&info, 32) == info.base + 96);
    @check_eq(info.mark_depth, 2);
    @check_eq(info.end - info.base, (long long)page);

    char *end_before = info.end;
    v_alloc_reset(&info);
    @check(info.ptr == info.base);
    @check_eq(info.mark_depth, 0);
    @check(info.end == end_before);

    @check(v_alloc_commit(&info, 16) == info.base);
    @check(info.end == end_before);
    @check(v_alloc_free(&info));
}

@test(mark_release_nesting_rewind_and_reuse) {
    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 4));

    VAllocMark m1 = v_alloc_mark(&info);
    @check_eq(m1.depth, 1);
    @check(m1.ptr == info.base);

    @check(v_alloc_commit(&info, 64) == info.base);
    VAllocMark m2 = v_alloc_mark(&info);
    @check_eq(m2.depth, 2);
    @check(m2.ptr == info.base + 64);
    @check(v_alloc_commit(&info, 64) == info.base + 64);

    v_alloc_release(&info, m2);
    @check(info.ptr == m2.ptr);
    @check_eq(info.mark_depth, 1);

    @check(v_alloc_commit(&info, 64) == m2.ptr);

    v_alloc_release(&info, m1);
    @check(info.ptr == info.base);
    @check_eq(info.mark_depth, 0);

    @check(v_alloc_commit(&info, 64) == info.base);
    @check(v_alloc_free(&info));
}

@test(release_does_not_decommit) {
    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 4));
    size_t page = info.page_size;

    @check(v_alloc_commit(&info, 1) == info.base);
    VAllocMark mark = v_alloc_mark(&info);
    @check(v_alloc_commit(&info, page) != NULL);
    @check_eq(info.end - info.base, (long long)(2 * page));

    v_alloc_release(&info, mark);
    @check(info.ptr == mark.ptr);
    @check_eq(info.end - info.base, (long long)(2 * page));

    @check(v_alloc_commit(&info, page - 16) != NULL);
    @check_eq(info.end - info.base, (long long)(2 * page));
    @check(v_alloc_free(&info));
}

@test(decommit_releases_pages_and_recommit_works) {
    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 4));
    size_t page = info.page_size;

    @check(v_alloc_commit(&info, 2 * page) == info.base);
    @check_eq(info.end - info.base, (long long)(2 * page));

    v_alloc_reset(&info);
    @check(v_alloc_decommit(&info, page));
    @check_eq(info.end - info.base, (long long)page);
    @check(info.ptr == info.base);

    @check(v_alloc_commit(&info, page) == info.base);
    @check(info.ptr == info.end);
    @check(v_alloc_free(&info));
}

@test(decommit_rejects_invalid_input) {
    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 4));
    size_t page = info.page_size;

    @check(v_alloc_commit(&info, 1) == info.base);
    char *end_before = info.end;

    @check(!v_alloc_decommit(&info, 0));
    @check(!v_alloc_decommit(&info, 2 * page));
    @check(info.end == end_before);

    @check(!v_alloc_decommit(NULL, page));
    @check(v_alloc_free(&info));
}

@test(free_and_null_inputs) {
    AllocInfo zeroed = {0};
    @check(!v_alloc_free(&zeroed));

    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 4));
    @check(v_alloc_free(&info));

    v_alloc_reset(NULL);
    v_alloc_reset(&zeroed);
    @check(zeroed.ptr == NULL);
    @check_eq(zeroed.mark_depth, 0);
}
