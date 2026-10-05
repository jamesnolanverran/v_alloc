// v_alloc_realloc_test.c -- resize/realloc tests for the standalone v_alloc package.
//
// Covers v_alloc_resize growth stability and content preservation, implicit
// reservation, zero-size free, out-of-reservation rejection, and
// v_alloc_realloc header-based allocation, in-place growth across pages,
// pattern preservation, and null/zero/over-reservation edge cases.
@import("mtest/mtest.h")
@import("v_alloc/v_alloc.h")
#include <stdint.h>
#include <string.h>

@test(resize_growth_is_stable_and_preserves_contents) {
    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 8));
    size_t page = info.page_size;

    void *p = v_alloc_resize(&info, 100);
    @check(p != NULL);
    @check(p == info.base);
    @check_eq(info.end - info.base, (long long)page);
    @check(info.ptr == info.end);

    memset(p, 0x5A, 100);

    void *q = v_alloc_resize(&info, 2 * page + 100);
    @check(q == p);
    @check_eq(info.end - info.base, (long long)(3 * page));
    @check(((unsigned char *)q)[0] == 0x5A);
    @check(((unsigned char *)q)[99] == 0x5A);
    @check(v_alloc_free(&info));
}

@test(resize_auto_reserve_when_unreserved) {
    AllocInfo info = {0};
    void *p = v_alloc_resize(&info, 64);
    @check(p != NULL);
    @check(p == info.base);
    @check_eq(info.reserved_size, MAX_ARENA_CAPACITY);
    @check(v_alloc_free(&info));
}

@test(resize_zero_frees_and_returns_null) {
    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 2));
    size_t page = info.page_size;

    @check(v_alloc_resize(&info, page) != NULL);
    @check(v_alloc_resize(&info, 0) == NULL);
}

@test(resize_out_of_reservation_fails_and_state_unchanged) {
    AllocInfo info = {0};
    @check(v_alloc_reserve(&info, 4096 * 2));
    size_t page = info.page_size;

    @check(v_alloc_resize(&info, page) != NULL);
    char *base_before = info.base;
    char *end_before = info.end;

    @check(v_alloc_resize(&info, 3 * page) == NULL);
    @check(info.base == base_before);
    @check(info.end == end_before);

    @check(v_alloc_resize(&info, 2 * page) != NULL);
    @check_eq(info.end - info.base, (long long)(2 * page));
    @check(v_alloc_free(&info));
}

@test(realloc_null_allocates_and_grows_in_place) {
    void *p = v_alloc_realloc(NULL, 64);
    @check(p != NULL);
    @check(((uintptr_t)p % V_ALLOC_ALIGNMENT) == 0);
    memset(p, 0x7E, 64);

    void *q = v_alloc_realloc(p, 4096);
    @check(q == p);
    @check(((unsigned char *)q)[0] == 0x7E);
    @check(((unsigned char *)q)[63] == 0x7E);

    @check(v_alloc_realloc(p, 0) == NULL);
}

@test(realloc_grow_preserves_pattern_across_pages) {
    void *p = v_alloc_realloc(NULL, 200);
    @check(p != NULL);

    unsigned char expected[200];
    for (size_t i = 0; i < 200; ++i) {
        ((unsigned char *)p)[i] = (unsigned char)(i * 7);
        expected[i] = (unsigned char)(i * 7);
    }

    void *q = v_alloc_realloc(p, 5000);
    @check(q == p);
    @check(memcmp(q, expected, 200) == 0);

    void *r = v_alloc_realloc(q, 9000);
    @check(r == q);
    @check(memcmp(r, expected, 200) == 0);

    @check(v_alloc_realloc(r, 0) == NULL);
}

@test(realloc_null_zero_is_noop) {
    @check(v_alloc_realloc(NULL, 0) == NULL);
}

@test(realloc_out_of_reservation_fails) {
    @check(v_alloc_realloc(NULL, MAX_ARENA_CAPACITY) == NULL);
}
