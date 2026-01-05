"""
Latency vs Concurrency and Bandwidth Utilization Analysis
Network Saturation Study for KVStore
"""

import matplotlib.pyplot as plt
import numpy as np

# Data from experiments
concurrency = [1, 2, 5, 10, 20, 50, 100, 200]

# Latency data (milliseconds)
lookup_p50 = [5.74, 5.48, 5.28, 5.58, 8.67, 14.13, 28.54, 81.28]
lookup_p99 = [10.44, 10.05, 11.55, 15.50, 35.54, 88.52, 220.87, 489.85]

read_p50 = [23.24, 23.27, 24.49, 27.86, 44.96, 124.12, 247.45, 504.41]
read_p99 = [38.81, 40.08, 42.93, 53.19, 98.45, 407.27, 800.67, 1240.36]

write_p50 = [40.20, 39.30, 39.75, 41.47, 49.18, 72.95, 107.01, 212.54]
write_p99 = [63.14, 99.35, 75.98, 78.92, 94.76, 200.69, 377.41, 654.68]

# Bandwidth utilization (Gbps)
bandwidth_gbps = [0.65, 1.31, 3.17, 5.75, 8.18, 10.45, 12.85, 12.85]
max_bandwidth = 30  # Theoretical max

# Create figure with multiple subplots
fig, axes = plt.subplots(2, 2, figsize=(14, 10))
fig.suptitle('Network Saturation Analysis: Latency vs Concurrency\n(1.2 MB payload, Standard_E80ids_v4 - 30 Gbps)', 
             fontsize=14, fontweight='bold')

# Plot 1: All latencies vs Concurrency (log scale)
ax1 = axes[0, 0]
ax1.plot(concurrency, read_p50, 'b-o', linewidth=2, markersize=8, label='Read p50')
ax1.plot(concurrency, read_p99, 'b--s', linewidth=2, markersize=6, label='Read p99')
ax1.plot(concurrency, write_p50, 'g-o', linewidth=2, markersize=8, label='Write p50')
ax1.plot(concurrency, write_p99, 'g--s', linewidth=2, markersize=6, label='Write p99')
ax1.plot(concurrency, lookup_p50, 'r-o', linewidth=2, markersize=8, label='Lookup p50')
ax1.set_xscale('log')
ax1.set_yscale('log')
ax1.set_xlabel('Concurrency', fontsize=11)
ax1.set_ylabel('Latency (ms)', fontsize=11)
ax1.set_title('Latency vs Concurrency (Log-Log Scale)')
ax1.legend(loc='upper left')
ax1.grid(True, alpha=0.3)
ax1.axvline(x=20, color='orange', linestyle=':', linewidth=2, label='Inflection point')
ax1.set_xticks(concurrency)
ax1.set_xticklabels(concurrency)

# Plot 2: Bandwidth Utilization vs Concurrency
ax2 = axes[0, 1]
bars = ax2.bar(range(len(concurrency)), bandwidth_gbps, color='steelblue', alpha=0.7, edgecolor='navy')
ax2.axhline(y=max_bandwidth, color='red', linestyle='--', linewidth=2, label=f'Theoretical Max ({max_bandwidth} Gbps)')
ax2.axhline(y=12.85, color='orange', linestyle='--', linewidth=2, label='Observed Max (~13 Gbps)')
ax2.set_xticks(range(len(concurrency)))
ax2.set_xticklabels(concurrency)
ax2.set_xlabel('Concurrency', fontsize=11)
ax2.set_ylabel('Bandwidth (Gbps)', fontsize=11)
ax2.set_title('Network Bandwidth Utilization')
ax2.legend()
ax2.set_ylim(0, 35)

# Add percentage labels on bars
for i, (bar, bw) in enumerate(zip(bars, bandwidth_gbps)):
    pct = (bw / max_bandwidth) * 100
    ax2.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.5, 
             f'{pct:.0f}%', ha='center', va='bottom', fontsize=9)

# Plot 3: Read Latency vs Bandwidth (showing correlation)
ax3 = axes[1, 0]
scatter = ax3.scatter(bandwidth_gbps, read_p50, c=concurrency, s=200, cmap='viridis', 
                       edgecolors='black', linewidths=1.5)
ax3.set_xlabel('Bandwidth Utilization (Gbps)', fontsize=11)
ax3.set_ylabel('Read p50 Latency (ms)', fontsize=11)
ax3.set_title('Read Latency vs Bandwidth Utilization')

# Add concurrency labels
for i, (bw, lat, c) in enumerate(zip(bandwidth_gbps, read_p50, concurrency)):
    ax3.annotate(f'C={c}', (bw, lat), textcoords="offset points", 
                 xytext=(5, 5), fontsize=9)

# Add regions
ax3.axvspan(0, 6, alpha=0.1, color='green', label='Linear region')
ax3.axvspan(6, 10, alpha=0.1, color='yellow', label='Transition')
ax3.axvspan(10, 15, alpha=0.1, color='red', label='Saturated')
ax3.legend(loc='upper left')
ax3.grid(True, alpha=0.3)

# Plot 4: Latency Multiplier (normalized to baseline)
ax4 = axes[1, 1]
read_multiplier = [r / read_p50[0] for r in read_p50]
write_multiplier = [w / write_p50[0] for w in write_p50]
lookup_multiplier = [l / lookup_p50[0] for l in lookup_p50]

ax4.plot(concurrency, read_multiplier, 'b-o', linewidth=2, markersize=8, label='Read')
ax4.plot(concurrency, write_multiplier, 'g-o', linewidth=2, markersize=8, label='Write')
ax4.plot(concurrency, lookup_multiplier, 'r-o', linewidth=2, markersize=8, label='Lookup')
ax4.axhline(y=1, color='gray', linestyle='-', linewidth=1)
ax4.axhline(y=2, color='orange', linestyle=':', linewidth=2, label='2x baseline')
ax4.axhline(y=5, color='red', linestyle=':', linewidth=2, label='5x baseline')

ax4.set_xscale('log')
ax4.set_yscale('log')
ax4.set_xlabel('Concurrency', fontsize=11)
ax4.set_ylabel('Latency Multiplier (vs baseline)', fontsize=11)
ax4.set_title('Latency Degradation Factor')
ax4.legend(loc='upper left')
ax4.grid(True, alpha=0.3)
ax4.set_xticks(concurrency)
ax4.set_xticklabels(concurrency)

plt.tight_layout()
plt.savefig('latency_bandwidth_analysis.png', dpi=150, bbox_inches='tight')
plt.show()

print("\n" + "="*70)
print("SUMMARY: Network Saturation Analysis")
print("="*70)
print(f"\n{'Concurrency':<12} {'Bandwidth':<12} {'Read p50':<12} {'Multiplier':<12} {'Status'}")
print("-"*60)
for i, c in enumerate(concurrency):
    bw = bandwidth_gbps[i]
    lat = read_p50[i]
    mult = lat / read_p50[0]
    status = "✓ OK" if mult < 2 else ("⚠ Warning" if mult < 5 else "✗ Saturated")
    print(f"{c:<12} {bw:<12.2f} {lat:<12.1f} {mult:<12.1f}x {status}")

print("\n" + "="*70)
print("KEY FINDINGS:")
print("="*70)
print("• Network saturates at ~13 Gbps (43% of theoretical 30 Gbps)")
print("• Inflection point: Concurrency 10-20")
print("• Beyond saturation: Latency scales linearly with concurrency")
print("• At 30 Gbps with 3125 concurrent 1.2MB reqs → ~1 second latency")
print("="*70)
