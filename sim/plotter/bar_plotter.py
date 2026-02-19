#!/usr/bin/env python3

import numpy as np

import matplotlib.pyplot as plt

# Sample data - replace with your actual data
traces = ['Long Exec. Trace', 'Short Exec. Trace', 'Multi-Region Trace']
avg_latency = [53.01, 61.10, 44.93]
min_latency = [7, 27, 7]
max_latency = [114, 82, 101]

# Set up the bar positions
x = np.arange(len(traces))
width = 0.25

# Create figure and axis
fig, ax = plt.subplots(figsize=(10, 6))

# Create the three sub-bars for each trace
bars1 = ax.bar(x - width, min_latency, width, label='Min Latency')
bars2 = ax.bar(x, avg_latency, width, label='Avg Latency')
bars3 = ax.bar(x + width, max_latency, width, label='Max Latency')

# Customize the plot
ax.set_xlabel('Traces')
ax.set_ylabel('Latency (ms)')
ax.set_title('Latency for Example Netrace Traces (# of VCs = 8)')
ax.set_xticks(x)
ax.set_xticklabels(traces)
ax.legend()

# Add grid for better readability
ax.grid(axis='y', alpha=0.3)

plt.tight_layout()
plt.show()