import csv
import sys

import matplotlib.pyplot as plt

data = {}
max_bars = {}

file = 1
try:
    while True:
        print("{}/heatmap_congestion_by_wiretype_{}.csv".format(sys.argv[1], file))
        with open("{}/heatmap_congestion_by_wiretype_{}.csv".format(sys.argv[1], file)) as f:
            reader = csv.reader(f, delimiter=',')
            for row in [x for x in reader][1:]:
                key = row[0]
                values = [float(x) for x in row[1:] if x != '']
                # Ignore wires without overuse
                values[0] = 0
                values[1] = 0
                if key not in data:
                    data[key] = []
                    max_bars[key] = 0
                data[key].append(values)
                max_bars[key] = max(max_bars[key], len(values))
            file += 1
except FileNotFoundError:
    pass
finally:
    file -= 1

to_remove = []
for key in data.keys():
    if sum([sum(values) for values in data[key]]) == 0:
        # Prune entries that never have any overuse to attempt to reduce visual clutter
        to_remove.append(key)
    else:
        # Pad entries as needed
        for values in data[key]:
            while len(values) < max_bars[key]:
                values.append(0)
for key in to_remove:
    del data[key]

COLS = 2
for i in range(file):
    plt.suptitle("heatmap for iteration {}".format(i))
    fig, axs = plt.subplots((len(data.keys())+(COLS-1))//COLS, COLS)
    for j, key in enumerate(data.keys()):
        if sum(data[key][i]) > 0:
            axs[j//COLS, j%COLS].bar([x for x in range(len(data[key][i]))], data[key][i])
            axs[j//COLS, j%COLS].set_title(key)
        else:
            axs[j//COLS, j%COLS].set_axis_off()
    plt.savefig("{}/heatmap_congestion_by_wiretype_{:03}.png".format(sys.argv[1], i), dpi=300)
    plt.close()
