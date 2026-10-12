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

- Clang with `-std=c23` on every platform. MSVC `cl` is not supported; on Windows `Build.bat` uses the Clang bundled with VS Build Tools plus the MSVC/SDK environment from `vcvars64.bat`, linking with lld.
- GCC is a secondary Linux compiler for catching portability issues (`CC=gcc ./Build.sh`).
- Static analysis uses `clang --analyze` (the `analyze` build flag) and `clang-tidy`. Do not use Cppcheck; it has no C23 support.
- Linux debug runs SHOULD use the `asan` flag (ASan + UBSan).
- Linux app dependencies: `wayland` (client, cursor, egl), `libxkbcommon`, and EGL (`mesa`/`libglvnd`), found through `pkg-config`. The xdg-shell protocol code is generated and committed under `Source/Linux/Protocols/`, so `wayland-protocols` is needed only to regenerate it:
  `wayland-scanner client-header|private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml Source/Linux/Protocols/XdgShell.generated.{h,c}`
- `EXTRA_CFLAGS` adds ad-hoc defines to either build script. `-DLOG_EVENTS` prints every platform event to stderr.
- WSLg provides a Wayland compositor, so the Linux window path can be run from WSL (software GL there).
- Output goes to `Bin/<debug|release>/`. The unity root is `Source/Unity.c`; base-layer tests are a second unity root, `Source/Tests/Tests.c`, built and run by the `test` flag.

```text
Build.bat [release] [analyze] [test]          Windows
./Build.sh [release] [analyze] [asan] [test]  Linux / WSL
```

## Source layout

```text
Source/
├─ Base/       Base.h + Base.c: types, asserts, arenas, pools, lists, strings, arrays, maps
├─ Platform/   Platform.h: the platform contract; PlatformEvents.c: shared event queue
├─ Win/        WinPlatform.c (memory, time) and WinWindow.c (Win32 window, input, WGL)
├─ Linux/      LinuxPlatform.c (memory, time) and LinuxWindow.c (Wayland window, input, EGL)
├─ Renderer/   Renderer.h contract; OpenGLRenderer.c backend
├─ Tests/      assert-based base-layer tests (link only *Platform.c, no window code)
└─ Unity.c     application unity root
```

- `function` and `global` expand to `static`. OS headers that use either as an identifier must be included between `#pragma push_macro("name")` / `#undef name` and `#pragma pop_macro("name")`, as `Win/WinPlatform.c` does for `windows.h` (`function`) and `Linux/LinuxWindow.c` does for Wayland (`global`).
- `u64` is `unsigned long` on Linux and `unsigned long long` on Windows. Write 64-bit constants with `UINT64_C(...)`, not a `ULL` suffix, so `_Generic` helpers such as `Min`/`Max` resolve on both platforms.

## Validation

- After logic changes, build with the cheapest useful configuration (`Build.bat` or `./Build.sh`).
- Base-layer changes (arenas, strings, arrays, maps, lists) need their tests in `Source/Tests/Tests.c` updated or extended, then a `test` run on both Windows and Linux (`./Build.sh asan test` in WSL).
- Do not reference spec sections in code comments.

## Workflow

- Commit each finished, validated step right away without asking. If follow-up fixes are requested for that work, amend the commit while it is unpushed.
