# Filey

A functional reimplementation of the reference file manager in C23. `AppSpecification.md` is the authoritative design document; read the relevant sections before changing architecture, memory, strings, containers, UI, or build code.

## Goals

- Match the reference's behavior, feel, and performance. Binary-identical output, original symbol names, and internal layout are not goals.
- Windows comes first, to validate the reverse-engineered behavior side by side with the original.
- Linux is the real target. Never take a Windows-only shortcut in shared code that would make the Linux port harder.
- macOS is out of scope. Do not add macOS code, stubs, or `#ifdef`s.

## Reverse-engineering reference

Spec Section 1.2 describes how to run headless Ghidra MCP against `reference.exe`. Use it to verify behavior when the spec is silent or ambiguous. Treat decompiled output as evidence to confirm, not as source to transcribe.

## Architecture rules

- Layers are Base, Platform, Renderer, UI, and Program. Dependencies only point downward.
- OS APIs (Win32, COM, POSIX, X11/Wayland) live only in the platform layer. The renderer's graphics API lives only in its backend files.
- Memory is arena/lifetime based (permanent, frame, scratch, job, feature). Do not add per-object `malloc`/`free`.
- `{0}` must be a valid state for core types, including for the first mutation.
- Strings are UTF-8 `String { const char *value; u64 length; }` views. Convert to UTF-16 or null-terminated strings only at the platform boundary.
- UI is immediate-mode with keyed retained blocks. UI code only queues commands; never block or do I/O while building UI.
- Slow work (enumeration, file operations, thumbnails, indexing) runs in background jobs that get copied input and their own arena, with cooperative cancellation.
- Unity build through a direct build script. Do not introduce CMake, Meson, or similar for the developer build.
- Third-party code must be small, single-purpose, auditable, and kept under `Source/Lib/`.

## Code style

- C23 only. C++ is allowed only for a tiny, isolated platform adapter that has no reasonable C alternative (spec Section 54.1).
- `PascalCase` for types, functions, and macros. Layer prefixes are `UI_`, `R_`, `P_`, and `Prog_`. Use `camelCase` for fields, locals, and parameters.
- Enum values are `Type_Value` (e.g. `FileKind_Directory`), and dense enums end with a `_Count` sentinel.
- Use `function`, `global`, and `local_persist` instead of bare `static`.
- Use the base-layer vocabulary (`u64`, `b32`, `S("...")`, `ArenaPushStruct`, `ArrayPush`, `QueuePush`, etc.) instead of raw runtime calls.
- Prefer standard C23 (`__VA_OPT__`, `static_assert`, `alignof`) over compiler extensions.
- Do not edit generated files (`*.generated.h`, `Meta/*.meta.h`). Change the generator or its input instead.

## Toolchain

- Clang with `-std=c23` on every platform. MSVC `cl` is not supported; on Windows `build.bat` uses the Clang bundled with VS Build Tools plus the MSVC/SDK environment from `vcvars64.bat`, linking with lld.
- GCC is a secondary Linux compiler for catching portability issues (`CC=gcc ./build.sh`).
- Static analysis uses `clang --analyze` (the `analyze` build flag) and `clang-tidy`. Do not use Cppcheck; it has no C23 support.
- Linux debug runs SHOULD use the `asan` flag (ASan + UBSan).
- Output goes to `Bin/<debug|release>/`. The unity root is `Source/Unity.c`.

```text
build.bat [release] [analyze]          Windows
./build.sh [release] [analyze] [asan]  Linux / WSL
```

## Validation

- After logic changes, build with the cheapest useful configuration (`build.bat` or `./build.sh`).
- Base-layer changes (arenas, strings, arrays, maps, lists) need their assert-based tests updated or extended (spec Section 30).
