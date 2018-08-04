#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ../nextpnr-ice40 --hx8k --tmfuzz > tmfuzz_hx8k.txt

import numpy as np
import matplotlib.pyplot as plt
from collections import defaultdict

device = "hx8k"
sel_src_type = "LUTFF_OUT"
sel_dst_type = "LUTFF_IN_LUT"

#%% Read fuzz data

src_dst_pairs = defaultdict(lambda: 0)

delay_data = list()
delay_map_sum = np.zeros((41, 41))
delay_map_sum2 = np.zeros((41, 41))
delay_map_count = np.zeros((41, 41))

with open("tmfuzz_%s.txt" % device, "r") as f:
    for line in f:
        line = line.split()

        if line[0] == "dst":
            dst_xy = (int(line[1]), int(line[2]))
            dst_type = line[3]
            dst_wire = line[4]

        src_xy = (int(line[1]), int(line[2]))
        src_type = line[3]
        src_wire = line[4]

        delay = int(line[5])
        estdelay = int(line[6])

        src_dst_pairs[src_type, dst_type] += 1

        if src_type == sel_src_type and dst_type == sel_dst_type:
            delay_data.append((delay, estdelay))
            relx = 20 + dst_xy[0] - src_xy[0]
            rely = 20 + dst_xy[1] - src_xy[1]

            if (0 <= relx <= 40) and (0 <= rely <= 40):
                delay_map_sum[relx, rely] += delay
                delay_map_sum2[relx, rely] += delay*delay
                delay_map_count[relx, rely] += 1

delay_data = np.array(delay_data)

#%% Apply simple low-weight bluring to fill gaps

for i in range(1):
    neigh_sum = np.zeros((41, 41))
    neigh_sum2 = np.zeros((41, 41))
    neigh_count = np.zeros((41, 41))

    for x in range(41):
        for y in range(41):
            for p in range(-1, 2):
                for q in range(-1, 2):
                    if p == 0 and q == 0:
                        continue
                    if 0 <= (x+p) <= 40:
                        if 0 <= (y+q) <= 40:
                            neigh_sum[x, y] += delay_map_sum[x+p, y+q]
                            neigh_sum2[x, y] += delay_map_sum2[x+p, y+q]
                            neigh_count[x, y] += delay_map_count[x+p, y+q]

    delay_map_sum += 0.1 * neigh_sum
    delay_map_sum2 += 0.1 * neigh_sum2
    delay_map_count += 0.1 * neigh_count

delay_map = delay_map_sum / delay_map_count
delay_map_std = np.sqrt(delay_map_count*delay_map_sum2 - delay_map_sum**2) / delay_map_count

#%% Print src-dst-pair summary

print("Src-Dst-Type pair summary:")
for cnt, src, dst in sorted([(v, k[0], k[1]) for k, v in src_dst_pairs.items()]):
    print("%20s %20s %5d%s" % (src, dst, cnt, " *" if src == sel_src_type and dst == sel_dst_type else ""))
print()

#%% Plot estimate vs actual delay

plt.figure()
plt.plot(delay_data[:,0], delay_data[:,1], ".")
plt.show()

#%% Plot delay heatmap and std dev heatmap

plt.figure(figsize=(9, 3))
plt.subplot(121)
plt.title("Actual Delay Map")
plt.imshow(delay_map)
plt.colorbar()
plt.subplot(122)
plt.title("Standard Deviation")
plt.imshow(delay_map_std)
plt.colorbar()
plt.show()

#%% Linear least-squares fits of delayEstimate models

def nonlinearPreprocessor1(dx, dy):
    dx, dy = abs(dx), abs(dy)
    values = [1.0]
    values.append(dx + dy)                    # 1-norm
    values.append((dx**2 + dy**2)**(1/2))     # 2-norm
    values.append((dx**3 + dy**3)**(1/3))     # 3-norm
    return np.array(values)

A = np.zeros((41*41, len(nonlinearPreprocessor1(0, 0))))
b = np.zeros(41*41)

index = 0
for x in range(41):
    for y in range(41):
        A[index, :] = nonlinearPreprocessor1(x-20, y-20)
        b[index] = delay_map[x, y]
        index += 1

model1_params, _, _, _ = np.linalg.lstsq(A, b)
print("Model #1 parameters:", model1_params)

model1_map = np.zeros((41, 41))
for x in range(41):
    for y in range(41):
        v = np.dot(model1_params, nonlinearPreprocessor1(x-20, y-20))
        model1_map[x, y] = v

plt.figure(figsize=(9, 3))
plt.subplot(121)
plt.title("Model #1 Delay Map")
plt.imshow(model1_map)
plt.colorbar()
plt.subplot(122)
plt.title("Model #1 Error Map")
plt.imshow(model1_map - delay_map)
plt.colorbar()
plt.show()

plt.figure(figsize=(8, 3))
plt.title("Model #1  vs Actual Delay")
plt.plot(delay_map.flat, model1_map.flat, ".")
plt.plot([0, 4000], [0, 4000], "k")
plt.ylabel("Model #1 Delay")
plt.xlabel("Actual Delay")
plt.grid()
plt.show()

print("Total RMS error: %f" % np.sqrt(np.mean((delay_map - model1_map)**2)))
print()

if True:
    def nonlinearPreprocessor2(v):
        return np.array([1, v, np.sqrt(v)])

    A = np.zeros((41*41, len(nonlinearPreprocessor2(0))))
    b = np.zeros(41*41)

    index = 0
    for x in range(41):
        for y in range(41):
            A[index, :] = nonlinearPreprocessor2(model1_map[x, y])
            b[index] = delay_map[x, y]
            index += 1

    model2_params, _, _, _ = np.linalg.lstsq(A, b)
    print("Model #2 parameters:", model2_params)

    model2_map = np.zeros((41, 41))
    for x in range(41):
        for y in range(41):
            v = np.dot(model1_params, nonlinearPreprocessor1(x-20, y-20))
            v = np.dot(model2_params, nonlinearPreprocessor2(v))
            model2_map[x, y] = v

    plt.figure(figsize=(9, 3))
    plt.subplot(121)
    plt.title("Model #2 Delay Map")
    plt.imshow(model2_map)
    plt.colorbar()
    plt.subplot(122)
    plt.title("Model #2 Error Map")
    plt.imshow(model2_map - delay_map)
    plt.colorbar()
    plt.show()

    plt.figure(figsize=(8, 3))
    plt.title("Model #2 vs Actual Delay")
    plt.plot(delay_map.flat, model2_map.flat, ".")
    plt.plot([0, 4000], [0, 4000], "k")
    plt.ylabel("Model #2 Delay")
    plt.xlabel("Actual Delay")
    plt.grid()
    plt.show()

    print("Total RMS error: %f" % np.sqrt(np.mean((delay_map - model2_map)**2)))
    print()
