#!/usr/bin/env python3
"""
Generate Lookup and Read Latency Charts from Benchmark Results

Creates 6 charts:
- Lookup p50, p90, p95 vs Token Throughput
- Read p50, p90, p95 vs Token Throughput
"""

import csv
import sys
import os

def parse_csv(csv_content):
    """Parse CSV content into data structure"""
    lines = csv_content.strip().split('\n')
    reader = csv.DictReader(lines)
    data = []
    for row in reader:
        data.append({
            'processes': int(row['processes']),
            'concurrency': int(row['concurrency_per_process']),
            'total_concurrency': int(row['total_concurrency']),
            'tps': float(row['tps']),
            'tokens_per_sec': float(row['tokens_per_sec']),
            'lookup_p50': float(row['lookup_p50_ms']),
            'lookup_p90': float(row['lookup_p90_ms']),
            'lookup_p99': float(row['lookup_p99_ms']),
            'read_p50': float(row['read_p50_ms']),
            'read_p90': float(row['read_p90_ms']),
            'read_p99': float(row['read_p99_ms']),
        })
    return data

def generate_chart_html(data, output_file):
    """Generate an HTML file with interactive charts using Chart.js"""
    
    # Sort by tokens per second
    data_sorted = sorted(data, key=lambda x: x['tokens_per_sec'])
    
    # Prepare data arrays
    labels = [f"{d['total_concurrency']}c" for d in data_sorted]
    tokens_k = [d['tokens_per_sec'] / 1_000 for d in data_sorted]
    tps = [d['tps'] for d in data_sorted]
    
    lookup_p50 = [d['lookup_p50'] for d in data_sorted]
    lookup_p90 = [d['lookup_p90'] for d in data_sorted]
    lookup_p99 = [d['lookup_p99'] for d in data_sorted]
    
    read_p50 = [d['read_p50'] for d in data_sorted]
    read_p90 = [d['read_p90'] for d in data_sorted]
    read_p99 = [d['read_p99'] for d in data_sorted]
    
    html = f'''<!DOCTYPE html>
<html>
<head>
    <title>KVStore Benchmark - Lookup & Read Latency</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body {{ font-family: Arial, sans-serif; margin: 20px; background: #1a1a2e; color: #eee; }}
        h1 {{ text-align: center; color: #00d4ff; }}
        h2 {{ color: #00d4ff; margin-top: 40px; }}
        .chart-container {{ width: 90%; max-width: 1000px; margin: 20px auto; background: #16213e; padding: 20px; border-radius: 10px; }}
        .chart-row {{ display: flex; flex-wrap: wrap; justify-content: center; gap: 20px; }}
        .chart-box {{ flex: 1; min-width: 450px; max-width: 600px; }}
        table {{ margin: 20px auto; border-collapse: collapse; background: #16213e; }}
        th, td {{ padding: 8px 15px; border: 1px solid #444; text-align: right; }}
        th {{ background: #0f3460; color: #00d4ff; }}
        tr:nth-child(even) {{ background: #1a1a3e; }}
    </style>
</head>
<body>
    <h1>KVStore Benchmark Results</h1>
    <h2>Latency vs Token Throughput</h2>
    
    <div class="chart-container">
        <h3 style="text-align:center; color:#ff6b6b;">Lookup Latency</h3>
        <canvas id="lookupChart"></canvas>
    </div>
    
    <div class="chart-container">
        <h3 style="text-align:center; color:#4ecdc4;">Read Latency</h3>
        <canvas id="readChart"></canvas>
    </div>
    
    <div class="chart-container">
        <h3 style="text-align:center; color:#ffe66d;">Throughput</h3>
        <canvas id="throughputChart"></canvas>
    </div>
    
    <h2>Raw Data</h2>
    <table>
        <tr>
            <th>Concurrency</th>
            <th>TPS</th>
            <th>K Tokens/s</th>
            <th>Lookup p50</th>
            <th>Lookup p90</th>
            <th>Lookup p99</th>
            <th>Read p50</th>
            <th>Read p90</th>
            <th>Read p99</th>
        </tr>
'''
    
    for d in data_sorted:
        html += f'''        <tr>
            <td>{d['total_concurrency']}</td>
            <td>{d['tps']:.1f}</td>
            <td>{d['tokens_per_sec']/1_000:.1f}</td>
            <td>{d['lookup_p50']:.2f}ms</td>
            <td>{d['lookup_p90']:.2f}ms</td>
            <td>{d['lookup_p99']:.2f}ms</td>
            <td>{d['read_p50']:.2f}ms</td>
            <td>{d['read_p90']:.2f}ms</td>
            <td>{d['read_p99']:.2f}ms</td>
        </tr>
'''
    
    html += f'''    </table>
    
    <script>
        const labels = {labels};
        const tokensK = {tokens_k};
        const tps = {tps};
        
        // Lookup Chart
        new Chart(document.getElementById('lookupChart'), {{
            type: 'line',
            data: {{
                labels: tokensK.map(t => t.toFixed(1) + 'K'),
                datasets: [
                    {{
                        label: 'p50',
                        data: {lookup_p50},
                        borderColor: '#ff6b6b',
                        backgroundColor: 'rgba(255,107,107,0.1)',
                        tension: 0.3,
                        pointRadius: 6
                    }},
                    {{
                        label: 'p90',
                        data: {lookup_p90},
                        borderColor: '#ffa502',
                        backgroundColor: 'rgba(255,165,2,0.1)',
                        tension: 0.3,
                        pointRadius: 6
                    }},
                    {{
                        label: 'p99',
                        data: {lookup_p99},
                        borderColor: '#ff4757',
                        backgroundColor: 'rgba(255,71,87,0.1)',
                        tension: 0.3,
                        pointRadius: 6
                    }}
                ]
            }},
            options: {{
                responsive: true,
                plugins: {{
                    title: {{ display: true, text: 'Lookup Latency vs Token Throughput', color: '#eee' }},
                    legend: {{ labels: {{ color: '#eee' }} }}
                }},
                scales: {{
                    x: {{ 
                        title: {{ display: true, text: 'Token Throughput (K tokens/sec)', color: '#eee' }},
                        ticks: {{ color: '#aaa' }},
                        grid: {{ color: '#333' }}
                    }},
                    y: {{ 
                        title: {{ display: true, text: 'Latency (ms)', color: '#eee' }},
                        ticks: {{ color: '#aaa' }},
                        grid: {{ color: '#333' }},
                        beginAtZero: true
                    }}
                }}
            }}
        }});
        
        // Read Chart
        new Chart(document.getElementById('readChart'), {{
            type: 'line',
            data: {{
                labels: tokensK.map(t => t.toFixed(1) + 'K'),
                datasets: [
                    {{
                        label: 'p50',
                        data: {read_p50},
                        borderColor: '#4ecdc4',
                        backgroundColor: 'rgba(78,205,196,0.1)',
                        tension: 0.3,
                        pointRadius: 6
                    }},
                    {{
                        label: 'p90',
                        data: {read_p90},
                        borderColor: '#45b7d1',
                        backgroundColor: 'rgba(69,183,209,0.1)',
                        tension: 0.3,
                        pointRadius: 6
                    }},
                    {{
                        label: 'p99',
                        data: {read_p99},
                        borderColor: '#96ceb4',
                        backgroundColor: 'rgba(150,206,180,0.1)',
                        tension: 0.3,
                        pointRadius: 6
                    }}
                ]
            }},
            options: {{
                responsive: true,
                plugins: {{
                    title: {{ display: true, text: 'Read Latency vs Token Throughput', color: '#eee' }},
                    legend: {{ labels: {{ color: '#eee' }} }}
                }},
                scales: {{
                    x: {{ 
                        title: {{ display: true, text: 'Token Throughput (K tokens/sec)', color: '#eee' }},
                        ticks: {{ color: '#aaa' }},
                        grid: {{ color: '#333' }}
                    }},
                    y: {{ 
                        title: {{ display: true, text: 'Latency (ms)', color: '#eee' }},
                        ticks: {{ color: '#aaa' }},
                        grid: {{ color: '#333' }},
                        beginAtZero: true
                    }}
                }}
            }}
        }});
        
        // Throughput Chart  
        new Chart(document.getElementById('throughputChart'), {{
            type: 'bar',
            data: {{
                labels: labels,
                datasets: [
                    {{
                        label: 'TPS',
                        data: tps,
                        backgroundColor: 'rgba(255,230,109,0.7)',
                        borderColor: '#ffe66d',
                        borderWidth: 2,
                        yAxisID: 'y'
                    }},
                    {{
                        label: 'K Tokens/sec',
                        data: tokensK,
                        backgroundColor: 'rgba(0,212,255,0.7)',
                        borderColor: '#00d4ff',
                        borderWidth: 2,
                        yAxisID: 'y1'
                    }}
                ]
            }},
            options: {{
                responsive: true,
                plugins: {{
                    title: {{ display: true, text: 'Throughput by Concurrency', color: '#eee' }},
                    legend: {{ labels: {{ color: '#eee' }} }}
                }},
                scales: {{
                    x: {{ 
                        title: {{ display: true, text: 'Total Concurrency', color: '#eee' }},
                        ticks: {{ color: '#aaa' }},
                        grid: {{ color: '#333' }}
                    }},
                    y: {{ 
                        type: 'linear',
                        position: 'left',
                        title: {{ display: true, text: 'TPS', color: '#ffe66d' }},
                        ticks: {{ color: '#ffe66d' }},
                        grid: {{ color: '#333' }}
                    }},
                    y1: {{
                        type: 'linear',
                        position: 'right',
                        title: {{ display: true, text: 'K Tokens/sec', color: '#00d4ff' }},
                        ticks: {{ color: '#00d4ff' }},
                        grid: {{ drawOnChartArea: false }}
                    }}
                }}
            }}
        }});
    </script>
</body>
</html>
'''
    
    with open(output_file, 'w') as f:
        f.write(html)
    print(f"Chart saved to: {output_file}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 generate_charts.py <benchmark_results.csv> [output.html]")
        sys.exit(1)
    
    csv_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else csv_file.replace('.csv', '_charts.html')
    
    with open(csv_file, 'r') as f:
        csv_content = f.read()
    
    data = parse_csv(csv_content)
    generate_chart_html(data, output_file)
    print(f"\nOpen {output_file} in a browser to view the charts.")

if __name__ == '__main__':
    main()
