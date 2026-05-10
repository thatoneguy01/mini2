import csv
import os
from statistics import mean, stdev
from collections import defaultdict

import matplotlib

matplotlib.use('Agg')
import matplotlib.pyplot as plt

LOG_ROOT = os.path.join(os.path.dirname(__file__), '..', 'logs')

def parse_process_ms(s):
    # '01' -> 0.1, '05'->0.5, '1'->1.0, '2'->2.0
    if s.isdigit() and len(s) == 2 and s.startswith('0'):
        return float(int(s) / 10.0)
    return float(s.replace('p', '.'))

def read_run(folder):
    path = os.path.join(LOG_ROOT, folder)
    nodes = []
    # find node_*.log
    for fn in os.listdir(path):
        if fn.startswith('node_') and fn.endswith('.log'):
            nodes.append(fn)
    if not nodes:
        return None

    node_samples = {}
    final_jobs = {}
    # read each node
    for fn in nodes:
        nid = fn.split('.')[0]
        ts = []
        qs = []
        last_jobs = 0
        file_path = os.path.join(path, fn)
        with open(file_path, 'r', newline='') as f:
            first = f.readline()
            # detect header
            f.seek(0)
            if 'timestamp_us' in first:
                r = csv.DictReader(f)
                for row in r:
                    if not row or 'timestamp_us' not in row or not row['timestamp_us']:
                        continue
                    try:
                        t = int(row['timestamp_us'])
                        q = int(row.get('queue_size', 0) or 0)
                        j = int(row.get('jobs_processed', 0) or 0)
                    except ValueError:
                        continue
                    ts.append(t)
                    qs.append(q)
                    last_jobs = j
            else:
                # no header, parse raw CSV rows
                f.seek(0)
                r2 = csv.reader(f)
                for row in r2:
                    if not row or len(row) < 4:
                        continue
                    try:
                        t = int(row[0])
                        q = int(row[2])
                        j = int(row[3])
                    except ValueError:
                        continue
                    ts.append(t)
                    qs.append(q)
                    last_jobs = j
        # store as dict for fast timestamp lookup
        node_samples[nid] = {t: q for t, q in zip(ts, qs)}
        final_jobs[nid] = last_jobs

    # build timestamp keyed queue-size lists (use timestamps present)
    ts_all = set()
    for samples in node_samples.values():
        for t in samples.keys():
            ts_all.add(t)
    ts_list = sorted(ts_all)

    # For each timestamp compute CV across available nodes
    cvs = []
    for t in ts_list:
        sizes = []
        for nid, samples in node_samples.items():
            q = samples.get(t, None)
            if q is not None:
                sizes.append(q)
        if not sizes:
            continue
        m = mean(sizes)
        if m == 0:
            cvs.append(0.0)
        else:
            if len(sizes) > 1:
                s = stdev(sizes)
            else:
                s = 0.0
            cvs.append(s / m)

    range_jobs = max(final_jobs.values()) - min(final_jobs.values())
    mean_queue_cv = mean(cvs) if cvs else 0.0

    return {
        'range_jobs': range_jobs,
        'mean_queue_cv': mean_queue_cv,
        'final_jobs': final_jobs,
    }

def parse_folder_name(folder):
    # test_{StealBelow}_{StealRatio}_{MaxSteal}_{ProcessMs}_{RebalanceMs}
    base = folder
    if not base.startswith('test_'):
        return None
    parts = base.split('_')
    if len(parts) < 6:
        return None
    sb = int(parts[1])
    sr = int(parts[2])
    ms = int(parts[3])
    pm = parse_process_ms(parts[4])
    rb = int(parts[5])
    return dict(StealBelow=sb, StealRatio=sr, MaxSteal=ms, ProcessMs=pm, RebalanceMs=rb)

def collect_runs():
    folders = [f for f in os.listdir(LOG_ROOT) if f.startswith('test_')]
    results = []
    for f in folders:
        meta = parse_folder_name(f)
        if meta is None:
            continue
        run = read_run(f)
        if run is None:
            continue
        meta.update(run)
        meta['folder'] = f
        results.append(meta)
    return results

def summarize(results):
    # 1) non-zero steal ratios: group by (StealRatio, RebalanceMs)
    grp_nr = defaultdict(list)
    for r in results:
        if r['StealRatio'] != 0:
            key = (r['StealRatio'], r['RebalanceMs'])
            grp_nr[key].append(r)

    nonzero_rows = []
    print('\nSummary: non-zero StealRatio by RebalanceMs')
    print('StealRatio,RebalanceMs,avg_range_jobs,avg_mean_queue_cv,count')
    for k in sorted(grp_nr.keys()):
        rows = grp_nr[k]
        avg_range = mean([x['range_jobs'] for x in rows])
        avg_cv = mean([x['mean_queue_cv'] for x in rows])
        nonzero_rows.append({'StealRatio': k[0], 'RebalanceMs': k[1], 'avg_range_jobs': avg_range, 'avg_mean_queue_cv': avg_cv, 'count': len(rows)})
        print(f"{k[0]},{k[1]},{avg_range:.2f},{avg_cv:.4f},{len(rows)}")

    # 2) StealRatio == 0: compare MaxSteal across RebalanceMs
    grp_z = defaultdict(list)
    for r in results:
        if r['StealRatio'] == 0:
            key = (r['MaxSteal'], r['RebalanceMs'])
            grp_z[key].append(r)

    fixed_rows = []
    print('\nSummary: StealRatio=0 (fixed-size) by MaxSteal & RebalanceMs')
    print('MaxSteal,RebalanceMs,avg_range_jobs,avg_mean_queue_cv,count')
    for k in sorted(grp_z.keys()):
        rows = grp_z[k]
        avg_range = mean([x['range_jobs'] for x in rows])
        avg_cv = mean([x['mean_queue_cv'] for x in rows])
        fixed_rows.append({'MaxSteal': k[0], 'RebalanceMs': k[1], 'avg_range_jobs': avg_range, 'avg_mean_queue_cv': avg_cv, 'count': len(rows)})
        print(f"{k[0]},{k[1]},{avg_range:.2f},{avg_cv:.4f},{len(rows)}")

    # 3) For StealRatio==0 find best StealBelow to MaxSteal ratio
    pair_scores = []
    for r in results:
        if r['StealRatio'] == 0:
            sb = r['StealBelow']
            ms = r['MaxSteal']
            # Use simple score: lower range_jobs and lower mean_queue_cv better
            score = r['range_jobs'] + (r['mean_queue_cv'] * 1000.0)
            pair_scores.append(((sb, ms), score, r))

    # group by (StealBelow, MaxSteal) and average score
    agg = defaultdict(list)
    for p, s, r in pair_scores:
        agg[p].append(s)
    scored = []
    for p, vals in agg.items():
        scored.append((p, mean(vals), len(vals)))
    scored.sort(key=lambda x: x[1])

    print('\nBest StealBelow/MaxSteal pairs (lower score better):')
    print('StealBelow,MaxSteal,avg_score,count')
    for p, sc, cnt in scored[:10]:
        print(f"{p[0]},{p[1]},{sc:.2f},{cnt}")

    return nonzero_rows, fixed_rows, scored

def _plot_metric(ax, rows, group_key, value_key, title, ylabel):
    groups = defaultdict(list)
    for row in rows:
        groups[row[group_key]].append(row)
    for group_value in sorted(groups.keys()):
        series = sorted(groups[group_value], key=lambda r: r['RebalanceMs'])
        xs = [r['RebalanceMs'] for r in series]
        ys = [r[value_key] for r in series]
        ax.plot(xs, ys, marker='o', linewidth=2, label=str(group_value))
    ax.set_title(title)
    ax.set_xlabel('RebalanceMs')
    ax.set_ylabel(ylabel)
    ax.grid(True, alpha=0.3)
    ax.legend(title=group_key)

def make_plots(nonzero_rows, fixed_rows):
    out_dir = os.path.join(os.path.dirname(__file__), '..', 'generated', 'analysis')
    os.makedirs(out_dir, exist_ok=True)

    # Figure 1: fractional steal ratios versus RebalanceMs
    fig, axes = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
    _plot_metric(
        axes[0],
        nonzero_rows,
        'StealRatio',
        'avg_range_jobs',
        'Final Jobs Range vs RebalanceMs (fractional steal ratios)',
        'Avg final jobs range',
    )
    _plot_metric(
        axes[1],
        nonzero_rows,
        'StealRatio',
        'avg_mean_queue_cv',
        'Mean Queue CV vs RebalanceMs (fractional steal ratios)',
        'Avg mean queue CV',
    )
    fig.tight_layout()
    frac_path = os.path.join(out_dir, 'fractional_steal_vs_rebalance.png')
    fig.savefig(frac_path, dpi=200)
    plt.close(fig)

    # Figure 2: fixed-size steal mode versus RebalanceMs
    fig, axes = plt.subplots(2, 1, figsize=(10, 8), sharex=True)
    _plot_metric(
        axes[0],
        fixed_rows,
        'MaxSteal',
        'avg_range_jobs',
        'Final Jobs Range vs RebalanceMs (StealRatio = 0)',
        'Avg final jobs range',
    )
    _plot_metric(
        axes[1],
        fixed_rows,
        'MaxSteal',
        'avg_mean_queue_cv',
        'Mean Queue CV vs RebalanceMs (StealRatio = 0)',
        'Avg mean queue CV',
    )
    fig.tight_layout()
    fixed_path = os.path.join(out_dir, 'fixed_size_steal_vs_rebalance.png')
    fig.savefig(fixed_path, dpi=200)
    plt.close(fig)

    print(f"\nSaved plots to {out_dir}")
    print(f"- {frac_path}")
    print(f"- {fixed_path}")

    return frac_path, fixed_path

def main():
    results = collect_runs()
    if not results:
        print('No runs found or no logs parsed.')
        return
    nonzero_rows, fixed_rows, _ = summarize(results)
    make_plots(nonzero_rows, fixed_rows)

if __name__ == '__main__':
    main()
