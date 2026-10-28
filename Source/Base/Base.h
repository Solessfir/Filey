#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdckdint.h>

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;

typedef float    f32;
typedef double   f64;

typedef i8       b8;
typedef i16      b16;
typedef i32      b32;
typedef i64      b64;

typedef wchar_t  wchar;  // Wide character, used in Win API
typedef i32      rune;   // Unicode codepoint

static_assert(sizeof(u32) == 4);
static_assert(sizeof(u64) == 8);
static_assert(sizeof(void *) == 8, "64-bit targets only");

#define function      static
#define global        static
#define local_persist static

#define enum8(type)  u8
#define enum16(type) u16
#define enum32(type) u32
#define enum64(type) u64

// -----------------------------------------------------------------------------
// Diagnostics
// -----------------------------------------------------------------------------

[[noreturn]] function void AssertFail(const char *expression, const char *file, i32 line);

#define AssertAlways(condition) \
    do { \
        if (!(condition)) { \
            AssertFail(#condition, __FILE__, __LINE__); \
        } \
    } while (0)

#if defined(DEBUG)
#define Assert(condition) AssertAlways(condition)
#else
// sizeof keeps variables used only by asserts from triggering unused warnings.
#define Assert(condition) ((void)sizeof(!(condition)))
#endif

#define InvalidPath AssertFail("Invalid path", __FILE__, __LINE__)

// -----------------------------------------------------------------------------
// Units, arrays, ranges, bits
// -----------------------------------------------------------------------------

#define Thousands(value) ((u64)(value) * UINT64_C(1000))
#define Millions(value)  (Thousands(value) * UINT64_C(1000))
#define Billions(value)  (Millions(value) * UINT64_C(1000))

#define Kilobytes(value) ((u64)(value) * UINT64_C(1024))
#define Megabytes(value) (Kilobytes(value) * UINT64_C(1024))
#define Gigabytes(value) (Megabytes(value) * UINT64_C(1024))
#define Terabytes(value) (Gigabytes(value) * UINT64_C(1024))

// Only valid for real arrays; a pointer silently gives the wrong count.
#define CArrayCount(array) (sizeof(array) / sizeof((array)[0]))
#define CArrayZero(array)  MemZero((array), sizeof(array))

#define IsBetween(x, lower, upper) (((lower) <= (x)) && ((x) <= (upper)))

#define Bit(pos) (UINT64_C(1) << (pos))

#define HasBit(n, pos)       (((n) & Bit(pos)) != 0)
#define FlagSet(n, f)        ((n) |= (f))
#define FlagClear(n, f)      ((n) &= ~(f))
#define FlagToggle(n, f)     ((n) ^= (f))
#define FlagExists(n, f)     (((n) & (f)) == (f))
#define FlagEquals(n, f)     ((n) == (f))
#define FlagIntersects(n, f) (((n) & (f)) != 0)

// -----------------------------------------------------------------------------
// Math
// -----------------------------------------------------------------------------

function b32 IsPow2(u64 value) { return value != 0 && (value & (value - 1)) == 0; }

// `alignment` must be a non-zero power of two.
function u64 AlignPow2(u64 value, u64 alignment) { return (value + alignment - 1) & ~(alignment - 1); }

function i32 MinI32(i32 a, i32 b) { return a < b ? a : b; }
function i64 MinI64(i64 a, i64 b) { return a < b ? a : b; }
function u32 MinU32(u32 a, u32 b) { return a < b ? a : b; }
function u64 MinU64(u64 a, u64 b) { return a < b ? a : b; }
function f32 MinF32(f32 a, f32 b) { return a < b ? a : b; }
function f64 MinF64(f64 a, f64 b) { return a < b ? a : b; }

function i32 MaxI32(i32 a, i32 b) { return a > b ? a : b; }
function i64 MaxI64(i64 a, i64 b) { return a > b ? a : b; }
function u32 MaxU32(u32 a, u32 b) { return a > b ? a : b; }
function u64 MaxU64(u64 a, u64 b) { return a > b ? a : b; }
function f32 MaxF32(f32 a, f32 b) { return a > b ? a : b; }
function f64 MaxF64(f64 a, f64 b) { return a > b ? a : b; }

// Dispatches on the usual arithmetic conversion of both arguments.
#define Min(a, b) _Generic((a) + (b), \
    i32: MinI32, i64: MinI64, u32: MinU32, u64: MinU64, f32: MinF32, f64: MinF64)((a), (b))

#define Max(a, b) _Generic((a) + (b), \
    i32: MaxI32, i64: MaxI64, u32: MaxU32, u64: MaxU64, f32: MaxF32, f64: MaxF64)((a), (b))

// -----------------------------------------------------------------------------
// Memory primitives
// -----------------------------------------------------------------------------

// All of these accept NULL when size is zero.
function void MemZero(void *dst, u64 size);
function void MemCopy(void *dst, const void *src, u64 size);
function void MemMove(void *dst, const void *src, u64 size);
function i32  MemCompare(const void *a, const void *b, u64 size);

#define ZeroStruct(ptr)      MemZero((ptr), sizeof(*(ptr)))
#define CopyStruct(dst, src) MemCopy((dst), (src), sizeof(*(dst)))

// -----------------------------------------------------------------------------
// Arena
// -----------------------------------------------------------------------------

#define DefaultAlignment alignof(max_align_t)

// Address space per block.
// A push that does not fit chains a new block, so this only bounds how often chaining happens, not how much an arena can hold.
#ifndef DefaultReserveSize
#define DefaultReserveSize Megabytes(64)
#endif

#define ArenaCommitGranularity Kilobytes(64)

// {0} is a valid empty arena; the first push reserves memory.
typedef struct Arena Arena;
struct Arena {
    u64    reserved;
    u64    committed;
    u64    used;
    u64    basePos;  // arena-wide position where the current block starts
    void  *base;
    Arena *prev;     // previous block's descriptor, stored inside the current block
};

typedef struct ArenaMark {
    Arena *arena;
    u64    pos;      // basePos + used at the time of the mark
} ArenaMark;

typedef ArenaMark Scratch;

function void *_ArenaPush(Arena *arena, u64 size, u64 alignment, b32 clearToZero);
function u64   ArenaPos(const Arena *arena);
function void  ArenaPopTo(Arena *arena, u64 pos);
function void  ArenaClear(Arena *arena);
function void  ArenaRelease(Arena *arena);

function ArenaMark ArenaGetMark(Arena *arena);
function void      ArenaRestore(ArenaMark mark);

// Returns NULL when `count * itemSize` overflows. Use for externally-derived counts.
function void *ArenaPushArrayChecked(Arena *arena, u64 count, u64 itemSize, u64 itemAlignment);

#define ArenaPush(arena, size) \
    _ArenaPush((arena), (u64)(size), DefaultAlignment, 1)

#define ArenaPushStruct(arena, type) \
    ((type *)_ArenaPush((arena), sizeof(type), alignof(type), 1))

#define ArenaPushArray(arena, count, type) \
    ((type *)_ArenaPush((arena), (u64)(count) * sizeof(type), alignof(type), 1))

#define ArenaPushStructNoZero(arena, type) \
    ((type *)_ArenaPush((arena), sizeof(type), alignof(type), 0))

#define ArenaPushArrayNoZero(arena, count, type) \
    ((type *)_ArenaPush((arena), (u64)(count) * sizeof(type), alignof(type), 0))

// Allocates a struct that owns the arena it lives in.
// ArenaRelease on that member frees the struct itself.
function void *ArenaBootstrapStructRaw(u64 structSize, u64 structAlignment, u64 arenaMemberOffset);

#define ArenaBootstrapStruct(type, arenaMember) \
    ((type *)ArenaBootstrapStructRaw(sizeof(type), alignof(type), offsetof(type, arenaMember)))

// Thread-local scratch.
// Pass the arena that results must survive in as `conflict` so the scratch scope never rolls back caller-owned allocations.
function Scratch GetScratchEx(Arena *conflict);
function void    ReleaseScratch(Scratch scratch);
// Gives back the calling thread's scratch arenas, for a thread that is about to end.
function void    ReleaseThreadScratch(void);

#define GetScratch() GetScratchEx(NULL)

// -----------------------------------------------------------------------------
// Pools
// -----------------------------------------------------------------------------

typedef struct PoolNode PoolNode;
struct PoolNode {
    PoolNode *next;
};

#define PoolBatchCount 16

function void *_PoolAlloc(Arena *arena, PoolNode **pool, u64 itemSize, u64 itemAlignment, b32 clearToZero, u32 batchCount);
function void  _PoolFree(PoolNode **pool, void *ptr);

#define PoolAlloc(arena, pool, type) \
    ((type *)_PoolAlloc((arena), (PoolNode **)&(pool), sizeof(type), alignof(type), 1, PoolBatchCount))

#define PoolAllocEx(arena, pool, type, clear, count) \
    ((type *)_PoolAlloc((arena), (PoolNode **)&(pool), sizeof(type), alignof(type), (clear), (count)))

#define PoolFree(pool, ptr) _PoolFree((PoolNode **)&(pool), (ptr))

// -----------------------------------------------------------------------------
// Intrusive lists
//
// Arguments must be plain lvalues without side effects.
// The N/NP suffix names the link member(s); the short forms assume `next` and `prev`.
// -----------------------------------------------------------------------------

#define StackPushN(first, node, nextMember) \
    do { \
        (node)->nextMember = (first); \
        (first) = (node); \
    } while (0)

#define StackPopN(first, nextMember) \
    do { \
        if ((first) != NULL) { \
            (first) = (first)->nextMember; \
        } \
    } while (0)

#define StackPush(first, node) StackPushN(first, node, next)
#define StackPop(first)        StackPopN(first, next)

#define QueuePushN(first, last, node, nextMember) \
    do { \
        (node)->nextMember = NULL; \
        if ((first) == NULL) { \
            (first) = (last) = (node); \
        } else { \
            (last)->nextMember = (node); \
            (last) = (node); \
        } \
    } while (0)

#define QueuePopN(first, last, nextMember) \
    do { \
        if ((first) != NULL) { \
            if ((first) == (last)) { \
                (first) = NULL; \
                (last) = NULL; \
            } else { \
                (first) = (first)->nextMember; \
            } \
        } \
    } while (0)

#define QueuePush(first, last, node) QueuePushN(first, last, node, next)
#define QueuePop(first, last)        QueuePopN(first, last, next)

#define DLLPushBackNP(first, last, node, nextMember, prevMember) \
    do { \
        (node)->nextMember = NULL; \
        (node)->prevMember = (last); \
        if ((last) != NULL) { \
            (last)->nextMember = (node); \
        } else { \
            (first) = (node); \
        } \
        (last) = (node); \
    } while (0)

#define DLLPushFrontNP(first, last, node, nextMember, prevMember) \
    do { \
        (node)->prevMember = NULL; \
        (node)->nextMember = (first); \
        if ((first) != NULL) { \
            (first)->prevMember = (node); \
        } else { \
            (last) = (node); \
        } \
        (first) = (node); \
    } while (0)

#define DLLInsertAfterNP(first, last, refNode, node, nextMember, prevMember) \
    do { \
        if ((refNode) == (last)) { \
            DLLPushBackNP(first, last, node, nextMember, prevMember); \
        } else { \
            (node)->prevMember = (refNode); \
            (node)->nextMember = (refNode)->nextMember; \
            (refNode)->nextMember->prevMember = (node); \
            (refNode)->nextMember = (node); \
        } \
    } while (0)

#define DLLInsertBeforeNP(first, last, refNode, node, nextMember, prevMember) \
    do { \
        if ((refNode) == (first)) { \
            DLLPushFrontNP(first, last, node, nextMember, prevMember); \
        } else { \
            (node)->nextMember = (refNode); \
            (node)->prevMember = (refNode)->prevMember; \
            (refNode)->prevMember->nextMember = (node); \
            (refNode)->prevMember = (node); \
        } \
    } while (0)

#define DLLRemoveNP(first, last, node, nextMember, prevMember) \
    do { \
        if ((node)->prevMember != NULL) { \
            (node)->prevMember->nextMember = (node)->nextMember; \
        } else { \
            (first) = (node)->nextMember; \
        } \
        if ((node)->nextMember != NULL) { \
            (node)->nextMember->prevMember = (node)->prevMember; \
        } else { \
            (last) = (node)->prevMember; \
        } \
        (node)->nextMember = NULL; \
        (node)->prevMember = NULL; \
    } while (0)

#define DLLPushBack(first, last, node)               DLLPushBackNP(first, last, node, next, prev)
#define DLLPushFront(first, last, node)              DLLPushFrontNP(first, last, node, next, prev)
#define DLLInsertAfter(first, last, refNode, node)   DLLInsertAfterNP(first, last, refNode, node, next, prev)
#define DLLInsertBefore(first, last, refNode, node)  DLLInsertBeforeNP(first, last, refNode, node, next, prev)
#define DLLRemove(first, last, node)                 DLLRemoveNP(first, last, node, next, prev)

// -----------------------------------------------------------------------------
// Index
// -----------------------------------------------------------------------------

// {0} means "not found".
typedef struct Index {
    b32 exists;
    u64 value;
} Index;

// -----------------------------------------------------------------------------
// Arrays
//
// Typed arrays are { ArrayHeader header; T *v; } and are valid when zeroed.
// Growth leaves the old storage dead inside the arena.
// -----------------------------------------------------------------------------

typedef struct ArrayHeader {
    u64 count;
    u64 capacity;
} ArrayHeader;

function void *_ArrayGrow(Arena *arena, u64 count, u64 *capacity, void *array, u64 itemSize, u64 add, b32 clearToZero);
function void *_ArrayReserve(Arena *arena, u64 count, u64 *capacity, void *array, u64 itemSize, u64 minCapacity);

#define ArrayReserve(arena, a, n) \
    ((a).v = _ArrayReserve((arena), (a).header.count, &(a).header.capacity, (a).v, sizeof(*(a).v), (n)))

#define ArrayPush(arena, a, value) \
    do { \
        (a).v = _ArrayGrow((arena), (a).header.count, &(a).header.capacity, (a).v, sizeof(*(a).v), 1, false); \
        (a).v[(a).header.count++] = (value); \
    } while (0)

// Appends `n` elements and evaluates to a pointer to the first one.
#define ArrayAddEx_(arena, a, n, clear) \
    ((a).v = _ArrayGrow((arena), (a).header.count, &(a).header.capacity, (a).v, sizeof(*(a).v), (n), (clear)), \
     (a).header.count += (n), \
     (a).v + ((a).header.count - (n)))

#define ArrayAdd(arena, a, n)      ArrayAddEx_(arena, a, n, false)
#define ArrayAddClear(arena, a, n) ArrayAddEx_(arena, a, n, true)

// The index is read once, into a variable, so that inserting at a constant 0 is not a comparison GCC warns is always true.
#define ArrayInsert(arena, a, index, value) \
    do { \
        u64 at_ = (u64)(index); \
        Assert(at_ <= (a).header.count); \
        (a).v = _ArrayGrow((arena), (a).header.count, &(a).header.capacity, (a).v, sizeof(*(a).v), 1, false); \
        MemMove((a).v + at_ + 1, (a).v + at_, ((a).header.count - at_) * sizeof(*(a).v)); \
        (a).v[at_] = (value); \
        (a).header.count += 1; \
    } while (0)

#define ArrayRemove(a, index, n) \
    do { \
        Assert((u64)(index) + (u64)(n) <= (a).header.count); \
        MemMove((a).v + (index), (a).v + (index) + (n), \
                ((a).header.count - (u64)(index) - (u64)(n)) * sizeof(*(a).v)); \
        (a).header.count -= (n); \
    } while (0)

#define ArrayClear(a) ((a).header.count = 0)

// -----------------------------------------------------------------------------
// Strings
// -----------------------------------------------------------------------------

// Immutable UTF-8 view. The bytes belong to static storage or an arena.
typedef struct String {
    const char *value;
    u64 length;
} String;

typedef struct ArrayStr {
    ArrayHeader header;
    String *v;
} ArrayStr;

// Only for string literals; a `char *` gives the pointer size.
#define S(text) ((String){ .value = (text), .length = sizeof(text) - 1 })
// S for initializers of static data, where a compound literal is not a constant expression.
#define SInit(text) { .value = (text), .length = sizeof(text) - 1 }

typedef u32 StrSearchFlags;
enum {
    SSF_None            = 0,
    SSF_SearchBackwards = Bit(0),
    SSF_IndexPlusOne    = Bit(1), // on match, return the index just past the match
    SSF_CountOnFail     = Bit(2), // on failure, return string.length instead of 0
};

function String StrMake(const char *value, u64 length);
function String StrFromCStr(const char *cstr);
function b32    StrIsEqual(String a, String b);
function String StrCopy(Arena *arena, String source);

// Slices never allocate. Out-of-range indices clamp to an empty or shorter slice.
function String StrFrom(String s, u64 from);
// A negative `to` counts back from the end: StrFromTo(S("abcdef"), 1, -2) == "bcd".
function String StrFromTo(String s, u64 from, i64 to);

function Index    StrIndexChar(String s, char c, StrSearchFlags flags);
function Index    StrIndexStr(String s, String needle, StrSearchFlags flags);
// Empty tokens are skipped, so "\\a\\\\b\\" split on '\\' gives ["a", "b"].
function ArrayStr StrSplit(Arena *arena, String s, char separator);

// Decodes the codepoint at *pos and advances past it.
// Malformed, overlong or truncated sequences give U+FFFD and advance one byte.
function rune StrDecodeUtf8(String s, u64 *pos);

// Format specifiers follow printf, plus %S for a String argument.
function String StrFormat(Arena *arena, const char *format, ...);

// -----------------------------------------------------------------------------
// String builder
//
// {0} is valid.
// `buffer` is the accumulated String; appends return just the newly appended slice.
// The builder writes into `buffer` through a cast, so nothing else may.
// -----------------------------------------------------------------------------

typedef struct StrBuilder {
    String buffer;
    u64    capacity;
} StrBuilder;

function String StrBuildBytes(Arena *arena, StrBuilder *builder, const void *data, u64 length);
function String StrBuildStr(Arena *arena, StrBuilder *builder, String s);
function String StrBuildCStr(Arena *arena, StrBuilder *builder, const char *cstr);
function String StrBuildChar(Arena *arena, StrBuilder *builder, char c);
function String StrBuildI32(Arena *arena, StrBuilder *builder, i32 value);
function String StrBuildU32(Arena *arena, StrBuilder *builder, u32 value);
function String StrBuildI64(Arena *arena, StrBuilder *builder, i64 value);
function String StrBuildU64(Arena *arena, StrBuilder *builder, u64 value);
function String StrBuildF32(Arena *arena, StrBuilder *builder, f32 value);
function String StrBuildF64(Arena *arena, StrBuilder *builder, f64 value);
function String StrBuildFormat(Arena *arena, StrBuilder *builder, const char *format, ...);
function String StrBuildFormatV(Arena *arena, StrBuilder *builder, const char *format, va_list args);

// Character constants such as '\n' have type int in C and select StrBuildI32; use StrBuildChar for single characters.
#define StrBuild(arena, builder, x) \
    _Generic((x), \
        String:       StrBuildStr, \
        char *:       StrBuildCStr, \
        const char *: StrBuildCStr, \
        char:         StrBuildChar, \
        i32:          StrBuildI32, \
        u32:          StrBuildU32, \
        i64:          StrBuildI64, \
        u64:          StrBuildU64, \
        f32:          StrBuildF32, \
        f64:          StrBuildF64 \
    )((arena), (builder), (x))

#define StrBuildF(arena, builder, format, ...) \
    StrBuildFormat((arena), (builder), (format) __VA_OPT__(,) __VA_ARGS__)

// -----------------------------------------------------------------------------
// Maps
//
// A typed map is { MapHeader header; KV *v; } where KV is { K key; V value; } with `key` as the first member.
// `v[0 .. header.count)` is a flat array of live entries in no particular order; deletion moves the last entry into the freed slot.
// The hash table stores indices into `v`.
//
// Map* compares keys bytewise, so key types must not contain padding.
// Maps* takes String keys and does not copy the key bytes; they must outlive the entry.
// -----------------------------------------------------------------------------

typedef struct MapHashEntry {
    u64 hash;   // 0 = empty, 1 = deleted
    u64 index;
} MapHashEntry;

typedef struct MapHeader {
    u64 count;
    u64 capacity;
    u64 tableSize;   // power of two, or 0 before the first insert
    u64 usedSlots;   // live + deleted table slots
    MapHashEntry *table;
} MapHeader;

function void *_MapPut(Arena *arena, MapHeader *header, void *v, u64 kvSize, const void *key, u64 keySize, b32 isStr, u64 *outIndex);
function Index _MapIndex(const MapHeader *header, const void *v, u64 kvSize, const void *key, u64 keySize, b32 isStr);
function void *_MapValuePtr(const MapHeader *header, void *v, u64 kvSize, const void *key, u64 keySize, b32 isStr, u64 valueOffset);
function const void *_MapValueOrZero(const MapHeader *header, void *v, u64 kvSize, const void *key, u64 keySize, b32 isStr, u64 valueOffset, u64 valueSize);
function b32   _MapDel(MapHeader *header, void *v, u64 kvSize, const void *key, u64 keySize, b32 isStr);
function void *_MapReserve(Arena *arena, MapHeader *header, void *v, u64 kvSize, u64 keySize, b32 isStr, u64 n);
function void  _MapClear(MapHeader *header);
// The hash maps key their entries by, of any bytes.
function u64   MapHashBytes(const void *data, u64 size);

// Pointer to a temporary copy of the key, typed like the map's key.
#define MapKeyPtr_(m, k) ((typeof((m).v->key)[1]){ (k) })

#define MapArgs_(m, k, isStr) \
    &(m).header, (m).v, sizeof(*(m).v), MapKeyPtr_(m, k), sizeof((m).v->key), (isStr)

#define MapPut_(arena, m, k, val, isStr) \
    do { \
        static_assert(offsetof(typeof(*(m).v), key) == 0, "key must be the first member"); \
        u64 mapAt_ = 0; \
        (m).v = _MapPut((arena), MapArgs_(m, k, isStr), &mapAt_); \
        (m).v[mapAt_].value = (val); \
    } while (0)

#define MapIndex_(m, k, isStr) _MapIndex(MapArgs_(m, k, isStr))

#define MapGetPtrNull_(m, k, isStr) \
    ((typeof(&(m).v->value))_MapValuePtr(MapArgs_(m, k, isStr), offsetof(typeof(*(m).v), value)))

// Evaluates to the stored value, or a zero value when the key is missing.
#define MapGet_(m, type, k, isStr) \
    (*(const type *)_MapValueOrZero(MapArgs_(m, k, isStr), offsetof(typeof(*(m).v), value), sizeof(type)))

#define MapTryGet_(m, k, out, isStr) \
    _MapTryGet(MapArgs_(m, k, isStr), offsetof(typeof(*(m).v), value), (out), sizeof(*(out)))

#define MapDel_(m, k, isStr) _MapDel(MapArgs_(m, k, isStr))

function b32 _MapTryGet(const MapHeader *header, void *v, u64 kvSize, const void *key, u64 keySize, b32 isStr, u64 valueOffset, void *out, u64 outSize);

#define MapReserve(arena, m, n) \
    ((m).v = _MapReserve((arena), &(m).header, (m).v, sizeof(*(m).v), sizeof((m).v->key), false, (n)))
#define MapClear(m) _MapClear(&(m).header)

#define MapPut(arena, m, k, val)  MapPut_(arena, m, k, val, false)
#define MapIndex(m, k)            MapIndex_(m, k, false)
#define MapGet(m, type, k)        MapGet_(m, type, k, false)
#define MapTryGet(m, k, out)      MapTryGet_(m, k, out, false)
#define MapGetPtrNull(m, k)       MapGetPtrNull_(m, k, false)
#define MapDel(m, k)              MapDel_(m, k, false)

#define MapsReserve(arena, m, n) \
    ((m).v = _MapReserve((arena), &(m).header, (m).v, sizeof(*(m).v), sizeof(String), true, (n)))
#define MapsPut(arena, m, k, val) MapPut_(arena, m, k, val, true)
#define MapsIndex(m, k)           MapIndex_(m, k, true)
#define MapsGet(m, type, k)       MapGet_(m, type, k, true)
#define MapsTryGet(m, k, out)     MapTryGet_(m, k, out, true)
#define MapsGetPtrNull(m, k)      MapGetPtrNull_(m, k, true)
#define MapsDel(m, k)             MapDel_(m, k, true)
