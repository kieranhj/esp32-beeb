#!/bin/sh
# Host build of the core and the Klaus Dormann tests, using zig cc (clang),
# which is on PATH on this machine. Run from the repo root in Git Bash:
#   sh host/build.sh
# Expected: 6502 functional test stops at 3448, 65C02 extended opcodes test
# stops at 23f0, both decimal tests stop at 024e with zp0b=00.
set -e
cd "$(dirname "$0")/.."
TESTS="../rg40xxv/b2-libretro/etc/6502_65C02_functional_tests"
zig cc -O2 -std=c11 -Wall -Wextra -o host/cputest.exe host/cputest.c main/cpu6502.c
host/cputest.exe "$TESTS/6502.bin"  0   0400 nmos trap | grep -E "pc=3448 " || { echo "FAIL: 6502 functional (nmos)"; exit 1; }
host/cputest.exe "$TESTS/6502.bin"  0   0400 cmos trap | grep -E "pc=3448 " || { echo "FAIL: 6502 functional (cmos)"; exit 1; }
host/cputest.exe "$TESTS/65c02.bin" 0   0400 cmos trap | grep -E "pc=23f0 " || { echo "FAIL: 65C02 extended opcodes"; exit 1; }
host/cputest.exe "$TESTS/d0.bin"    200 0200 nmos 024e | grep -E "zp0b=00" || { echo "FAIL: NMOS decimal"; exit 1; }
host/cputest.exe "$TESTS/d1.bin"    200 0200 cmos 024e | grep -E "zp0b=00" || { echo "FAIL: CMOS decimal"; exit 1; }
echo "all CPU tests pass"
