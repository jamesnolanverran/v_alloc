#ifndef V_ALLOC_H
#define V_ALLOC_H
#include <stdbool.h>
#include <stddef.h>

#ifndef V_ALLOC_ALIGNMENT
    #define V_ALLOC_ALIGNMENT 16
#endif
#define MAX_ARENA_CAPACITY (1024 * 1024 * 1024) 

typedef struct AllocInfo {
    char* base;
    char* ptr;
    char* end;
    size_t reserved_size;
    size_t page_size;
    size_t mark_depth;
} AllocInfo;

// Snapshot of an arena cursor taken by v_alloc_mark. `depth` identifies the
// nesting level of the mark (1-based); `ptr` is the cursor to rewind to.
typedef struct VAllocMark {
    char *ptr;
    size_t depth;
} VAllocMark;

bool v_alloc_reserve(AllocInfo* alloc_info, size_t reserve_size);
void *v_alloc_commit(AllocInfo* alloc_info, size_t additional_bytes);
bool v_alloc_decommit(AllocInfo *alloc_info, size_t extra_size);
void v_alloc_reset(AllocInfo* alloc_info);
bool v_alloc_free(AllocInfo* alloc_info);

void *v_alloc_resize(AllocInfo *alloc_info, size_t size_in_bytes);
void *v_alloc_realloc(void *data, size_t total_size);

VAllocMark v_alloc_mark(AllocInfo *info);
void v_alloc_release(AllocInfo *info, VAllocMark mark);
#endif // V_ALLOC_H