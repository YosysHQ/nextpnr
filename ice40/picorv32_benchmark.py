#!/usr/bin/env python3
import os, sys, threading
from os import path
import subprocess
import re

num_runs = 8

if not path.exists("picorv32.json"):
    subprocess.run(["wget", "https://raw.githubusercontent.com/cliffordwolf/picorv32/master/picorv32.v"], check=True)
    subprocess.run(["yosys", "-q", "-p", "synth_ice40 -json picorv32.json -top top", "picorv32.v", "picorv32_top.v"], check=True)

fmax = {}

if not path.exists("picorv32_work"):
    os.mkdir("picorv32_work")

threads = []

for i in range(num_runs):
    def runner(run):
        ascfile = "picorv32_work/picorv32_s{}.asc".format(run)
        if path.exists(ascfile):
            os.remove(ascfile)
        result = subprocess.run(["../nextpnr-ice40", "--hx8k", "--seed", str(run), "--json", "picorv32.json", "--asc", ascfile, "--freq", "70"], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
        if result.returncode != 0:
            print("Run {} failed!".format(run))
        else:
            icetime_res = subprocess.check_output(["icetime", "-d", "hx8k", ascfile])
            fmax_m = re.search(r'\(([0-9.]+) MHz\)', icetime_res.decode('utf-8'))
            fmax[run] = float(fmax_m.group(1))
    threads.append(threading.Thread(target=runner, args=[i+1]))

for t in threads: t.start()
for t in threads: t.join()

fmax_min = min(fmax.values())
fmax_max = max(fmax.values())
fmax_avg = sum(fmax.values()) / len(fmax)

print("{}/{} runs passed".format(len(fmax), num_runs))
print("icetime: min = {} MHz, avg = {} MHz, max = {} MHz".format(fmax_min, fmax_avg, fmax_max))
