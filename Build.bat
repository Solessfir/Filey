@echo off
setlocal

rem Usage: Build.bat [release] [analyze] [test]

set MODE=debug
set ANALYZE=0
set TEST=0
for %%A in (%*) do (
    if /i "%%A"=="release" set MODE=release
    if /i "%%A"=="analyze" set ANALYZE=1
    if /i "%%A"=="test" set TEST=1
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
if "%MODE%"=="release" (
    set CFLAGS=%CFLAGS% -O2 -DNDEBUG
) else (
    set CFLAGS=%CFLAGS% -O0 -DDEBUG
)

if "%ANALYZE%"=="1" (
    "%CLANG%" %CFLAGS% --analyze -Xclang -analyzer-output=text "%ROOT%Source\Unity.c" -o nul || exit /b 1
)

"%CLANG%" %CFLAGS% -fuse-ld=lld "%ROOT%Source\Unity.c" -o "%OUT%\Filey.exe" || exit /b 1

if "%TEST%"=="1" (
    "%CLANG%" %CFLAGS% -fuse-ld=lld "%ROOT%Source\Tests\Tests.c" -o "%OUT%\FileyTests.exe" || exit /b 1
    "%OUT%\FileyTests.exe" || exit /b 1
)
