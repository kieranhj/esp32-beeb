#!/usr/bin/env python3
"""Every disc in discs/ (NuLA aside, out of scope) against beebjit at the
given cycles, through frametest.run_disc_test, 14 at a time. Prints one
line per frame; anything but 0 differing pixels wants a look.
    python tools/discsweep.py 15000000,30000000,60000000
"""
import os, sys, subprocess, concurrent.futures as cf
sys.path.insert(0, 'tools')
import frametest as ft
cycles = [int(c) for c in sys.argv[1].split(',')]
skip = ('nula',)
discs = sorted(d for d in os.listdir('discs') if d.lower().endswith(('.ssd', '.dsd')) and not any(k in d.lower() for k in skip))
jobs = [(d, c) for d in discs for c in cycles]
def run(job):
    d, c = job
    name = 'sw_' + d.rsplit('.', 1)[0] + '_' + str(c // 1000000) + 'M'
    r = subprocess.run([sys.executable, '-c', f'import sys; sys.path.insert(0,"tools"); import frametest as ft; ft.run_disc_test({name!r}, {d!r}, {c})'],
                       capture_output=True, text=True)
    return r.stdout.strip() or r.stderr.strip()[-200:]
with cf.ThreadPoolExecutor(max_workers=14) as ex:
    for out in ex.map(run, jobs):
        print(out, flush=True)
