// examples/lifo_violation.c -- assert-abort probe for strict-LIFO mark release.
//
// Releases an older mark before a newer one. Under asserts this aborts; the
// SIGABRT handler converts that into a deterministic exit 42 so the harness can
// assert the failure without aborting the main suite.
@import("v_alloc/v_alloc.h")
#include <signal.h>
#include <stdlib.h>

static void on_abort(int sig) {
    (void)sig;
    _exit(42);
}

int main(void) {
    signal(SIGABRT, on_abort);
    AllocInfo arena = {0};
    if (!v_alloc_reserve(&arena, 4 * 4096)) return 1;
    VAllocMark outer = v_alloc_mark(&arena);
    (void)v_alloc_commit(&arena, 64);
    VAllocMark inner = v_alloc_mark(&arena);
    (void)v_alloc_commit(&arena, 64);
    /* Strict-LIFO violation: release the older mark first. Under asserts this
       aborts; the handler converts the abort into a deterministic exit 42 so
       the harness can assert it without the main suite aborting. */
    v_alloc_release(&arena, outer);
    (void)inner;
    return 0;
}
