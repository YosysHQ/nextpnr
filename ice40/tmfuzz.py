#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ../nextpnr-ice40 --hx8k --tmfuzz > tmfuzz_hx8k.txt

import numpy as np
import matplotlib.pyplot as plt
from collections import defaultdict

device = "hx8k"
sel_src_type = "LUTFF_OUT"
sel_dst_type = "LUTFF_IN_LUT"

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

#%%

print("Src-Dst-Type pair summary:")
for cnt, src, dst in sorted([(v, k[0], k[1]) for k, v in src_dst_pairs.items()]):
    print("%20s %20s %5d%s" % (src, dst, cnt, " *" if src == sel_src_type and dst == sel_dst_type else ""))
print()

#%%

plt.figure()
plt.imshow(delay_map_sum / delay_map_count)
plt.colorbar()
plt.show()

#%%

plt.figure()
plt.plot(delay_data[:,0], delay_data[:,1], ".")
plt.show()

