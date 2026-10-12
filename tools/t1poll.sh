#!/bin/sh
# Regression test for VIA accesses that land on a timer event: a loop that
# runs the user VIA's T1 free-running and polls its IFR flag
# (LDA &FE6D : AND #&40 : BEQ), counting expiries in X, jumped to at cycle
# 2,000,000 of a no-disc boot in both emulators and traced for 200 K
# cycles. Run it for a few latch values; all must be identical.
#   sh tools/t1poll.sh [trace exe] [T1 latch low byte, hex] [diff lines]
# The NOP first: beebjit executes the opcode it had already fetched after
# "e pc=", so the first instruction at the new PC is skipped there.
cd /c/Users/khcon/OneDrive/Projects/esp32-beeb
EXE=${1:-host/trace.exe}
LO=${2:-0D}
AT=2000000
END=2200000
HEX="EA78A97F8D6EFEA9408D6BFEA9${LO}8D64FEA9008D65FEA200AD6DFE2940F0F9AD64FEE886704C1820"
BYTES=$(echo $HEX | sed 's/../0x& /g')
(cd out/beebjit-win && ./beebjit.exe -master -mode interp -headless -fast -accurate -debug -opt debug:print-ticks \
    -commands "breakat $AT;c;writem 0x2000 $BYTES;e pc=0x2000;p;breakat $END;c;q") 2>/dev/null \
    | grep -v "ticks=0\]" > out/bj_t1.txt
TRACE_FROM=$AT TRACE_JSR="$AT 2000 $HEX" $EXE roms/mos3.20 $END 2>/dev/null | awk -v e=$END '$NF<=e' > out/ours_t1.txt
awk "/ITRP/{n++} n>1 || !/ITRP/" out/bj_t1.txt > out/bj_t1b.txt; tail -n +2 out/ours_t1.txt > out/ours_t1b.txt
python tools/tracediff.py out/bj_t1b.txt out/ours_t1b.txt | head -${3:-12}
