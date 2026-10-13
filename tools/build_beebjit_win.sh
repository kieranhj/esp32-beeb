#!/bin/sh
# Builds a native Windows beebjit from ../rg40xxv/beebjit with zig cc (clang),
# for use as the trace and frame oracle. Output: out/beebjit-win/beebjit.exe
# with the ROM set beside it. Run from the repo root in Git Bash:
#   sh tools/build_beebjit_win.sh
#   out/beebjit-win/beebjit.exe -master -mode interp -headless -fast -accurate \
#       -debug -print -commands "breakat 100000;c;q"
#
# The sources are copied to out/beebjit-src and patched before building:
#   - a debugger command "dumpframe <file>" that advances the video to the
#     current cycle and writes the render buffer (two uint32 for width and
#     height, then width*height ARGB words), for tools/framediff.py; beebjit
#     has no frame dump of its own.
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
      /* The video catches up lazily; bring it to the current cycle so the
       * dump is exactly as of now. */
      video_advance_for_memory_sync(bbc_get_video(p_bbc));
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

# NMI edge logging for matching the 1770 model: BEEBJIT_LOG_NMI=1 prints the
# tick of every rising edge of the 1770's NMI line to stderr.
python -I - "$PATCHED/wd_fdc.c" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, newline='').read()
anchor = '  state_6502_set_irq_level(p_state_6502, k_state_6502_irq_nmi, new_level);'
assert anchor in s
add = '  if (new_level && !old_level && getenv("BEEBJIT_LOG_NMI")) {' + chr(10)
add += '    fprintf(stderr, "wdnmi %llu' + chr(92) + 'n",' + chr(10)
add += '            (unsigned long long) timing_get_total_timer_ticks(p_fdc->p_timing));' + chr(10)
add += '  }' + chr(10)
s = s.replace(anchor, add + anchor)
s = s.replace('#include <assert.h>', '#include <assert.h>' + chr(10) + '#include <stdio.h>' + chr(10) + '#include <stdlib.h>', 1)
open(p, 'w', newline=chr(10)).write(s)
print("wd_fdc.c patched: nmi log")
PYEOF

# Likewise BEEBJIT_LOG_DRIVE=1 prints every drive timer tick: tick count,
# track and head position before the controller sees the pulses.
python -I - "$PATCHED/disc_drive.c" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, newline='').read()
anchor = '  uint32_t pulse_position = p_drive->pulse_position;'
assert anchor in s
add = anchor + chr(10)
add += '  if (getenv("BEEBJIT_LOG_DRIVE")) {' + chr(10)
add += '    fprintf(stderr, "drv %llu %u %u %u' + chr(92) + 'n", (unsigned long long) timing_get_total_timer_ticks(p_drive->p_timing), track, head_position, pulse_position);' + chr(10)
add += '  }'
s = s.replace(anchor, add, 1)
s = s.replace('#include <assert.h>', '#include <assert.h>' + chr(10) + '#include <stdio.h>' + chr(10) + '#include <stdlib.h>', 1)
open(p, 'w', newline=chr(10)).write(s)
print("disc_drive.c patched: drive log")
PYEOF

# And BEEBJIT_LOG_FDC=1 prints every control register write: tick count,
# value, whether a drive is current and spinning, the motor status bit and
# the current drive's head position, before the write takes effect.
python -I - "$PATCHED/wd_fdc.c" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, newline='').read()
anchor = '  int is_motor_on = !!(p_fdc->status_register & k_wd_fdc_status_motor_on);'
assert anchor in s
add = anchor + chr(10)
add += '  if (getenv("BEEBJIT_LOG_FDC")) {' + chr(10)
add += '    fprintf(stderr, "ctrl %llu cpu %llu countdown %lld %02x cur %d spin %d motor %d head %u' + chr(92) + 'n",' + chr(10)
add += '            (unsigned long long) timing_get_total_timer_ticks(p_fdc->p_timing),' + chr(10)
add += '            (unsigned long long) state_6502_get_cycles(p_fdc->p_state_6502),' + chr(10)
add += '            (long long) timing_get_countdown(p_fdc->p_timing), val,' + chr(10)
add += '            p_current_drive != NULL, p_current_drive ? disc_drive_is_spinning(p_current_drive) : 0,' + chr(10)
add += '            is_motor_on, p_current_drive ? disc_drive_get_head_position(p_current_drive) : 0);' + chr(10)
add += '  }'
s = s.replace(anchor, add, 1)
open(p, 'w', newline=chr(10)).write(s)
print("wd_fdc.c patched: control log")
PYEOF

# BEEBJIT_LOG_FETCH=lo,hi prints every display fetch with its tick in that
# window: tick, horizontal, vertical and scanline counters, MA and the byte,
# for comparing with host/frame.exe's FRAME_LOG_FETCH.
python -I - "$PATCHED/video.c" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, newline='').read().replace(chr(13) + chr(10), chr(10))
anchor = """                                                      is_odd_tick);
      }

      render_render(p_render, p_video->data_byte, is_odd_tick);"""
assert anchor in s
add = """                                                      is_odd_tick);
        {
          static long long log_lo = -1, log_hi = -1;
          if (log_lo < 0) {
            const char* e = getenv("BEEBJIT_LOG_FETCH");
            log_lo = log_hi = 0;
            if (e) sscanf(e, "%lld,%lld", &log_lo, &log_hi);
          }
          if ((long long) ticks >= log_lo && (long long) ticks < log_hi) {
            fprintf(stderr, "F %llu h %u v %u s %u ma %04X d %02X@N@",
                    (unsigned long long) ticks, horiz_counter,
                    p_video->vert_counter, p_video->scanline_counter,
                    (p_video->address_counter - 1) & 0x3FFF,
                    p_video->data_byte);
          }
        }
      }

      render_render(p_render, p_video->data_byte, is_odd_tick);"""
add = add.replace("@N@", chr(92) + "n")
s = '#include <stdio.h>' + chr(10) + '#include <stdlib.h>' + chr(10) + s
s = s.replace(anchor, add, 1)
open(p, 'w', newline=chr(10)).write(s)
print("video.c patched: fetch log")
PYEOF

# BEEBJIT_MEMSYNC=1 keeps the video's catch-up after every storing
# instruction in -fast mode. beebjit installs it only when not fast, so a
# headless -fast run lets a display write show retroactively back to the
# last catch-up of any kind; real-time beebjit (what is seen) syncs.
python -I - "$PATCHED/bbc.c" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, newline='').read().replace(chr(13) + chr(10), chr(10))
anchor = """  if (!is_fast) {
    p_memory_written_callback = video_advance_for_memory_sync;
  }"""
assert anchor in s
s = "#include <stdlib.h>" + chr(10) + s.replace(anchor, """  if (!is_fast || getenv("BEEBJIT_MEMSYNC")) {
    p_memory_written_callback = video_advance_for_memory_sync;
  }""", 1)
open(p, 'w', newline=chr(10)).write(s)
print("bbc.c patched: memory sync in fast mode")
PYEOF

# BEEBJIT_LOG_VSYNC=1 prints every vsync edge: the CRTC tick it happened on
# and the system time when the video caught up to it (CA1 lands then).
python -I - "$PATCHED/video.c" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, newline='').read().replace(chr(13) + chr(10), chr(10))
anchor = """  p_video->in_vsync = is_new_vsync;
"""
assert anchor in s
add = anchor + """  if (getenv("BEEBJIT_LOG_VSYNC")) {
    fprintf(stderr, "V %llu now %llu level %d@N@", (unsigned long long) ticks,
            (unsigned long long) timing_get_total_timer_ticks(p_video->p_timing), is_new_vsync);
  }
"""
add = add.replace("@N@", chr(92) + "n")
s = s.replace(anchor, add, 1)
open(p, 'w', newline=chr(10)).write(s)
print("video.c patched: vsync log")
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
