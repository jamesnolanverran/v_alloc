#ifndef V_ALLOC_H
#define V_ALLOC_H
#include <stdbool.h>

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
} AllocInfo;

bool v_alloc_reserve(AllocInfo* alloc_info, size_t reserve_size);
void* v_alloc_committ(AllocInfo* alloc_info, size_t additional_bytes);
bool v_alloc_decommit(AllocInfo *alloc_info, size_t extra_size);
void v_alloc_reset(AllocInfo* alloc_info);
bool v_alloc_free(AllocInfo* alloc_info);

void* v_alloc_grow(AllocInfo *alloc_info, size_t total_size);
void *v_alloc_realloc(void *data, size_t total_size);
#endif // V_ALLOC_H