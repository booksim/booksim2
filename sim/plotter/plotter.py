#!/usr/bin/env python3
"""
Simple plotting script for BookSim results from JSON files.
Enhanced with spanning tree routing analysis.

Usage examples:
  # VC sweep plot
  python plot_booksim_results.py --type vc --files vc1.json vc2.json vc4.json vc8.json --vc-labels 1 2 4 8
  
  # Rate sweep plot
  python plot_booksim_results.py --type rate --files rate_*.json --vc-labels 1 1 1 4 4 4
  
  # Network size scaling
  python plot_booksim_results.py --type network-size --files n16.json n32.json n64.json --sizes 16 32 64
  
  # Hop analysis (path stretch)
  python plot_booksim_results.py --type hops --files uniform.json transpose.json --labels uniform transpose
"""

import json
import argparse
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

def load_json_files(file_paths):
    """Load data from JSON files."""
    data = []
    for path in file_paths:
        with open(path, 'r') as f:
            data.append(json.load(f))
    return data

def plot_vc_sweep(data, vc_values, output_path='vc_sweep.png'):
    """
    Plot VC sweep results.
    
    Args:
        data: List of metric dictionaries
        vc_values: List of VC counts (e.g., [1, 2, 4, 8])
        output_path: Output file path
    """
    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    
    # Extract metrics
    latencies = [d['latency'] for d in data]
    max_latencies = [d['max_latency'] for d in data]
    
    # Average latency
    axes[0].plot(vc_values, latencies, marker='o', linewidth=2.5, 
                markersize=10, color='#2E86AB', markerfacecolor='#A23B72')
    axes[0].set_xlabel('Number of Virtual Channels', fontsize=13, fontweight='bold')
    axes[0].set_ylabel('Average Latency (cycles)', fontsize=13, fontweight='bold')
    axes[0].set_title('VC Scaling - Average Latency', fontsize=15, fontweight='bold', pad=15)
    axes[0].grid(True, alpha=0.3, linestyle='--')
    axes[0].set_xticks(vc_values)
    
    # Add value labels on points
    for x, y in zip(vc_values, latencies):
        axes[0].annotate(f'{y:.1f}', (x, y), textcoords="offset points", 
                        xytext=(0,10), ha='center', fontsize=10)
    
    # Max latency
    axes[1].plot(vc_values, max_latencies, marker='s', linewidth=2.5, 
                markersize=10, color='#F18F01', markerfacecolor='#C73E1D')
    axes[1].set_xlabel('Number of Virtual Channels', fontsize=13, fontweight='bold')
    axes[1].set_ylabel('Maximum Latency (cycles)', fontsize=13, fontweight='bold')
    axes[1].set_title('VC Scaling - Maximum Latency', fontsize=15, fontweight='bold', pad=15)
    axes[1].grid(True, alpha=0.3, linestyle='--')
    axes[1].set_xticks(vc_values)
    
    # Add value labels
    for x, y in zip(vc_values, max_latencies):
        axes[1].annotate(f'{y:.0f}', (x, y), textcoords="offset points", 
                        xytext=(0,10), ha='center', fontsize=10)
    
    plt.tight_layout()
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"✓ Plot saved to: {output_path}")
    plt.close()

def plot_network_size(data, sizes, output_path='network_size.png'):
    """
    Plot latency vs network size for spanning tree scaling analysis.
    
    Args:
        data: List of metric dictionaries
        sizes: List of network sizes (number of nodes)
        output_path: Output file path
    """
    fig, axes = plt.subplots(1, 3, figsize=(18, 5))
    
    # Extract metrics
    avg_latencies = [d['latency'] for d in data]
    max_latencies = [d['max_latency'] for d in data]
    hops = [d['hops'] for d in data]
    
    # Average latency vs network size
    axes[0].plot(sizes, avg_latencies, marker='o', linewidth=2.5, 
                markersize=10, color='#2E86AB', markerfacecolor='#A23B72')
    axes[0].set_xlabel('Network Size (nodes)', fontsize=13, fontweight='bold')
    axes[0].set_ylabel('Average Latency (cycles)', fontsize=13, fontweight='bold')
    axes[0].set_title('Spanning Tree Scaling - Avg Latency', fontsize=15, fontweight='bold', pad=15)
    axes[0].grid(True, alpha=0.3, linestyle='--')
    
    for x, y in zip(sizes, avg_latencies):
        axes[0].annotate(f'{y:.1f}', (x, y), textcoords="offset points", 
                        xytext=(0,10), ha='center', fontsize=9)
    
    # Max latency vs network size
    axes[1].plot(sizes, max_latencies, marker='s', linewidth=2.5, 
                markersize=10, color='#F18F01', markerfacecolor='#C73E1D')
    axes[1].set_xlabel('Network Size (nodes)', fontsize=13, fontweight='bold')
    axes[1].set_ylabel('Maximum Latency (cycles)', fontsize=13, fontweight='bold')
    axes[1].set_title('Spanning Tree Scaling - Max Latency', fontsize=15, fontweight='bold', pad=15)
    axes[1].grid(True, alpha=0.3, linestyle='--')
    
    for x, y in zip(sizes, max_latencies):
        axes[1].annotate(f'{y:.0f}', (x, y), textcoords="offset points", 
                        xytext=(0,10), ha='center', fontsize=9)
    
    # Average hops vs network size
    axes[2].plot(sizes, hops, marker='^', linewidth=2.5, 
                markersize=10, color='#6A994E', markerfacecolor='#BC4B51')
    axes[2].set_xlabel('Network Size (nodes)', fontsize=13, fontweight='bold')
    axes[2].set_ylabel('Average Hops', fontsize=13, fontweight='bold')
    axes[2].set_title('Path Length Scaling', fontsize=15, fontweight='bold', pad=15)
    axes[2].grid(True, alpha=0.3, linestyle='--')
    
    for x, y in zip(sizes, hops):
        axes[2].annotate(f'{y:.2f}', (x, y), textcoords="offset points", 
                        xytext=(0,10), ha='center', fontsize=9)
    
    plt.tight_layout()
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"✓ Plot saved to: {output_path}")
    plt.close()

def plot_hops_analysis(data, labels, output_path='hops_analysis.png'):
    """
    Plot hop count analysis across different configurations or traffic patterns.
    Useful for comparing path efficiency.
    
    Args:
        data: List of metric dictionaries
        labels: List of labels for each configuration
        output_path: Output file path
    """
    fig, axes = plt.subplots(1, 2, figsize=(14, 6))
    
    hops = [d['hops'] for d in data]
    latencies = [d['latency'] for d in data]
    
    colors = ['#2E86AB', '#F18F01', '#A23B72', '#C73E1D', '#6A994E', '#BC4B51']
    
    # Bar chart of average hops
    x = np.arange(len(labels))
    bars = axes[0].bar(x, hops, color=colors[:len(labels)], 
                      edgecolor='black', linewidth=1.2, alpha=0.8)
    
    for bar, val in zip(bars, hops):
        height = bar.get_height()
        axes[0].text(bar.get_x() + bar.get_width()/2., height,
                    f'{val:.2f}', ha='center', va='bottom', fontsize=11, fontweight='bold')
    
    axes[0].set_xticks(x)
    axes[0].set_xticklabels(labels, rotation=45, ha='right', fontsize=11)
    axes[0].set_ylabel('Average Hops', fontsize=13, fontweight='bold')
    axes[0].set_title('Path Length Comparison', fontsize=15, fontweight='bold', pad=15)
    axes[0].grid(True, alpha=0.3, axis='y', linestyle='--')
    
    # Latency vs hops scatter
    axes[1].scatter(hops, latencies, s=200, c=colors[:len(labels)], 
                   edgecolors='black', linewidth=1.5, alpha=0.8)
    
    for i, label in enumerate(labels):
        axes[1].annotate(label, (hops[i], latencies[i]), 
                        textcoords="offset points", xytext=(5,5), 
                        ha='left', fontsize=10)
    
    axes[1].set_xlabel('Average Hops', fontsize=13, fontweight='bold')
    axes[1].set_ylabel('Average Latency (cycles)', fontsize=13, fontweight='bold')
    axes[1].set_title('Latency vs Path Length', fontsize=15, fontweight='bold', pad=15)
    axes[1].grid(True, alpha=0.3, linestyle='--')
    
    plt.tight_layout()
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"✓ Plot saved to: {output_path}")
    plt.close()

def plot_latency_distribution(data, labels, output_path='latency_distribution.png'):
    """
    Plot min/avg/max latency distribution across configurations.
    Shows latency ranges and variance.
    
    Args:
        data: List of metric dictionaries
        labels: List of labels for each configuration
        output_path: Output file path
    """
    fig, ax = plt.subplots(figsize=(12, 7))
    
    min_latencies = [d['min_latency'] for d in data]
    avg_latencies = [d['latency'] for d in data]
    max_latencies = [d['max_latency'] for d in data]
    
    x = np.arange(len(labels))
    width = 0.25
    
    colors = ['#6A994E', '#2E86AB', '#C73E1D']
    
    bars1 = ax.bar(x - width, min_latencies, width, label='Min', 
                   color=colors[0], edgecolor='black', linewidth=1, alpha=0.8)
    bars2 = ax.bar(x, avg_latencies, width, label='Average', 
                   color=colors[1], edgecolor='black', linewidth=1, alpha=0.8)
    bars3 = ax.bar(x + width, max_latencies, width, label='Max', 
                   color=colors[2], edgecolor='black', linewidth=1, alpha=0.8)
    
    # Add value labels
    for bars in [bars1, bars2, bars3]:
        for bar in bars:
            height = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2., height,
                   f'{height:.1f}', ha='center', va='bottom', fontsize=9)
    
    ax.set_xlabel('Configuration', fontsize=13, fontweight='bold')
    ax.set_ylabel('Latency (cycles)', fontsize=13, fontweight='bold')
    ax.set_title('Latency Distribution', fontsize=15, fontweight='bold', pad=15)
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=45, ha='right', fontsize=11)
    ax.legend(fontsize=12, framealpha=0.9)
    ax.grid(True, alpha=0.3, axis='y', linestyle='--')
    
    plt.tight_layout()
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"✓ Plot saved to: {output_path}")
    plt.close()

def plot_rate_sweep(data, rates, vc_labels, output_path='rate_sweep.png'):
    """
    Plot comprehensive rate sweep with avg/max latency and throughput.
    
    Args:
        data: List of metric dictionaries
        rates: List of injection rates for each data point
        vc_labels: List of VC counts corresponding to each data point
        output_path: Output file path
    """
    fig, axes = plt.subplots(1, 3, figsize=(18, 5))
    
    # Group data by VC count
    vc_groups = {}
    for d, rate, vc in zip(data, rates, vc_labels):
        if vc not in vc_groups:
            vc_groups[vc] = {'rates': [], 'avg_latencies': [], 'max_latencies': [], 'throughputs': []}
        vc_groups[vc]['rates'].append(rate)
        vc_groups[vc]['avg_latencies'].append(d['latency'])
        vc_groups[vc]['max_latencies'].append(d['max_latency'])
        vc_groups[vc]['throughputs'].append(d['accepted_rate'])
    
    colors = ['#2E86AB', '#A23B72', '#F18F01', '#C73E1D', '#6A994E', '#BC4B51']
    
    # Average Latency vs rate
    for i, (vc, group) in enumerate(sorted(vc_groups.items())):
        color = colors[i % len(colors)]
        axes[0].plot(group['rates'], group['avg_latencies'], marker='o', 
                    linewidth=2.5, markersize=8, label=f'{vc} VC{"s" if vc > 1 else ""}',
                    color=color)
    
    axes[0].set_xlabel('Injection Rate (packets/cycle/node)', fontsize=13, fontweight='bold')
    axes[0].set_ylabel('Average Latency (cycles)', fontsize=13, fontweight='bold')
    axes[0].set_title('Average Latency vs Load', fontsize=15, fontweight='bold', pad=15)
    axes[0].legend(fontsize=11, framealpha=0.9, loc='best')
    axes[0].grid(True, alpha=0.3, linestyle='--')
    
    # Maximum Latency vs rate
    for i, (vc, group) in enumerate(sorted(vc_groups.items())):
        color = colors[i % len(colors)]
        axes[1].plot(group['rates'], group['max_latencies'], marker='s', 
                    linewidth=2.5, markersize=8, label=f'{vc} VC{"s" if vc > 1 else ""}',
                    color=color)
    
    axes[1].set_xlabel('Injection Rate (packets/cycle/node)', fontsize=13, fontweight='bold')
    axes[1].set_ylabel('Maximum Latency (cycles)', fontsize=13, fontweight='bold')
    axes[1].set_title('Maximum Latency vs Load', fontsize=15, fontweight='bold', pad=15)
    axes[1].legend(fontsize=11, framealpha=0.9, loc='best')
    axes[1].grid(True, alpha=0.3, linestyle='--')
    
    # Throughput vs rate (saturation curve)
    for i, (vc, group) in enumerate(sorted(vc_groups.items())):
        color = colors[i % len(colors)]
        axes[2].plot(group['rates'], group['throughputs'], marker='D', 
                    linewidth=2.5, markersize=8, label=f'{vc} VC{"s" if vc > 1 else ""}',
                    color=color)
    
    # Add ideal line
    max_rate = max(rates)
    axes[2].plot([0, max_rate], [0, max_rate], 'k--', alpha=0.5, 
                linewidth=2, label='Ideal')
    
    axes[2].set_xlabel('Injection Rate (packets/cycle/node)', fontsize=13, fontweight='bold')
    axes[2].set_ylabel('Throughput (packets/cycle/node)', fontsize=13, fontweight='bold')
    axes[2].set_title('Saturation Curve', fontsize=15, fontweight='bold', pad=15)
    axes[2].legend(fontsize=11, framealpha=0.9, loc='best')
    axes[2].grid(True, alpha=0.3, linestyle='--')
    
    plt.tight_layout()
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"✓ Plot saved to: {output_path}")
    plt.close()

def plot_rate_sweep_avg(data, rates, vc_labels, output_path='rate_sweep_avg_latency.png'):
    """
    Plot ONLY average latency vs injection rate for multiple VC counts.
    Each VC count is a separate curve.
    
    Args:
        data: List of metric dictionaries
        rates: List of injection rates for each data point
        vc_labels: List of VC counts corresponding to each data point
        output_path: Output file path
    """
    fig, ax = plt.subplots(figsize=(10, 7))
    
    # Group data by VC count
    vc_groups = {}
    for d, rate, vc in zip(data, rates, vc_labels):
        if vc not in vc_groups:
            vc_groups[vc] = {'rates': [], 'avg_latencies': []}
        vc_groups[vc]['rates'].append(rate)
        vc_groups[vc]['avg_latencies'].append(d['latency'])
    
    # Color palette for up to 16 VCs
    colors = ['#2E86AB', '#A23B72', '#F18F01', '#C73E1D', '#6A994E', '#BC4B51',
              '#577590', '#F3722C', '#90BE6D', '#43AA8B', '#4D908E', '#277DA1',
              '#F94144', '#F8961E', '#F9C74F', '#90BE6D']
    markers = ['o', 's', '^', 'D', 'v', '<', '>', 'p', '*', 'h', 'H', '+', 'x', '|', '_', '.']
    
    # Plot each VC count as a separate curve
    for i, (vc, group) in enumerate(sorted(vc_groups.items())):
        color = colors[i % len(colors)]
        marker = markers[i % len(markers)]
        ax.plot(group['rates'], group['avg_latencies'], 
               marker=marker, linewidth=3, markersize=9, 
               label=f'{vc} VC{"s" if vc > 1 else ""}',
               color=color)
    
    ax.set_xlabel('Injection Rate (packets/cycle/node)', fontsize=15, fontweight='bold')
    ax.set_ylabel('Average Latency (cycles)', fontsize=15, fontweight='bold')
    ax.set_title('Average Latency vs Injection Rate', fontsize=17, fontweight='bold', pad=20)
    ax.legend(fontsize=12, framealpha=0.95, loc='best', ncol=2 if len(vc_groups) > 8 else 1)
    ax.grid(True, alpha=0.3, linestyle='--')
    ax.tick_params(labelsize=12)
    
    plt.tight_layout()
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"✓ Average latency plot saved to: {output_path}")
    plt.close()

def plot_rate_sweep_max(data, rates, vc_labels, output_path='rate_sweep_max_latency.png'):
    """
    Plot ONLY maximum latency vs injection rate for multiple VC counts.
    Each VC count is a separate curve.
    
    Args:
        data: List of metric dictionaries
        rates: List of injection rates for each data point
        vc_labels: List of VC counts corresponding to each data point
        output_path: Output file path
    """
    fig, ax = plt.subplots(figsize=(10, 7))
    
    # Group data by VC count
    vc_groups = {}
    for d, rate, vc in zip(data, rates, vc_labels):
        if vc not in vc_groups:
            vc_groups[vc] = {'rates': [], 'max_latencies': []}
        vc_groups[vc]['rates'].append(rate)
        vc_groups[vc]['max_latencies'].append(d['max_latency'])
    
    # Color palette for up to 16 VCs
    colors = ['#2E86AB', '#A23B72', '#F18F01', '#C73E1D', '#6A994E', '#BC4B51',
              '#577590', '#F3722C', '#90BE6D', '#43AA8B', '#4D908E', '#277DA1',
              '#F94144', '#F8961E', '#F9C74F', '#90BE6D']
    markers = ['o', 's', '^', 'D', 'v', '<', '>', 'p', '*', 'h', 'H', '+', 'x', '|', '_', '.']
    
    # Plot each VC count as a separate curve
    for i, (vc, group) in enumerate(sorted(vc_groups.items())):
        color = colors[i % len(colors)]
        marker = markers[i % len(markers)]
        ax.plot(group['rates'], group['max_latencies'], 
               marker=marker, linewidth=3, markersize=9,
               label=f'{vc} VC{"s" if vc > 1 else ""}',
               color=color)
    
    ax.set_xlabel('Injection Rate (packets/cycle/node)', fontsize=15, fontweight='bold')
    ax.set_ylabel('Maximum Latency (cycles)', fontsize=15, fontweight='bold')
    ax.set_title('Maximum Latency vs Injection Rate', fontsize=17, fontweight='bold', pad=20)
    ax.legend(fontsize=12, framealpha=0.95, loc='best', ncol=2 if len(vc_groups) > 8 else 1)
    ax.grid(True, alpha=0.3, linestyle='--')
    ax.tick_params(labelsize=12)
    
    plt.tight_layout()
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"✓ Maximum latency plot saved to: {output_path}")
    plt.close()

def plot_traffic_comparison(data, traffic_labels, output_path='traffic_comparison.png'):
    """
    Plot bar chart comparing different traffic patterns.
    
    Args:
        data: List of metric dictionaries
        traffic_labels: List of traffic pattern names
        output_path: Output file path
    """
    fig, ax = plt.subplots(figsize=(10, 6))
    
    latencies = [d['latency'] for d in data]
    colors = ['#2E86AB', '#F18F01', '#A23B72', '#C73E1D', '#6A994E', '#BC4B51']
    
    x = np.arange(len(traffic_labels))
    bars = ax.bar(x, latencies, color=colors[:len(traffic_labels)], 
                  edgecolor='black', linewidth=1.2, alpha=0.8)
    
    # Add value labels on bars
    for bar, val in zip(bars, latencies):
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2., height,
                f'{val:.1f}', ha='center', va='bottom', fontsize=11, fontweight='bold')
    
    ax.set_xticks(x)
    ax.set_xticklabels(traffic_labels, rotation=45, ha='right', fontsize=12)
    ax.set_ylabel('Average Latency (cycles)', fontsize=13, fontweight='bold')
    ax.set_title('Traffic Pattern Comparison', fontsize=15, fontweight='bold', pad=15)
    ax.grid(True, alpha=0.3, axis='y', linestyle='--')
    
    plt.tight_layout()
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"✓ Plot saved to: {output_path}")
    plt.close()

def plot_custom_comparison(data, x_values, x_label, y_metric='latency', 
                          output_path='custom_plot.png', title='Custom Comparison'):
    """
    Generic plotting function for any x-axis vs any metric.
    
    Args:
        data: List of metric dictionaries
        x_values: List of x-axis values
        x_label: Label for x-axis
        y_metric: Metric to plot ('latency', 'throughput', 'max_latency', etc.)
        output_path: Output file path
        title: Plot title
    """
    fig, ax = plt.subplots(figsize=(10, 6))
    
    y_values = [d[y_metric] for d in data]
    
    ax.plot(x_values, y_values, marker='o', linewidth=2.5, 
           markersize=10, color='#2E86AB', markerfacecolor='#A23B72')
    
    ax.set_xlabel(x_label, fontsize=13, fontweight='bold')
    ax.set_ylabel(y_metric.replace('_', ' ').title(), fontsize=13, fontweight='bold')
    ax.set_title(title, fontsize=15, fontweight='bold', pad=15)
    ax.grid(True, alpha=0.3, linestyle='--')
    
    # Add value labels
    for x, y in zip(x_values, y_values):
        ax.annotate(f'{y:.2f}', (x, y), textcoords="offset points", 
                   xytext=(0,10), ha='center', fontsize=10)
    
    plt.tight_layout()
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"✓ Plot saved to: {output_path}")
    plt.close()

def main():
    parser = argparse.ArgumentParser(
        description='Plot BookSim simulation results from JSON files',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # VC sweep (must provide VC labels in order)
  python plot_booksim_results.py --type vc \\
      --files sim1/metrics.json sim2/metrics.json sim3/metrics.json sim4/metrics.json \\
      --vc-labels 1 2 4 8 \\
      --output vc_sweep.png
  
  # Network size scaling
  python plot_booksim_results.py --type network-size \\
      --files n16.json n32.json n64.json n128.json \\
      --sizes 16 32 64 128 \\
      --output network_scaling.png
  
  # Hop analysis
  python plot_booksim_results.py --type hops \\
      --files uniform.json transpose.json tornado.json \\
      --labels uniform transpose tornado \\
      --output hops.png
  
  # Latency distribution
  python plot_booksim_results.py --type latency-dist \\
      --files config1.json config2.json config3.json \\
      --labels "Config 1" "Config 2" "Config 3" \\
      --output latency_dist.png
  
  # Rate sweep (provide rates and VCs for each file)
  python plot_booksim_results.py --type rate \\
      --files rate0.05_vc1.json rate0.1_vc1.json rate0.15_vc1.json rate0.05_vc4.json rate0.1_vc4.json \\
      --rates 0.05 0.1 0.15 0.05 0.1 \\
      --vc-labels 1 1 1 4 4 \\
      --output rate_sweep.png
  
  # Traffic comparison
  python plot_booksim_results.py --type traffic \\
      --files uniform.json transpose.json tornado.json \\
      --labels uniform transpose tornado \\
      --output traffic.png
        """
    )
    
    parser.add_argument('--type', choices=['vc', 'rate', 'rate-avg', 'rate-max', 'traffic', 'custom',
                                          'network-size', 'hops', 'latency-dist'],
                       required=True, help='Type of plot to generate')
    parser.add_argument('--files', nargs='+', required=True,
                       help='JSON files with metrics')
    parser.add_argument('--output', default=None,
                       help='Output file path (default: auto-named based on type)')
    
    # VC sweep options
    parser.add_argument('--vc-labels', type=int, nargs='+',
                       help='VC counts for each file (required for vc and rate types)')
    
    # Rate sweep options
    parser.add_argument('--rates', type=float, nargs='+',
                       help='Injection rates for each file (required for rate types)')
    
    # Network size options
    parser.add_argument('--sizes', type=int, nargs='+',
                       help='Network sizes (number of nodes) for each file (required for network-size type)')
    
    # Traffic comparison / hops analysis options
    parser.add_argument('--labels', nargs='+',
                       help='Labels for each file (required for traffic, hops, and latency-dist types)')
    
    # Custom plot options
    parser.add_argument('--x-values', type=float, nargs='+',
                       help='X-axis values for custom plot')
    parser.add_argument('--x-label', default='X-axis',
                       help='X-axis label for custom plot')
    parser.add_argument('--y-metric', default='latency',
                       help='Metric to plot on y-axis (latency, accepted_rate, max_latency, etc.)')
    parser.add_argument('--title', default='Custom Comparison',
                       help='Plot title for custom plot')
    
    args = parser.parse_args()
    
    # Validate inputs
    if args.type == 'vc' and not args.vc_labels:
        parser.error("--vc-labels required for vc type")
    if args.type in ['rate', 'rate-avg', 'rate-max'] and (not args.rates or not args.vc_labels):
        parser.error("--rates and --vc-labels required for rate types")
    if args.type == 'network-size' and not args.sizes:
        parser.error("--sizes required for network-size type")
    if args.type in ['traffic', 'hops', 'latency-dist'] and not args.labels:
        parser.error("--labels required for traffic, hops, and latency-dist types")
    if args.type == 'custom' and not args.x_values:
        parser.error("--x-values required for custom type")
    
    # Load data
    print(f"Loading {len(args.files)} JSON files...")
    data = load_json_files(args.files)
    
    # Set default output path
    if args.output is None:
        output_names = {
            'vc': 'vc_sweep.png',
            'rate': 'rate_sweep.png',
            'rate-avg': 'rate_sweep_avg.png',
            'rate-max': 'rate_sweep_max.png',
            'traffic': 'traffic_comparison.png',
            'network-size': 'network_size_scaling.png',
            'hops': 'hops_analysis.png',
            'latency-dist': 'latency_distribution.png',
            'custom': 'custom_plot.png'
        }
        args.output = output_names.get(args.type, f"{args.type}_plot.png")
    
    # Generate plot
    print(f"Generating {args.type} plot...")
    
    if args.type == 'vc':
        if len(data) != len(args.vc_labels):
            parser.error(f"Number of files ({len(data)}) must match number of VC labels ({len(args.vc_labels)})")
        plot_vc_sweep(data, args.vc_labels, args.output)
    
    elif args.type == 'network-size':
        if len(data) != len(args.sizes):
            parser.error(f"Number of files ({len(data)}) must match number of sizes ({len(args.sizes)})")
        plot_network_size(data, args.sizes, args.output)
    
    elif args.type == 'hops':
        if len(data) != len(args.labels):
            parser.error(f"Number of files ({len(data)}) must match number of labels ({len(args.labels)})")
        plot_hops_analysis(data, args.labels, args.output)
    
    elif args.type == 'latency-dist':
        if len(data) != len(args.labels):
            parser.error(f"Number of files ({len(data)}) must match number of labels ({len(args.labels)})")
        plot_latency_distribution(data, args.labels, args.output)
        
    elif args.type == 'rate':
        if len(data) != len(args.rates) or len(data) != len(args.vc_labels):
            parser.error(f"Number of files must match number of rates and VC labels")
        plot_rate_sweep(data, args.rates, args.vc_labels, args.output)
    
    elif args.type == 'rate-avg':
        if len(data) != len(args.rates) or len(data) != len(args.vc_labels):
            parser.error(f"Number of files must match number of rates and VC labels")
        plot_rate_sweep_avg(data, args.rates, args.vc_labels, args.output)
    
    elif args.type == 'rate-max':
        if len(data) != len(args.rates) or len(data) != len(args.vc_labels):
            parser.error(f"Number of files must match number of rates and VC labels")
        plot_rate_sweep_max(data, args.rates, args.vc_labels, args.output)
        
    elif args.type == 'traffic':
        if len(data) != len(args.labels):
            parser.error(f"Number of files ({len(data)}) must match number of labels ({len(args.labels)})")
        plot_traffic_comparison(data, args.labels, args.output)
        
    elif args.type == 'custom':
        if len(data) != len(args.x_values):
            parser.error(f"Number of files ({len(data)}) must match number of x-values ({len(args.x_values)})")
        plot_custom_comparison(data, args.x_values, args.x_label, 
                             args.y_metric, args.output, args.title)

if __name__ == '__main__':
    main()