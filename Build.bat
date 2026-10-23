@echo off
setlocal

rem Usage: Build.bat [release] [analyze] [profile] [test] [install]

set MODE=debug
set ANALYZE=0
set TEST=0
set PROFILE=0
set INSTALL=0
for %%A in (%*) do (
    if /i "%%A"=="release" set MODE=release
    if /i "%%A"=="analyze" set ANALYZE=1
    if /i "%%A"=="test" set TEST=1
    if /i "%%A"=="profile" set PROFILE=1
    if /i "%%A"=="install" set INSTALL=1
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
set RC=llvm-rc
where llvm-rc >nul 2>nul || set "RC=%VCINSTALLDIR%Tools\Llvm\x64\bin\llvm-rc.exe"

set ROOT=%~dp0
set OUT=%ROOT%Bin\%MODE%
if not exist "%OUT%" mkdir "%OUT%"

set CFLAGS=-std=c23 -Wall -Wextra -Wpedantic -Wconversion -Wno-unused-parameter -Wno-unused-function -I"%ROOT%Source" -D_CRT_SECURE_NO_WARNINGS -g
rem The release build has no console window; debug keeps one for logs.
set APP_LINK=-luser32 -lgdi32 -lopengl32 -ldwrite -lshell32 -lole32 -ladvapi32 -lshlwapi -lruntimeobject -lshcore -ldwmapi -lversion -lwinmm -lnetapi32 -lmpr
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
rem FILEY_VERSION, such as 1.2.3 for a release, is the version the app and its executable's details show. Without it, git describes the
rem build: the last v tag and the commits since it, or 0.1.0 and the commit while there is no tag, and 0.1.0 alone outside a repository.
if not defined FILEY_VERSION for /f "delims=" %%v in ('git -C "%ROOT%." describe --tags --always --dirty 2^>nul') do set "FILEY_DESCRIBE=%%v"
if not defined FILEY_VERSION if defined FILEY_DESCRIBE if "%FILEY_DESCRIBE:~0,1%"=="v" set "FILEY_VERSION=%FILEY_DESCRIBE:~1%"
if not defined FILEY_VERSION if defined FILEY_DESCRIBE set "FILEY_VERSION=0.1.0+%FILEY_DESCRIBE%"
set RCFLAGS=
if defined FILEY_VERSION (
    set CFLAGS=%CFLAGS% -DFILEY_VERSION=\"%FILEY_VERSION%\"
    for /f "tokens=1-3 delims=.-+" %%a in ("%FILEY_VERSION%") do set "RCFLAGS=/D FILEY_VERSION_NUMBERS=%%a,%%b,%%c,0"
)
if defined FILEY_VERSION set RCFLAGS=%RCFLAGS% /D "FILEY_VERSION_TEXT=\"%FILEY_VERSION%\""

rem Generators run on every build and rewrite their outputs only when the content changes.
"%CLANG%" %CFLAGS% -fuse-ld=lld "%ROOT%Source\Tools\Codegen.c" -o "%OUT%\Codegen.exe" || exit /b 1
"%OUT%\Codegen.exe" "%ROOT%Source" || exit /b 1

if "%ANALYZE%"=="1" (
    "%CLANG%" %CFLAGS% --analyze -Xclang -analyzer-output=text "%ROOT%Source\Unity.c" -o nul || exit /b 1
)

"%CLANG%" -x c++ %CXXFLAGS% -c "%ROOT%Source\Win\WinFontRasterizer.cpp" -o "%OUT%\WinFontRasterizer.obj" || exit /b 1
rem The icon and version details are a resource linked into the executable.
"%RC%" /nologo %RCFLAGS% /fo "%OUT%\Filey.res" "%ROOT%Source\Resources\Filey.rc" || exit /b 1
"%CLANG%" %CFLAGS% -fuse-ld=lld "%ROOT%Source\Unity.c" "%OUT%\WinFontRasterizer.obj" "%OUT%\Filey.res" %APP_LINK% -o "%OUT%\Filey.exe" || exit /b 1

rem The tests keep several program states on the stack at once, more than Windows' default megabyte; Linux gives 8 MB.
if "%TEST%"=="1" (
    "%CLANG%" %CFLAGS% -fuse-ld=lld "%ROOT%Source\Tests\Tests.c" -luser32 -lgdi32 -lshell32 -lole32 -ladvapi32 -lshlwapi -lruntimeobject -lshcore -lversion -lwinmm -lnetapi32 -lmpr -Wl,/STACK:8388608 -o "%OUT%\FileyTests.exe" || exit /b 1
    "%OUT%\FileyTests.exe" || exit /b 1
)

rem install replaces the copy Filey Setup installed, when there is one; a running copy is moved aside, as Setup does, and goes once nothing runs it.
if "%INSTALL%"=="1" (
    if exist "%LOCALAPPDATA%\Filey\Filey.exe" (
        del "%LOCALAPPDATA%\Filey\Filey.old.exe" 2>nul
        move /y "%LOCALAPPDATA%\Filey\Filey.exe" "%LOCALAPPDATA%\Filey\Filey.old.exe" >nul || exit /b 1
        copy /y "%OUT%\Filey.exe" "%LOCALAPPDATA%\Filey\Filey.exe" >nul || exit /b 1
        echo Installed to %LOCALAPPDATA%\Filey
    ) else (
        echo Not installed: run Filey Setup first
    )
)
