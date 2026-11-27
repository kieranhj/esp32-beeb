#!/usr/bin/env python3
"""Every disc in the board's bundle for a fixed time each: selects it
(console `boot`), captures, and prints one line per disc with the worst
5 s sample of core 1 (clocks per emulated cycle), the fewest frames in a
sample (250 is full speed) and the highest core 0 load. The logs go to
out/sweep_<disc>.txt. NVS (the volume among it) is not touched.
    python tools/boardsweep.py COM3 90 [disc ...]
"""
import os, re, subprocess, sys

port = sys.argv[1]
secs = sys.argv[2]
discs = sys.argv[3:] or sorted(d for d in os.listdir('discs')
                               if d.lower().endswith(('.ssd', '.dsd')) and 'nula' not in d.lower())
for d in discs:
    subprocess.run([sys.executable, 'tools/beeb.py', port, 'boot', d], capture_output=True, timeout=60)
    log = 'out/sweep_' + d.rsplit('.', 1)[0] + '.txt'
    with open(log, 'w', encoding='utf-8') as f:
        subprocess.run([sys.executable, 'tools/capture.py', secs, port], stdout=f, stderr=subprocess.STDOUT)
    text = open(log, encoding='utf-8', errors='replace').read()
    if ('disc 0: ' + d) not in text and (' ' + d + ' ') not in text:
        booted = re.search(r'disc 0: (\S+)', text)
        print(f'{d}: booted {booted.group(1) if booted else "nothing"}', flush=True)
        continue
    clk, frames, load, prev = [], [], [], None
    for m in re.finditer(r'STATS .*?([\d.]+) clk/cyc.*?frames (\d+).*?render (\d+)%', text):
        clk.append(float(m.group(1)))
        f = int(m.group(2))
        if prev is not None:
            frames.append(f - prev)
        prev = f
        load.append(int(m.group(3)))
    if not clk:
        print(f'{d}: no STATS', flush=True)
        continue
    worst = max(range(len(clk)), key=lambda i: clk[i])
    print(f'{d}: core 1 max {clk[worst]:.1f} (sample {worst}), frames min {min(frames) if frames else "-"}, '
          f'core 0 max {max(load)}%', flush=True)
