#!/bin/bash
# See aarch64-none-elf-gcc-wrapped.sh for why this exists.
export TMP=/tmp
export TEMP=/tmp
export DEVKITPRO=/opt/devkitpro
exec /c/devkitPro/devkitA64/bin/aarch64-none-elf-g++.exe "$@"
