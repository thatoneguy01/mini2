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
    """Read a log file and return dict of node_id -> [(timestamp_us, queue_size, jobs_processed), ...]"""
    data = defaultdict(list)
    try:
        with open(filepath, "r") as f:
            header = f.readline().strip()
            # Support both old and new log formats
            expected_headers = [
                "timestamp_us,node_id,queue_size,jobs_processed",
                "timestamp_us,node_id,queue_size"
            ]
            if header not in expected_headers:
                print(f"Warning: unexpected header in {filepath}: {header}")

            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    parts = line.split(",")
                    timestamp_us = int(parts[0])
                    node_id = parts[1]
                    queue_size = int(parts[2])
                    # Handle new format with jobs_processed
                    jobs_processed = int(parts[3]) if len(parts) > 3 else 0
                    data[node_id].append((timestamp_us, queue_size, jobs_processed))
                except (ValueError, IndexError):
                    print(f"Warning: skipping malformed line: {line}")
    except IOError as e:
        print(f"Error reading {filepath}: {e}")
    return data


def find_active_window(all_data):
    """Find the time window where the cluster was active (any queue > 0 to all queues = 0)."""
    # Find first timestamp where any node has queue_size > 0
    first_active = None
    for node_data in all_data.values():
        for timestamp_us, queue_size, _ in node_data:
            if queue_size > 0:
                if first_active is None or timestamp_us < first_active:
                    first_active = timestamp_us

    if first_active is None:
        return None, None  # No activity detected

    # Find the last timestamp where all nodes are at 0 (after first_active)
    last_all_zero = None
    all_timestamps = set()
    for node_data in all_data.values():
        for timestamp_us, _, _ in node_data:
            if timestamp_us >= first_active:
                all_timestamps.add(timestamp_us)

    for timestamp_us in sorted(all_timestamps, reverse=True):
        all_zero = True
        for node_data in all_data.values():
            # Find the queue size at this timestamp (or closest before it)
            queue_size = 0
            for ts, qs, _ in sorted(node_data):
                if ts <= timestamp_us:
                    queue_size = qs
                else:
                    break
            if queue_size > 0:
                all_zero = False
                break
        if all_zero:
            last_all_zero = timestamp_us
            break

    if last_all_zero is None:
        last_all_zero = max(ts for node_data in all_data.values() for ts, _, _ in node_data)

    return first_active, last_all_zero


def normalize_timestamps(all_data):
    """Convert timestamps to relative seconds from the earliest timestamp across all data."""
    all_timestamps = []
    for node_data in all_data.values():
        for timestamp_us, _, _ in node_data:
            all_timestamps.append(timestamp_us)

    if not all_timestamps:
        return all_data

    min_timestamp_us = min(all_timestamps)

    normalized = {}
    for node_id, data in all_data.items():
        normalized[node_id] = [(
            (timestamp_us - min_timestamp_us) / 1_000_000.0,  # Convert to seconds
            queue_size,
            jobs_processed
        ) for timestamp_us, queue_size, jobs_processed in data]

    return normalized


def filter_by_active_window(all_data, first_active_us, last_active_us):
    """Filter data to only include the active window."""
    if first_active_us is None or last_active_us is None:
        return all_data

    filtered = {}
    for node_id, data in all_data.items():
        filtered[node_id] = [
            (timestamp_us, queue_size, jobs_processed)
            for timestamp_us, queue_size, jobs_processed in data
            if first_active_us <= timestamp_us <= last_active_us
        ]

    return filtered


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

    max_x = 0.0
    max_y = 0

    for node_id in sorted(all_data.keys()):
        data = all_data[node_id]
        if data:
            timestamps_s, queue_sizes, _ = zip(*data)
            max_x = max(max_x, max(timestamps_s))
            max_y = max(max_y, max(queue_sizes))
            color = colors.get(node_id, None)
            plt.plot(timestamps_s, queue_sizes, label=f"Node {node_id}", marker="o", markersize=3, color=color)

    plt.xlabel("Time (seconds)", fontsize=12)
    plt.ylabel("Queue Size (jobs)", fontsize=12)
    plt.title("Node Queue Sizes Over Time", fontsize=14, fontweight="bold")
    handles, labels = plt.gca().get_legend_handles_labels()
    if handles:
        plt.legend(loc="best", fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.xlim(left=0, right=max_x if max_x > 0 else 1)
    plt.ylim(bottom=0, top=max_y * 1.05 if max_y > 0 else 1)
    plt.tight_layout()

    if output_file:
        plt.savefig(output_file, dpi=150)
        print(f"Graph saved to {output_file}")
    else:
        plt.show()


def plot_jobs_processed(all_data, output_file=None):
    """Plot jobs processed for all nodes on a single graph."""
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

    max_x = 0.0
    max_y = 0

    for node_id in sorted(all_data.keys()):
        data = all_data[node_id]
        if data:
            timestamps_s, _, jobs_processed = zip(*data)
            max_x = max(max_x, max(timestamps_s))
            max_y = max(max_y, max(jobs_processed))
            color = colors.get(node_id, None)
            plt.plot(timestamps_s, jobs_processed, label=f"Node {node_id}", marker="o", markersize=3, color=color)

    plt.xlabel("Time (seconds)", fontsize=12)
    plt.ylabel("Jobs Processed (cumulative)", fontsize=12)
    plt.title("Jobs Processed Over Time", fontsize=14, fontweight="bold")
    handles, labels = plt.gca().get_legend_handles_labels()
    if handles:
        plt.legend(loc="best", fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.xlim(left=0, right=max_x if max_x > 0 else 1)
    plt.ylim(bottom=0, top=max_y * 1.05 if max_y > 0 else 1)
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

    # Find active window (first non-zero queue to all zeros)
    first_active_us, last_active_us = find_active_window(all_data)
    if first_active_us is None:
        print("No queue activity detected (all queues were 0).")
        return

    # Filter to active window
    all_data = filter_by_active_window(all_data, first_active_us, last_active_us)

    # Normalize timestamps after filtering so the active window starts at zero
    all_data = normalize_timestamps(all_data)

    # Print summary
    for node_id in sorted(all_data.keys()):
        data = all_data[node_id]
        if data:
            queue_sizes = [q for _, q, _ in data]
            jobs_processed = [j for _, _, j in data]
            print(f"  Node {node_id}: {len(data)} samples, max queue = {max(queue_sizes)}, final jobs = {jobs_processed[-1] if jobs_processed else 0}")

    # Plot both graphs
    if args.output:
        # Save to files with _queues and _jobs suffixes
        base_name = args.output.rsplit(".", 1)[0] if "." in args.output else args.output
        queue_output = f"{base_name}_queues.png"
        jobs_output = f"{base_name}_jobs.png"
        plot_queue_sizes(all_data, queue_output)
        plot_jobs_processed(all_data, jobs_output)
    else:
        # Display interactively
        plot_queue_sizes(all_data)
        plot_jobs_processed(all_data)


if __name__ == "__main__":
    main()
