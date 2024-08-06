import csv
import sys

import matplotlib.pyplot as plt

data = []

file = 1
try:
    while True:
        print("{}/heatmap_congestion_by_coordinate_{}.csv".format(sys.argv[1], file))
        with open("{}/heatmap_congestion_by_coordinate_{}.csv".format(sys.argv[1], file)) as f:
            file_data = []
            reader = csv.reader(f, delimiter=',')
            for row in reader:
                file_data.append([float(x) for x in row if x != ''])
            data.append(file_data)
        file += 1
except FileNotFoundError:
    pass

for i, file_data in enumerate(data):
    plt.imshow(file_data, cmap="gray", aspect="equal")
    plt.title("heatmap for iteration {}".format(i+1))
    plt.tight_layout()
    plt.savefig("{}/heatmap_congestion_by_coordinate_{:03}.png".format(sys.argv[1], i), dpi=300)
    plt.clf()
