#ifndef _CY_H
#define _CY_H

// NOTE(cya): a lot of ideas here are from GingerBill's gb library
// https://github.com/gingerBill/gb/blob/master/gb.h

#if defined(_WIN32) || defined(_WIN64)
    #define CY_OS_WINDOWS 1
    #define _CRT_SECURE_NO_WARNINGS 1
    #define WIN32_LEAN_AND_MEAN 1
    #define VC_EXTRALEAN 1
    #define NOMINMAX 1
    #include <windows.h>
    #undef WIN32_LEAN_AND_MEAN
    #undef VC_EXTRALEAN
    #undef NOMINMAX
#elif defined(__APPLE__) && defined(__MACH__)
    #define CY_OS_MACOSX 1
#elif defined(__unix__)
    #define CY_OS_UNIX 1
    #ifndef _DEFAULT_SOURCE
        #define _DEFAULT_SOURCE 1
    #endif

    #if defined(__linux__)
        #define CY_OS_LINUX 1
    #elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
        #define CY_OS_FREEBSD 1
    #else
        #error Unsupported UNIX-like OS
    #endif
#else
    #error Unsupported OS
#endif

#if defined(_WIN64) || defined(__x86_64__) || defined(_M_X64) || \
    defined(__64BIT__) || defined(__powerpc64__) || defined(__ppc64__)
    #ifndef CY_ARCH_64_BIT
        #define CY_ARCH_64_BIT 1
    #endif
#else
    #ifndef CY_ARCH_32_BIT
        #define CY_ARCH_32_BIT 1
    #endif
#endif

#ifndef CY_STATIC_ASSERT
    // NOTE(cya): because C macro expansion bizarreness
    #define CY_STATIC_ASSERT_INTERNAL_EX(cond, line) \
        typedef char STATIC_ASSERTION_FAILED_AT_LINE_##line[!!(cond) * 2 - 1]
    #define CY_STATIC_ASSERT_INTERNAL(cond, line) \
        CY_STATIC_ASSERT_INTERNAL_EX(cond, line)
    #define CY_STATIC_ASSERT(cond) CY_STATIC_ASSERT_INTERNAL(cond, __LINE__)
#endif

#ifndef NDEBUG
    #define CY_DEBUG 1
#endif

#ifndef CY_DEBUG_TRAP
    #if defined(_MSC_VER)
        #if _MSC_VER < 1300
            #define CY_DEBUG_TRAP() __asm int 3
        #else
            #define CY_DEBUG_TRAP() __debugbreak()
        #endif
    #else
        #define CY_DEBUG_TRAP() __builtin_trap()
    #endif
#endif

#ifndef CY_ASSERT_MSG
    #if defined(CY_DEBUG)
        #define CY_ASSERT_MSG(cond, ...) { \
            if (!(cond)) { \
                cy_handle_assertion( \
                    "Assertion failed",#cond, \
                    __FILE__, (i64)__LINE__, __VA_ARGS__ \
                ); \
                CY_DEBUG_TRAP(); \
            } \
        } (void)0
    #else
        #define CY_ASSERT_MSG(cond, ...) (void)(cond)
    #endif
#endif

#ifndef CY_ASSERT
#define CY_ASSERT(cond) CY_ASSERT_MSG(cond, NULL)
#define CY_ASSERT_NOT_NULL(ptr) CY_ASSERT(ptr != NULL)
#endif

#if defined(CY_STATIC)
    #define CY_DEF static
#else
    #define CY_DEF extern
#endif


/* ================================= Types ================================== */
#include <stdint.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float f32;
typedef double f64;

CY_STATIC_ASSERT(sizeof(f32) == 4);
CY_STATIC_ASSERT(sizeof(f64) == 8);

#if defined(CY_OS_UNIX)
    #include <stddef.h>
#endif

typedef ptrdiff_t isize;
typedef size_t usize;

CY_STATIC_ASSERT(sizeof(isize) == sizeof(usize));

typedef intptr_t intptr;
typedef uintptr_t uintptr;

CY_STATIC_ASSERT(sizeof(intptr) == sizeof(uintptr));

typedef i8 b8;
typedef i16 b16;
typedef i32 b32;

#ifndef true
    #define true (0 == 0)
#endif
#ifndef false
    #define false (0 != 0)
#endif

/* --------------------------------- Limits --------------------------------- */
#define U8_MIN 0U
#define U8_MAX 0xFFU
#define I8_MIN (-0x7F - 1)
#define I8_MAX 0x7F

#define U16_MIN 0U
#define U16_MAX 0xFFFFU
#define I16_MIN (-0x7FFF - 1)
#define I16_MAX 0x7FFF

#define U32_MIN 0U
#define U32_MAX 0xFFFFFFFFU
#define I32_MIN (-0x7FFFFFFF - 1)
#define I32_MAX 0x7FFFFFFF

#define U64_MIN 0U
#define U64_MAX 0xFFFFFFFFFFFFFFFFULL
#define I64_MIN (-0x7FFFFFFFFFFFFFFFLL - 1)
#define I64_MAX 0x7FFFFFFFFFFFFFFFFLL

#define F32_MIN 1.17549435e-38F
#define F32_MAX 3.40282347e+38F

#define F64_MIN 2.2250738585072014e-308
#define F64_MAX 1.7976931348623157e+308

#if defined(CY_ARCH_64_BIT)
    #define ISIZE_MIN I64_MIN
    #define ISIZE_MAX I64_MAX

    #define USIZE_MIN U64_MIN
    #define USIZE_MAX U64_MAX
#else
    #define ISIZE_MIN I32_MIN
    #define ISIZE_MAX I32_MAX

    #define USIZE_MIN U32_MIN
    #define USIZE_MAX U32_MAX
#endif

typedef i32 Rune; // Unicode codepoint

#define CY_RUNE_INVALID (Rune)(0XFFFD)
#define CY_RUNE_MAX (Rune)(0X0010FFFF)
#define CY_RUNE_BOM (Rune)(0XFEFF)
#define CY_RUNE_EOF (Rune)(-1)

// NOTE(cya): should clarify the meaning behind keyword usage
#define cy_global static
#define cy_internal static
#define cy_persist static

#include <stdio.h>   // for optional logging
#include <stdarg.h>  // va_args

// TODO(cya): implement printf family and move this stuff to some better place
#define cy_printf_err(...) fprintf(stderr, __VA_ARGS__)
#define cy_printf_err_va(fmt, va) vfprintf(stderr, fmt, va)

CY_DEF void cy_handle_assertion(
    const char *prefix,
    const char *cond,
    const char *file,
    i32 line,
    const char *msg,
    ...
) {
    cy_printf_err("%s(%d): %s: ", file, line, prefix);
    if (cond != NULL) {
        cy_printf_err( "`%s` ", cond);
    }
    if (msg != NULL) {
        va_list va;
        va_start(va, msg);
        cy_printf_err_va(msg, va);
        va_end(va);
    }

    cy_printf_err("\n");
}

// TODO(cya): remove header and implement mem functions
#include <string.h>

CY_DEF void *cy_mem_copy(
    void *restrict dst,
    const void *restrict src,
    usize bytes
) {
    return memcpy(dst, src, bytes);
}

CY_DEF void *cy_mem_move(void *dst, const void *src, usize bytes)
{
    return memmove(dst, src, bytes);
}

CY_DEF void *cy_mem_set(void *dst, u8 val, usize bytes)
{
    return memset(dst, val, bytes);
}

CY_DEF void *cy_mem_zero(void *dst, usize bytes)
{
    return cy_mem_set(dst, 0, bytes);
}

CY_DEF isize cy_mem_compare(void *a, void *b, usize bytes)
{
    return (isize)memcmp(a, b, bytes);
}

#define CY_BIT(n) (1 << (n))
#define CY_UNUSED(param) (void)(param)


CY_DEF inline b8 cy_is_power_of_two(isize n)
{
    return (n & (n - 1)) == 0;
}

CY_DEF inline isize cy_align_forward(isize size, isize align)
{
    CY_ASSERT(align > 0 && cy_is_power_of_two(align));

    isize mod = size & (align - 1);
    return mod ? size + align - mod : size;
}

CY_DEF inline void *cy_align_ptr_forward(void *ptr, isize align)
{
    return (void*)cy_align_forward((isize)ptr, align);
}

/* Aligns a pointer forward accounting for both the size
 * of a header and the alignment */
CY_DEF inline usize cy_calc_header_padding(
    uintptr ptr,
    usize align,
    usize header_size
) {
    CY_ASSERT(cy_is_power_of_two(align));

    uintptr a = (uintptr)align;
    uintptr mod = ptr & (a - 1);
    uintptr padding = mod ? a - mod : 0;
    if (padding < (uintptr)header_size) {
        uintptr needed_space = header_size - padding;
        padding += (needed_space & (a - 1)) ?
            a * (needed_space / a + 1) : a * (needed_space / a);
    }

    return (usize)padding;
}

/* =============================== Allocators =============================== */
typedef enum {
    CY_ALLOCATION_ALLOC,
    CY_ALLOCATION_FREE,
    CY_ALLOCATION_FREE_ALL,
    CY_ALLOCATION_RESIZE,
} CyAllocationType;

typedef enum {
    CY_ALLOCATOR_CLEAR_TO_ZERO = CY_BIT(0),
} CyAllocatorFlags;

#define CY_ALLOCATOR_PROC(name)                      \
    void *name(                                      \
        void *allocator_data, CyAllocationType type, \
        isize size, isize align,                     \
        void *old_mem, isize old_size,               \
        u64 flags                                    \
    )

typedef CY_ALLOCATOR_PROC(CyAllocatorProc);

typedef struct {
    CyAllocatorProc *proc;
    void *data;
} CyAllocator;

#define CY_DEFAULT_ALIGNMENT (2 * sizeof(void*))
#define CY_DEFAULT_ALLOCATOR_FLAGS (CY_ALLOCATOR_CLEAR_TO_ZERO)

#define cy_alloc_item(allocator, type) cy_alloc(allocator, sizeof(type))
#define cy_alloc_array(allocator, type, count) \
    cy_alloc(allocator, (count) * sizeof(type))
#define cy_resize_array(allocator, arr, type, old_count, new_count) \
    cy_resize(allocator, arr, (old_count) * sizeof(type), (new_count) * sizeof(type))

CY_DEF inline void *cy_alloc_align(CyAllocator a, isize size, isize align)
{
    return a.proc(
        a.data, CY_ALLOCATION_ALLOC,
        size, align,
        NULL, 0,
        CY_DEFAULT_ALLOCATOR_FLAGS
    );
}

CY_DEF inline void *cy_alloc(CyAllocator a, isize size)
{
    return cy_alloc_align(a, size, CY_DEFAULT_ALIGNMENT);
}

CY_DEF inline void cy_free(CyAllocator a, void *ptr)
{
    if (ptr != NULL) {
        a.proc(
            a.data, CY_ALLOCATION_FREE,
            0, 0,
            ptr, 0,
            CY_DEFAULT_ALLOCATOR_FLAGS
        );
    }
}

CY_DEF inline void cy_free_all(CyAllocator a)
{
    a.proc(
        a.data, CY_ALLOCATION_FREE_ALL,
        0, 0,
        NULL, 0,
        CY_DEFAULT_ALLOCATOR_FLAGS
    );
}

CY_DEF inline void *cy_resize_align(
    CyAllocator a,
    void *ptr,
    isize old_size,
    isize new_size,
    isize align
) {
    return a.proc(
        a.data, CY_ALLOCATION_RESIZE,
        new_size, align,
        ptr, old_size,
        CY_DEFAULT_ALLOCATOR_FLAGS
    );
}

CY_DEF inline void *cy_resize(
    CyAllocator a,
    void *ptr,
    isize old_size,
    isize new_size
) {
    return cy_resize_align(a, ptr, old_size, new_size, CY_DEFAULT_ALIGNMENT);
}

// NOTE(cya): a simple resize that should fit most use cases
CY_DEF inline void *cy_default_resize_align(
    CyAllocator a,
    void *old_mem,
    isize old_size,
    isize new_size,
    isize align
) {
    if (old_mem == NULL) {
        return cy_alloc_align(a, new_size, align);
    }
    if (new_size == 0) {
        cy_free(a, old_mem);
        return NULL;
    }
    if (new_size <= old_size) {
        return old_mem;
    }

    void *new_mem = cy_alloc_align(a, new_size, align);
    if (new_mem == NULL) {
        return NULL;
    }

    cy_mem_move(new_mem, old_mem, old_size);
    cy_free(a, old_mem);
    return new_mem;
}

CY_DEF inline void *cy_default_resize(
    CyAllocator a,
    void *old_mem,
    isize old_size,
    isize new_size
) {
    return cy_default_resize_align(
        a, old_mem,
        old_size, new_size,
        CY_DEFAULT_ALIGNMENT
    );
}

CY_DEF inline void *cy_alloc_copy_align(
    CyAllocator a,
    const void *src,
    isize size,
    isize align
) {
    return cy_mem_copy(cy_alloc_align(a, size, align), src, size);
}

CY_DEF inline void *cy_alloc_copy(CyAllocator a, const void *src, isize size)
{
    return cy_alloc_copy_align(a, src, size, CY_DEFAULT_ALIGNMENT);
}

CY_DEF inline char *cy_alloc_string_len(
    CyAllocator a,
    const char *str,
    isize len
) {
    char *res = cy_alloc_copy(a, str, len + 1);
    res[len] = '\0';
    return res;
}

// TODO(cya): figure out this sorcery
#define CY__LO_ONES ((usize)-1 / U8_MAX)
#define CY__HI_ONES (CY__LO_ONES * (U8_MAX / 2 + 1))
#define CY__HAS_ZERO_BYTE(n) !!(((n) - CY__LO_ONES) & ~(n) & CY__HI_ONES)

#define CY_MIN(a, b) (a < b ? a : b)
#define CY_MAX(a, b) (a > b ? a : b)

#define CY_VALIDATE_PTR(p) if (p == NULL) return NULL

#define CY_STATIC_STR_LEN(str) ((sizeof(str) - 1) / sizeof(*(str)))

CY_DEF inline isize cy_str_len(const char *str)
{
    if (str == NULL) {
        return 0;
    }

    const char *begin = str;
    while ((uintptr)str % sizeof(isize) != 0) {
        if (*str == '\0') {
            return str - begin;
        }

        str += 1;
    }

    const isize *w = (const isize*)str;
    while (!CY__HAS_ZERO_BYTE(*w)) w += 1;

    str = (const char*)w;
    while (*str != '\0') str += 1;

    return str - begin;
}

CY_DEF inline char *cy_alloc_string(CyAllocator a, const char *str)
{
    return cy_alloc_string_len(a, str, cy_str_len(str));
}

#include <malloc.h>

CyAllocatorProc cy_heap_allocator_proc;

// NOTE(cya): the default malloc-style heap allocator
CY_DEF CyAllocator cy_heap_allocator(void)
{
    return (CyAllocator){
        .proc = cy_heap_allocator_proc,
    };
}

#ifdef CY_OS_WINDOWS
    #define malloc_align(s, a) _aligned_malloc(s, a)
    #define realloc_align(alloc, mem, old_size, new_size, align) \
        _aligned_realloc(mem, new_size, align)
    #define free_align(p, a) _aligned_free(p)
#else
extern int posix_memalign(void **, size_t, size_t);

void *malloc_align(isize size, isize align)
{
    void *ptr = NULL;
    if (posix_memalign(&ptr, align, size) != 0) {
        return NULL;
    }

    return ptr;
}

    #define realloc_align cy_default_resize_align
    #define free_align(p, a) free(p)
#endif

CY_ALLOCATOR_PROC(cy_heap_allocator_proc)
{
    CY_UNUSED(allocator_data);
    CY_UNUSED(old_size);

    void *ptr = NULL;
    switch(type) {
    case CY_ALLOCATION_ALLOC: {
#if 0
        cy_printf_err("Allocated %zu bytes\n", size);
#endif
        ptr = malloc_align(size, align);
        if (flags & CY_ALLOCATOR_CLEAR_TO_ZERO) {
            cy_mem_zero(ptr, size);
        }
    } break;
    case CY_ALLOCATION_FREE: {
        free_align(old_mem, align);
    } break;
    case CY_ALLOCATION_FREE_ALL: {
        CY_ASSERT_MSG(false, "heap allocator doesn't support free-all");
    } break;
    case CY_ALLOCATION_RESIZE: {
        void *new_ptr = realloc_align(
            cy_heap_allocator(), old_mem,
            old_size, size,
            align
        );
        CY_VALIDATE_PTR(new_ptr);
        ptr = new_ptr;
    } break;
    }

    return ptr;
}

#define cy_heap_alloc(size) cy_alloc(cy_heap_allocator(), size)
#define cy_heap_free(ptr) cy_free(cy_heap_allocator(), ptr)

/* ------------------------- Page Allocator Section ------------------------- */
#if defined(CY_OS_WINDOWS)
    #define CY_PAGE_SIZE (4 * 1024)
#else
    #include <sys/mman.h>
    #ifndef MAP_ANONYMOUS
        #define MAP_ANONYMOUS MAP_ANON
    #endif

    #if defined(CY_OS_MACOSX)
        #define CY_PAGE_SIZE (16 * 1024)
    #else
        #define CY_PAGE_SIZE (4 * 1024)
    #endif
#endif

typedef struct {
    void *start; // Actual beginning of the region of pages
    isize size;  // Total size of allocation (including aligned meta-chunk)
} CyPageChunk;

CyAllocatorProc cy_page_allocator_proc;

CY_DEF CyAllocator cy_page_allocator(void)
{
    return (CyAllocator){
        .proc = cy_page_allocator_proc,
    };
}

// NOTE(cya): size of actual usable memory starting at *ptr
CY_DEF isize cy_page_allocator_alloc_size(void *ptr)
{
    CyPageChunk *chunk = (CyPageChunk*)ptr - 1;
    intptr diff = (uintptr)ptr - (uintptr)chunk->start;
    return chunk->size - diff;
}

CY_ALLOCATOR_PROC(cy_page_allocator_proc)
{
    CY_UNUSED(allocator_data);
    // NOTE(cya): shouldn't need to handle clear-to-zero flag
    // since both VirtualAlloc and mmap clear the memory for us
    CY_UNUSED(flags);

    void *ptr = NULL;
    switch (type) {
    case CY_ALLOCATION_ALLOC: {
        CY_ASSERT_MSG(size > 0, "page allocator: invalid allocation size");

        isize chunk_padding = sizeof(CyPageChunk) + align;
        isize alloc_size = size + align;
        isize total_size = cy_align_forward(
            chunk_padding + alloc_size, CY_PAGE_SIZE
        );

        void *mem = NULL;
#if defined(CY_OS_WINDOWS)
        mem = VirtualAlloc(
            NULL, total_size,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE
        );
#else
        mem = mmap(
            NULL, total_size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1, 0
        );
#endif
        CY_VALIDATE_PTR(mem);

        CyPageChunk *chunk_start = (CyPageChunk*)
            ((u8*)mem + chunk_padding - sizeof(*chunk_start));
        *chunk_start = (CyPageChunk){
            .start = mem,
            .size = total_size,
        };

        ptr = (u8*)mem + chunk_padding;
        CY_ASSERT(ptr == cy_align_ptr_forward(ptr, align));
    } break;
    case CY_ALLOCATION_FREE: {
        CyPageChunk *chunk = (CyPageChunk*)old_mem - 1;
        void *start = chunk->start;

#if defined(CY_OS_WINDOWS)
        VirtualFree(start, 0, MEM_RELEASE);
#else
        isize total_size = chunk->size;
        munmap(start, total_size);
#endif
    } break;
    case CY_ALLOCATION_FREE_ALL: {
        CY_ASSERT_MSG(false, "page allocator doesn't support free-all");
    } break;
    case CY_ALLOCATION_RESIZE: {
        // TODO(cya): test this resizing logic a bit more thoroughly
        CyPageChunk *chunk = (CyPageChunk*)old_mem - 1;
        void *cur_start = chunk->start;
        isize chunk_padding = sizeof(*chunk) + align;

        isize cur_total_size = chunk->size;
        isize new_total_size = cy_align_forward(
            chunk_padding + size + align, CY_PAGE_SIZE
        );
        if (new_total_size <= cur_total_size) {
            isize size_to_free = cur_total_size - new_total_size;
            if (size_to_free >= CY_PAGE_SIZE) {
                u8 *region_to_free = (u8*)cur_start + new_total_size;

#if defined(CY_OS_WINDOWS)
                VirtualFree(region_to_free, size_to_free, MEM_DECOMMIT);
#else
                munmap(region_to_free, size_to_free);
#endif

                CyPageChunk *new_chunk = (CyPageChunk*)
                    cur_start + chunk_padding - sizeof(*new_chunk);
                *new_chunk = (CyPageChunk){
                    .start = cur_start,
                    .size = new_total_size,
                };
            }

            return cy_align_ptr_forward(old_mem, align);
        }

        CyAllocator a = cy_page_allocator();
        void *new_ptr = cy_alloc_align(a, size, align);
        CY_VALIDATE_PTR(new_ptr);

        cy_mem_copy(new_ptr, old_mem, old_size);
        cy_free(a, old_mem);

        ptr = new_ptr;
    } break;
    }

    return ptr;
}

/* ------------------------ Arena Allocator Section ------------------------- */
typedef struct CyArenaNode {
    u8 *buf;                  // actual arena memory
    isize size;               // size of the buffer in bytes
    isize offset;             // offset to first byte of (unaligned) free memory
    isize prev_offset;        // offset to first byte of previous allocation
    struct CyArenaNode *next; // next node (duh)
} CyArenaNode;

typedef struct {
    CyArenaNode *first_node;
} CyArenaState;

typedef struct {
    CyAllocator backing;
    CyArenaState state;
} CyArena;

CyAllocatorProc cy_arena_allocator_proc;

CY_DEF inline CyAllocator cy_arena_allocator(CyArena *arena)
{
    return (CyAllocator){
        .proc = cy_arena_allocator_proc,
        .data = arena,
    };
}

#define CY_ARENA_INIT_SIZE     CY_PAGE_SIZE
#define CY_ARENA_GROWTH_FACTOR 2.0

CY_DEF inline CyArenaNode *cy_arena_insert_node(CyArena *arena, isize size)
{
    isize node_padding = sizeof(CyArenaNode) + CY_DEFAULT_ALIGNMENT;
    isize new_node_size = node_padding + size;
    CyArenaNode *new_node = cy_alloc(arena->backing, new_node_size);
    CY_VALIDATE_PTR(new_node);

    new_node->buf = cy_align_ptr_forward(new_node + 1, CY_DEFAULT_ALIGNMENT);
    new_node->size = size;
    new_node->next = arena->state.first_node;
    arena->state.first_node = new_node;

    return new_node;
}

CY_DEF inline CyArena cy_arena_init(CyAllocator backing, isize initial_size)
{
    CY_ASSERT_NOT_NULL(backing.proc);

    isize default_size = CY_ARENA_INIT_SIZE;
    if (initial_size == 0) {
        initial_size = default_size;
    }

    CyArena arena = {0};
    arena.backing = backing;
    arena.state.first_node = cy_arena_insert_node(&arena, initial_size);
    return arena;
}

CY_DEF inline void arena_deinit(CyArena *arena)
{
    CyArenaNode *cur_node = arena->state.first_node, *next = cur_node->next;
    while (next != NULL) {
        cur_node = next;
        next = next->next;
        cy_free(arena->backing, cur_node);
    }
}

CY_ALLOCATOR_PROC(cy_arena_allocator_proc)
{
    CY_UNUSED(flags);

    CyArena *arena = (CyArena*)allocator_data;
    void *ptr = NULL;
    switch(type) {
    case CY_ALLOCATION_ALLOC: {
        isize req_size = size + align;
        CyArenaNode *cur_node = arena->state.first_node;
        u8 *end = cur_node->buf + cur_node->offset;
        u8 *aligned_end = cy_align_ptr_forward(end, align);
        while (aligned_end + size > cur_node->buf + cur_node->size) {
            if (cur_node->next == NULL) {
                // NOTE(cya): need more memory! (add new node to linked list)
                isize largest_node_size = arena->state.first_node->size;
                isize new_size = largest_node_size * CY_ARENA_GROWTH_FACTOR;

                cur_node = cy_arena_insert_node(
                    arena, CY_MAX(new_size, req_size)
                );
                CY_VALIDATE_PTR(cur_node);
                end = cur_node->buf + cur_node->offset;
                aligned_end = cy_align_ptr_forward(end, align);
                break;
            }

            cur_node = cur_node->next;
            end = cur_node->buf + cur_node->offset;
            aligned_end = cy_align_ptr_forward(end, align);
        }

        isize aligned_offset = aligned_end - cur_node->buf;

        cur_node->prev_offset = aligned_offset;
        cur_node->offset = aligned_offset + size;

        ptr = aligned_end;
    } break;
    case CY_ALLOCATION_FREE: {
        CY_ASSERT_MSG(false, "arenas don't support individual frees");
    } break;
    case CY_ALLOCATION_FREE_ALL: {
        CyArenaNode *cur_node = arena->state.first_node, *next = cur_node->next;
        while (next != NULL) {
            cur_node = next;
            next = next->next;
            cy_free(arena->backing, cur_node);
        }

        CyArenaNode *first_node = arena->state.first_node;
        cy_mem_set(first_node->buf, 0, first_node->size);
        first_node->prev_offset = first_node->offset = 0;
        first_node->next = NULL;
    } break;
    case CY_ALLOCATION_RESIZE: {
        CY_ASSERT(cy_is_power_of_two(align));

        u8 *old_memory = old_mem;
        if (old_memory == NULL || old_size == 0) {
            return cy_alloc_align(cy_arena_allocator(arena), size, align);
        }

        CyArenaNode *cur_node = arena->state.first_node;
        intptr prev_offset;
        b32 found_node = false;
        while (cur_node != NULL) {
            prev_offset = cur_node->prev_offset;
            if (cur_node->buf + prev_offset == old_memory) {
                found_node = true;
                break;
            }

            cur_node = cur_node->next;
        }
        if (!found_node) {
            // NOTE(cya): memory is not the latest allocation (making new one)
            void *new_mem = cy_alloc_align(cy_arena_allocator(arena), size, align);
            CY_VALIDATE_PTR(new_mem);

            cy_mem_copy(new_mem, old_mem, old_size);
            return new_mem;
        }

        intptr aligned_offset = cy_align_forward(prev_offset, align);
        u8 *new_memory = cur_node->buf + aligned_offset;
        if (new_memory + size < cur_node->buf + cur_node->size) {
            cur_node->offset = aligned_offset + size;
            if (cur_node->offset < cur_node->prev_offset + old_size) {
                cy_mem_set(
                    cur_node->buf + cur_node->offset, 0,
                    old_size - size
                );
            }

            cur_node->prev_offset = aligned_offset;
            return new_memory;
        }

        void *new_ptr = cy_alloc_align(cy_arena_allocator(arena), size, align);
        CY_VALIDATE_PTR(new_ptr);

        cy_mem_move(new_ptr, old_memory, CY_MIN(old_size, size));
        ptr = new_ptr;
    } break;
    }

    return ptr;
}

/* -------------------------Stack Allocator Section ------------------------- */
typedef struct StackNode {
    unsigned char *buf;
    usize size;
    usize prev_offset;
    usize offset;
    struct StackNode *next;
} StackNode;

typedef struct StackState {
    StackNode *first_node;
} StackState;

typedef struct Stack {
    void *(*alloc)(usize);
    void (*free)(void*);
    StackState *state;
} Stack;

typedef struct StackHeader {
    usize prev_offset;
    usize padding;
} StackHeader;

/* Default initial size set to one page */
#define CY_STACK_INIT_SIZE     CY_PAGE_SIZE
#define CY_STACK_GROWTH_FACTOR 2.0
#define CY_STACK_STRUCTS_SIZE \
    (sizeof(Stack) + sizeof(StackState) + sizeof(StackNode))

CY_DEF inline Stack *stack_init(
    usize initial_size,
    void *(*backing_allocator)(usize),
    void (*backing_deallocator)(void*)
) {
    if (initial_size == 0) initial_size = CY_STACK_INIT_SIZE;

    if (backing_allocator == NULL && backing_deallocator == NULL) {
        // backing_allocator = page_alloc;
        // backing_deallocator = page_free;
        initial_size = cy_align_forward(initial_size, CY_PAGE_SIZE);
    } else if (backing_allocator == NULL || backing_deallocator == NULL) {
        return NULL;
    }

    usize size = CY_STACK_STRUCTS_SIZE + initial_size;
    Stack *stack = backing_allocator(size);
    if (stack == NULL) return NULL;

    stack->alloc = backing_allocator;
    stack->free = backing_deallocator;
    stack->state = (StackState*)(stack + 1);

    stack->state->first_node = (StackNode*)(stack->state + 1);

    StackNode *first_node = stack->state->first_node;
    first_node->buf = (unsigned char*)(first_node + 1);
    first_node->size = initial_size;

    return stack;
}

CY_DEF inline StackNode *cy_stack_insert_node(Stack *stack, usize size)
{
    if (stack == NULL) return NULL;

    StackNode *cur_node = stack->state->first_node;
    usize new_node_size = sizeof(*cur_node) + size;
    StackNode *new_node = stack->alloc(new_node_size);
    if (new_node == NULL) return NULL;

    new_node->buf = (unsigned char*)(new_node + 1);
    new_node->size = size;
    new_node->next = stack->state->first_node;
    stack->state->first_node = new_node;

    return new_node;
}

void *stack_alloc_align(Stack *stack, usize size, usize align)
{
    if (stack == NULL) return NULL;

    StackHeader *header;
    StackNode *cur_node = stack->state->first_node;
    uintptr cur_address = (uintptr)(cur_node->buf + cur_node->offset);
    usize padding = cy_calc_header_padding(cur_address,
        align, sizeof(*header));
    usize alloc_size = padding + size;
    if (cur_node->offset + padding + size > cur_node->size) {
        /* out of memory (TODO: implement finished linked-list stack logic) */
        usize node_size = cur_node->size * CY_STACK_GROWTH_FACTOR;
        if (node_size < alloc_size)
            node_size = cy_align_forward(alloc_size, CY_PAGE_SIZE);

        cur_node = cy_stack_insert_node(stack, node_size);
        if (cur_node == NULL) return NULL;

        cur_address = (uintptr)(cur_node->buf + cur_node->offset);
        padding = cy_calc_header_padding(cur_address, align, sizeof(*header));
    }

    uintptr next_address = cur_address + (uintptr)padding;
    header = (StackHeader*)(next_address - sizeof(*header));
    header->padding = (u8)padding;
    header->prev_offset = cur_node->offset;

    cur_node->prev_offset = header->prev_offset;
    cur_node->offset += alloc_size;

    return (void*)next_address;
}

void *stack_alloc(Stack *stack, usize size)
{
    return stack_alloc_align(stack, size, CY_DEFAULT_ALIGNMENT);
}

void stack_free(Stack *stack, void *ptr)
{
    if (ptr == NULL) return;

    StackNode *cur_node = stack->state->first_node;
    uintptr start = (uintptr)cur_node->buf;
    uintptr end = start + (uintptr)cur_node->size;
    uintptr cur_address = (uintptr)ptr;
    if (!(start <= cur_address && cur_address < end)) {
        CY_ASSERT(0 && "out-of-bounds pointer deallocation (stack_free())");
        return;
    }
    if (cur_address >= start + (uintptr)cur_node->offset) {
        /* allow double-frees */
        return;
    }

    StackHeader *header = (StackHeader*)(cur_address - sizeof(*header));
    usize prev_offset = (usize)(cur_address - header->padding - start);
    if (prev_offset != header->prev_offset) {
        CY_ASSERT(0 && "out-of-order stack deallocation (stack_free())");
        return;
    }
    cur_node->offset = prev_offset;
    cur_node->prev_offset = header->prev_offset;
}

void *stack_realloc_align(
    Stack *stack,
    void *ptr,
    usize old_size,
    usize new_size,
    usize align
) {
    if (ptr == NULL) {
        return stack_alloc_align(stack, new_size, CY_DEFAULT_ALIGNMENT);
    } else if (new_size == 0) {
        stack_free(stack, ptr);
        return NULL;
    }

    StackNode *cur_node = stack->state->first_node;
    uintptr start = (uintptr)cur_node->buf;
    uintptr end = start + cur_node->size;
    uintptr cur_address = (uintptr)ptr;
    if (!(start <= cur_address && cur_address < end)) {
        CY_ASSERT(0 && "out-of-bounds stack reallocation (stack_realloc())");
        return NULL;
    }
    if (cur_address >= start + (uintptr)cur_node->offset) {
        CY_ASSERT(0 && "out-of-order stack reallocation (stack_realloc())");
        return NULL;
    }

    StackHeader *header = (StackHeader*)(cur_address - sizeof(*header));
    uintptr cur_padding = header->padding;
    uintptr alloc_start = (uintptr)header - cur_padding;
    uintptr new_padding =
        cy_calc_header_padding(alloc_start, align, sizeof(*header));
    if (new_size <= old_size && new_padding == cur_padding) {
        cur_node->offset -= old_size - new_size;
        return ptr;
    }

    usize prev_offset = (usize)(cur_address - header->padding - start);
    usize new_offset = (usize)(alloc_start + new_padding + new_size - start);
    if (new_offset <= end) {
        uintptr new_address = alloc_start + new_padding;
        usize min_size = old_size < new_size ? old_size : new_size;
        cy_mem_move((void*)new_address, ptr, min_size);

        header = (StackHeader*)(new_address - sizeof(*header));
        header->padding = new_padding;
        header->prev_offset = prev_offset;
        cur_node->offset = new_offset;

        return (void*)new_address;
    }

    void *new_ptr = stack_alloc_align(stack, new_size, align);
    cy_mem_move(new_ptr, ptr, old_size < new_size ? old_size : new_size);

    cur_node->offset = prev_offset;
    cur_node->prev_offset = header->prev_offset;

    return new_ptr;
}

void *stack_realloc(Stack *stack, void *ptr, usize old_size, usize new_size)
{
    return stack_realloc_align(stack, ptr,
        old_size, new_size, CY_DEFAULT_ALIGNMENT);
}

void stack_deinit(Stack *stack)
{
    StackNode *cur_node = stack->state->first_node, *next = cur_node->next;
    while (next != NULL) {
        stack->free(cur_node);
        cur_node = next;
        next = next->next;
    }

    stack->free(stack);
}

/* TODO(cya):
 * add init_from_buffer proc to arena and other 'fixed' allocators
 * repurpose stack allocator
 * pool allocator
 * buddy allocator
 * freelist allocator
 * general-purpose heap allocator to replace malloc
 */

 /* ============================ Char functions ============================= */
CY_DEF const char *cy_char_first_occurence(const char *str, char c)
{
    for (; *str != c; str++) {
        if (*str == '\0')  {
            return NULL;
        }
    }

    return str;
}

CY_DEF const char *cy_char_last_occurence(const char *str, char c)
{
    const char *res = NULL;
    do {
        if (*str == c) {
            res = str;
        }
    } while (*str++ != '\0');

    return res;
}

/* ================================ Strings ================================= */
typedef char *CyString;

typedef struct {
    CyAllocator alloc;
    isize len;
    isize cap;
} CyStringHeader;

#define CY_STRING_HEADER(str) ((CyStringHeader*)(str) - 1)

CY_DEF inline isize cy_string_len(CyString str)
{
    return (str == NULL) ? 0 : CY_STRING_HEADER(str)->len;
}

CY_DEF inline isize cy_string_cap(CyString str)
{
    return (str == NULL) ? 0 : CY_STRING_HEADER(str)->cap;
}

CY_DEF inline isize cy_string_alloc_size(CyString str)
{
    return sizeof(CyStringHeader) + cy_string_cap(str) + 1;
}

CY_DEF inline isize cy_string_available_space(CyString str)
{
    CyStringHeader *h = CY_STRING_HEADER(str);
    if (h->cap > h->len) {
        return h->cap - h->len;
    }

    return 0;
}

CY_DEF inline void cy__string_set_len(CyString str, isize len)
{
    if (str == NULL) {
        return;
    }

    CY_STRING_HEADER(str)->len = len;
}

CY_DEF inline void cy__string_set_cap(CyString str, isize cap)
{
    if (str == NULL) {
        return;
    }

    CY_STRING_HEADER(str)->cap = cap;
}

CY_DEF CyString cy_string_create_reserve(CyAllocator a, isize cap)
{
    isize header_size = sizeof(CyStringHeader);
    isize total_size = header_size + cap + 1;
    void *ptr = cy_alloc(a, total_size);
    CY_VALIDATE_PTR(ptr);

    cy_mem_zero(ptr, total_size);

    CyStringHeader *header = ptr;
    *header = (CyStringHeader){
        .alloc = a,
        .len = 0,
        .cap = cap,
    };

    return (CyString)ptr + header_size;
}

CY_DEF CyString cy_string_create_len(CyAllocator a, const char *str, isize len)
{
    isize header_size = sizeof(CyStringHeader);
    isize total_size = header_size + len + 1;
    void *ptr = cy_alloc(a, total_size);
    CY_VALIDATE_PTR(ptr);

    if (str == NULL) {
        cy_mem_zero(ptr, total_size);
    }

    CyStringHeader *header = ptr;
    *header = (CyStringHeader){
        .alloc = a,
        .len = len,
        .cap = len,
    };

    CyString string = (CyString)ptr + header_size;
    if (len > 0 && str != NULL) {
        cy_mem_copy(string, str, len);
    }

    string[len] = '\0';
    return string;
}

CY_DEF CyString cy_string_create(CyAllocator a, const char *str)
{
    return cy_string_create_len(a, str, cy_str_len(str));
}

CY_DEF void cy_string_free(CyString str)
{
    if (str != NULL) {
        cy_free(CY_STRING_HEADER(str)->alloc, CY_STRING_HEADER(str));
    }
}

CY_DEF CyString cy_string_reserve_space_for(CyString str, isize extra_len)
{
    isize available = cy_string_available_space(str);
    if (available >= extra_len) {
        return str;
    }

    void *mem = CY_STRING_HEADER(str);
    CyStringHeader *header = mem;
    CyAllocator a = header->alloc;

    isize new_cap = cy_string_len(str) + extra_len;
    isize old_size = sizeof(*header) + cy_string_cap(str) + 1;
    isize new_size = sizeof(*header) + new_cap + 1;

    void *new_mem = cy_resize(a, mem, old_size, new_size);
    CY_VALIDATE_PTR(new_mem);

    header = new_mem;
    header->alloc = a;

    str = (CyString)(header + 1);
    cy__string_set_cap(str, new_cap);
    return str;
}

CY_DEF CyString cy_string_append_len(CyString str, const char *other, isize len)
{
    if (len > 0) {
        isize cur_len = cy_string_len(str);

        str = cy_string_reserve_space_for(str, len);
        CY_VALIDATE_PTR(str);

        cy_mem_copy(str + cur_len, other, len);

        isize new_len = cur_len + len;
        str[new_len] = '\0';

        cy__string_set_len(str, new_len);
    }

    return str;
}

CY_DEF CyString cy_string_append(CyString str, CyString other)
{
    return cy_string_append_len(str, other, cy_string_len(other));
}

CY_DEF CyString cy_string_append_c(CyString str, const char *other)
{
    return cy_string_append_len(str, other, cy_str_len(other));
}

CY_DEF CyString cy_string_append_rune(CyString str, Rune r)
{
    // TODO(cya): utf-8 encode rune
    isize width = 1;
    return cy_string_append_len(str, (const char*)&r, width);
}

CY_DEF CyString cy_string_pad_right(CyString str, isize width, Rune r)
{
    // TODO(cya): calculate width with utf8_width proc (to be implemented)
    isize str_width = cy_string_len(str);
    isize rune_width = 1;
    while (str_width < width) {
        str = cy_string_append_rune(str, r);
        str_width += rune_width;
    }

    CY_VALIDATE_PTR(str);
    return str;
}

CY_DEF CyString cy_string_set(CyString str, const char *c_str)
{
    isize new_len = cy_str_len(c_str);
    if (cy_string_cap(str) < new_len) {
        str = cy_string_reserve_space_for(str, new_len - cy_string_len(str));
        CY_VALIDATE_PTR(str);
    }

    cy_mem_copy(str, c_str, new_len + 1);
    cy__string_set_len(str, new_len);

    return str;
}

CY_DEF CyString cy_string_dup(CyAllocator a, const CyString src)
{
    return cy_string_create_len(a, src, cy_string_len(src));
}

CY_DEF b32 cy_string_are_equal(const CyString a, const CyString b)
{
    isize a_len = cy_string_len(a);
    isize b_len = cy_string_len(b);
    if (a_len != b_len) {
        return false;
    }

    return cy_mem_compare(a, b, a_len) == 0;
}

CY_DEF CyString cy_string_trim(CyString str, const char *char_set)
{
    char *start, *new_start;
    new_start = start = str;

    char *end, *new_end;
    new_end = end = str + cy_string_len(str) - 1;
    while (
        new_start <= end &&
            cy_char_first_occurence(char_set, *new_start) != NULL
    ) {
        new_start += 1;
    }
    while (
        new_end > new_start &&
            cy_char_first_occurence(char_set, *new_end) != NULL
    ) {
        new_end -= 1;
    }

    isize len = (isize)(new_start > new_end ? 0 : new_end - new_start + 1);
    if (str != new_start) {
        cy_mem_move(str, new_start, len);
    }

    str[len] = '\0';
    cy__string_set_len(str, len);
    return str;
}

typedef struct {
    const u8 *text;
    isize len;
} CyStringView;

CY_DEF CyStringView cy_string_view_create_len(const char *str, isize len)
{
    if (len < 0) {
        len = cy_str_len(str);
    }

    return (CyStringView){
        .text = (const u8*)str,
        .len = len,
    };
}

CY_DEF CyStringView cy_string_view_create(const CyString str)
{
    return cy_string_view_create_len(str, cy_string_len(str));
}

CY_DEF CyStringView cy_string_view_create_c(const char *str)
{
    return cy_string_view_create_len(str, cy_str_len(str));
}

CY_DEF CyStringView cy_string_view_substring(
    const CyStringView str,
    isize begin_idx,
    isize end_idx
) {
    isize max = str.len, lo = begin_idx, hi = end_idx;
    CY_ASSERT_MSG(lo <= hi && hi <= max, "%td..%td..%td", lo, hi, max);

    return cy_string_view_create_len((const char*)str.text + lo, hi - lo);
}

CY_DEF inline b32 cy_string_view_are_equal(const CyStringView a, const CyStringView b)
{
    return a.len == b.len &&
        cy_mem_compare((void*)a.text, (void*)b.text, a.len) == 0;
}

CY_DEF inline CyString cy_string_append_view(CyString str, CyStringView view)
{
    return cy_string_append_len(str, (const char*)view.text, view.len);
}

CY_DEF inline b32 cy_string_view_has_prefix(CyStringView str, const char *prefix)
{
    CyStringView other = cy_string_view_create_c(prefix);
    return cy_string_view_are_equal(
        cy_string_view_create_len((const char*)str.text, other.len), other
    );
}

/* TODO(cya):
 * figure out string views
 * more CyString procedures (trim, etc.)
 */

#endif /* _CY_H */
