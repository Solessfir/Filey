# Filey

A functional reimplementation of the reference file manager in C23. `AppSpecification.md` is the authoritative design document; read the relevant sections before changing architecture, memory, strings, containers, UI, or build code.

## Goals

- Match the reference's behavior, feel, and performance. Binary-identical output, original symbol names, and internal layout are not goals.
- Windows comes first, to validate the reverse-engineered behavior side by side with the original.
- Linux is the real target. Never take a Windows-only shortcut in shared code that would make the Linux port harder.
- macOS is out of scope. Do not add macOS code, stubs, or `#ifdef`s.

## Reverse-engineering reference

Spec Section 1.2 describes how to run headless Ghidra MCP against `reference.exe`. Use it to verify behavior when the spec is silent or ambiguous. Treat decompiled output as evidence to confirm, not as source to transcribe.

App screenshots in `%USERPROFILE%\Pictures\Screenshots\Reference\App\` are the visual reference for UI appearance and layout. Inspect the relevant screenshots before implementing or changing UI.

`Source/Tools/Compare/Compare.py` runs the reference and Filey one after the other on a fixture folder, in the middle of the main screen without taking the focus, posts the same input to both, and saves matching screenshots and animation frames, so UI work can be measured against the original without screenshots from the user. the reference gets `FakeInput.dll` (pointer, focus and held keys from shared memory), so the real pointer and keyboard are never used. Set `COMPARE_REFERENCE` to `reference.exe` and `COMPARE_REFERENCE_STATE` to its session and settings files (`%LOCALAPPDATA%\Reference\Reference-Session.json`, `%APPDATA%\Reference\Reference-Config.json` and `Reference-UserData.json`), which it backs up and restores around every run. the reference set to its built-in Fullmoon scheme matches Filey's `theme = fullmoon`. The display here is at 125%, so sizes measured in screenshots are divided by 1.25 before they go into the code as logical units.

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
- Use the base type aliases (`u8`..`u64`, `i32`, `f32`, `b32`, `wchar`, `rune`) instead of `int`, `float`, `unsigned`, `size_t`, `WCHAR`, etc., including in the C++ adapter. Internal helpers of a layer use a double underscore (`R__FontFindGlyph`, `GL__PushInstance`).
- Screenshots of the original source in `%USERPROFILE%\Pictures\Screenshots\Reference\` show its style (`result` return variables, `Prefix__Name` internals, `enum8(...)` fields, `_Count` sentinels). Use them as a loose reference, not a formatting rule.
- Do not edit generated files (`*.generated.h`, `Meta/*.meta.h`). Change the generator or its input instead.
- Commands and their default hotkeys are declared once in `Source/Program/Commands.txt`. Add a command there, then handle its `Prog_CommandKind_` case in `Prog_ProcessCommands`.
- The app icon is `Source/Resources/Filey.svg`. After changing it, run `python3 Source/Tools/IconBake.py` (needs `rsvg-convert`), which rewrites `Filey.ico` (linked into the Windows executable through `Filey.rc`) and the Linux theme sizes in `Resources/Icons/`; commit all of them. `Resources/filey-symbolic.svg` is the one-color version that panels recolor to their theme; keep its shapes in step with the master by hand.
- `./Build.sh install` installs a build for the current user (`~/.local/bin`, the desktop entry `Resources/filey.desktop` and the hicolor icons); `Packaging/Arch/PKGBUILD` builds the `filey-git` package.
- UI icons are a Lucide subset (ISC) embedded with `#embed`. To add one, append its Lucide name to `Source/Lib/Lucide/Icons.txt` and run `python Source/Tools/LucideSubset.py <extracted lucide-static npm package>` (needs `fonttools`). It rewrites `Lucide.ttf`, its `LICENSE` and `Meta/Icons.meta.h` (`Icon_<Name>` strings drawn with the icon font); commit all of them.

## Toolchain

- Clang with `-std=c23` on every platform. MSVC `cl` is not supported; on Windows `Build.bat` uses the Clang bundled with VS Build Tools plus the MSVC/SDK environment from `vcvars64.bat`, linking with lld. The DirectWrite rasterizer `Win/WinFontRasterizer.cpp` is the only C++ file; it is compiled separately with `-fno-exceptions -fno-rtti` and linked in through the C header `Win/WinFontRasterizer.h`.
- GCC is a secondary Linux compiler for catching portability issues (`CC=gcc ./Build.sh`).
- Static analysis uses `clang --analyze` (the `analyze` build flag) and `clang-tidy`. Do not use Cppcheck; it has no C23 support.
- Linux debug runs SHOULD use the `asan` flag (ASan + UBSan).
- Linux app dependencies: `wayland` (client, cursor, egl), `libxkbcommon`, EGL (`mesa`/`libglvnd`), `freetype2`, and `fontconfig`, found through `pkg-config`. The xdg-shell, fractional-scale and viewporter protocol code is generated and committed under `Source/Linux/Protocols/`, so `wayland-protocols` is needed only to regenerate it:
  `wayland-scanner client-header|private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml Source/Linux/Protocols/XdgShell.generated.{h,c}`, and likewise `staging/fractional-scale/fractional-scale-v1.xml` to `FractionalScale.generated.{h,c}` and `stable/viewporter/viewporter.xml` to `Viewporter.generated.{h,c}`.
- The `profile` flag defines `PROFILE`, which turns `ProfileScope("Name") { ... }` (`Platform/Profile.h`) into timed scopes on any thread. On exit the app writes `Filey.trace.json` to the working directory; open it in https://ui.perfetto.dev. Without the flag the macros compile to nothing.
- `EXTRA_CFLAGS` adds ad-hoc defines to either build script. `-DLOG_EVENTS` prints every platform event to stderr.
- Bookmarks and recents are saved as `Bookmarks.txt` and `Recents.txt` in `%LOCALAPPDATA%\Filey` or `~/.config/filey`, and the panels and tabs as `Session.txt` on exit, which a start without folder arguments restores. The drive index Go To searches is written there as `Index.bin` while a walk of the drives runs (`Index.bin.walk`, moved over it once the walk is done), and the next start searches it until its own walk is done. `Settings.txt` there (the "Settings" command writes a commented template on first use) holds `scale`, `animations = off`, `theme = fullmoon` (the reference's built-in scheme, for side-by-side checks), `color.<theme field>`, `glass.window`, `glass.popup`, `glass.blur` and `glass.backdrop` (the transparent theme's tints, how far its floating boxes blur what is under them, and what Windows draws behind the window) and `hotkey.<Command>` lines and applies on save. Manual test runs SHOULD set `FILEY_CONFIG_DIR` to a temp folder so that they leave the user's settings alone; the headless tests never load settings.
- `FILEY_DEBUG_THEME=1` starts the app with the debug theme (the "Debug Colors" palette command toggles it), which gives every theme color its own loud hue so that screenshots show which surface draws where.
- `FILEY_CAPTURE_DIR=<folder>` writes every presented frame there as `frame.bmp`, or with `FILEY_CAPTURE_SEQUENCE=1` as `frame-<n>-<microseconds>.bmp`, so that screenshots and animation timings can be taken with the window off-screen.
- WSLg provides a Wayland compositor, so the Linux window path can be run from WSL (software GL there).
- Both build scripts first compile and run `Source/Tools/Codegen.c`, which regenerates `Source/Meta/*.meta.h` (committed) and fails the build on malformed input.
- Output goes to `Bin/<debug|release>/`. The unity root is `Source/Unity.c`; base-layer tests are a second unity root, `Source/Tests/Tests.c`, built and run by the `test` flag.

```text
Build.bat [release] [analyze] [profile] [test]          Windows
./Build.sh [release] [analyze] [asan] [profile] [test]  Linux / WSL
```

## Source layout

```text
Source/
├─ Base/       Base.h + Base.c: types, asserts, arenas, pools, lists, strings, arrays, maps
├─ Platform/   Platform.h + PlatformEvents.c: window/input contract; Jobs.h + PlatformJobs.c: worker queue; Files.h: filesystem contract; Profile.h + PlatformProfile.c: scope profiler
├─ Win/        WinPlatform.c (memory, time), WinWindow.c (Win32 window, input, WGL, clipboard), WinFont.c + WinFontRasterizer.cpp (DirectWrite glyphs), WinThreads.c + WinFiles.c (listing, watching, file operations through the shell), WinIcons.c (shell image-list file icons, type names), WinShell.c (shell context menus, drag and drop), WinLaunch.c (opening files; WinRT launcher with neighboring files for packaged apps)
├─ Linux/      LinuxPlatform.c (memory, time), LinuxWindow.c (Wayland window, input, EGL, clipboard, drag and drop), LinuxFont.c (fontconfig + FreeType glyphs), LinuxThreads.c + LinuxFiles.c (listing, watching, launching, file operations and freedesktop trash), LinuxIcons.c (freedesktop icon theme file icons), LinuxApps.c (desktop entries and mimeapps.list for Open With, Open in Terminal, Filey's own context menu)
├─ Renderer/   Renderer.h contract; OpenGLRenderer.c backend; Font.c atlas packing and text
├─ UI/         UI.h + UI.c: keyed blocks, layout, input and rendering
├─ Program/    Program.h + Directory.c: browser state and background results; Commands.txt + Commands.c: command table and deferred actions; Icons.c: file icon cache and atlas; Program.c: UI
├─ Meta/       generated tables (Commands.meta.h) and icon codepoints (Icons.meta.h)
├─ Lib/        third-party code and assets: Lucide icon font subset, stb_image (PNG), nanosvg (SVG, Linux icons)
├─ Resources/  app icon (Filey.svg master, generated Filey.ico and Icons/ sizes), Filey.rc for the Windows executable, filey.desktop
├─ Tools/      Codegen.c: build-time table generator; LucideSubset.py: icon font subsetter; IconBake.py: app icon sizes; Compare/: side-by-side screenshots against the reference
├─ Tests/      assert-based base, UI, filesystem, jobs, browser and codegen tests (native platform backends, no window code)
└─ Unity.c     application unity root
```

- `function` and `global` expand to `static`. OS headers that use either as an identifier must be included between `#pragma push_macro("name")` / `#undef name` and `#pragma pop_macro("name")`, as `Win/WinPlatform.c` does for `windows.h` (`function`) and `Linux/LinuxWindow.c` does for Wayland (`global`).
- `u64` is `unsigned long` on Linux and `unsigned long long` on Windows. Write 64-bit constants with `UINT64_C(...)`, not a `ULL` suffix, so `_Generic` helpers such as `Min`/`Max` resolve on both platforms.
- Job descriptors are managed on the main thread. Submitting transfers the job arena to the worker; taking completion transfers it back. Adopt result arenas or copy results before releasing a job. Publish only the latest directory generation at a frame boundary.

## Validation

- After logic changes, build with the cheapest useful configuration (`Build.bat` or `./Build.sh`).
- Base-layer changes (arenas, strings, arrays, maps, lists) need their tests in `Source/Tests/Tests.c` updated or extended, then a `test` run on both Windows and Linux (`./Build.sh asan test` in WSL).
- Do not reference spec sections in code comments.

## Workflow

- Commit each finished, validated step right away without asking. If follow-up fixes are requested for that work, amend the commit while it is unpushed.
