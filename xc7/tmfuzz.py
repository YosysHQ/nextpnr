#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ../nextpnr-ice40 --hx8k --tmfuzz > tmfuzz_hx8k.txt
# ../nextpnr-ice40 --lp8k --tmfuzz > tmfuzz_lp8k.txt
# ../nextpnr-ice40 --up5k --tmfuzz > tmfuzz_up5k.txt

import numpy as np
import matplotlib.pyplot as plt
from collections import defaultdict

device = "hx8k"
# device = "lp8k"
# device = "up5k"

sel_src_type = "LUTFF_OUT"
sel_dst_type = "LUTFF_IN_LUT"

#%% Read fuzz data

src_dst_pairs = defaultdict(lambda: 0)

delay_data = list()
all_delay_data = list()

delay_map_sum = np.zeros((41, 41))
delay_map_sum2 = np.zeros((41, 41))
delay_map_count = np.zeros((41, 41))

same_tile_delays = list()
neighbour_tile_delays = list()

type_delta_data = dict()

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

        all_delay_data.append((delay, estdelay))

        src_dst_pairs[src_type, dst_type] += 1

        dx = dst_xy[0] - src_xy[0]
        dy = dst_xy[1] - src_xy[1]

        if src_type == sel_src_type and dst_type == sel_dst_type:
            if dx == 0 and dy == 0:
                same_tile_delays.append(delay)

            elif abs(dx) <= 1 and abs(dy) <= 1:
                neighbour_tile_delays.append(delay)

            else:
                delay_data.append((delay, estdelay, dx, dy, 0, 0, 0))

                relx = 20 + dst_xy[0] - src_xy[0]
                rely = 20 + dst_xy[1] - src_xy[1]

                if (0 <= relx <= 40) and (0 <= rely <= 40):
                    delay_map_sum[relx, rely] += delay
                    delay_map_sum2[relx, rely] += delay*delay
                    delay_map_count[relx, rely] += 1

        if dst_type == sel_dst_type:
            if src_type not in type_delta_data:
                type_delta_data[src_type] = list()

            type_delta_data[src_type].append((dx, dy, delay))

delay_data = np.array(delay_data)
all_delay_data = np.array(all_delay_data)
max_delay = np.max(delay_data[:, 0:2])

mean_same_tile_delays = np.mean(neighbour_tile_delays)
mean_neighbour_tile_delays = np.mean(neighbour_tile_delays)

print("Avg same tile delay: %.2f (%.2f std, N=%d)" % \
        (mean_same_tile_delays, np.std(same_tile_delays), len(same_tile_delays)))
print("Avg neighbour tile delay: %.2f (%.2f std, N=%d)" % \
        (mean_neighbour_tile_delays, np.std(neighbour_tile_delays), len(neighbour_tile_delays)))

#%% Apply simple low-weight bluring to fill gaps

for i in range(0):
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

plt.figure(figsize=(8, 3))
plt.title("Estimate vs Actual Delay")
plt.plot(all_delay_data[:, 0], all_delay_data[:, 1], ".")
plt.plot(delay_data[:, 0], delay_data[:, 1], ".")
plt.plot([0, max_delay], [0, max_delay], "k")
plt.ylabel("Estimated Delay")
plt.xlabel("Actual Delay")
plt.grid()
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

#%% Generate Model #0

def nonlinearPreprocessor0(dx, dy):
    dx, dy = abs(dx), abs(dy)
    values = [1.0]
    values.append(dx + dy)
    return np.array(values)

A = np.zeros((41*41, len(nonlinearPreprocessor0(0, 0))))
b = np.zeros(41*41)

index = 0
for x in range(41):
    for y in range(41):
        if delay_map_count[x, y] > 0:
            A[index, :] = nonlinearPreprocessor0(x-20, y-20)
            b[index] = delay_map[x, y]
        index += 1

model0_params, _, _, _ = np.linalg.lstsq(A, b)
print("Model #0 parameters:", model0_params)

model0_map = np.zeros((41, 41))
for x in range(41):
    for y in range(41):
        v = np.dot(model0_params, nonlinearPreprocessor0(x-20, y-20))
        model0_map[x, y] = v

plt.figure(figsize=(9, 3))
plt.subplot(121)
plt.title("Model #0 Delay Map")
plt.imshow(model0_map)
plt.colorbar()
plt.subplot(122)
plt.title("Model #0 Error Map")
plt.imshow(model0_map - delay_map)
plt.colorbar()
plt.show()

for i in range(delay_data.shape[0]):
    dx = delay_data[i, 2]
    dy = delay_data[i, 3]
    delay_data[i, 4] =  np.dot(model0_params, nonlinearPreprocessor0(dx, dy))

plt.figure(figsize=(8, 3))
plt.title("Model #0 vs Actual Delay")
plt.plot(delay_data[:, 0], delay_data[:, 4], ".")
plt.plot(delay_map.flat, model0_map.flat, ".")
plt.plot([0, max_delay], [0, max_delay], "k")
plt.ylabel("Model #0 Delay")
plt.xlabel("Actual Delay")
plt.grid()
plt.show()

print("In-sample RMS error: %f" % np.sqrt(np.nanmean((delay_map - model0_map)**2)))
print("Out-of-sample RMS error: %f" % np.sqrt(np.nanmean((delay_data[:, 0] - delay_data[:, 4])**2)))
print()

#%% Generate Model #1

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
        if delay_map_count[x, y] > 0:
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

for i in range(delay_data.shape[0]):
    dx = delay_data[i, 2]
    dy = delay_data[i, 3]
    delay_data[i, 5] = np.dot(model1_params, nonlinearPreprocessor1(dx, dy))

plt.figure(figsize=(8, 3))
plt.title("Model #1 vs Actual Delay")
plt.plot(delay_data[:, 0], delay_data[:, 5], ".")
plt.plot(delay_map.flat, model1_map.flat, ".")
plt.plot([0, max_delay], [0, max_delay], "k")
plt.ylabel("Model #1 Delay")
plt.xlabel("Actual Delay")
plt.grid()
plt.show()

print("In-sample RMS error: %f" % np.sqrt(np.nanmean((delay_map - model1_map)**2)))
print("Out-of-sample RMS error: %f" % np.sqrt(np.nanmean((delay_data[:, 0] - delay_data[:, 5])**2)))
print()

#%% Generate Model #2

def nonlinearPreprocessor2(v):
    return np.array([1, v, np.sqrt(v)])

A = np.zeros((41*41, len(nonlinearPreprocessor2(0))))
b = np.zeros(41*41)

index = 0
for x in range(41):
    for y in range(41):
        if delay_map_count[x, y] > 0:
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

for i in range(delay_data.shape[0]):
    dx = delay_data[i, 2]
    dy = delay_data[i, 3]
    delay_data[i, 6] = np.dot(model2_params, nonlinearPreprocessor2(delay_data[i, 5]))

plt.figure(figsize=(8, 3))
plt.title("Model #2 vs Actual Delay")
plt.plot(delay_data[:, 0], delay_data[:, 6], ".")
plt.plot(delay_map.flat, model2_map.flat, ".")
plt.plot([0, max_delay], [0, max_delay], "k")
plt.ylabel("Model #2 Delay")
plt.xlabel("Actual Delay")
plt.grid()
plt.show()

print("In-sample RMS error: %f" % np.sqrt(np.nanmean((delay_map - model2_map)**2)))
print("Out-of-sample RMS error: %f" % np.sqrt(np.nanmean((delay_data[:, 0] - delay_data[:, 6])**2)))
print()

#%% Generate deltas for different source net types

type_deltas = dict()

print("Delay deltas for different src types:")
for src_type in sorted(type_delta_data.keys()):
    deltas = list()

    for dx, dy, delay in type_delta_data[src_type]:
        dx = abs(dx)
        dy = abs(dy)

        if dx > 1 or dy > 1:
            est = model0_params[0] + model0_params[1] * (dx + dy)
        else:
            est = mean_neighbour_tile_delays
        deltas.append(delay - est)

    print("%15s: %8.2f (std %6.2f)" % (\
            src_type, np.mean(deltas), np.std(deltas)))

    type_deltas[src_type] = np.mean(deltas)

#%% Print C defs of model parameters

print("--snip--")
print("%d, %d, %d," % (mean_neighbour_tile_delays, 128 * model0_params[0], 128 * model0_params[1]))
print("%d, %d, %d, %d," % (128 * model1_params[0], 128 * model1_params[1], 128 * model1_params[2], 128 * model1_params[3]))
print("%d, %d, %d," % (128 * model2_params[0], 128 * model2_params[1], 128 * model2_params[2]))
print("%d, %d, %d, %d" % (type_deltas["LOCAL"], type_deltas["LUTFF_IN"], \
                          (type_deltas["SP4_H"] + type_deltas["SP4_V"]) / 2,
                          (type_deltas["SP12_H"] + type_deltas["SP12_V"]) / 2))
print("--snap--")
