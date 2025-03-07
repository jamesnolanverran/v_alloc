# v\_alloc: Cross-Platform Virtual Memory Arena Allocator

## Overview

`v_alloc` is a lightweight, cross-platform virtual memory allocator designed for dynamic memory management. It provides two key allocation strategies:

1. **Bump Arena Allocation** – A simple, fast, and resettable allocator that allows sequential allocations with minimal overhead.
2. **Reallocation (Stable Virtual Memory)** – A `realloc`-like API that embeds metadata in the allocation and never moves the base pointer, leveraging OS-level virtual memory mechanisms (`mmap` on POSIX and `VirtualAlloc` on Windows).

## Features

- **Cross-platform support** (Linux, macOS, Windows)
- **Stable pointers** – memory is never moved after allocation
- **Bump allocator with reset support** – efficient for temporary allocations
- **Efficient virtual memory management** – avoids copying overhead
- **Drop-in ****\`\`**** replacement** – through `v_alloc_realloc`

## API

### Bump Arena Allocation

#### `v_alloc_reserve(AllocInfo *alloc_info, size_t reserve_size)`

Reserves a large virtual memory region for use with the bump allocator.

#### `v_alloc_committ(AllocInfo *alloc_info, size_t additional_bytes)`

Allocates memory from the reserved region.

#### `v_alloc_reset(AllocInfo *alloc_info)`

Resets the allocator, making all memory available for reuse without deallocation.

#### `v_alloc_free(AllocInfo *alloc_info)`

Frees an allocated virtual memory region.

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
char *str = v_alloc_committ(&alloc_info, str_len + 1);
if (!str) {
    // handle error
}
snprintf(str, str_len + 1, "Hello, V_Alloc!");
printf("%s\n", str);

// Reset allocator (memory can be reused)
v_alloc_reset(&alloc_info);

// Allocate another object
int *arr = v_alloc_committ(&alloc_info, 10 * sizeof(int));
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

## Reallocation API

### `v_alloc_resize(AllocInfo *alloc_info, size_t size_in_bytes)`

Resizes an allocation, manually managing an `AllocInfo` struct. If `size_in_bytes == 0`, the memory is freed.

**Example:**

```c
AllocInfo alloc_info = {0};
if (!v_alloc_resize(&alloc_info, 1024)) {
    // handle error
}
```

### `v_alloc_realloc(void *data, size_t total_size)`

Acts like `realloc`, embedding `AllocInfo` in the allocation itself.

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

### `v_alloc_hdr_from_data(void *data)`

Retrieves the embedded `AllocInfo` from a pointer returned by `v_alloc_realloc`.

## Example: Using `v_alloc` with DMAP

The following example integrates `v_alloc_realloc` with DMAP, ensuring stable pointers for dynamically growing data structures.

```c
size_t *dmap_1 = NULL;
dmap_kstr_init(dmap_1, 256, v_alloc_realloc);

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

To retrieve the allocator metadata from a pointer:

```c
AllocHdr *hdr = v_alloc_hdr_from_data(ptr);
```

## Notes

- \*\*Do not use \*\*`** on pointers from **` – always free them using `v_alloc_realloc(ptr, 0)`.
- Memory is only committed when needed, making it efficient for large reserved regions.
- `v_alloc_resize` is for explicit control over allocation, while `v_alloc_realloc` is higher-level and self-contained.

## License

MIT License

