#include <stdio.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Diagnostics
// -----------------------------------------------------------------------------

[[noreturn]] function void AssertFail(const char *expression, const char *file, i32 line)
{
    fprintf(stderr, "%s(%d): assertion failed: %s\n", file, line, expression);
    fflush(stderr);
    __builtin_trap();
}

// -----------------------------------------------------------------------------
// Memory primitives
// -----------------------------------------------------------------------------

// The C library calls are undefined for NULL even with a zero size.

function void MemZero(void *dst, u64 size)
{
    if (size != 0) {
        memset(dst, 0, (size_t)size);
    }
}

function void MemCopy(void *dst, const void *src, u64 size)
{
    if (size != 0) {
        memcpy(dst, src, (size_t)size);
    }
}

function void MemMove(void *dst, const void *src, u64 size)
{
    if (size != 0) {
        memmove(dst, src, (size_t)size);
    }
}

function i32 MemCompare(const void *a, const void *b, u64 size)
{
    return size != 0 ? memcmp(a, b, (size_t)size) : 0;
}

// -----------------------------------------------------------------------------
// Arena
// -----------------------------------------------------------------------------

// A chained block starts with a copy of the previous block's descriptor.
function void ArenaChainBlock(Arena *arena, u64 size, u64 alignment)
{
    u64 headerSize = arena->base != NULL ? sizeof(Arena) : 0;

    u64 needed = 0;
    AssertAlways(!ckd_add(&needed, headerSize + alignment, size));
    u64 reserveSize = Max(DefaultReserveSize, AlignPow2(needed, ArenaCommitGranularity));

    Arena next = {
        .reserved = reserveSize,
        .basePos  = arena->basePos + arena->reserved,
        .base     = PlatformVMReserve(reserveSize),
    };
    AssertAlways(next.base != NULL);

    if (headerSize != 0) {
        next.committed = AlignPow2(headerSize, ArenaCommitGranularity);
        AssertAlways(PlatformVMCommit(next.base, next.committed));

        next.prev  = (Arena *)next.base;
        *next.prev = *arena;
        next.used  = headerSize;
    }

    *arena = next;
}

function void *_ArenaPush(Arena *arena, u64 size, u64 alignment, b32 clearToZero)
{
    Assert(arena != NULL);
    Assert(IsPow2(alignment));

    u64 alignedUsed = AlignPow2(arena->used, alignment);
    AssertAlways(size <= UINT64_MAX - alignedUsed);

    if (arena->base == NULL || alignedUsed + size > arena->reserved) {
        ArenaChainBlock(arena, size, alignment);
        alignedUsed = AlignPow2(arena->used, alignment);
    }

    u64 newUsed = alignedUsed + size;
    Assert(newUsed <= arena->reserved);

    if (newUsed > arena->committed) {
        u64 newCommitted = Min(AlignPow2(newUsed, ArenaCommitGranularity), arena->reserved);
        // Out of physical memory/commit charge; there is no sensible recovery.
        AssertAlways(PlatformVMCommit((u8 *)arena->base + arena->committed, newCommitted - arena->committed));
        arena->committed = newCommitted;
    }

    void *result = (u8 *)arena->base + alignedUsed;
    arena->used = newUsed;

    if (clearToZero) {
        MemZero(result, size);
    }

    return result;
}

function void *ArenaPushArrayChecked(Arena *arena, u64 count, u64 itemSize, u64 itemAlignment)
{
    u64 totalSize = 0;
    if (ckd_mul(&totalSize, count, itemSize)) {
        return NULL;
    }
    return _ArenaPush(arena, totalSize, itemAlignment, 1);
}

function u64 ArenaPos(const Arena *arena)
{
    return arena->basePos + arena->used;
}

function void ArenaPopTo(Arena *arena, u64 pos)
{
    // Positions inside a chained block's header belong to the previous block.
    while (arena->prev != NULL && pos < arena->basePos + sizeof(Arena)) {
        Arena prev = *arena->prev;
        PlatformVMRelease(arena->base, arena->reserved);
        *arena = prev;
    }

    Assert(pos >= arena->basePos);
    Assert(pos - arena->basePos <= arena->used);
    arena->used = pos - arena->basePos;
}

function void ArenaClear(Arena *arena)
{
    ArenaPopTo(arena, 0);
}

function void ArenaRelease(Arena *arena)
{
    if (arena == NULL) {
        return;
    }

    // `arena` may live inside one of the blocks (bootstrapped structs), so walk the chain by value and only touch `arena` afterwards if it lives outside.
    Arena current = *arena;
    b32 descriptorInside = false;

    while (current.base != NULL) {
        u8 *begin = current.base;
        if ((u8 *)arena >= begin && (u8 *)arena < begin + current.reserved) {
            descriptorInside = true;
        }

        Arena prev = current.prev != NULL ? *current.prev : (Arena){0};
        PlatformVMRelease(current.base, current.reserved);
        current = prev;
    }

    if (!descriptorInside) {
        *arena = (Arena){0};
    }
}

function ArenaMark ArenaGetMark(Arena *arena)
{
    return (ArenaMark){
        .arena = arena,
        .pos   = ArenaPos(arena),
    };
}

function void ArenaRestore(ArenaMark mark)
{
    Assert(mark.arena != NULL);
    Assert(mark.pos <= ArenaPos(mark.arena));
    ArenaPopTo(mark.arena, mark.pos);
}

function void *ArenaBootstrapStructRaw(u64 structSize, u64 structAlignment, u64 arenaMemberOffset)
{
    Assert(arenaMemberOffset + sizeof(Arena) <= structSize);

    Arena arena = {0};
    void *object = _ArenaPush(&arena, structSize, structAlignment, 1);

    Arena *embedded = (Arena *)((u8 *)object + arenaMemberOffset);
    *embedded = arena;
    return object;
}

// Two arenas per thread are enough as long as callers pass the arena their results live in as the conflict.
global thread_local Arena ScratchArenas[2];

function Scratch GetScratchEx(Arena *conflict)
{
    Arena *arena = &ScratchArenas[0];
    if (arena == conflict) {
        arena = &ScratchArenas[1];
    }
    return ArenaGetMark(arena);
}

function void ReleaseScratch(Scratch scratch)
{
    ArenaRestore(scratch);
}

// -----------------------------------------------------------------------------
// Pools
// -----------------------------------------------------------------------------

function void *_PoolAlloc(Arena *arena, PoolNode **pool, u64 itemSize, u64 itemAlignment, b32 clearToZero, u32 batchCount)
{
    Assert(itemSize >= sizeof(PoolNode));
    Assert(batchCount > 0);

    if (*pool == NULL) {
        u64 alignment = Max(itemAlignment, (u64)alignof(PoolNode));
        u64 stride = AlignPow2(itemSize, alignment);
        u8 *batch = _ArenaPush(arena, stride * batchCount, alignment, 0);

        // Link back to front so slots are handed out in address order.
        for (u32 i = batchCount; i > 0; --i) {
            PoolNode *node = (PoolNode *)(batch + stride * (i - 1));
            node->next = *pool;
            *pool = node;
        }
    }

    PoolNode *node = *pool;
    *pool = node->next;

    if (clearToZero) {
        MemZero(node, itemSize);
    }
    return node;
}

function void _PoolFree(PoolNode **pool, void *ptr)
{
    PoolNode *node = ptr;
    node->next = *pool;
    *pool = node;
}

// -----------------------------------------------------------------------------
// Arrays
// -----------------------------------------------------------------------------

function void *ArrayRealloc(Arena *arena, u64 count, u64 *capacity, void *array, u64 itemSize, u64 newCapacity)
{
    u64 bytes = 0;
    AssertAlways(!ckd_mul(&bytes, newCapacity, itemSize));

    void *data = _ArenaPush(arena, bytes, DefaultAlignment, 0);
    MemCopy(data, array, count * itemSize);
    *capacity = newCapacity;
    return data;
}

function void *_ArrayGrow(Arena *arena, u64 count, u64 *capacity, void *array, u64 itemSize, u64 add, b32 clearToZero)
{
    u64 needed = 0;
    AssertAlways(!ckd_add(&needed, count, add));

    if (needed > *capacity) {
        u64 newCapacity = Max(Max(*capacity * 2, needed), (u64)8);
        array = ArrayRealloc(arena, count, capacity, array, itemSize, newCapacity);
    }
    Assert(array != NULL || add == 0);

    if (clearToZero && add != 0) {
        MemZero((u8 *)array + count * itemSize, add * itemSize);
    }
    return array;
}

function void *_ArrayReserve(Arena *arena, u64 count, u64 *capacity, void *array, u64 itemSize, u64 minCapacity)
{
    if (minCapacity > *capacity) {
        array = ArrayRealloc(arena, count, capacity, array, itemSize, minCapacity);
    }
    return array;
}

// -----------------------------------------------------------------------------
// Strings
// -----------------------------------------------------------------------------

function String StrMake(const char *value, u64 length)
{
    return (String){ .value = value, .length = length };
}

function String StrFromCStr(const char *cstr)
{
    return cstr != NULL ? StrMake(cstr, strlen(cstr)) : (String){0};
}

function b32 StrIsEqual(String a, String b)
{
    return a.length == b.length && MemCompare(a.value, b.value, a.length) == 0;
}

function String StrCopy(Arena *arena, String source)
{
    if (source.length == 0) {
        return (String){0};
    }
    char *data = _ArenaPush(arena, source.length, 1, 0);
    MemCopy(data, source.value, source.length);
    return StrMake(data, source.length);
}

function String StrFrom(String s, u64 from)
{
    from = Min(from, s.length);
    // Avoids NULL + 0 for the zero string.
    if (from == 0) {
        return s;
    }
    return StrMake(s.value + from, s.length - from);
}

function String StrFromTo(String s, u64 from, i64 to)
{
    u64 end = 0;
    if (to < 0) {
        u64 back = (u64)(-(to + 1)) + 1;
        end = back < s.length ? s.length - back : 0;
    } else {
        end = Min((u64)to, s.length);
    }

    String result = StrFrom(s, from);
    u64 start = Min(from, s.length);
    result.length = end > start ? end - start : 0;
    return result;
}

function rune StrDecodeUtf8(String s, u64 *pos)
{
    Assert(*pos < s.length);
    const u8 *p = (const u8 *)s.value + *pos;
    u64 left = s.length - *pos;

    u32 length = 0;
    rune result = 0;
    rune minimum = 0;
    if (p[0] < 0x80) {
        *pos += 1;
        return p[0];
    } else if ((p[0] & 0xE0) == 0xC0) {
        length = 2, result = p[0] & 0x1F, minimum = 0x80;
    } else if ((p[0] & 0xF0) == 0xE0) {
        length = 3, result = p[0] & 0x0F, minimum = 0x800;
    } else if ((p[0] & 0xF8) == 0xF0) {
        length = 4, result = p[0] & 0x07, minimum = 0x10000;
    }

    b32 valid = length != 0 && length <= left;
    for (u32 i = 1; valid && i < length; ++i) {
        valid = (p[i] & 0xC0) == 0x80;
        result = (result << 6) | (p[i] & 0x3F);
    }
    valid = valid && result >= minimum && result <= 0x10FFFF && !IsBetween(result, 0xD800, 0xDFFF);

    *pos += valid ? length : 1;
    return valid ? result : 0xFFFD;
}

function Index StrSearchResult(String s, b32 found, u64 at, u64 matchLength, StrSearchFlags flags)
{
    Index result = { .exists = found, .value = found ? at : 0 };
    if (found && FlagExists(flags, SSF_IndexPlusOne)) {
        result.value = at + matchLength;
    }
    if (!found && FlagExists(flags, SSF_CountOnFail)) {
        result.value = s.length;
    }
    return result;
}

function Index StrIndexChar(String s, char c, StrSearchFlags flags)
{
    if (FlagExists(flags, SSF_SearchBackwards)) {
        for (u64 i = s.length; i > 0; --i) {
            if (s.value[i - 1] == c) {
                return StrSearchResult(s, true, i - 1, 1, flags);
            }
        }
    } else {
        for (u64 i = 0; i < s.length; ++i) {
            if (s.value[i] == c) {
                return StrSearchResult(s, true, i, 1, flags);
            }
        }
    }
    return StrSearchResult(s, false, 0, 1, flags);
}

function Index StrIndexStr(String s, String needle, StrSearchFlags flags)
{
    if (needle.length <= s.length) {
        u64 last = s.length - needle.length;
        if (FlagExists(flags, SSF_SearchBackwards)) {
            for (u64 i = last + 1; i > 0; --i) {
                if (MemCompare(s.value + (i - 1), needle.value, needle.length) == 0) {
                    return StrSearchResult(s, true, i - 1, needle.length, flags);
                }
            }
        } else {
            for (u64 i = 0; i <= last; ++i) {
                if (MemCompare(s.value + i, needle.value, needle.length) == 0) {
                    return StrSearchResult(s, true, i, needle.length, flags);
                }
            }
        }
    }
    return StrSearchResult(s, false, 0, needle.length, flags);
}

function ArrayStr StrSplit(Arena *arena, String s, char separator)
{
    ArrayStr result = {0};
    u64 start = 0;
    for (u64 i = 0; i <= s.length; ++i) {
        if (i == s.length || s.value[i] == separator) {
            if (i > start) {
                ArrayPush(arena, result, StrMake(s.value + start, i - start));
            }
            start = i + 1;
        }
    }
    return result;
}

function String StrFormat(Arena *arena, const char *format, ...)
{
    StrBuilder builder = {0};
    va_list args;
    va_start(args, format);
    StrBuildFormatV(arena, &builder, format, args);
    va_end(args);
    return builder.buffer;
}

// -----------------------------------------------------------------------------
// String builder
// -----------------------------------------------------------------------------

// Returns writable space for `extra` more bytes at the end of the buffer.
function char *StrBuilderReserve(Arena *arena, StrBuilder *builder, u64 extra)
{
    u64 needed = 0;
    AssertAlways(!ckd_add(&needed, builder->buffer.length, extra));

    if (needed > builder->capacity || builder->buffer.value == NULL) {
        u64 capacity = Max(Max(builder->capacity * 2, needed), (u64)64);
        char *data = _ArenaPush(arena, capacity, 1, 0);
        MemCopy(data, builder->buffer.value, builder->buffer.length);
        builder->buffer.value = data;
        builder->capacity = capacity;
    }
    return (char *)builder->buffer.value + builder->buffer.length;
}

function String StrBuildBytes(Arena *arena, StrBuilder *builder, const void *data, u64 length)
{
    char *dst = StrBuilderReserve(arena, builder, length);
    MemCopy(dst, data, length);
    builder->buffer.length += length;
    return StrMake(dst, length);
}

function String StrBuildStr(Arena *arena, StrBuilder *builder, String s)
{
    return StrBuildBytes(arena, builder, s.value, s.length);
}

function String StrBuildCStr(Arena *arena, StrBuilder *builder, const char *cstr)
{
    return StrBuildStr(arena, builder, StrFromCStr(cstr));
}

function String StrBuildChar(Arena *arena, StrBuilder *builder, char c)
{
    return StrBuildBytes(arena, builder, &c, 1);
}

// printf into the builder; the formatted text never includes the terminator.
function String StrBuildPrintfV(Arena *arena, StrBuilder *builder, const char *format, va_list args)
{
    va_list measure;
    va_copy(measure, args);
    i32 length = vsnprintf(NULL, 0, format, measure);
    va_end(measure);

    if (length <= 0) {
        return (String){0};
    }

    // +1 for the terminator vsnprintf always writes.
    char *dst = StrBuilderReserve(arena, builder, (u64)length + 1);
    vsnprintf(dst, (size_t)length + 1, format, args);
    builder->buffer.length += (u64)length;
    return StrMake(dst, (u64)length);
}

function String StrBuildPrintf(Arena *arena, StrBuilder *builder, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    String result = StrBuildPrintfV(arena, builder, format, args);
    va_end(args);
    return result;
}

function String StrBuildI32(Arena *arena, StrBuilder *builder, i32 value)
{
    return StrBuildPrintf(arena, builder, "%d", value);
}

function String StrBuildU32(Arena *arena, StrBuilder *builder, u32 value)
{
    return StrBuildPrintf(arena, builder, "%u", value);
}

function String StrBuildI64(Arena *arena, StrBuilder *builder, i64 value)
{
    return StrBuildPrintf(arena, builder, "%lld", (long long)value);
}

function String StrBuildU64(Arena *arena, StrBuilder *builder, u64 value)
{
    return StrBuildPrintf(arena, builder, "%llu", (unsigned long long)value);
}

function String StrBuildF32(Arena *arena, StrBuilder *builder, f32 value)
{
    return StrBuildPrintf(arena, builder, "%g", (f64)value);
}

function String StrBuildF64(Arena *arena, StrBuilder *builder, f64 value)
{
    return StrBuildPrintf(arena, builder, "%g", value);
}

// Walks the format string, handing each conversion to the C library except %S.
// `*` widths/precisions and %n are not supported.
function String StrBuildFormatV(Arena *arena, StrBuilder *builder, const char *format, va_list args)
{
    u64 start = builder->buffer.length;
    const char *at = format;

    while (*at != 0) {
        const char *percent = at;
        while (*percent != 0 && *percent != '%') {
            ++percent;
        }
        StrBuildBytes(arena, builder, at, (u64)(percent - at));
        if (*percent == 0) {
            break;
        }

        const char *spec = percent++;
        while (*percent != 0 && strchr("-+ #0", *percent) != NULL) {
            ++percent;
        }
        while (*percent >= '0' && *percent <= '9') {
            ++percent;
        }
        if (*percent == '.') {
            ++percent;
            while (*percent >= '0' && *percent <= '9') {
                ++percent;
            }
        }
        AssertAlways(*percent != '*');

        enum { Len_None, Len_Long, Len_LongLong, Len_Size, Len_Max, Len_Ptrdiff, Len_LongDouble } len = Len_None;
        if (percent[0] == 'h') {
            percent += percent[1] == 'h' ? 2 : 1;
        } else if (percent[0] == 'l') {
            len = percent[1] == 'l' ? Len_LongLong : Len_Long;
            percent += percent[1] == 'l' ? 2 : 1;
        } else if (percent[0] == 'z') {
            len = Len_Size, ++percent;
        } else if (percent[0] == 'j') {
            len = Len_Max, ++percent;
        } else if (percent[0] == 't') {
            len = Len_Ptrdiff, ++percent;
        } else if (percent[0] == 'L') {
            len = Len_LongDouble, ++percent;
        }

        char conversion = *percent;
        AssertAlways(conversion != 0);
        ++percent;

        char specBuffer[32] = {0};
        u64 specLength = (u64)(percent - spec);
        AssertAlways(specLength < sizeof(specBuffer));
        MemCopy(specBuffer, spec, specLength);

        switch (conversion) {
            case '%': {
                StrBuildChar(arena, builder, '%');
            } break;

            case 'S': {
                StrBuildStr(arena, builder, va_arg(args, String));
            } break;

            case 'd':
            case 'i': {
                switch (len) {
                    case Len_Long:     StrBuildPrintf(arena, builder, specBuffer, va_arg(args, long)); break;
                    case Len_LongLong: StrBuildPrintf(arena, builder, specBuffer, va_arg(args, long long)); break;
                    case Len_Size:     StrBuildPrintf(arena, builder, specBuffer, va_arg(args, ptrdiff_t)); break;
                    case Len_Max:      StrBuildPrintf(arena, builder, specBuffer, va_arg(args, intmax_t)); break;
                    case Len_Ptrdiff:  StrBuildPrintf(arena, builder, specBuffer, va_arg(args, ptrdiff_t)); break;
                    default:           StrBuildPrintf(arena, builder, specBuffer, va_arg(args, int)); break;
                }
            } break;

            case 'u':
            case 'x':
            case 'X':
            case 'o': {
                switch (len) {
                    case Len_Long:     StrBuildPrintf(arena, builder, specBuffer, va_arg(args, unsigned long)); break;
                    case Len_LongLong: StrBuildPrintf(arena, builder, specBuffer, va_arg(args, unsigned long long)); break;
                    case Len_Size:     StrBuildPrintf(arena, builder, specBuffer, va_arg(args, size_t)); break;
                    case Len_Max:      StrBuildPrintf(arena, builder, specBuffer, va_arg(args, uintmax_t)); break;
                    case Len_Ptrdiff:  StrBuildPrintf(arena, builder, specBuffer, va_arg(args, size_t)); break;
                    default:           StrBuildPrintf(arena, builder, specBuffer, va_arg(args, unsigned int)); break;
                }
            } break;

            case 'c': {
                StrBuildPrintf(arena, builder, specBuffer, va_arg(args, int));
            } break;

            case 'f': case 'F':
            case 'e': case 'E':
            case 'g': case 'G':
            case 'a': case 'A': {
                if (len == Len_LongDouble) {
                    StrBuildPrintf(arena, builder, specBuffer, va_arg(args, long double));
                } else {
                    StrBuildPrintf(arena, builder, specBuffer, va_arg(args, double));
                }
            } break;

            case 's': {
                StrBuildPrintf(arena, builder, specBuffer, va_arg(args, const char *));
            } break;

            case 'p': {
                StrBuildPrintf(arena, builder, specBuffer, va_arg(args, void *));
            } break;

            default: {
                InvalidPath;
            } break;
        }

        at = percent;
    }

    return StrFrom(builder->buffer, start);
}

function String StrBuildFormat(Arena *arena, StrBuilder *builder, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    String result = StrBuildFormatV(arena, builder, format, args);
    va_end(args);
    return result;
}

// -----------------------------------------------------------------------------
// Maps
// -----------------------------------------------------------------------------

enum {
    MapHash_Empty   = 0,
    MapHash_Deleted = 1,
};

function u64 MapHashBytes(const void *data, u64 size)
{
    // ponytail: FNV-1a, swap for a faster hash if profiling shows map lookups matter.
    const u8 *bytes = data;
    u64 hash = 14695981039346656037ULL;
    for (u64 i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ULL;
    }
    // Keep clear of the empty/deleted markers.
    return hash < 2 ? hash + 2 : hash;
}

function u64 MapHashKey(const void *key, u64 keySize, b32 isStr)
{
    if (isStr) {
        const String *s = key;
        return MapHashBytes(s->value, s->length);
    }
    return MapHashBytes(key, keySize);
}

function b32 MapKeyEqual(const void *a, const void *b, u64 keySize, b32 isStr)
{
    if (isStr) {
        return StrIsEqual(*(const String *)a, *(const String *)b);
    }
    return MemCompare(a, b, keySize) == 0;
}

function MapHashEntry *MapFindSlot(const MapHeader *header, const void *v, u64 kvSize, const void *key, u64 keySize, b32 isStr, u64 hash)
{
    if (header->tableSize == 0) {
        return NULL;
    }

    // The load factor keeps at least one empty slot, so probing terminates.
    u64 mask = header->tableSize - 1;
    for (u64 i = hash & mask;; i = (i + 1) & mask) {
        MapHashEntry *slot = &header->table[i];
        if (slot->hash == MapHash_Empty) {
            return NULL;
        }
        if (slot->hash == hash && MapKeyEqual((const u8 *)v + slot->index * kvSize, key, keySize, isStr)) {
            return slot;
        }
    }
}

function void MapInsertSlot(MapHeader *header, u64 hash, u64 index)
{
    u64 mask = header->tableSize - 1;
    for (u64 i = hash & mask;; i = (i + 1) & mask) {
        MapHashEntry *slot = &header->table[i];
        if (slot->hash == MapHash_Empty || slot->hash == MapHash_Deleted) {
            if (slot->hash == MapHash_Empty) {
                header->usedSlots += 1;
            }
            *slot = (MapHashEntry){ .hash = hash, .index = index };
            return;
        }
    }
}

// Rebuilds the table for at least `liveCount` entries, dropping tombstones.
// The old table stays dead inside the arena.
function void MapRehash(Arena *arena, MapHeader *header, const void *v, u64 kvSize, u64 keySize, b32 isStr, u64 liveCount)
{
    u64 tableSize = 16;
    while (liveCount * 2 > tableSize) {
        tableSize *= 2;
    }

    header->table = ArenaPushArray(arena, tableSize, MapHashEntry);
    header->tableSize = tableSize;
    header->usedSlots = 0;

    for (u64 i = 0; i < header->count; ++i) {
        const void *key = (const u8 *)v + i * kvSize;
        MapInsertSlot(header, MapHashKey(key, keySize, isStr), i);
    }
}

function void *_MapPut(Arena *arena, MapHeader *header, void *v, u64 kvSize, const void *key, u64 keySize, b32 isStr, u64 *outIndex)
{
    u64 hash = MapHashKey(key, keySize, isStr);

    MapHashEntry *slot = MapFindSlot(header, v, kvSize, key, keySize, isStr, hash);
    if (slot != NULL) {
        *outIndex = slot->index;
        return v;
    }

    // Keep live + deleted slots at or below 3/4 of the table.
    if ((header->usedSlots + 1) * 4 > header->tableSize * 3) {
        MapRehash(arena, header, v, kvSize, keySize, isStr, header->count + 1);
    }

    v = _ArrayGrow(arena, header->count, &header->capacity, v, kvSize, 1, true);

    u64 index = header->count++;
    MemCopy((u8 *)v + index * kvSize, key, keySize);
    MapInsertSlot(header, hash, index);

    *outIndex = index;
    return v;
}

function Index _MapIndex(const MapHeader *header, const void *v, u64 kvSize, const void *key, u64 keySize, b32 isStr)
{
    MapHashEntry *slot = MapFindSlot(header, v, kvSize, key, keySize, isStr, MapHashKey(key, keySize, isStr));
    return slot != NULL ? (Index){ .exists = true, .value = slot->index } : (Index){0};
}

function void *_MapValuePtr(const MapHeader *header, void *v, u64 kvSize, const void *key, u64 keySize, b32 isStr, u64 valueOffset)
{
    Index index = _MapIndex(header, v, kvSize, key, keySize, isStr);
    return index.exists && v != NULL ? (u8 *)v + index.value * kvSize + valueOffset : NULL;
}

global const u8 MapZeroValue[256];

function const void *_MapValueOrZero(const MapHeader *header, void *v, u64 kvSize, const void *key, u64 keySize, b32 isStr, u64 valueOffset, u64 valueSize)
{
    AssertAlways(valueSize <= sizeof(MapZeroValue));
    void *value = _MapValuePtr(header, v, kvSize, key, keySize, isStr, valueOffset);
    return value != NULL ? value : MapZeroValue;
}

function b32 _MapTryGet(const MapHeader *header, void *v, u64 kvSize, const void *key, u64 keySize, b32 isStr, u64 valueOffset, void *out, u64 outSize)
{
    void *value = _MapValuePtr(header, v, kvSize, key, keySize, isStr, valueOffset);
    if (value != NULL) {
        MemCopy(out, value, outSize);
    }
    return value != NULL;
}

function b32 _MapDel(MapHeader *header, void *v, u64 kvSize, const void *key, u64 keySize, b32 isStr)
{
    MapHashEntry *slot = MapFindSlot(header, v, kvSize, key, keySize, isStr, MapHashKey(key, keySize, isStr));
    if (slot == NULL) {
        return false;
    }

    u64 at = slot->index;
    u64 last = header->count - 1;
    slot->hash = MapHash_Deleted;

    // Swap-remove keeps the flat array dense.
    if (at != last) {
        u8 *lastEntry = (u8 *)v + last * kvSize;
        MapHashEntry *lastSlot = MapFindSlot(header, v, kvSize, lastEntry, keySize, isStr, MapHashKey(lastEntry, keySize, isStr));
        Assert(lastSlot != NULL);
        lastSlot->index = at;
        MemCopy((u8 *)v + at * kvSize, lastEntry, kvSize);
    }

    MemZero((u8 *)v + last * kvSize, kvSize);
    header->count = last;
    return true;
}

function void *_MapReserve(Arena *arena, MapHeader *header, void *v, u64 kvSize, u64 keySize, b32 isStr, u64 n)
{
    v = _ArrayReserve(arena, header->count, &header->capacity, v, kvSize, n);
    if (n * 4 > header->tableSize * 3) {
        MapRehash(arena, header, v, kvSize, keySize, isStr, n);
    }
    return v;
}

function void _MapClear(MapHeader *header)
{
    header->count = 0;
    header->usedSlots = 0;
    if (header->table != NULL) {
        MemZero(header->table, header->tableSize * sizeof(MapHashEntry));
    }
}
