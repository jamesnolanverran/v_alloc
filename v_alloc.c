#include "v_alloc.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void *(*reserve)(size_t size);   
    bool (*commit)(void *addr, size_t total_size, size_t additional_bytes);    
    bool (*decommit)(void *addr, size_t size);  
    bool (*release)(void *addr, size_t size);  
    size_t page_size;               // System page size
} V_Allocator;

// MARK: WIN32
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>

    static void *v_alloc_win_reserve(size_t size);
    static bool v_alloc_win_commit(void *addr, size_t total_size, size_t additional_bytes);
    static bool v_alloc_win_decommit(void *addr, size_t size);
    static bool v_alloc_win_release(void *addr, size_t size);

    V_Allocator v_alloc = {
        .reserve = v_alloc_win_reserve,
        .commit = v_alloc_win_commit,
        .decommit = v_alloc_win_decommit,
        .release = v_alloc_win_release,
        .page_size = 0,
    };
    static size_t v_alloc_win_get_page_size(){
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        return (size_t)sys_info.dwPageSize;
    }
    static void *v_alloc_win_reserve(size_t size) {
        if(v_alloc.page_size == 0){
            v_alloc.page_size = v_alloc_win_get_page_size();
        }
        return VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
    }
    static bool v_alloc_win_commit(void *addr, size_t total_size, size_t additional_bytes) {
        (void)additional_bytes;
        void *result = VirtualAlloc(addr, total_size, MEM_COMMIT, PAGE_READWRITE);
        return result ? true : false;
    }
    static bool v_alloc_win_decommit(void *addr, size_t extra_size) {
        // VirtualFree(base_addr + 1MB, MEM_DECOMMIT, extra_size);
    /* 
        "The VirtualFree function can decommit a range of pages that are in 
        different states, some committed and some uncommitted. This means 
        that you can decommit a range of pages without first determining 
        the current commitment state of each page."
    */
        BOOL success = VirtualFree(addr, MEM_DECOMMIT, (DWORD)extra_size);
        return success ? true : false;
    }
    static bool v_alloc_win_release(void *addr, size_t size) {
        (void)size; 
        return VirtualFree(addr, 0, MEM_RELEASE);
    }

   // MARK: LINUX
#elif defined(__linux__) || defined(__APPLE__)
    #include <unistd.h>
    #include <sys/mman.h>

    static void *v_alloc_posix_reserve(size_t size);
    static bool v_alloc_posix_commit(void *addr, size_t total_size, size_t additional_bytes);
    static bool v_alloc_posix_decommit(void *addr, size_t extra_size);
    static bool v_alloc_posix_release(void *addr, size_t size);

    V_Allocator v_alloc = {
        .reserve = v_alloc_posix_reserve,
        .commit = v_alloc_posix_commit,
        .decommit = v_alloc_posix_decommit,
        .release = v_alloc_posix_release,
        .page_size = 0,
    };
    static size_t v_alloc_posix_get_page_size(){
        s32 page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0) {
            //todo: error ?
            return 0;
        }
        return (size_t)page_size;
    }
    static void *v_alloc_posix_reserve(size_t size) {
        if (v_alloc.page_size == 0) {
            v_alloc.page_size = v_alloc_posix_get_page_size();
        }
        void *ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        return ptr == MAP_FAILED ? NULL : ptr;
    }
    static bool v_alloc_posix_commit(void *addr, size_t total_size, size_t additional_bytes) {
        addr = (char *)addr + total_size - additional_bytes;
        s32 result = mprotect(addr, additional_bytes, PROT_READ | PROT_WRITE);
        return result ? true : false;
    }
    static bool v_alloc_posix_decommit(void *addr, size_t extra_size) {
        s32 result = madvise(addr, extra_size, MADV_DONTNEED);
        if(result == 0){
            result = mprotect(addr, extra_size, PROT_NONE);
        }
        return result == 0;
    }
    static bool v_alloc_posix_release(void *addr, size_t size) {
        return munmap(addr, size) == 0 ? true : false;
    }
#else
    #error "Unsupported platform"
#endif
// /////////////////////////////////////////////
// /////////////////////////////////////////////
// MARK: v_alloc
// /////////////////////////////////////////////
// /////////////////////////////////////////////

// returns true on success, false on fail
bool v_alloc_reserve(AllocInfo *alloc_info, size_t reserve_size) {
    alloc_info->base = (char*)v_alloc.reserve(reserve_size);
    if (alloc_info->base == NULL) {
        return false; // initialization failed
    }
    alloc_info->ptr = alloc_info->base;
    alloc_info->end = alloc_info->base; // because we're only reserving
    alloc_info->reserved_size = reserve_size;
    alloc_info->page_size = v_alloc.page_size;
    return true; 
}
// commits initial size or grows alloc_info by additional size, returns NULL on fail
void* v_alloc_committ(AllocInfo *alloc_info, size_t additional_bytes) {
    if(additional_bytes == 0){ // we will consider this an error
        return NULL;
    }
    additional_bytes = ALIGN_UP(additional_bytes, V_ALLOC_ALIGNMENT);
    if (additional_bytes > (size_t)(alloc_info->end - alloc_info->ptr)) {
        if(alloc_info->base == 0){ // reserve default
            if(!v_alloc_reserve(alloc_info, MAX_ARENA_CAPACITY)){
                return NULL; // unable to reserve memory
            }
        }
        // internally we align_up to page_size
        size_t adjusted_additional_bytes = ALIGN_UP(additional_bytes, alloc_info->page_size);

        if (alloc_info->ptr + adjusted_additional_bytes > alloc_info->base + alloc_info->reserved_size) {
            return NULL; // out of reserved memory
        }
        size_t new_size = alloc_info->end - alloc_info->base + adjusted_additional_bytes;
        int result = v_alloc.commit(alloc_info->base, new_size, adjusted_additional_bytes);
        if (result == -1) {
            return NULL; // failed commit
        }
        alloc_info->end = alloc_info->base + new_size;
    }
    void* ptr = alloc_info->ptr;
    alloc_info->ptr = alloc_info->ptr + additional_bytes; 
    return ptr; 
}
void v_alloc_reset(AllocInfo *alloc_info) {
    // reset the pointer to the start of the committed region
    // todo: decommit
    if(alloc_info){
        alloc_info->ptr = alloc_info->base;
    }
}
bool v_alloc_decommit(AllocInfo *alloc_info, size_t extra_size) {
    if (!alloc_info || extra_size == 0) {
        return false; // invalid input
    }
    // ensure extra_size is aligned to the page size
    extra_size = ALIGN_UP(extra_size, alloc_info->page_size);
    // ensure extra_size does not exceed the committed memory size
    if (extra_size > (size_t)(alloc_info->end - alloc_info->base)) {
        return false; // cannot decommit more memory than is committed
    }
    // ensure the decommit region is page-aligned
    char *decommit_start = ALIGN_DOWN_PTR(alloc_info->end - extra_size, alloc_info->page_size);
    // decommit the memory
    bool result = v_alloc.decommit(decommit_start, extra_size);
    if (result) {
        alloc_info->end = decommit_start;
    }
    return result;
}
bool v_alloc_free(AllocInfo* alloc_info) {
    if (alloc_info->base == NULL) {
        return false; // nothing to free
    }
    return v_alloc.release(alloc_info->base, alloc_info->reserved_size);
}


typedef struct AllocHdr {  
    AllocInfo alloc_info; 
    _Alignas(V_ALLOC_ALIGNMENT) char data[];
} AllocHdr;

static inline AllocHdr *v_alloc_hdr_from_data(void *data) { 
    return (AllocHdr*)((char*)(data) - offsetof(AllocHdr, data)); 
}

// same as above but param is total size in bytes - mimic behaviour of realloc but user is required to manage AllocInfo
void* v_alloc_resize(AllocInfo *alloc_info, size_t size_in_bytes) {
    if(size_in_bytes == 0){ 
        v_alloc_free(alloc_info);
        return NULL;
    }
    if (size_in_bytes > (size_t)(alloc_info->end - alloc_info->base)) {
        if(alloc_info->base == 0){ // reserve default
            if(!v_alloc_reserve(alloc_info, MAX_ARENA_CAPACITY)){
                return NULL; // unable to reserve memory
            }
        }
        // internally we align_up to page_size
        size_t adjusted_size_in_bytes = ALIGN_UP(size_in_bytes, alloc_info->page_size);

        if (adjusted_size_in_bytes + alloc_info->base > alloc_info->base + alloc_info->reserved_size) {
            return NULL; // out of reserved memory
        }
        size_t additional_bytes =  adjusted_size_in_bytes - (alloc_info->end - alloc_info->base);
        int result = v_alloc.commit(alloc_info->base, adjusted_size_in_bytes, additional_bytes);
        if (result == -1) {
            return NULL; // failed commit
        }
        alloc_info->end = alloc_info->base + adjusted_size_in_bytes;
        alloc_info->ptr = alloc_info->end;
    }
    return alloc_info->base;
}
// embeds AllocInfo at beginning of allocation
void *v_alloc_realloc(void *data, size_t total_size){
    AllocHdr *alloc_hdr;
    if(total_size == 0){
        if(!data){
            return NULL;
        }
        alloc_hdr = v_alloc_hdr_from_data(data);
        v_alloc_free(&alloc_hdr->alloc_info);
        return NULL;
    }
    total_size += offsetof(AllocHdr, data);
    if(!data){
        AllocInfo alloc_info = {0};
        if(!v_alloc_resize(&alloc_info, total_size)) {
            return NULL;
        }
        alloc_hdr = (AllocHdr*)alloc_info.base;
        alloc_hdr->alloc_info = alloc_info;
        return alloc_hdr->data;
    }
    alloc_hdr = v_alloc_hdr_from_data(data);
    if(!v_alloc_resize(&alloc_hdr->alloc_info, total_size)) {
        return NULL;
    }
    return alloc_hdr->data;
}