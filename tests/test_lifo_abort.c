/* Expected-failure check: marks must be released in strict LIFO order.
 *
 * The contract is enforced with assert(), so this program is supposed to
 * abort rather than return. The test runner treats a non-zero exit as a pass.
 *
 * If the build disables asserts (NDEBUG), the violation is not caught; this
 * program then says so and exits 0, and the runner reports the check as
 * skipped rather than pretending it passed.
 */

#include "v_alloc.h"

#include <stdio.h>

int main(void) {
    AllocInfo arena = {0};
    if (!v_alloc_reserve(&arena, 64 * 1024)) {
        printf("test_lifo_abort: could not reserve an arena\n");
        return 2;
    }

    VAllocMark outer = v_alloc_mark(&arena);
    (void)v_alloc_commit(&arena, 32);

    VAllocMark inner = v_alloc_mark(&arena);
    if (inner.depth != outer.depth + 1) {
        printf("test_lifo_abort: unexpected mark depth\n");
        return 2;
    }
    (void)v_alloc_commit(&arena, 32);

    /* Out of order on purpose: the inner mark is still outstanding. */
    v_alloc_release(&arena, outer);

    printf("test_lifo_abort: SKIP - released marks out of order without an "
           "assertion (asserts disabled?)\n");
    return 0;
}
