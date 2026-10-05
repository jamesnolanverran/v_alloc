# v_alloc: Cross-Platform Virtual Memory Arena Allocator

## Overview

`v_alloc` is a lightweight, cross-platform virtual memory allocator. It provides two key allocation strategies:

1. **Bump Arena Allocation** – A simple bump allocator that allows sequential allocations with minimal overhead.
2. **Reallocation (Stable Virtual Memory)** – A `realloc`-like API that embeds metadata in the allocation and never moves the base pointer, leveraging OS-level virtual memory mechanisms (`mmap` on POSIX and `VirtualAlloc` on Windows).

## Features

- **Cross-platform support** (Linux, macOS, Windows)
- **Stable pointers** – memory is never moved after allocation
- **Bump allocator with reset and mark/release scopes** – efficient for temporary allocations
- **Efficient virtual memory management** – avoids copying overhead
- **Drop-in realloc replacement** – through `v_alloc_realloc`

## AllocInfo

`AllocInfo` describes one reserved arena. It is not a plain two-field struct:

```c
typedef struct AllocInfo {
    char*  base;           // start of the reserved region
    char*  ptr;            // current bump cursor
    char*  end;            // end of the committed region
    size_t reserved_size;  // total bytes reserved (never committed up front)
    size_t page_size;      // OS page size for this arena
    size_t mark_depth;     // current mark/release nesting depth (0 when none)
} AllocInfo;
```

`base` and `reserved_size` describe the whole reservation, while `end` tracks how much of it has actually been committed. `ptr` is the bump cursor advanced by `v_alloc_commit`. `mark_depth` counts outstanding `v_alloc_mark` scopes and is maintained by `v_alloc_mark`/`v_alloc_release`/`v_alloc_reset`; callers do not set it directly.

## API

### Bump Arena Allocation

#### `bool v_alloc_reserve(AllocInfo *alloc_info, size_t reserve_size)`

Reserves a large virtual memory region for use with the bump allocator. The region is reserved but not committed. Returns `true` on success and `false` on failure.

#### `void *v_alloc_commit(AllocInfo *alloc_info, size_t additional_bytes)`

Commits and allocates `additional_bytes` from the reserved region, growing the committed area when necessary. Returns a pointer to the usable memory on success, or `NULL` on failure. `v_alloc_commit(info, 0)` is treated as an error and returns `NULL`.

#### `bool v_alloc_decommit(AllocInfo *alloc_info, size_t extra_size)`

Decommits the trailing committed region of the arena and adjusts `alloc_info->end` to the new boundary. `extra_size` is aligned up to the page size and must be page-representable; the decommitted region starts at the page-aligned boundary derived from `end - extra_size`. Returns `true` on success and `false` for invalid input (a `NULL` allocator or `extra_size == 0`), when `extra_size` exceeds the committed size, or when the underlying OS decommit fails.

Only decommit memory that is **not in use**. Do not decommit into live allocations below `ptr` – the function does not track live allocations and will not refuse such a call; call `v_alloc_reset` first to release the whole committed region.

#### `void v_alloc_reset(AllocInfo *alloc_info)`

Resets the arena: sets `ptr` back to `base` and resets `mark_depth` to `0`. It does **not** decommit or release any pages – previously committed memory stays committed and is reused by later allocations. A `NULL` allocator is ignored.

#### `bool v_alloc_free(AllocInfo *alloc_info)`

Releases the entire reserved virtual memory region back to the OS. Returns `false` when `base == NULL` (nothing to free), otherwise returns the result of the underlying release.

#### `VAllocMark v_alloc_mark(AllocInfo *info)` / `void v_alloc_release(AllocInfo *info, VAllocMark mark)`

Scope-based rewinding of the bump cursor. See [Memory Scopes: Mark and Release](#memory-scopes-mark-and-release).

**Example:**

```c
// Initialize virtual memory allocator
AllocInfo alloc_info = {0};
size_t reserve_size = 1024 * 1024; // 1MB
if (!v_alloc_reserve(&alloc_info, reserve_size)) {
    // handle error
}

// Allocate a string using the bump allocator
size_t str_len = 32;
char *str = v_alloc_commit(&alloc_info, str_len + 1);
if (!str) {
    // handle error
}
snprintf(str, str_len + 1, "Hello, V_Alloc!");
printf("%s\n", str);

// Reset allocator (memory can be reused)
v_alloc_reset(&alloc_info);

// Allocate another object
int *arr = v_alloc_commit(&alloc_info, 10 * sizeof(int));
if (!arr) {
    // handle error
}
arr[0] = 33;
printf("First value: %d\n", arr[0]);

// Free virtual memory
if (!v_alloc_free(&alloc_info)) {
    // handle error
}
```

## Memory Scopes: Mark and Release

A mark snapshots the current bump cursor so that everything allocated after it can be handed back in one step.

- `VAllocMark v_alloc_mark(AllocInfo *info)` records `info->ptr` in the returned mark and increments `info->mark_depth`. The mark's `depth` is the **1-based** nesting level of the scope (the first mark has depth `1`, the next nested mark depth `2`, and so on).
- `void v_alloc_release(AllocInfo *info, VAllocMark mark)` rewinds `info->ptr` to `mark.ptr` and restores `mark_depth` to `mark.depth - 1`.

Marks must be released in **strict LIFO order**: the most recently taken mark is the one to release next. `v_alloc_release` asserts both the LIFO depth (`info->mark_depth == mark.depth`) and that the mark still lies inside the arena (`base <= mark.ptr <= ptr`). These are `assert`s: they are active in debug builds and compiled out when `NDEBUG` is defined.

Releasing only moves the cursor. Committed pages stay committed and are reused by later allocations – `v_alloc_release` does **not** decommit or free anything. To actually return pages to the OS, use `v_alloc_decommit` or `v_alloc_free`.

```c
AllocInfo arena = {0};
v_alloc_reserve(&arena, 1024 * 1024);

VAllocMark outer = v_alloc_mark(&arena);

int *a = v_alloc_commit(&arena, 16 * sizeof(int));
// ... use a ...

{
    VAllocMark inner = v_alloc_mark(&arena);
    int *b = v_alloc_commit(&arena, 16 * sizeof(int));
    // ... use b ...
    v_alloc_release(&arena, inner); // rewinds b; a stays valid
}

v_alloc_release(&arena, outer);     // rewinds a
```

## Failure and return conventions

- `v_alloc_reserve` returns `bool`: `false` on failure.
- `v_alloc_commit` returns a usable pointer, or `NULL` on failure; `v_alloc_commit(info, 0)` is treated as an error and returns `NULL`.
- `v_alloc_decommit` and `v_alloc_free` return `bool`. `v_alloc_free` returns `false` when `base == NULL`.
- `v_alloc_reset` returns `void`.
- `v_alloc_resize` returns the arena base pointer, or `NULL` on failure.
- `v_alloc_realloc` returns a usable pointer or `NULL`:
  - `v_alloc_realloc(NULL, n)` allocates a new block;
  - `v_alloc_realloc(ptr, 0)` frees the block and returns `NULL`;
  - `v_alloc_realloc(NULL, 0)` is a no-op returning `NULL`.
- `v_alloc_mark` always returns a `VAllocMark`; `v_alloc_release` returns `void`.

On failure memory is not consumed and existing allocations stay valid.

## Reallocation API

### `v_alloc_resize(AllocInfo *alloc_info, size_t size_in_bytes)`

Resizes an allocation, manually managing an `AllocInfo` struct. If `size_in_bytes == 0`, the memory is freed and `NULL` is returned. On success it returns the arena base pointer.

**Example:**

```c
AllocInfo alloc_info = {0};
if (!v_alloc_resize(&alloc_info, 1024)) {
    // handle error
}
```

### `v_alloc_realloc(void *data, size_t total_size)`

Acts like `realloc`, same as v_alloc_resize, but embedding `AllocInfo` in the allocation itself.

- If `data == NULL`, it creates a new allocation.
- If `total_size == 0`, it frees the allocation.
- Returns a pointer to the usable memory region.

**Example:**

```c
char *str = v_alloc_realloc(NULL, 64);
snprintf(str, 64, "Hello, v_alloc!");

str = v_alloc_realloc(str, 128); // Grows allocation without moving it
v_alloc_realloc(str, 0); // Frees allocation
```

## Example: Using `v_alloc` with DMAP

The following example integrates `v_alloc_realloc` with DMAP, ensuring stable pointers for dynamically growing data structures.

```c
size_t *dmap_1 = NULL;
dmap_kstr_init(dmap_1, 256, v_alloc_realloc);
// use dmap normally
char *test_key = "test_key";
size_t idx = dmap_kstr_insert(dmap_1, test_key, 144, strlen(test_key));
assert(dmap_1[idx] == 144);

size_t idx2 = dmap_kstr_get_idx(dmap_1, test_key, strlen(test_key));
```

## How `v_alloc_realloc` Works

Unlike standard `realloc`, `v_alloc_realloc` embeds metadata at the start of the allocation, allowing memory expansion **without moving the base pointer**. This ensures that all pointers remain valid across reallocation.

### Header Structure

```c
typedef struct AllocHdr {
    AllocInfo alloc_info;
    _Alignas(V_ALLOC_ALIGNMENT) char data[];
} AllocHdr;
```

## Notes

- Do not use `free()` on pointers from `v_alloc`, use `v_alloc_free` or `v_alloc_realloc(ptr, 0)`.
- Memory is only committed when needed, making it efficient for large reserved regions.

## Tests

`tests\` holds an mtest suite covering the bump-allocator contract
(`v_alloc_test.c`), the resize/realloc API (`v_alloc_realloc_test.c`), and an
assert-enabled probe for the strict-LIFO release check
(`examples\lifo_violation.c`).

mtest is an external dependency and is not part of this repository. Point
`MVERSELIBS` at a checkout of the `mverse-libs` repository (the directory that
contains `mtest\`), and make the `mverse` compiler (set `MVERSE` to `mverse.exe`
or its directory) and `clang` available:

```
set MVERSELIBS=D:\path\to\mverse-libs
cd tests
cmd /c build.bat
```

Build products land under `tests\build\`, and the runner exits `0` only when
every case passes.

## License

MIT License
