#!/bin/sh
# Builds a native Windows beebjit from ../rg40xxv/beebjit with zig cc (clang),
# for use as the trace and frame oracle. Output: out/beebjit-win/beebjit.exe
# with the ROM set beside it. Run from the repo root in Git Bash:
#   sh tools/build_beebjit_win.sh
#   out/beebjit-win/beebjit.exe -master -mode interp -headless -fast -accurate \
#       -debug -print -commands "breakat 100000;c;q"
#
# The sources are copied to out/beebjit-src and patched before building:
#   - a debugger command "dumpframe <file>" that writes the render buffer
#     (two uint32 for width and height, then width*height ARGB words), for
#     tools/framediff.py; beebjit has no frame dump of its own.
#
# Why not build_win_opt.sh: that needs mingw gcc, which is not installed.
# zig cc assembles the x64 .S files fine but its assembler, unlike GNU as,
# turns a .globl of a label that is never defined into an undefined
# reference; beebjit has one such label, hence stub.c.
set -e
cd "$(dirname "$0")/.."
SRC=../rg40xxv/beebjit
OUT=out/beebjit-win
PATCHED=out/beebjit-src
mkdir -p "$OUT" "$PATCHED"
rm -rf "$PATCHED"/*
cp -r "$SRC"/*.c "$SRC"/*.h "$SRC"/asm "$PATCHED"/

python -I - "$PATCHED/debug.c" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, newline='').read()
anchor = '    } else if (!strcmp(p_command, "writem")) {'
assert anchor in s and 'dumpframe' not in s
add = '''    } else if (!strcmp(p_command, "dumpframe") && (p_param_1_str != NULL)) {
      struct render_struct* p_r = bbc_get_render(p_bbc);
      uint32_t hdr[2];
      struct util_file* p_f;
      hdr[0] = render_get_width(p_r);
      hdr[1] = render_get_height(p_r);
      p_f = util_file_open(p_param_1_str, 1, 1);
      util_file_write(p_f, hdr, sizeof(hdr));
      util_file_write(p_f, render_get_buffer(p_r), ((uint64_t) hdr[0] * hdr[1] * 4));
      util_file_close(p_f);
'''
s = s.replace(anchor, add + anchor)
open(p, 'w', newline='\n').write(s)
print("debug.c patched: dumpframe")
PYEOF

cat > "$OUT/stub.c" <<'STUB'
/* beebjit declares this label .globl in asm/x64/asm_inturbo_x64.S but never
 * defines it; GNU as lets that pass, clang's assembler does not. */
void asm_instruction_Bxx_interp_accurate_jb_patch(void) {}
STUB
cd "$PATCHED"
zig cc -target x86_64-windows-gnu -D__USE_MINGW_ANSI_STDIO=0 \
    -Wall -Wno-unknown-warning-option -Wno-address-of-packed-member \
    -O2 -DNDEBUG -o "$OLDPWD/$OUT/beebjit.exe" \
    main.c config.c bbc.c defs_6502.c state.c video.c via.c \
    emit_6502.c interp.c inturbo.c state_6502.c sound.c timing.c \
    jit_compiler.c jit_metadata.c cpu_driver.c \
    jit_optimizer.c jit_opcode.c keyboard.c \
    teletext.c render.c mc6850.c serial_ula.c \
    log.c test.c adc.c cmos.c joystick.c \
    tape.c tape_csw.c tape_uef.c \
    intel_fdc.c wd_fdc.c \
    disc_drive.c disc.c ibm_disc_format.c disc_tool.c \
    disc_fsd.c disc_hfe.c disc_ssd.c disc_adl.c \
    disc_rfi.c disc_kryo.c disc_scp.c disc_dfi.c \
    debug.c jit.c expression.c \
    util.c util_string.c util_container.c util_compress.c \
    os.c \
    asm/asm_abi.c asm/asm_tables.c asm/asm_util.c \
    asm/asm_common.c asm/asm_common.S \
    asm/asm_inturbo.c asm/asm_inturbo.S \
    asm/asm_jit.c asm/asm_jit.S \
    "$OLDPWD/$OUT/stub.c" \
    -lgdi32 -lwinmm 2>&1 | grep -v "warning:" || true
cd "$OLDPWD"
rm -rf "$OUT/roms" && cp -r "$SRC/roms" "$OUT/roms"
ls -la "$OUT/beebjit.exe"
