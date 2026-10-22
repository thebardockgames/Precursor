#!/bin/bash
# Workaround for a Windows/MSYS2 quirk: devkitPro's own bundled make.exe strips
# TMP/TEMP/TMPDIR from the environment before spawning recipe subshells, which makes
# aarch64-none-elf-gcc.exe fall back to (unwritable) C:\WINDOWS\ for temp files and fail
# with "Cannot create temporary file in C:\WINDOWS\: Permission denied". Setting TMP/TEMP
# only works if done inside the recipe's own shell, not as an ambient env var to `make`
# itself - so we bake it into the compiler invocation via CMAKE_C_COMPILER instead.
export TMP=/tmp
export TEMP=/tmp
export DEVKITPRO=/opt/devkitpro
exec /c/devkitPro/devkitA64/bin/aarch64-none-elf-gcc.exe "$@"
