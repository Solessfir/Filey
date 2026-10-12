#pragma once

#include "Jobs.h"

typedef enum P_FileKind {
    P_FileKind_Directory,
    P_FileKind_File,
    P_FileKind_Other,
    P_FileKind_Count,
} P_FileKind;

typedef struct P_File {
    String name;
    u64 size;
    i64 modifiedTimeUnix; // Seconds since the Unix epoch.
    enum8(P_FileKind) kind;
    b8 hidden;
    b8 metadataValid;
    b8 symbolicLink;
} P_File;

typedef struct P_FileArray {
    ArrayHeader header;
    P_File *v;
} P_FileArray;

typedef struct P_DirectoryResult {
    P_FileArray files;
    u32 errorCode;
    String errorString;
    b32 cancelled;
} P_DirectoryResult;

typedef struct P_FileError {
    u32 code;
    String message;
} P_FileError;

// Results belong to arena. Cancellation and directory errors discard partial listings.
function P_DirectoryResult P_EnumerateDirectory(Arena *arena, String path, const P_CancelToken *cancel);
// Lexically normalized absolute path with '/' separators; empty means failure.
function String P_AbsolutePath(Arena *arena, String path);
function String P_FormatFileTime(Arena *arena, i64 modifiedTimeUnix);
// May block while the system launches its default application. Run in a job.
function P_FileError P_OpenPath(Arena *arena, String path);
