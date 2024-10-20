#ifndef _CY_H
#define _CY_H

// NOTE(cya): a lot of this library is borrowed from GingerBill's gb library
// https://github.com/gingerBill/gb/blob/master/gb.h

/******************************************************************************
 ******************************* DECLARATIONS *********************************
 ******************************************************************************/

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

/* ================================= Runtime ================================ */
#include <stdio.h>   // for optional logging
#include <stdarg.h>  // va_args

// TODO(cya): implement printf family and move this stuff to some better place
#define cy_printf_err(...) fprintf(stderr, __VA_ARGS__)
#define cy_printf_err_va(fmt, va) vfprintf(stderr, fmt, va)

#define CY_BIT(n) (1 << (n))
#define CY_UNUSED(param) (void)(param)

CY_DEF void cy_handle_assertion(
    const char *prefix,
    const char *cond,
    const char *file,
    i32 line,
    const char *msg,
    ...
);

// TODO(cya): remove header and implement mem functions
#include <string.h>

CY_DEF void *cy_mem_copy(
    void *restrict dst, const void *restrict src, isize bytes
);
CY_DEF void *cy_mem_move(void *dst, const void *src, isize bytes);
CY_DEF void *cy_mem_set(void *dst, u8 val, isize bytes);
CY_DEF void *cy_mem_zero(void *dst, isize bytes);
CY_DEF isize cy_mem_compare(const void *a, const void *b, isize bytes);

CY_DEF b8 cy_is_power_of_two(isize n);

CY_DEF isize cy_align_forward(isize size, isize align);
CY_DEF void *cy_align_ptr_forward(void *ptr, isize align);

/* Aligns a pointer forward accounting for both the size
 * of a header and the alignment */
CY_DEF usize cy_calc_header_padding(
    uintptr ptr, usize align, usize header_size
);

typedef struct CyTicks CyTicks;

typedef enum {
    CY_SECONDS = 1ULL,
    CY_MILISECONDS = 1000ULL,
    CY_MICROSECONDS = 1000000ULL,
    CY_NANOSECONDS = 1000000000ULL,
} CyTimeUnit;

CY_DEF CyTicks cy_ticks_query(void);
CY_DEF CyTicks cy_ticks_elapsed(CyTicks start, CyTicks end);
CY_DEF f64 cy_ticks_to_time_unit(CyTicks ticks, CyTimeUnit unit);

/* =============================== Allocators =============================== */
typedef enum {
    CY_ALLOCATION_ALLOC,
    CY_ALLOCATION_ALLOC_ALL,
    CY_ALLOCATION_FREE,
    CY_ALLOCATION_FREE_ALL,
    CY_ALLOCATION_RESIZE,
} CyAllocationType;

typedef enum {
    CY_ALLOCATOR_CLEAR_TO_ZERO = CY_BIT(0),
} CyAllocatorFlags;

#define CY_ALLOCATOR_PROC(name) void *name(      \
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
#define cy_resize_array(allocator, arr, type, old_count, new_count) cy_resize( \
    allocator, arr, (old_count) * sizeof(type), (new_count) * sizeof(type) \
)

CY_DEF void *cy_alloc_align(CyAllocator a, isize size, isize align);
CY_DEF void *cy_alloc(CyAllocator a, isize size);
CY_DEF void cy_free(CyAllocator a, void *ptr);
CY_DEF void cy_free_all(CyAllocator a);
CY_DEF void *cy_resize_align(
    CyAllocator a, void *ptr, isize old_size, isize new_size, isize align
);
CY_DEF void *cy_resize(
    CyAllocator a, void *ptr, isize old_size, isize new_size
);
// NOTE(cya): a simple resize that should fit most use cases
CY_DEF void *cy_default_resize_align(
    CyAllocator a, void *ptr, isize old_size, isize new_size, isize align
);
CY_DEF void *cy_default_resize(
    CyAllocator a, void *ptr, isize old_size, isize new_size
);
CY_DEF void *cy_alloc_copy_align(
    CyAllocator a, const void *src, isize size, isize align
);
CY_DEF void *cy_alloc_copy(CyAllocator a, const void *src, isize size);
CY_DEF char *cy_alloc_string_len(CyAllocator a, const char *str, isize len);
CY_DEF char *cy_alloc_string(CyAllocator a, const char *str);

// TODO(cya): figure out this sorcery
#define CY__LO_ONES ((usize)-1 / U8_MAX)
#define CY__HI_ONES (CY__LO_ONES * (U8_MAX / 2 + 1))
#define CY__HAS_ZERO_BYTE(n) !!(((n) - CY__LO_ONES) & ~(n) & CY__HI_ONES)

#define CY_MIN(a, b) (a < b ? a : b)
#define CY_MAX(a, b) (a > b ? a : b)

#define CY_VALIDATE_PTR(p) if (p == NULL) return NULL

#define CY_STATIC_STR_LEN(str) (isize)((sizeof(str) - 1) / sizeof(*(str)))
#define CY_STATIC_ARR_LEN(arr) (isize)((sizeof(arr)) / sizeof(*(arr)))

// NOTE(cya): for testing allocators and program behavior on OOM errors
CY_DEF CyAllocatorProc cy_null_allocator_proc;
CY_DEF CyAllocator cy_null_allocator(void);

// NOTE(cya): the default malloc-style heap allocator
CY_DEF CyAllocatorProc cy_heap_allocator_proc;
CY_DEF CyAllocator cy_heap_allocator(void);

#define cy_heap_alloc(size) cy_alloc(cy_heap_allocator(), size)
#define cy_heap_free(ptr) cy_free(cy_heap_allocator(), ptr)

/* ------------------------- Page Allocator Section ------------------------- */
// TODO(cya): query for page size at runtime
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

CY_DEF CyAllocatorProc cy_page_allocator_proc;
CY_DEF CyAllocator cy_page_allocator(void);

// NOTE(cya): size of actual usable memory starting at *ptr
CY_DEF isize cy_page_allocator_alloc_size(void *ptr);

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

CY_DEF CyAllocatorProc cy_arena_allocator_proc;
CY_DEF CyAllocator cy_arena_allocator(CyArena *arena);
CY_DEF CyArenaNode *cy_arena_insert_node(CyArena *arena, isize size);
CY_DEF CyArena cy_arena_init(CyAllocator backing, isize initial_size);
CY_DEF void cy_arena_deinit(CyArena *arena);

/* ------------------------- Stack Allocator Section ------------------------ */
typedef struct CyStackNode {
    u8 *buf;
    isize size;
    isize prev_offset;
    isize offset;
    struct CyStackNode *next;
} CyStackNode;

typedef struct {
    CyStackNode *first_node;
} CyStackState;

typedef struct {
    CyAllocator backing;
    CyStackState state;
} CyStack;

typedef struct {
    isize prev_offset;
    isize padding;
} CyStackHeader;

/* Default initial size set to one page */
#define CY_STACK_INIT_SIZE     0x4000
#define CY_STACK_GROWTH_FACTOR 2.0

CY_DEF CyAllocatorProc cy_stack_allocator_proc;
CY_DEF CyAllocator cy_stack_allocator(CyStack *stack);

CY_DEF CyStackNode *cy_stack_insert_node(CyStack *stack, isize size);
CY_DEF CyStack cy_stack_init(CyAllocator backing, isize initial_size);
CY_DEF void cy_stack_deinit(CyStack *stack);

/* ------------------------- Pool Allocator Section ------------------------- */
typedef struct CyPoolFreeNode {
    struct CyPoolFreeNode *next;
} CyPoolFreeNode;

typedef struct {
    u8 *buf;
    isize len;
    isize chunk_size;

    CyPoolFreeNode *head;
} CyPool;

CY_DEF CyAllocatorProc cy_pool_allocator_proc;
CY_DEF CyAllocator cy_pool_allocator(CyPool *pool);

CY_DEF CyPool cy_pool_init(
    void *backing_buf, isize size, isize chunk_size, isize chunk_align
);

/* TODO(cya):
 * add new behaviors to arena/stack allocators:
 *     (page expansion, static backing buffers and scratch memory (ring bufs))
 * virtual memory allocator (includes commit and reserve helper functions)
 * bump allocator
 * bitmap allocator
 * pool allocator
 * free-list allocator
 * buddy allocator
 * extra allocator strategies from: https://www.youtube.com/watch?v=LIb3L4vKZ7U
 * general-purpose heap allocator composed of the basic ones to replace malloc
 */

/* ============================== Char procs ================================ */
CY_DEF const char *cy_char_first_occurence(const char *str, char c);
CY_DEF const char *cy_char_last_occurence(const char *str, char c);

/* ============================ C-String procs ============================== */
CY_DEF isize cy_str_len(const char *str);

/* ======================= Strings (and StringViews) ======================== */
typedef char *CyString;

typedef struct {
    CyAllocator alloc;
    isize len;
    isize cap;
} CyStringHeader;

#define CY_STRING_HEADER(str) ((CyStringHeader*)(str) - 1)

typedef struct {
    const u8 *text;
    isize len;
} CyStringView;

CY_DEF isize cy_string_len(CyString str);
CY_DEF isize cy_string_cap(CyString str);
CY_DEF isize cy_string_alloc_size(CyString str);
CY_DEF isize cy_string_available_space(CyString str);

CY_DEF void cy__string_set_len(CyString str, isize len);
CY_DEF void cy__string_set_cap(CyString str, isize cap);

CY_DEF CyString cy_string_create_reserve(CyAllocator a, isize cap);
CY_DEF CyString cy_string_create_len(CyAllocator a, const char *str, isize len);
CY_DEF CyString cy_string_create(CyAllocator a, const char *str);
CY_DEF CyString cy_string_create_view(CyAllocator a, CyStringView str);
CY_DEF CyString cy_string_reserve_space_for(CyString str, isize extra_len);
CY_DEF CyString cy_string_shrink(CyString str);
CY_DEF void cy_string_free(CyString str);

CY_DEF CyString cy_string_append_len(
    CyString str, const char *other, isize len
);
CY_DEF CyString cy_string_append(CyString str, const CyString other);
CY_DEF CyString cy_string_append_c(CyString str, const char *other);
CY_DEF CyString cy_string_append_rune(CyString str, Rune r);
CY_DEF CyString cy_string_append_fmt(CyString str, const char *fmt, ...);
CY_DEF CyString cy_string_append_view(CyString str, CyStringView view);
CY_DEF CyString cy_string_prepend_len(
    CyString str, const char *other, isize len
);
CY_DEF CyString cy_string_prepend(CyString str, const CyString other);
CY_DEF CyString cy_string_prepend_c(CyString str, const char *other);
CY_DEF CyString cy_string_prepend_rune(CyString str, Rune r);
CY_DEF CyString cy_string_prepend_fmt(CyString str, const char *fmt, ...);
CY_DEF CyString cy_string_prepend_view(CyString str, CyStringView view);
CY_DEF CyString cy_string_pred_right(CyString str, isize width, Rune r);
CY_DEF CyString cy_string_set(CyString str, const char *c_str);
CY_DEF CyString cy_string_dup(CyAllocator a, const CyString src);
CY_DEF b32 cy_string_are_equal(const CyString a, const CyString b);

typedef enum {
    CY__STRING_TRIM_LEADING = 0x01,
    CY__STRING_TRIM_TRAILING = 0x02,
} CyStringTrimFlags;

CY_DEF CyString cy_string_trim(CyString str, const char *char_set);
CY_DEF CyString cy_string_trim_leading(CyString str, const char *char_set);
CY_DEF CyString cy_string_trim_trailing(CyString str, const char *char_set);
CY_DEF CyString cy__string_trim_internal(
    CyString str, const char *char_set, CyStringTrimFlags flags
);
CY_DEF CyString cy_string_trim_whitespace(CyString str);
CY_DEF CyString cy_string_trim_leading_whitespace(CyString str);
CY_DEF CyString cy_string_trim_trailing_whitespace(CyString str);

CY_DEF CyStringView cy_string_view_create_len(const char *str, isize len);
CY_DEF CyStringView cy_string_view_create(CyString str);
CY_DEF CyStringView cy_string_view_create_c(const char *str);
// NOTE(cya): exclusive range
CY_DEF CyStringView cy_string_view_substring(
    CyStringView str, isize begin_idx, isize end_idx
);
CY_DEF b32 cy_string_view_are_equal(CyStringView a, CyStringView b);
CY_DEF b32 cy_string_view_has_prefix(CyStringView str, const char *prefix);
CY_DEF b32 cy_string_view_contains(CyStringView str, const char *char_set);

/* TODO(cya):
 * more CyString/CyStringView procedures (trim, etc.)
 */


/* ============================ Unicode helpers ============================= */
CY_DEF isize cy_utf8_codepoints(const char *str);


/******************************************************************************
 ******************************* IMPLEMENTATION *******************************
 ******************************************************************************/

#ifdef CY_IMPLEMENTATION
/* ================================= Runtime ================================ */
void cy_handle_assertion(
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

inline void *cy_mem_copy(
    void *restrict dst, const void *restrict src, isize bytes
) {
    return memcpy(dst, src, (usize)bytes);
}

inline void *cy_mem_move(void *dst, const void *src, isize bytes)
{
    return memmove(dst, src, (usize)bytes);
}

inline void *cy_mem_set(void *dst, u8 val, isize bytes)
{
    return memset(dst, val, (usize)bytes);
}

inline void *cy_mem_zero(void *dst, isize bytes)
{
    return cy_mem_set(dst, 0, bytes);
}

inline isize cy_mem_compare(const void *a, const void *b, isize bytes)
{
    return memcmp(a, b, (usize)bytes);
}

inline b8 cy_is_power_of_two(isize n)
{
    return (n & (n - 1)) == 0;
}

inline isize cy_align_forward(isize size, isize align)
{
    CY_ASSERT(align > 0 && cy_is_power_of_two(align));

    isize mod = size & (align - 1);
    return mod ? size + align - mod : size;
}

inline void *cy_align_ptr_forward(void *ptr, isize align)
{
    return (void*)cy_align_forward((isize)ptr, align);
}

inline usize cy_calc_header_padding(uintptr ptr, usize align, usize header_size)
{
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

#ifndef CY_OS_WINDOWS
    #include <time.h>
#endif
struct CyTicks {
#if defined(CY_OS_WINDOWS)
    LARGE_INTEGER counter;
#else
    struct timespec counter;
#endif
};

CyTicks cy_ticks_query(void)
{
    CyTicks ticks;
#if defined(CY_OS_WINDOWS)
    QueryPerformanceCounter(&ticks.counter);
#else
    clock_gettime(CLOCK_MONOTONIC_RAW, &ticks.counter);
#endif

    return ticks;
}

inline CyTicks cy_ticks_elapsed(CyTicks start, CyTicks end)
{
    return (CyTicks){
#if defined(CY_OS_WINDOWS)
        .counter.QuadPart = end.counter.QuadPart - start.counter.QuadPart,
#else
        .counter.tv_sec = end.counter.tv_sec - start.counter.tv_sec,
        .counter.tv_nsec = end.counter.tv_nsec - start.counter.tv_nsec,
#endif
    };
}

inline f64 cy_ticks_to_time_unit(CyTicks ticks, CyTimeUnit unit)
{
#if defined(CY_OS_WINDOWS)
    static LARGE_INTEGER perf_freq;
    if (perf_freq.QuadPart == 0) {
        QueryPerformanceFrequency(&perf_freq);
    }

    return (f64)ticks.counter.QuadPart * unit / perf_freq.QuadPart;
#else
    return (f64)ticks.counter.tv_sec * unit +
        ticks.counter.tv_nsec / (1.0e9 / unit);
#endif
}

/* =============================== Allocators =============================== */
inline void *cy_alloc_align(CyAllocator a, isize size, isize align)
{
    return a.proc(
        a.data, CY_ALLOCATION_ALLOC,
        size, align,
        NULL, 0,
        CY_DEFAULT_ALLOCATOR_FLAGS
    );
}

inline void *cy_alloc(CyAllocator a, isize size)
{
    return cy_alloc_align(a, size, CY_DEFAULT_ALIGNMENT);
}

inline void cy_free(CyAllocator a, void *ptr)
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

inline void cy_free_all(CyAllocator a)
{
    a.proc(
        a.data, CY_ALLOCATION_FREE_ALL,
        0, 0,
        NULL, 0,
        CY_DEFAULT_ALLOCATOR_FLAGS
    );
}

inline void *cy_resize_align(
    CyAllocator a, void *ptr, isize old_size, isize new_size, isize align
) {
    return a.proc(
        a.data, CY_ALLOCATION_RESIZE,
        new_size, align,
        ptr, old_size,
        CY_DEFAULT_ALLOCATOR_FLAGS
    );
}

inline void *cy_resize(CyAllocator a, void *ptr, isize old_size, isize new_size)
{
    return cy_resize_align(a, ptr, old_size, new_size, CY_DEFAULT_ALIGNMENT);
}

CY_DEF inline void *cy_default_resize_align(
    CyAllocator a, void *old_mem, isize old_size, isize new_size, isize align
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

inline void *cy_default_resize(
    CyAllocator a, void *old_mem, isize old_size, isize new_size
) {
    return cy_default_resize_align(
        a, old_mem,
        old_size, new_size,
        CY_DEFAULT_ALIGNMENT
    );
}

inline void *cy_alloc_copy_align(
    CyAllocator a, const void *src, isize size, isize align
) {
    return cy_mem_copy(cy_alloc_align(a, size, align), src, size);
}

inline void *cy_alloc_copy(CyAllocator a, const void *src, isize size)
{
    return cy_alloc_copy_align(a, src, size, CY_DEFAULT_ALIGNMENT);
}

inline char *cy_alloc_string_len(CyAllocator a, const char *str, isize len)
{
    char *res = cy_alloc_copy(a, str, len);
    res[len] = '\0';
    return res;
}

inline char *cy_alloc_string(CyAllocator a, const char *str)
{
    return cy_alloc_string_len(a, str, cy_str_len(str));
}

inline CyAllocator cy_null_allocator(void)
{
    return (CyAllocator){
        .proc = cy_null_allocator_proc,
    };
}

CY_ALLOCATOR_PROC(cy_null_allocator_proc)
{
    CY_UNUSED(allocator_data);
    CY_UNUSED(size);
    CY_UNUSED(align);
    CY_UNUSED(old_size);
    CY_UNUSED(flags);

    void *ptr = NULL;
    switch(type) {
    case CY_ALLOCATION_FREE: {
        CY_ASSERT_MSG(old_mem == NULL, "null allocator: invalid pointer");
    } break;
    default: break;
    }

    return ptr;
}

CY_DEF CyAllocator cy_heap_allocator(void)
{
    return (CyAllocator){
        .proc = cy_heap_allocator_proc,
    };
}

#include <malloc.h>

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
#if defined(CY_OS_WINDOWS)
    CY_UNUSED(old_size);
#endif

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
    case CY_ALLOCATION_ALLOC_ALL: {
    } break;
    case CY_ALLOCATION_FREE: {
        free_align(old_mem, align);
    } break;
    case CY_ALLOCATION_FREE_ALL: {
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

/* ------------------------- Page Allocator Section ------------------------- */
CyAllocator cy_page_allocator(void)
{
    return (CyAllocator){
        .proc = cy_page_allocator_proc,
    };
}

isize cy_page_allocator_alloc_size(void *ptr)
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
    case CY_ALLOCATION_ALLOC_ALL: {
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
inline CyAllocator cy_arena_allocator(CyArena *arena)
{
    return (CyAllocator){
        .proc = cy_arena_allocator_proc,
        .data = arena,
    };
}

#define CY_ARENA_INIT_SIZE     CY_PAGE_SIZE
#define CY_ARENA_GROWTH_FACTOR 2.0

inline CyArenaNode *cy_arena_insert_node(CyArena *arena, isize size)
{
    CY_VALIDATE_PTR(arena);

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

inline CyArena cy_arena_init(CyAllocator backing, isize initial_size)
{
    CY_ASSERT_NOT_NULL(backing.proc);

    isize default_size = CY_ARENA_INIT_SIZE;
    if (initial_size == 0) {
        initial_size = default_size;
    }

    CyArena arena = {
        .backing = backing,
    };
    arena.state.first_node = cy_arena_insert_node(&arena, initial_size);
    return arena;
}

inline void cy_arena_deinit(CyArena *arena)
{
    if (arena == NULL) {
        return;
    }

    CyArenaNode *cur_node = arena->state.first_node;
    while (cur_node != NULL) {
        CyArenaNode *next = cur_node->next;
        cy_free(arena->backing, cur_node);
        cur_node = next;
    }

    cy_mem_set(arena, 0, sizeof(*arena));
}

CY_ALLOCATOR_PROC(cy_arena_allocator_proc)
{
    CY_UNUSED(flags);

    CyArena *arena = (CyArena*)allocator_data;
    CyAllocator a = cy_arena_allocator(arena);
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
    case CY_ALLOCATION_ALLOC_ALL: {
        CyAllocator backing = arena->backing;
        CyArenaNode *cur_node = arena->state.first_node->next, *next;
        while (cur_node != NULL) {
            next = cur_node->next;
            cy_free(backing, cur_node);
            cur_node = next;
        }

        cur_node = arena->state.first_node;
        isize remaining = cur_node->size - cur_node->offset;
        ptr = cy_alloc_align(backing, remaining, align);
        CY_VALIDATE_PTR(ptr);

        ptr = cy_align_ptr_forward(ptr, align);
    } break;
    case CY_ALLOCATION_FREE: {
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
            return cy_alloc_align(a, size, align);
        }

        CyArenaNode *cur_node = arena->state.first_node;
        isize prev_offset;
        b32 is_in_range = false, found_node = false;
        while (cur_node != NULL) {
            is_in_range = (u8*)old_mem >= cur_node->buf &&
                (u8*)old_mem < (cur_node->buf + cur_node->size);
            if (is_in_range) {
                prev_offset = cur_node->prev_offset;
                found_node = cur_node->buf + prev_offset == old_memory &&
                    (cur_node->offset - cur_node->prev_offset == old_size);
                break;
            }

            cur_node = cur_node->next;
        }
        if (!is_in_range) {
            CY_ASSERT_MSG(false, "out-of-bounds arena reallocation");
            break;
        } else if (!found_node) {
            // NOTE(cya): is in valid space but is not the latest allocation
            // (this might result in some odd behavior in some edge cases)
            void *new_mem = cy_alloc_align(a, size, align);
            CY_VALIDATE_PTR(new_mem);

            cy_mem_copy(new_mem, old_mem, old_size);
            ptr = new_mem;
            break;
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

        void *new_ptr = cy_alloc_align(a, size, align);
        CY_VALIDATE_PTR(new_ptr);

        cy_mem_move(new_ptr, old_memory, CY_MIN(old_size, size));
        ptr = new_ptr;
    } break;
    }

    return ptr;
}

/* -------------------------Stack Allocator Section ------------------------- */
inline CyAllocator cy_stack_allocator(CyStack *stack)
{
    return (CyAllocator){
        .data = stack,
        .proc = cy_stack_allocator_proc,
    };
}

inline CyStackNode *cy_stack_insert_node(CyStack *stack, isize size)
{
    CY_VALIDATE_PTR(stack);

    isize node_padding = sizeof(CyStackNode) + CY_DEFAULT_ALIGNMENT;
    isize new_node_size = node_padding + size;
    CyStackNode *new_node = cy_alloc(stack->backing, new_node_size);
    CY_VALIDATE_PTR(new_node);

    new_node->buf = cy_align_ptr_forward(new_node + 1, CY_DEFAULT_ALIGNMENT);
    new_node->size = size;
    new_node->next = stack->state.first_node;
    stack->state.first_node = new_node;

    return new_node;
}

inline CyStack cy_stack_init(CyAllocator backing, isize initial_size)
{
    isize default_size = CY_STACK_INIT_SIZE;
    if (initial_size == 0) {
        initial_size = default_size;
    }

    CyStack stack = {
        .backing = backing,
    };
    stack.state.first_node = cy_stack_insert_node(&stack, initial_size);
    return stack;
}

inline void cy_stack_deinit(CyStack *stack)
{
    if (stack == NULL) {
        return;
    }

    CyStackNode *cur_node = stack->state.first_node;
    while (cur_node != NULL) {
        CyStackNode *next = cur_node->next;
        cy_free(stack->backing, cur_node);
        cur_node = next;
    }

    cy_mem_set(stack, 0, sizeof(*stack));
}

CY_ALLOCATOR_PROC(cy_stack_allocator_proc)
{
    CY_UNUSED(flags);
    CY_ASSERT(cy_is_power_of_two(align));

    CyStack *stack = (CyStack*)allocator_data;
    CyAllocator a = cy_stack_allocator(stack);
    void *ptr = NULL;
    switch(type) {
    case CY_ALLOCATION_ALLOC: {
        CyStackNode *cur_node = stack->state.first_node;
        u8 *cur_addr = cur_node->buf + cur_node->offset;
        CyStackHeader *header;
        isize padding = cy_calc_header_padding(
            (uintptr)cur_addr, align, sizeof(*header)
        );
        u8 *alloc_end = cur_addr + padding + size;
        while (alloc_end > cur_node->buf + cur_node->size) {
            if (cur_node->next == NULL) {
                isize largest_node_size = stack->state.first_node->size;
                isize new_size = largest_node_size * CY_STACK_GROWTH_FACTOR;
                isize max_alloc_size = align + sizeof(*header) + size;

                cur_node = cy_stack_insert_node(
                    stack, CY_MAX(new_size, max_alloc_size)
                );
                CY_VALIDATE_PTR(cur_node);
            } else {
                cur_node = cur_node->next;
            }

            cur_addr = cur_node->buf + cur_node->offset;
            padding = cy_calc_header_padding(
                (uintptr)cur_addr, align, sizeof(*header)
            );
            alloc_end = cur_addr + padding + size;
        }

        ptr = cur_addr + padding;
        header = (CyStackHeader*)ptr - 1;
        header->padding = padding;
        header->prev_offset = cur_node->offset;

        cur_node->prev_offset = header->prev_offset;
        cur_node->offset += size + padding;
    } break;
    case CY_ALLOCATION_ALLOC_ALL: {
        CyAllocator backing = stack->backing;
        CyStackNode *cur_node = stack->state.first_node->next, *next;
        while (cur_node != NULL) {
            next = cur_node->next;
            cy_free(backing, cur_node);
            cur_node = next;
        }

        cur_node = stack->state.first_node;
        isize remaining = cur_node->size - cur_node->offset;
        ptr = cy_alloc_align(backing, remaining, align);
        CY_VALIDATE_PTR(ptr);

        ptr = cy_align_ptr_forward(ptr, align);
    } break;
    case CY_ALLOCATION_FREE: {
        CY_VALIDATE_PTR(old_mem);

        CyStackNode *cur_node = stack->state.first_node;
        u8 *start = cur_node->buf;
        u8 *end = start + cur_node->size;
        u8 *cur_addr = old_mem;
        if (!(start <= cur_addr && cur_addr < end)) {
            CY_ASSERT_MSG(false, "out-of-bounds pointer");
            break;;
        }
        if (cur_addr >= start + cur_node->offset) {
            break;; // NOTE(cya): allowing double-frees
        }

        CyStackHeader *header = (CyStackHeader*)cur_addr - 1;
        isize prev_offset = cur_addr - header->padding - start;
        if (prev_offset != header->prev_offset) {
            CY_ASSERT_MSG(false, "out-of-order deallocation");
            break;;
        }

        cur_node->offset = cur_node->prev_offset;
        cur_node->prev_offset = header->prev_offset;
    } break;
    case CY_ALLOCATION_FREE_ALL: {
        CyStackNode *cur_node = stack->state.first_node, *next = cur_node->next;
        while (next != NULL) {
            cur_node = next;
            next = next->next;
            cy_free(stack->backing, cur_node);
        }

        cur_node = stack->state.first_node;
        cy_mem_set(cur_node->buf, 0, cur_node->size);
        cur_node->prev_offset = cur_node->offset = 0;
        cur_node->next = NULL;
    } break;
    case CY_ALLOCATION_RESIZE: {
        if (old_mem == NULL || old_size == 0) {
            return cy_alloc_align(a, size, align);
        } else if (size == 0) {
            cy_free(a, old_mem);
            break;
        }

        CyStackNode *cur_node = stack->state.first_node;
        u8 *start = cur_node->buf;
        u8 *end = start + cur_node->size;
        u8 *cur_addr = old_mem;
        if (!(start <= cur_addr && cur_addr < end)) {
            CY_ASSERT_MSG(false, "out-of-bounds reallocation");
            break;
        }
        if (cur_addr >= start + cur_node->offset) {
            CY_ASSERT_MSG(false, "out-of-order reallocation");
            break;
        }

        b32 unchanged = old_size == size &&
            old_mem == cy_align_ptr_forward(old_mem, align);
        if (unchanged) {
            return old_mem;
        }

        CyStackHeader *header = (CyStackHeader*)cur_addr - 1;
        isize cur_padding = header->padding;
        u8 *alloc_start = cur_addr - cur_padding;
        isize new_padding = cy_calc_header_padding(
            (uintptr)alloc_start, align, sizeof(*header)
        );
        if (size < old_size && new_padding == cur_padding) {
            cur_node->offset -= (old_size - size);
            return cy_align_ptr_forward(old_mem, align);
        }

        isize prev_offset = (isize)(cur_addr - cur_padding - start);
        isize new_offset = (isize)(alloc_start + new_padding + size - start);
        if (new_offset <= cur_node->size) {
            u8 *new_addr = alloc_start + new_padding;
            header = (CyStackHeader*)new_addr - 1;
            header->padding = new_padding;
            header->prev_offset = prev_offset;

            cur_node->offset = new_offset;
            ptr = alloc_start;
            break;
        }

        // NOTE(cya): not enough memory on current node
        isize largest_node_size = stack->state.first_node->size;
        isize new_size = largest_node_size * CY_STACK_GROWTH_FACTOR;
        isize max_alloc_size = align + sizeof(*header) + size;

        cur_node = cy_stack_insert_node(
            stack, CY_MAX(new_size, max_alloc_size)
        );
        CY_VALIDATE_PTR(cur_node);

        cur_addr = cur_node->buf;
        new_padding = cy_calc_header_padding(
            (uintptr)cur_addr, align, sizeof(*header)
        );

        ptr = cur_addr + new_padding;
        header = (CyStackHeader*)ptr - 1;
        header->padding = new_padding;
        header->prev_offset = cur_node->offset;

        cur_node->offset += new_padding + size;
    } break;
    }

    return ptr;
}

/* ------------------------- Pool Allocator Section ------------------------- */

#if 0
inline CyPool cy_pool_init(
    void *backing_buf, isize size, isize chunk_size, isize chunk_align
) {
    u8 *start = backing_buf;
    u8 *aligned_start = cy_align_ptr_forward(backing_buf, chunk_align);
    isize backing_buf_size = aligned_start - start;

    return (CyPool){
        .buf = aligned_start,
        .len = backing_buf_size,
        .chunk_size = 0,
    };
}

CY_ALLOCATOR_PROC(cy_pool_allocator_proc)
{
    return NULL;
}
#endif

/* ============================== Char procs =============================== */
const char *cy_char_first_occurence(const char *str, char c)
{
    for (; *str != c; str++) {
        if (*str == '\0')  {
            return NULL;
        }
    }

    return str;
}

const char *cy_char_last_occurence(const char *str, char c)
{
    const char *res = NULL;
    do {
        if (*str == c) {
            res = str;
        }
    } while (*str++ != '\0');

    return res;
}

/* ============================ C-String procs ============================== */
inline isize cy_str_len(const char *str)
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

/* ================================ Strings ================================= */
inline isize cy_string_len(CyString str)
{
    return (str == NULL) ? 0 : CY_STRING_HEADER(str)->len;
}

inline isize cy_string_cap(CyString str)
{
    return (str == NULL) ? 0 : CY_STRING_HEADER(str)->cap;
}

inline isize cy_string_alloc_size(CyString str)
{
    return sizeof(CyStringHeader) + cy_string_cap(str) + 1;
}

inline isize cy_string_available_space(CyString str)
{
    CyStringHeader *h = CY_STRING_HEADER(str);
    if (h->cap > h->len) {
        return h->cap - h->len;
    }

    return 0;
}

inline void cy__string_set_len(CyString str, isize len)
{
    if (str == NULL) {
        return;
    }

    CY_STRING_HEADER(str)->len = len;
}

inline void cy__string_set_cap(CyString str, isize cap)
{
    if (str == NULL) {
        return;
    }

    CY_STRING_HEADER(str)->cap = cap;
}

CyString cy_string_create_reserve(CyAllocator a, isize cap)
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

CyString cy_string_create_len(CyAllocator a, const char *str, isize len)
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

CyString cy_string_create(CyAllocator a, const char *str)
{
    return cy_string_create_len(a, str, cy_str_len(str));
}

inline CyString cy_string_create_view(CyAllocator a, CyStringView str)
{
    return cy_string_create_len(a, (const char*)str.text, str.len);
}

void cy_string_free(CyString str)
{
    if (str != NULL) {
        cy_free(CY_STRING_HEADER(str)->alloc, CY_STRING_HEADER(str));
    }
}

CyString cy_string_reserve_space_for(CyString str, isize extra_len)
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

CyString cy_string_shrink(CyString str)
{
    CY_VALIDATE_PTR(str);

    isize len = cy_string_len(str), cap = cy_string_cap(str);
    isize header_size = sizeof(CyStringHeader);
    isize size = header_size + cap + 1;
    isize new_size = header_size + len + 1;
    void *ptr = str - header_size;
    if (new_size < size) {
        ptr = cy_resize(CY_STRING_HEADER(str)->alloc, ptr, size, new_size);
        str = (CyString)ptr + header_size;
        cy__string_set_cap(str, len);
    }

    return str;
}

CyString cy_string_append_len(CyString str, const char *other, isize len)
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

inline CyString cy_string_append(CyString str, const CyString other)
{
    return cy_string_append_len(str, other, cy_string_len(other));
}

inline CyString cy_string_append_c(CyString str, const char *other)
{
    return cy_string_append_len(str, other, cy_str_len(other));
}

CyString cy_string_append_rune(CyString str, Rune r)
{
    // TODO(cya): utf-8 encode rune
    isize width = 1;
    return cy_string_append_len(str, (const char*)&r, width);
}

CyString cy_string_append_fmt(CyString str, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);

    // TODO(cya): maybe have some fancier size handling than this?
    char buf[0x1000] = {0};
    isize len = vsnprintf(buf, CY_STATIC_ARR_LEN(buf), fmt, va);

    va_end(va);
    return cy_string_append_len(str, buf, len);
}

inline CyString cy_string_append_view(CyString str, CyStringView view)
{
    return cy_string_append_len(str, (const char*)view.text, view.len);
}

CyString cy_string_prepend_len(CyString str, const char *other, isize len)
{
    if (len > 0) {
        isize cur_len = cy_string_len(str);

        str = cy_string_reserve_space_for(str, len);
        CY_VALIDATE_PTR(str);

        isize new_len = cur_len + len;
        cy_mem_move(str + len, str, cur_len);
        cy_mem_copy(str, other, len);

        str[new_len] = '\0';
        cy__string_set_len(str, new_len);
    }

    return str;
}

inline CyString cy_string_prepend(CyString str, const CyString other)
{
    return cy_string_prepend_len(str, other, cy_string_len(other));
}

inline CyString cy_string_prepend_c(CyString str, const char *other)
{
    return cy_string_prepend_len(str, other, cy_str_len(other));
}

CY_DEF CyString cy_string_prepend_rune(CyString str, Rune r)
{
    // TODO(cya): utf-8 encode rune
    isize width = 1;
    return cy_string_prepend_len(str, (const char*)&r, width);
}

CY_DEF CyString cy_string_prepend_fmt(CyString str, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);

    // TODO(cya): maybe have some fancier size handling than this?
    char buf[0x1000] = {0};
    isize len = vsnprintf(buf, CY_STATIC_ARR_LEN(buf), fmt, va);

    va_end(va);
    return cy_string_prepend_len(str, buf, len);
}

CY_DEF CyString cy_string_prepend_view(CyString str, CyStringView view)
{
    return cy_string_prepend_len(str, (const char*)view.text, view.len);
}

CyString cy_string_pad_right(CyString str, isize width, Rune r)
{
    // TODO(cya): calculate width with utf8_width proc (to be implemented)
    isize str_width = cy_utf8_codepoints(str);
    isize rune_width = 1;
    while (str_width < width) {
        str = cy_string_append_rune(str, r);
        str_width += rune_width;
    }

    CY_VALIDATE_PTR(str);
    return str;
}

CyString cy_string_set(CyString str, const char *c_str)
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

CyString cy_string_dup(CyAllocator a, const CyString src)
{
    return cy_string_create_len(a, src, cy_string_len(src));
}

b32 cy_string_are_equal(const CyString a, const CyString b)
{
    isize a_len = cy_string_len(a);
    isize b_len = cy_string_len(b);
    if (a_len != b_len) {
        return false;
    }

    return cy_mem_compare(a, b, a_len) == 0;
}

CyString cy__string_trim_internal(
    CyString str, const char *char_set, CyStringTrimFlags flags
) {
    char *start, *new_start;
    new_start = start = str;
    char *end, *new_end;
    new_end = end = str + cy_string_len(str) - 1;
    if (flags & CY__STRING_TRIM_LEADING) {
        while (
            new_start <= end &&
                cy_char_first_occurence(char_set, *new_start) != NULL
        ) {
            new_start += 1;
        }
    }
    if (flags & CY__STRING_TRIM_TRAILING) {
        while (
            new_end > new_start &&
                cy_char_first_occurence(char_set, *new_end) != NULL
        ) {
            new_end -= 1;
        }
    }

    isize len = (isize)(new_start > new_end ? 0 : new_end - new_start + 1);
    if (str != new_start) {
        cy_mem_move(str, new_start, len);
    }

    str[len] = '\0';
    cy__string_set_len(str, len);
    return str;
}

inline CyString cy_string_trim(CyString str, const char *char_set)
{
    return cy__string_trim_internal(
        str, char_set, CY__STRING_TRIM_LEADING | CY__STRING_TRIM_TRAILING
    );
}

inline CyString cy_string_trim_leading(CyString str, const char *char_set)
{
    return cy__string_trim_internal(str, char_set, CY__STRING_TRIM_LEADING);
}

inline CyString cy_string_trim_trailing(CyString str, const char *char_set)
{
    return cy__string_trim_internal(str, char_set, CY__STRING_TRIM_TRAILING);
}

#define CY__WHITESPACE " \t\r\n\v\f"

inline CyString cy_string_trim_whitespace(CyString str)
{
    return cy_string_trim(str, CY__WHITESPACE);
}

inline CyString cy_string_trim_leading_whitespace(CyString str)
{
    return cy_string_trim_leading(str, CY__WHITESPACE);
}

inline CyString cy_string_trim_trailing_whitespace(CyString str)
{
    return cy_string_trim_trailing(str, CY__WHITESPACE);
}

/* ----------------------------- String views ------------------------------- */
CyStringView cy_string_view_create_len(const char *str, isize len)
{
    if (len < 0) {
        len = cy_str_len(str);
    }

    return (CyStringView){
        .text = (const u8*)str,
        .len = len,
    };
}

CyStringView cy_string_view_create(CyString str)
{
    return cy_string_view_create_len(str, cy_string_len(str));
}

CyStringView cy_string_view_create_c(const char *str)
{
    return cy_string_view_create_len(str, cy_str_len(str));
}

CyStringView cy_string_view_substring(
    CyStringView str, isize begin_idx, isize end_idx
) {
    isize max = str.len, lo = begin_idx, hi = end_idx;
    CY_ASSERT_MSG(lo <= hi && hi <= max, "%td..%td..%td", lo, hi, max);

    return cy_string_view_create_len((const char*)str.text + lo, hi - lo);
}

inline b32 cy_string_view_are_equal(CyStringView a, CyStringView b)
{
    return (a.len == b.len) && (cy_mem_compare(a.text, b.text, a.len) == 0);
}

inline b32 cy_string_view_has_prefix(CyStringView str, const char *prefix)
{
    CyStringView other = cy_string_view_create_c(prefix);
    return cy_string_view_are_equal(
        cy_string_view_create_len((const char*)str.text, other.len), other
    );
}

b32 cy_string_view_contains(CyStringView str, const char *char_set)
{
    isize len = cy_str_len(char_set);
    // TODO(cya): unicode rune support
    for (isize i = 0; i < str.len; i++) {
        for (isize j = 0; j < len; j++) {
            if (str.text[i] == char_set[j]) {
                return true;
            }
        }
    }

    return false;
}

/* ============================ Unicode helpers ============================= */
#if 0
static u8 cy__utf8_class_table[32] = {
    0, 0, 0, 0, // 0 = 0xxxx
    0, 0, 0, 0, // 1 = 10xxx
    0, 0, 0, 0, // 2 = 110xx
    0, 0, 0, 0, // 3 = 1110x
    1, 1, 1, 1, // 4 = 11110
    1, 1, 1, 1, // 5 = 11111 (invalid)
    2, 2, 2, 2,
    3, 3, 4, 5,
};

isize cy_utf8_encode_rune(u8 buf[4], Rune r)
{
    return 0;
}

isize cy_utf8_decode(const u8 *str, Rune *codepoint_out)
{
    return 0;
}
#endif

inline isize cy_utf8_codepoints(const char *str)
{
    isize count = 0;
    while (*str != '\0') {
        u8 c = *str;
        u8 inc = 0;
        if (c < 0x80) {
            inc = 1;
        } else if ((c & 0xE0) == 0xC0) {
            inc = 2;
        } else if ((c & 0xF0) == 0xE0) {
            inc = 3;
        } else if ((c & 0xF8) == 0xF0) {
            inc = 4;
        } else {
            return -1;
        }

        str += inc;
        count += 1;
    }

    return count;
}

#endif // CY_IMPLEMENTATION
#endif // _CY_H

