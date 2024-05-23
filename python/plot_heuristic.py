import matplotlib.pyplot as plt
import csv

RED = '#ca684d'
GREEN = '#7aa354'
BLUE = '#5e84d0'

EPSILON = 0.02

heuristic = []
actual = []
color = []
with open('heuristic.csv') as f:
    reader = csv.reader(f, delimiter=',')
    rows = [row for row in reader]
    for row in rows[1:]:
        heuristic_value = float(row[2])
        actual_value = float(row[3])
        heuristic.append(heuristic_value)
        actual.append(actual_value)
        if heuristic_value > actual_value+EPSILON:
            color.append(RED)
        elif heuristic_value < actual_value-EPSILON:
            color.append(BLUE)
        else:
            color.append(GREEN)

plt.scatter(actual, heuristic, c=color)
plt.xlabel("Actual Delay (ns)")
plt.ylabel("Estimated Delay (ns)")
plt.savefig("heuristic_vs_actual.png")

