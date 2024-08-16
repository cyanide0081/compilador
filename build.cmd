@rem build script (gcc/clang + win32-msvc)
@echo off

where /Q gcc && (set "CC=gcc")
where /Q clang && (set "CC=clang")

%CC% -o compiler_win32.exe compiler_win32.c -std=c99 -Wall -Wextra -pedantic -luser32 -lcomctl32 -lgdi32 -lcomdlg32 -g -gcodeview
