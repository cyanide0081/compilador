@rem build script (gcc/clang + win32-msvc)
@echo off

where /Q gcc && (set "CC=gcc")
where /Q clang && (set "CC=clang")

if "%CC%" equ "" (
	echo ERROR: unable to fing gcc or clang installed
	exit /b
)

set "FLAGS=-std=c99 -Wall -Wextra -pedantic"
set "LFLAGS=-luser32 -lcomctl32 -lgdi32 -lcomdlg32 -luxtheme"
if "%CC%" equ "clang" (
	set "DFLAGS=-g -gcodeview"
) else (
	set "FLAGS=%FLAGS% -municode"
	set "DFLAGS=-g"
)
if "%~1" equ "debug" (
	set "MFLAGS=%DFLAGS%"
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
	start compiler_win32.exe
)
