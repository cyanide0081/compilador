@rem build script (gcc/clang + win32-msvc)
@echo off

set "CLANG=x86_64-w64-mingw32-clang"
where /Q %CLANG% && (set "CC=%CLANG%")

if "%CC%" equ "" (
	echo ERROR: unable to find mingw-w64-clang compiler installed
	echo ^(refer to https://github.com/mstorsjo/llvm-mingw/releases/^)
	exit /b
)

set "FLAGS=-std=c99 -Wall -Wextra -pedantic"
set "LFLAGS=-luser32 -lcomctl32 -lgdi32 -lcomdlg32 -luxtheme"
set "FLAGS=%FLAGS% -municode"
if "%~1" equ "debug" (
	set "MFLAGS=-g -gcodeview"
) else (
	set "MFLAGS=-DNDEBUG"
)

@echo on
%CC% -o compiler_win32.exe compiler_win32.c %FLAGS% %MFLAGS% %LFLAGS%
@echo off

set "run="
if "%~1" equ "run" (
	set "run=true"
) else if "%~2" equ "run" (
	set "run=true"
)
if defined run (
	call compiler_win32.exe
)
