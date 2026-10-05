/* Tests for v_alloc.
 *
 * Plain C, no dependencies: build with any C11 compiler and run.
 *
 * The file has two sections, and they overlap on purpose:
 *
 *   1. Common use patterns - the idiomatic shapes from the readme, kept
 *      together up front so the expected usage is readable in one place.
 *   2. Detailed behaviour - the full contract, edge cases, and the ways the
 *      API is expected to fail.
 *
 * A failing check prints one line and the run reports how many failed; the
 * process exits non-zero if any check failed.
 */

#include "v_alloc.h"

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        g_checks++;                                                     \
        if (!(cond)) {                                                  \
            g_failures++;                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                               \
    } while (0)

static void section(const char *title) {
    printf("\n%s\n", title);
    printf("--------------------------------------------------------------\n");
}

static void report(const char *name) {
    printf("  %-42s checks=%d failures=%d\n", name, g_checks, g_failures);
}

/* ==========================================================================
 * Section 1: common use patterns
 *
 * The shapes a caller reaches for first. These are the readme examples with
 * real assertions attached.
 * ========================================================================== */

/* Reserve an arena, bump some values into it, then release the whole region. */
static void pattern_bump_and_free(void) {
    section("1. Common use patterns");

    AllocInfo arena = {0};
    CHECK(v_alloc_reserve(&arena, 64 * 1024));

    char *greeting = v_alloc_commit(&arena, 32);
    CHECK(greeting != NULL);
    snprintf(greeting, 32, "Hello, v_alloc!");
    CHECK(strcmp(greeting, "Hello, v_alloc!") == 0);

    int *numbers = v_alloc_commit(&arena, 10 * sizeof(int));
    CHECK(numbers != NULL);
    for (int i = 0; i < 10; i++) {
        numbers[i] = i * 10;
    }
    CHECK(numbers[0] == 0 && numbers[9] == 90);

    /* Earlier allocations stay valid: the allocator never moves memory. */
    CHECK(strcmp(greeting, "Hello, v_alloc!") == 0);

    CHECK(v_alloc_free(&arena));
}

/* The arena reserves itself on the first commit, so a caller can skip the
 * explicit reserve when the default size is fine. */
static void pattern_auto_reserve(void) {
    AllocInfo arena = {0};
    CHECK(arena.base == NULL);

    int *value = v_alloc_commit(&arena, sizeof(int));
    CHECK(value != NULL);
    CHECK(arena.base != NULL);
    CHECK(arena.reserved_size == MAX_ARENA_CAPACITY);

    *value = 42;
    CHECK(*value == 42);

    CHECK(v_alloc_free(&arena));
}

/* Many small allocations advance through the arena without overlapping. */
static void pattern_many_small_allocations(void) {
    AllocInfo arena = {0};
    CHECK(v_alloc_reserve(&arena, 256 * 1024));

    char *previous = NULL;
    for (int i = 0; i < 64; i++) {
        char *block = v_alloc_commit(&arena, 100);
        CHECK(block != NULL);
        if (previous != NULL) {
            CHECK(block > previous);
            CHECK(block >= previous + 100);
        }
        memset(block, i, 100);
        CHECK(block[0] == (char)i && block[99] == (char)i);
        previous = block;
    }

    CHECK(v_alloc_free(&arena));
}

/* Scoped temporaries: take a mark, use the arena, rewind to the mark. */
static void pattern_scoped_temporaries(void) {
    AllocInfo arena = {0};
    CHECK(v_alloc_reserve(&arena, 64 * 1024));

    char *permanent = v_alloc_commit(&arena, 64);
    CHECK(permanent != NULL);
    memcpy(permanent, "keep me", sizeof("keep me"));

    VAllocMark mark = v_alloc_mark(&arena);
    CHECK(mark.ptr != NULL);

    char *scratch = v_alloc_commit(&arena, 4096);
    CHECK(scratch != NULL);
    memcpy(scratch, "temporary", sizeof("temporary"));

    v_alloc_release(&arena, mark);

    /* The scratch space is rewound and handed out again; the permanent
     * allocation is untouched. */
    char *reused = v_alloc_commit(&arena, 4096);
    CHECK(reused == scratch);
    CHECK(strcmp(permanent, "keep me") == 0);

    CHECK(v_alloc_free(&arena));
}

/* A growable buffer: the base pointer is stable, so content survives growth. */
static void pattern_growable_buffer(void) {
    char *buffer = v_alloc_realloc(NULL, 64);
    CHECK(buffer != NULL);
    memcpy(buffer, "grow me", sizeof("grow me"));

    char *grown = v_alloc_realloc(buffer, 8 * 1024);
    CHECK(grown != NULL);
    CHECK(grown == buffer);
    CHECK(strcmp(grown, "grow me") == 0);

    memcpy(grown, "grow me again", sizeof("grow me again"));
    CHECK(strcmp(grown, "grow me again") == 0);

    CHECK(v_alloc_realloc(grown, 0) == NULL);
}

/* ==========================================================================
 * Section 2: detailed behaviour, edge cases and failure modes
 * ========================================================================== */

/* reserve leaves the arena empty but ready. */
static void detail_reserve_initial_state(void) {
    section("2. Detailed behaviour and failure modes");

    AllocInfo arena = {0};
    CHECK(v_alloc_reserve(&arena, 64 * 1024));

    CHECK(arena.base != NULL);
    CHECK(arena.ptr == arena.base);   /* nothing allocated yet */
    CHECK(arena.end == arena.base);   /* nothing committed yet */
    CHECK(arena.reserved_size == 64 * 1024);
    CHECK(arena.page_size > 0);
    CHECK(arena.mark_depth == 0);

    CHECK(v_alloc_free(&arena));
}

/* An impossible reservation fails and reports it. */
static void detail_reserve_failure(void) {
    AllocInfo arena = {0};
    CHECK(!v_alloc_reserve(&arena, (size_t)-1));
    CHECK(arena.base == NULL);
    CHECK(arena.ptr == NULL);
    CHECK(arena.end == NULL);

    /* A normal reservation still works afterwards. */
    CHECK(v_alloc_reserve(&arena, 4096));
    CHECK(v_alloc_free(&arena));
}

/* Committing zero bytes is an error, not an empty allocation. */
static void detail_commit_zero_rejected(void) {
    AllocInfo arena = {0};
    CHECK(v_alloc_reserve(&arena, 64 * 1024));

    char *before = arena.ptr;
    CHECK(v_alloc_commit(&arena, 0) == NULL);
    CHECK(arena.ptr == before);   /* failure consumes nothing */

    CHECK(v_alloc_free(&arena));
}

/* Successive allocations are distinct and aligned. */
static void detail_commit_alignment(void) {
    AllocInfo arena = {0};
    CHECK(v_alloc_reserve(&arena, 64 * 1024));

    char *first = v_alloc_commit(&arena, 1);
    char *second = v_alloc_commit(&arena, 1);
    char *third = v_alloc_commit(&arena, 1);
    CHECK(first != NULL && second != NULL && third != NULL);

    CHECK((size_t)(second - first) >= V_ALLOC_ALIGNMENT);
    CHECK((size_t)(third - second) >= V_ALLOC_ALIGNMENT);
    CHECK(((size_t)first % V_ALLOC_ALIGNMENT) == 0);
    CHECK(((size_t)second % V_ALLOC_ALIGNMENT) == 0);
    CHECK(((size_t)third % V_ALLOC_ALIGNMENT) == 0);

    CHECK(v_alloc_free(&arena));
}

/* Committing past the reservation fails and leaves the arena usable. */
static void detail_commit_beyond_reservation(void) {
    AllocInfo arena = {0};
    CHECK(v_alloc_reserve(&arena, 64 * 1024));

    char *kept = v_alloc_commit(&arena, 128);
    CHECK(kept != NULL);
    memcpy(kept, "still here", sizeof("still here"));

    char *end_before = arena.end;
    CHECK(v_alloc_commit(&arena, 128 * 1024) == NULL);
    CHECK(arena.end == end_before);
    CHECK(strcmp(kept, "still here") == 0);

    CHECK(v_alloc_free(&arena));
}

/* reset rewinds the cursor without releasing the committed pages. */
static void detail_reset(void) {
    AllocInfo arena = {0};
    CHECK(v_alloc_reserve(&arena, 64 * 1024));

    char *first = v_alloc_commit(&arena, 512);
    CHECK(first != NULL);
    char *end_committed = arena.end;

    VAllocMark mark = v_alloc_mark(&arena);
    CHECK(mark.depth == 1);

    v_alloc_reset(&arena);
    CHECK(arena.ptr == arena.base);
    CHECK(arena.mark_depth == 0);   /* reset clears outstanding marks */
    CHECK(arena.end == end_committed);   /* but does not decommit */

    /* The first allocation's space is handed out again. */
    char *again = v_alloc_commit(&arena, 512);
    CHECK(again == first);

    CHECK(v_alloc_free(&arena));
}

/* Marks nest, release rewinds to the mark, and space is reused. */
static void detail_mark_release_nesting(void) {
    AllocInfo arena = {0};
    CHECK(v_alloc_reserve(&arena, 64 * 1024));

    VAllocMark outer = v_alloc_mark(&arena);
    CHECK(outer.depth == 1);
    char *a = v_alloc_commit(&arena, 128);
    CHECK(a != NULL);

    VAllocMark inner = v_alloc_mark(&arena);
    CHECK(inner.depth == 2);
    char *b = v_alloc_commit(&arena, 256);
    CHECK(b != NULL);

    v_alloc_release(&arena, inner);
    CHECK(arena.ptr == a + 128);   /* rewound to just after a */
    CHECK(arena.mark_depth == 1);

    char *b_again = v_alloc_commit(&arena, 256);
    CHECK(b_again == b);   /* rewound space is reused */

    v_alloc_release(&arena, outer);
    CHECK(arena.ptr == arena.base);
    CHECK(arena.mark_depth == 0);

    CHECK(v_alloc_free(&arena));
}

/* Releasing a mark never returns pages to the OS. */
static void detail_release_does_not_decommit(void) {
    AllocInfo arena = {0};
    CHECK(v_alloc_reserve(&arena, 256 * 1024));

    CHECK(v_alloc_commit(&arena, 8192) != NULL);

    VAllocMark mark = v_alloc_mark(&arena);
    CHECK(v_alloc_commit(&arena, 4096) != NULL);
    char *end_after_commit = arena.end;

    v_alloc_release(&arena, mark);

    /* The cursor rewinds, but nothing is handed back to the OS. */
    CHECK(arena.ptr == mark.ptr);
    CHECK(arena.end == end_after_commit);

    CHECK(v_alloc_free(&arena));
}

/* decommit rejects input it cannot honour. */
static void detail_decommit_rejects_invalid(void) {
    AllocInfo arena = {0};
    CHECK(v_alloc_reserve(&arena, 64 * 1024));
    CHECK(v_alloc_commit(&arena, 4096) != NULL);

    CHECK(!v_alloc_decommit(NULL, 4096));
    CHECK(!v_alloc_decommit(&arena, 0));
    CHECK(!v_alloc_decommit(&arena, 128 * 1024));   /* more than is committed */

    CHECK(v_alloc_free(&arena));
}

/* decommit returns committed-but-unused pages, after which they can be
 * committed again. Only unused memory may be decommitted. */
static void detail_decommit_then_reuse(void) {
    AllocInfo arena = {0};
    CHECK(v_alloc_reserve(&arena, 256 * 1024));
    CHECK(v_alloc_commit(&arena, 8192) != NULL);

    char *end_before = arena.end;

    v_alloc_reset(&arena);   /* release the whole committed region first */
    CHECK(v_alloc_decommit(&arena, 4096));
    CHECK(arena.end == end_before - 4096);

    char *reused = v_alloc_commit(&arena, 4096);
    CHECK(reused == arena.base);

    CHECK(v_alloc_free(&arena));
}

/* resize manages an arena the caller owns. */
static void detail_resize(void) {
    AllocInfo arena = {0};

    void *base = v_alloc_resize(&arena, 1024);
    CHECK(base != NULL);
    memcpy(base, "preserved", 10);

    void *grown = v_alloc_resize(&arena, 64 * 1024);
    CHECK(grown == base);   /* the base never moves */
    CHECK(memcmp(grown, "preserved", 10) == 0);

    CHECK(v_alloc_resize(&arena, 0) == NULL);   /* zero frees */
}

/* realloc grows in place, and its edge cases are defined. */
static void detail_realloc(void) {
    CHECK(v_alloc_realloc(NULL, 0) == NULL);   /* no-op */

    char *buffer = v_alloc_realloc(NULL, 16);
    CHECK(buffer != NULL);
    memcpy(buffer, "stable", sizeof("stable"));

    char *across_pages = v_alloc_realloc(buffer, 16 * 1024);
    CHECK(across_pages != NULL);
    CHECK(across_pages == buffer);   /* stable pointer, no copy */
    CHECK(strcmp(across_pages, "stable") == 0);

    CHECK(v_alloc_realloc(across_pages, 0) == NULL);
}

/* free reports whether there was anything to release. */
static void detail_free(void) {
    AllocInfo empty = {0};
    CHECK(!v_alloc_free(&empty));   /* nothing to free */

    AllocInfo arena = {0};
    CHECK(v_alloc_reserve(&arena, 4096));
    CHECK(v_alloc_free(&arena));
}

int main(void) {
    printf("v_alloc tests\n");

    /* Section 1: common use patterns. */
    pattern_bump_and_free();
    pattern_auto_reserve();
    pattern_many_small_allocations();
    pattern_scoped_temporaries();
    pattern_growable_buffer();

    /* Section 2: detailed behaviour and failure modes. */
    detail_reserve_initial_state();
    detail_reserve_failure();
    detail_commit_zero_rejected();
    detail_commit_alignment();
    detail_commit_beyond_reservation();
    detail_reset();
    detail_mark_release_nesting();
    detail_release_does_not_decommit();
    detail_decommit_rejects_invalid();
    detail_decommit_then_reuse();
    detail_resize();
    detail_realloc();
    detail_free();

    report("total");
    if (g_failures != 0) {
        printf("\nFAILED: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("\nOK: %d checks passed\n", g_checks);
    return 0;
}
