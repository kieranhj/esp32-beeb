#!/bin/sh
# Regression test for interrupts that arrive during a polling loop: the
# user VIA's T1 free-running with its interrupt enabled (IRQ1V pointed at a
# handler that clears it and counts in X; system VIA interrupts off), and a
# loop of LDA &FE6D : AND #2 : BEQ, so the IRQ lands on every phase of a
# taken branch and a stretched read. Jumped to at cycle 2,000,000 of a
# no-disc boot in both emulators and traced for 200 K cycles; run it for a
# few latch values, all must be identical.
#   sh tools/irqpoll.sh [trace exe] [T1 latch low byte, hex] [diff lines] [lda|ldy|ldiy|sta|inc]
cd /c/Users/khcon/OneDrive/Projects/esp32-beeb
EXE=${1:-host/trace.exe}
LO=${2:-0D}
AT=2000000
END=2200000
HEX=$(python tools/irqpoll_asm.py $LO ${4:-lda})
BYTES=$(echo $HEX | sed 's/../0x& /g')
(cd out/beebjit-win && ./beebjit.exe -master -mode interp -headless -fast -accurate -debug -opt debug:print-ticks \
    -commands "breakat $AT;c;writem 0x2000 $BYTES;e pc=0x2000;p;breakat $END;c;q") 2>/dev/null \
    | grep -v "ticks=0\]" > out/bj_irqp.txt
TRACE_FROM=$AT TRACE_JSR="$AT 2000 $HEX" $EXE roms/mos3.20 $END 2>/dev/null | awk -v e=$END '$NF<=e' > out/ours_irqp.txt
awk "/ITRP/{n++} n>1 || !/ITRP/" out/bj_irqp.txt > out/bj_irqpb.txt; tail -n +2 out/ours_irqp.txt > out/ours_irqpb.txt
python tools/tracediff.py out/bj_irqpb.txt out/ours_irqpb.txt | head -${3:-12}
