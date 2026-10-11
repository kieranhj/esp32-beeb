#!/bin/sh
# Host build of the core and its test harnesses, using zig cc (clang) which is
# on PATH on this machine. Run from the repo root in Git Bash:
#   sh host/build.sh && host/klaus.exe <path>/6502.bin
set -e
cd "$(dirname "$0")/.."
TESTS="../rg40xxv/b2-libretro/etc/6502_65C02_functional_tests"
zig cc -O2 -std=c11 -Wall -Wextra -o host/klaus.exe host/klaus.c main/cpu6502.c
zig cc -O2 -std=c11 -Wall -Wextra -o host/decimal.exe host/decimal.c main/cpu6502.c
host/klaus.exe "$TESTS/6502.bin"
host/decimal.exe "$TESTS/d0.bin"
