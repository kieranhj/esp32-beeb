#!/usr/bin/env python3
"""Assembles the tools/irqpoll.sh test: user VIA T1 free running with its
interrupt on, IRQ1V pointed at a handler that clears it (LDA &FE64) and
counts in X, system VIA interrupts off, and a loop that touches the user
VIA in one addressing mode, so the IRQ lands on every phase of it.
    irqpoll_asm.py <T1 latch low, hex> [lda|ldy|ldiy|sta|inc]"""
import sys
lo = int(sys.argv[1], 16)
mode = sys.argv[2] if len(sys.argv) > 2 else 'lda'
code = []
def b(*x): code.extend(x)
org = 0x2000
b(0xEA, 0x78)                       # NOP (beebjit skips one), SEI
b(0xA9, 0x7F, 0x8D, 0x4E, 0xFE)     # system VIA: no interrupts
fix_lo = len(code) + 1; b(0xA9, 0x00, 0x8D, 0x04, 0x02)   # IRQ1V low
fix_hi = len(code) + 1; b(0xA9, 0x00, 0x8D, 0x05, 0x02)   # IRQ1V high
b(0xA9, 0x6D, 0x85, 0x70, 0xA9, 0xFE, 0x85, 0x71)         # &70 -> &FE6D
b(0xA9, 0x40, 0x8D, 0x6B, 0xFE)     # user VIA ACR: T1 free running
b(0xA9, 0xC0, 0x8D, 0x6E, 0xFE)     # IER: T1
b(0xA9, lo, 0x8D, 0x64, 0xFE)       # T1 latch low
b(0xA9, 0x00, 0x8D, 0x65, 0xFE)     # T1 high: start
b(0xA2, 0x00, 0xA0, 0x00, 0x58)     # LDX #0, LDY #0, CLI
loop = org + len(code)
body = {
    'lda': [0xAD, 0x6D, 0xFE],          # LDA &FE6D
    'ldy': [0xB9, 0x6D, 0xFE],          # LDA &FE6D,Y
    'ldiy': [0xB1, 0x70],               # LDA (&70),Y
    'sta': [0x8D, 0x6F, 0xFE],          # STA &FE6F (ORA, no handshake)
    'inc': [0xEE, 0x6F, 0xFE],          # INC &FE6F
}[mode]
b(*body)
b(0x29, 0x02)                           # AND #2 (Z set: the VIA's CA1 is idle)
b(0xF0, (loop - (org + len(code) + 2)) & 0xFF)   # BEQ loop
b(0x4C, loop & 0xFF, loop >> 8)
handler = org + len(code)
b(0xAD, 0x64, 0xFE, 0xE8, 0xA5, 0xFC, 0x40)      # LDA &FE64 (clears T1), INX, LDA &FC, RTI
code[fix_lo] = handler & 0xFF; code[fix_hi] = handler >> 8
print(''.join('%02X' % x for x in code))
