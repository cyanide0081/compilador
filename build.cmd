@rem build script (llvm-mingw)
@echo off

set "CLANG=x86_64-w64-mingw32-clang"
where /q %CLANG% && (set "CC=%CLANG%")

if "%CC%" == "" (
	echo ERROR: unable to find mingw-w64-clang compiler installed
	echo ^(refer to https://github.com/mstorsjo/llvm-mingw/releases/latest^)
	exit /b
)

set "FLAGS=-std=c99 -Wall -Wextra -pedantic"
set "LFLAGS=-luser32 -lcomctl32 -lgdi32 -lcomdlg32 -luxtheme"
set "FLAGS=%FLAGS% -municode -Wl,--subsystem,windows"
if "%~1" equ "debug" (
	set "MFLAGS=-g -gcodeview"
) else (
	set "MFLAGS=-DNDEBUG -O2"
)

@echo on
%CC% -o compiler_win32.exe compiler_win32.c %FLAGS% %MFLAGS% %LFLAGS%
@echo off

if %errorlevel% neq 0 exit /b %errorlevel%

set "run="
if "%~1" equ "run" (
	set "run=true"
) else if "%~2" equ "run" (
	set "run=true"
)
if defined run (
	call compiler_win32.exe
)
