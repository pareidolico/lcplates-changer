#!/bin/sh
# Builds PlateChanger.asi (32-bit Windows DLL) with zig, from Linux, macOS or Windows.
#   pip install ziglang
#   ./build.sh
set -e
ZIG="python3 -m ziglang"
mkdir -p build
$ZIG cc -target x86-windows-gnu -O2 -fno-stack-protector -fno-builtin -Wall -Wextra -Wno-unused-parameter \
    -c src/platechanger.c -o build/platechanger.o
# no C runtime: the plugin only depends on kernel32, user32 and gdi32
$ZIG cc -target x86-windows-gnu -shared -nostdlib -o build/PlateChanger.asi build/platechanger.o \
    -lkernel32 -luser32 -lgdi32 -Wl,--entry=DllMain
echo "built build/PlateChanger.asi"
