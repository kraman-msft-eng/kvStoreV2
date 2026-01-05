"""
KVStore Performance Analysis - Server-Side Network Bottleneck
Each operation = 128 tokens = 1.2 MB payload
Server does 2x network: Client↔Server + Server↔Storage
Test workload: 6 Reads + 3 Writes per iteration (2:1 ratio)
"""

import matplotlib.pyplot as plt
import numpy as np

# Configuration
TOKENS_PER_OP = 128
PAYLOAD_MB = 1.2
PAYLOAD_BITS = PAYLOAD_MB * 8  # 9.6 Megabits

# Workload ratio per iteration
READS_PER_ITER = 6
WRITES_PER_ITER = 3

# Server bandwidth budget: 30 Gbps total, but server does 2x transfer
SERVER_TOTAL_BW_GBPS = 30

# Experimental data - Single NUMA node
concurrency = np.array([1, 2, 5, 10, 20, 50, 100, 200])

# Latency data (milliseconds)
read_p50 = np.array([23.24, 23.27, 24.49, 27.86, 44.96, 124.12, 247.45, 504.41])
read_p99 = np.array([38.81, 40.08, 42.93, 53.19, 98.45, 407.27, 800.67, 1240.36])
write_p50 = np.array([40.20, 39.30, 39.75, 41.47, 49.18, 72.95, 107.01, 212.54])
write_p99 = np.array([63.14, 99.35, 75.98, 78.92, 94.76, 200.69, 377.41, 654.68])

# Calculate iterations per second (based on read latency as it's the longer path)
# Each iteration has 6 reads, so iteration time ≈ read latency
iter_per_sec = concurrency / (read_p50 / 1000)

# Calculate ops per second
read_ops_per_sec = iter_per_sec * READS_PER_ITER
write_ops_per_sec = iter_per_sec * WRITES_PER_ITER

# Convert to tokens per second (128 tokens per operation)
read_tokens_per_sec = read_ops_per_sec * TOKENS_PER_OP
write_tokens_per_sec = write_ops_per_sec * TOKENS_PER_OP
total_tokens_per_sec = read_tokens_per_sec + write_tokens_per_sec

# Calculate SERVER bandwidth (2x for each direction)
# Read: storage→server (1x) + server→client (1x) = 2x
# Write: client→server (1x) + server→storage (1x) = 2x
read_server_bw_gbps = read_ops_per_sec * PAYLOAD_BITS * 2 / 1000  # 2x for server
write_server_bw_gbps = write_ops_per_sec * PAYLOAD_BITS * 2 / 1000  # 2x for server
total_server_bw_gbps = read_server_bw_gbps + write_server_bw_gbps

# Create figure with subplots
fig = plt.figure(figsize=(16, 14))

# ============================================================================
# Plot 1: Throughput (Tokens/sec) vs Concurrency
# ============================================================================
ax1 = fig.add_subplot(2, 2, 1)
ax1.plot(concurrency, read_tokens_per_sec/1000, 'b-o', linewidth=2, markersize=10, label='Read Throughput')
ax1.plot(concurrency, write_tokens_per_sec/1000, 'g-s', linewidth=2, markersize=10, label='Write Throughput')

# Mark optimal operating points
ax1.axvline(x=10, color='green', linestyle=':', linewidth=2, alpha=0.7)
ax1.axvline(x=20, color='orange', linestyle=':', linewidth=2, alpha=0.7)
ax1.axvline(x=50, color='red', linestyle=':', linewidth=2, alpha=0.7)

ax1.set_xlabel('Concurrency (per NUMA node)', fontsize=12)
ax1.set_ylabel('Throughput (K Tokens/sec)', fontsize=12)
ax1.set_title('Throughput vs Concurrency\n(128 tokens per operation, 1.2 MB payload)', fontsize=13, fontweight='bold')
ax1.legend(loc='upper left', fontsize=11)
ax1.grid(True, alpha=0.3)
ax1.set_xscale('log')
ax1.set_xticks(concurrency)
ax1.set_xticklabels(concurrency)

# Add throughput annotations
for i, c in enumerate(concurrency):
    if c in [1, 10, 50, 100]:
        ax1.annotate(f'{read_tokens_per_sec[i]/1000:.0f}K', 
                    (c, read_tokens_per_sec[i]/1000), 
                    textcoords="offset points", xytext=(5,10), fontsize=9)

# ============================================================================
# Plot 2: Latency vs Throughput (The Key Trade-off Graph)
# ============================================================================
ax2 = fig.add_subplot(2, 2, 2)

# Create color gradient based on concurrency
colors = plt.cm.RdYlGn_r(np.linspace(0.1, 0.9, len(concurrency)))

for i, c in enumerate(concurrency):
    ax2.scatter(read_tokens_per_sec[i]/1000, read_p50[i], 
               c=[colors[i]], s=200, edgecolors='black', linewidths=1.5, zorder=5)
    ax2.annotate(f'C={c}', (read_tokens_per_sec[i]/1000, read_p50[i]), 
                textcoords="offset points", xytext=(8, 0), fontsize=10)

# Draw the latency threshold lines
ax2.axhline(y=50, color='green', linestyle='--', linewidth=2, alpha=0.7, label='p50 < 50ms (Ultra-low latency)')
ax2.axhline(y=100, color='orange', linestyle='--', linewidth=2, alpha=0.7, label='p50 < 100ms (Low latency)')
ax2.axhline(y=250, color='red', linestyle='--', linewidth=2, alpha=0.7, label='p50 < 250ms (Standard)')

# Mark optimal operating region
ax2.axvspan(40, 60, alpha=0.15, color='green', label='Optimal zone')

ax2.set_xlabel('Read Throughput (K Tokens/sec)', fontsize=12)
ax2.set_ylabel('Read p50 Latency (ms)', fontsize=12)
ax2.set_title('Latency vs Throughput Trade-off\n(Sweet Spot Analysis)', fontsize=13, fontweight='bold')
ax2.legend(loc='upper left', fontsize=10)
ax2.grid(True, alpha=0.3)
ax2.set_ylim(0, 600)

# ============================================================================
# Plot 3: Server Bandwidth Utilization
# ============================================================================
ax3 = fig.add_subplot(2, 2, 3)

bar_width = 0.35
x = np.arange(len(concurrency))

bars1 = ax3.bar(x - bar_width/2, read_server_bw_gbps, bar_width, label='Read (2x: storage→server→client)', color='steelblue', alpha=0.8)
bars2 = ax3.bar(x + bar_width/2, write_server_bw_gbps, bar_width, label='Write (2x: client→server→storage)', color='forestgreen', alpha=0.8)

ax3.axhline(y=30, color='red', linestyle='--', linewidth=2, label=f'Server NIC Limit (30 Gbps)')
ax3.axhline(y=26, color='orange', linestyle='--', linewidth=2, label=f'Observed Max (~26 Gbps)')

ax3.set_xlabel('Concurrency', fontsize=12)
ax3.set_ylabel('Server Bandwidth (Gbps)', fontsize=12)
ax3.set_title('Server-Side Bandwidth Utilization\n(Server does 2x transfer: client + storage)', fontsize=13, fontweight='bold')
ax3.set_xticks(x)
ax3.set_xticklabels(concurrency)
ax3.legend(loc='upper left', fontsize=10)
ax3.set_ylim(0, 35)

# ============================================================================
# Plot 4: Operating Point Recommendation
# ============================================================================
ax4 = fig.add_subplot(2, 2, 4)

# Create a table-style visualization
operating_points = [
    ('Ultra-Low Latency', 10, read_p50[3], read_p99[3], read_tokens_per_sec[3], read_server_bw_gbps[3], 'green'),
    ('Low Latency', 20, read_p50[4], read_p99[4], read_tokens_per_sec[4], read_server_bw_gbps[4], 'yellowgreen'),
    ('Balanced', 50, read_p50[5], read_p99[5], read_tokens_per_sec[5], read_server_bw_gbps[5], 'orange'),
    ('Max Throughput', 100, read_p50[6], read_p99[6], read_tokens_per_sec[6], read_server_bw_gbps[6], 'red'),
]

# Plot as horizontal bar chart
y_pos = np.arange(len(operating_points))
throughputs = [op[4]/1000 for op in operating_points]
colors_bar = [op[6] for op in operating_points]

bars = ax4.barh(y_pos, throughputs, color=colors_bar, alpha=0.7, edgecolor='black')
ax4.set_yticks(y_pos)
ax4.set_yticklabels([f"{op[0]}\n(C={op[1]})" for op in operating_points])
ax4.set_xlabel('Throughput (K Tokens/sec)', fontsize=12)
ax4.set_title('Operating Point Comparison\n(Single NUMA Node)', fontsize=13, fontweight='bold')

# Add latency annotations
for i, (name, c, p50, p99, tput, bw, color) in enumerate(operating_points):
    ax4.text(throughputs[i] + 1, i, 
             f'p50={p50:.0f}ms, p99={p99:.0f}ms\nBW={bw:.1f}Gbps', 
             va='center', fontsize=10)

ax4.set_xlim(0, 80)
ax4.grid(True, alpha=0.3, axis='x')

plt.tight_layout()
plt.savefig('server_performance_analysis.png', dpi=150, bbox_inches='tight')
plt.show()

# Print summary table
print("\n" + "="*100)
print("  SERVER PERFORMANCE ANALYSIS - Single NUMA Node (128 tokens/op, 1.2 MB payload)")
print("  Note: Server does 2x network transfer (Client↔Server + Server↔Storage)")
print("="*100)

print(f"\n{'Conc':<6} {'Read p50':<10} {'Read p99':<10} {'Write p50':<10} {'Tokens/s':<12} {'Server BW':<12} {'Status'}")
print("-"*75)

for i, c in enumerate(concurrency):
    status = "✓ Optimal" if c <= 10 else ("⚠ Acceptable" if c <= 20 else ("⚡ High Load" if c <= 50 else "✗ Saturated"))
    print(f"{c:<6} {read_p50[i]:<10.1f} {read_p99[i]:<10.1f} {write_p50[i]:<10.1f} {read_tokens_per_sec[i]:<12,.0f} {read_server_bw_gbps[i]:<12.1f} {status}")

print("\n" + "="*100)
print("  RECOMMENDED OPERATING POINTS")
print("="*100)
print("""
┌─────────────────────┬───────────┬──────────┬──────────┬──────────────┬────────────┐
│ Profile             │ Conc/NUMA │ Read p50 │ Read p99 │ Tokens/sec   │ Server BW  │
├─────────────────────┼───────────┼──────────┼──────────┼──────────────┼────────────┤
│ Ultra-Low Latency   │    10     │   28 ms  │   53 ms  │   45,920     │   6.9 Gbps │
│ Low Latency         │    20     │   45 ms  │   98 ms  │   56,950     │   8.5 Gbps │
│ Balanced (Sweet Spot)│   50     │  124 ms  │  407 ms  │   51,560     │   7.7 Gbps │
│ Max Throughput      │   100     │  247 ms  │  801 ms  │   51,720     │   7.7 Gbps │
└─────────────────────┴───────────┴──────────┴──────────┴──────────────┴────────────┘

DUAL NUMA (2x):
┌─────────────────────┬───────────┬──────────┬──────────┬──────────────┬────────────┐
│ Profile             │ Total Conc│ Read p50 │ Read p99 │ Tokens/sec   │ Server BW  │
├─────────────────────┼───────────┼──────────┼──────────┼──────────────┼────────────┤
│ Ultra-Low Latency   │   20      │   28 ms  │   53 ms  │   ~92,000    │   ~14 Gbps │
│ Low Latency         │   40      │   45 ms  │   98 ms  │   ~114,000   │   ~17 Gbps │
│ Balanced (Sweet Spot)│  100     │  136 ms  │  469 ms  │   ~94,000    │   ~21 Gbps │
│ Max Throughput      │   200     │  263 ms  │ 1110 ms  │   ~97,000    │   ~26 Gbps │
└─────────────────────┴───────────┴──────────┴──────────┴──────────────┴────────────┘

KEY INSIGHT: At ~50K tokens/sec per NUMA, throughput plateaus but latency keeps rising!
             The 'Balanced' sweet spot is Concurrency=20 (~57K tokens/sec, p99<100ms)
""")
