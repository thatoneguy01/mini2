#!/usr/bin/env python3
"""
Visualize queue sizes from all 9 nodes on a single line graph.
Reads CSV log files with format: timestamp_us,node_id,queue_size
"""

import argparse
import glob
import sys
from collections import defaultdict

try:
    import matplotlib.pyplot as plt
except ImportError:
    print("Error: matplotlib is required. Install with: pip install matplotlib")
    sys.exit(1)


def read_log_file(filepath):
    """Read a log file and return dict of node_id -> [(timestamp_us, queue_size), ...]"""
    data = defaultdict(list)
    try:
        with open(filepath, "r") as f:
            header = f.readline().strip()
            if header != "timestamp_us,node_id,queue_size":
                print(f"Warning: unexpected header in {filepath}: {header}")

            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    timestamp_us, node_id, queue_size = line.split(",")
                    timestamp_us = int(timestamp_us)
                    queue_size = int(queue_size)
                    data[node_id].append((timestamp_us, queue_size))
                except ValueError:
                    print(f"Warning: skipping malformed line: {line}")
    except IOError as e:
        print(f"Error reading {filepath}: {e}")
    return data


def normalize_timestamps(all_data):
    """Convert timestamps to relative seconds from the earliest timestamp across all data."""
    all_timestamps = []
    for node_data in all_data.values():
        for timestamp_us, _ in node_data:
            all_timestamps.append(timestamp_us)

    if not all_timestamps:
        return all_data

    min_timestamp_us = min(all_timestamps)

    normalized = {}
    for node_id, data in all_data.items():
        normalized[node_id] = [(
            (timestamp_us - min_timestamp_us) / 1_000_000.0,  # Convert to seconds
            queue_size
        ) for timestamp_us, queue_size in data]

    return normalized


def plot_queue_sizes(all_data, output_file=None):
    """Plot queue sizes for all nodes on a single graph."""
    if not all_data:
        print("No data to plot.")
        return

    plt.figure(figsize=(12, 7))

    colors = {
        "A": "#FF6B6B",  # Red
        "B": "#4ECDC4",  # Teal
        "C": "#45B7D1",  # Blue
        "D": "#FFA07A",  # Light Salmon
        "E": "#98D8C8",  # Mint
        "F": "#F7DC6F",  # Yellow
        "G": "#BB8FCE",  # Purple
        "H": "#85C1E2",  # Sky Blue
        "I": "#F8B88B",  # Peach
    }

    for node_id in sorted(all_data.keys()):
        data = all_data[node_id]
        if data:
            timestamps_s, queue_sizes = zip(*data)
            color = colors.get(node_id, None)
            plt.plot(timestamps_s, queue_sizes, label=f"Node {node_id}", marker="o", markersize=3, color=color)

    plt.xlabel("Time (seconds)", fontsize=12)
    plt.ylabel("Queue Size (jobs)", fontsize=12)
    plt.title("Node Queue Sizes Over Time", fontsize=14, fontweight="bold")
    plt.legend(loc="best", fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()

    if output_file:
        plt.savefig(output_file, dpi=150)
        print(f"Graph saved to {output_file}")
    else:
        plt.show()


def main():
    parser = argparse.ArgumentParser(
        description="Visualize queue sizes from node log files"
    )
    parser.add_argument(
        "--log-dir",
        default="logs",
        help="Directory containing node_*.log files (default: logs)",
    )
    parser.add_argument(
        "--pattern",
        default="node_*.log",
        help="Glob pattern for log files (default: node_*.log)",
    )
    parser.add_argument(
        "--output",
        help="Save graph to file instead of displaying",
    )
    args = parser.parse_args()

    # Find log files
    log_pattern = f"{args.log_dir}/{args.pattern}"
    log_files = glob.glob(log_pattern)

    if not log_files:
        print(f"No log files found matching pattern: {log_pattern}")
        print(f"Try: python visualize_queues.py --log-dir . --pattern '*.log'")
        return

    print(f"Found {len(log_files)} log file(s)")

    # Read all log files
    all_data = {}
    for log_file in sorted(log_files):
        print(f"Reading {log_file}...")
        file_data = read_log_file(log_file)
        all_data.update(file_data)

    if not all_data:
        print("No data found in log files.")
        return

    # Normalize timestamps
    all_data = normalize_timestamps(all_data)

    # Print summary
    for node_id in sorted(all_data.keys()):
        data = all_data[node_id]
        if data:
            queue_sizes = [q for _, q in data]
            print(f"  Node {node_id}: {len(data)} samples, max queue = {max(queue_sizes)}")

    # Plot
    plot_queue_sizes(all_data, args.output)


if __name__ == "__main__":
    main()
