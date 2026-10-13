@echo off
setlocal

rem Usage: Build.bat [release] [analyze] [profile] [test]

set MODE=debug
set ANALYZE=0
set TEST=0
set PROFILE=0
for %%A in (%*) do (
    if /i "%%A"=="release" set MODE=release
    if /i "%%A"=="analyze" set ANALYZE=1
    if /i "%%A"=="test" set TEST=1
    if /i "%%A"=="profile" set PROFILE=1
)

rem Load the MSVC/Windows SDK environment once per shell.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if defined VCINSTALLDIR goto :env_ready
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSROOT=%%I"
rem vcvars itself looks for vswhere on PATH.
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:env_ready

set CLANG=clang
where clang >nul 2>nul || set "CLANG=%VCINSTALLDIR%Tools\Llvm\x64\bin\clang.exe"

set ROOT=%~dp0
set OUT=%ROOT%Bin\%MODE%
if not exist "%OUT%" mkdir "%OUT%"

set CFLAGS=-std=c23 -Wall -Wextra -Wpedantic -Wconversion -Wno-unused-parameter -Wno-unused-function -I"%ROOT%Source" -D_CRT_SECURE_NO_WARNINGS -g
rem The release build has no console window; debug keeps one for logs.
set APP_LINK=-luser32 -lgdi32 -lopengl32 -ldwrite -lshell32 -lole32
rem The DirectWrite adapter is the only C++ and uses no C++ runtime features.
set CXXFLAGS=-std=c++20 -Wall -Wextra -fno-exceptions -fno-rtti -I"%ROOT%Source" -g
if "%MODE%"=="release" (
    set CFLAGS=%CFLAGS% -O2 -DNDEBUG
    set CXXFLAGS=%CXXFLAGS% -O2 -DNDEBUG
    set APP_LINK=%APP_LINK% -Wl,/SUBSYSTEM:WINDOWS -Wl,/ENTRY:mainCRTStartup
) else (
    set CFLAGS=%CFLAGS% -O0 -DDEBUG
    set CXXFLAGS=%CXXFLAGS% -O0 -DDEBUG
)
if "%PROFILE%"=="1" set CFLAGS=%CFLAGS% -DPROFILE
rem EXTRA_CFLAGS passes ad-hoc defines such as -DLOG_EVENTS.
set CFLAGS=%CFLAGS% %EXTRA_CFLAGS%

rem Generators run on every build and rewrite their outputs only when the content changes.
"%CLANG%" %CFLAGS% -fuse-ld=lld "%ROOT%Source\Tools\Codegen.c" -o "%OUT%\Codegen.exe" || exit /b 1
"%OUT%\Codegen.exe" "%ROOT%Source" || exit /b 1

if "%ANALYZE%"=="1" (
    "%CLANG%" %CFLAGS% --analyze -Xclang -analyzer-output=text "%ROOT%Source\Unity.c" -o nul || exit /b 1
)

"%CLANG%" -x c++ %CXXFLAGS% -c "%ROOT%Source\Win\WinFontRasterizer.cpp" -o "%OUT%\WinFontRasterizer.obj" || exit /b 1
"%CLANG%" %CFLAGS% -fuse-ld=lld "%ROOT%Source\Unity.c" "%OUT%\WinFontRasterizer.obj" %APP_LINK% -o "%OUT%\Filey.exe" || exit /b 1

if "%TEST%"=="1" (
    "%CLANG%" %CFLAGS% -fuse-ld=lld "%ROOT%Source\Tests\Tests.c" -luser32 -lgdi32 -lshell32 -lole32 -o "%OUT%\FileyTests.exe" || exit /b 1
    "%OUT%\FileyTests.exe" || exit /b 1
)
