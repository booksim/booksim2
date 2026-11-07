"""Finite-window comparison; raw logs, config and per-seed metrics are retained."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import csv
import hashlib
import json
import math
import re
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'src'
POLICIES = {
    'hop_age': ('separable_input_first(hop_age)', 'hop_count'),
    'rr_hops': ('separable_input_first(round_robin)', 'hop_count'),
    'round_robin': ('separable_input_first(round_robin)', 'none'),
    'matrix_hops': ('separable_input_first(matrix)', 'hop_count'),
    'tree_rr_hops': ('separable_input_first(tree(5,round_robin))', 'hop_count'),
    'tree_matrix_hops': ('separable_input_first(tree(5,matrix))', 'hop_count'),
    'tree_hop_age': ('separable_input_first(tree(5,hop_age))', 'hop_count'),
    'oldest_first': ('separable_input_first(round_robin)', 'network_age'),
    'queue_length': ('separable_input_first(round_robin)', 'queue_length'),
    'islip': ('islip', 'none'),
}
SCENARIOS = {
    'uniform': ('uniform', [i / 100.0 for i in range(1, 21)]),
    'transpose': ('transpose', [i / 200.0 for i in range(1, 17)] + [.09, .10, .11, .12]),
    'hotspot': ('hotspot(0)', [i / 1000.0 for i in range(1, 21)]),
}

def array(text, name):
    matches = re.findall(re.escape(name) + r'\(1,:\)\s*=\s*\[([^\]]*)\]', text)
    if not matches:
        raise ValueError('Missing array: ' + name)
    return [float(x) for x in matches[-1].split()]

def scalar(text, name):
    matches = re.findall(re.escape(name) + r'\(1\)\s*=\s*([^;]+);', text)
    if not matches:
        raise ValueError('Missing scalar: ' + name)
    return float(matches[-1])

def percentile(hist, q):
    threshold, count = math.ceil(sum(hist) * q), 0
    for i, n in enumerate(hist):
        count += n
        if count >= threshold:
            return i, i == len(hist) - 1  # last bin contains overflow
    raise ValueError('Empty latency histogram')

def jain(values):
    return sum(values) ** 2 / (len(values) * sum(x*x for x in values))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--seeds', type=int, default=1)
    parser.add_argument('--period', type=int, default=3000)
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--plot-dir', type=Path, default=ROOT/'docs/comparison')
    parser.add_argument('--output', type=Path, default=ROOT/'tests/hop_age/.build/comparison-current')
    args = parser.parse_args()
    # Fail before running experiments if the plotting dependency is missing.
    import matplotlib
    matplotlib.use('Agg')
    if min(args.seeds, args.period, args.workers) < 1:
        parser.error('seeds, period and workers must be positive')
    args.output.mkdir(parents=True, exist_ok=True)
    config = '''topology = mesh;
k = 4;
n = 2;
routing_function = dim_order;
num_vcs = 4;
vc_buf_size = 8;
packet_size = 4;
vc_allocator = islip;
vc_priority_donation = 0;
speculative = 0;
hold_switch_for_packet = 0;
sim_type = latency;
warmup_periods = 2;
max_samples = 5;
stopping_thres = -1.0;
acc_stopping_thres = -1.0;
latency_thres = 500.0;
'''
    config_path = args.output / 'comparison.config'
    config_path.write_text(config)
    manifest = {'policies': POLICIES, 'scenarios': SCENARIOS,
                'seeds': list(range(1,args.seeds+1)), 'sample_period': args.period,
                'warmup_cycles': 2*args.period, 'measurement_cycles': 3*args.period,
                'binary_sha256': hashlib.sha256((SRC/'booksim').read_bytes()).hexdigest(),
                'note': 'Fixed finite windows, not a proof of steady-state convergence. '
                        'Same seed and workload parameters; backpressure can change injection timing. '
                        'No topology, routing, VC allocator or packet size changes across policies.'}
    (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2))

    jobs = [(policy, scenario, traffic, rate, seed)
            for scenario,(traffic,rates) in SCENARIOS.items() for rate in rates
            for seed in range(1,args.seeds+1) for policy in POLICIES]

    def execute(job):
        policy, scenario, traffic, rate, seed = job
        label = '{}-{}-{}-{}'.format(scenario,rate,seed,policy)
        stats_file = args.output/(label+'.m')
        allocator, priority = POLICIES[policy]
        command = [str(SRC/'booksim'),str(config_path), 'sample_period='+str(args.period),
                   'sw_allocator='+allocator, 'priority='+priority, 'traffic='+traffic,
                   'injection_rate='+str(rate), 'seed='+str(seed), 'stats_out='+str(stats_file)]
        row = {'policy':policy,'scenario':scenario,'rate':rate,'seed':seed}
        try:
            result = subprocess.run(command,capture_output=True,text=True,timeout=45)
            log = result.stdout+result.stderr
        except subprocess.TimeoutExpired as exc:
            log = (exc.stdout or b'').decode(errors='replace') if isinstance(exc.stdout,bytes) else (exc.stdout or '')
            (args.output/(label+'.log')).write_text(log)
            row['status']='timeout'; return row
        (args.output/(label+'.log')).write_text(log)
        if 'Aborting simulation' in log:
            row['status']='latency_threshold_abort'; return row
        if result.returncode != 255 or 'Overall Traffic Statistics' not in log or 'Error' in log:
            row['status']='failed'; return row
        try:
            stats = stats_file.read_text()
            hist = array(stats,'plat_hist')
            p95, cap95 = percentile(hist,.95)
            p99, cap99 = percentile(hist,.99)
            delivered = array(stats,'accepted_flits')
            sent = array(stats,'sent_flits')
            final = log.split('Overall Traffic Statistics',1)[1]
            maximum = float(re.search(r'Packet latency average.*?maximum = ([0-9.eE+\-]+)',final,re.S).group(1))
            row.update(status='completed', latency=scalar(stats,'plat'), max_latency=maximum,
                       p95=p95,p99=p99,p99_capped=cap99,p95_capped=cap95,
                       delivered_flits_per_cycle_node=statistics.mean(delivered),
                       source_admission_jain=jain(sent),samples=int(sum(hist)))
        except (ValueError,AttributeError,ZeroDivisionError) as exc:
            row.update(status='parse_error',error=str(exc))
        return row

    rows = []
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        for row in pool.map(execute,jobs):
            rows.append(row)
            if len(rows)%100==0:print('Completed {}/{} runs'.format(len(rows),len(jobs)),flush=True)
    (args.output/'runs.json').write_text(json.dumps(rows,indent=2,allow_nan=False))
    fields = sorted(set().union(*(r.keys() for r in rows)))
    with (args.output/'runs.csv').open('w',newline='') as file:
        writer=csv.DictWriter(file,fieldnames=fields);writer.writeheader();writer.writerows(rows)

    groups=[]
    for scenario,(_,rates) in SCENARIOS.items():
        for rate in rates:
            for policy in POLICIES:
                selected=[r for r in rows if r['scenario']==scenario and r['rate']==rate and r['policy']==policy]
                complete=[r for r in selected if r['status']=='completed']
                group={'scenario':scenario,'rate':rate,'policy':policy,
                       'completed':len(complete),'total':len(selected)}
                if complete:
                    for metric in ['latency','p95','p99','delivered_flits_per_cycle_node','source_admission_jain']:
                        group[metric]=statistics.mean(r[metric] for r in complete)
                    group['latency_std']=statistics.stdev(r['latency'] for r in complete) if len(complete)>1 else 0
                    group['worst_max_latency']=max(r['max_latency'] for r in complete)
                    group['p99_capped']=any(r['p99_capped'] for r in complete)
                groups.append(group)
    (args.output/'summary.json').write_text(json.dumps(groups,indent=2))
    plot_results(groups, args.seeds, args.plot_dir)
    print('Saved {} runs and {} aggregate rows to {}'.format(len(rows),len(groups),args.output),flush=True)

def plot_results(groups, seeds, output):
    import matplotlib.pyplot as plt
    output.mkdir(parents=True, exist_ok=True)
    styles = {
        'hop_age': ('Hops + earlier injection', '#182c36', 'o', '-'),
        'rr_hops': ('RR + hops', '#2374ab', 's', '-'),
        'round_robin': ('RR, equal priority', '#888888', 'x', '-'),
        'matrix_hops': ('Matrix + hops', '#35915b', 'v', '-'),
        'tree_rr_hops': ('Tree(RR) + hops', '#2374ab', '^', '--'),
        'tree_matrix_hops': ('Tree(Matrix) + hops', '#35915b', 'D', '--'),
        'tree_hop_age': ('Tree(Hops + injection time)', '#182c36', '*', '--'),
        'oldest_first': ('RR + network age', '#d48026', 'P', '-'),
        'queue_length': ('RR + VC length', '#8a5bad', '+', '-'),
        'islip': ('iSLIP allocator', '#cc5683', 'd', '-'),
    }
    plt.rcParams.update({'font.size': 12, 'axes.titlesize': 16, 'axes.labelsize': 13})
    metrics = [
        ('latency', 'latency', 'Mean packet latency (cycles)'),
        ('p99', 'p99', 'p99 packet latency (cycles)' if seeds==1 else 'Mean per-run p99 (cycles)'),
        ('delivered_flits_per_cycle_node', 'throughput', 'Accepted flits / cycle / node'),
    ]
    def draw(ax, metric, scenario, rates):
        handles = []
        for policy in POLICIES:
            label, color, marker, linestyle = styles[policy]
            rows = [next(g for g in groups if g['scenario']==scenario and g['rate']==rate
                         and g['policy']==policy) for rate in rates]
            valid = [g['completed']==g['total']==seeds and
                     not (metric=='p99' and g.get('p99_capped',False)) for g in rows]
            # NaNs leave gaps; never connect across an unmeasured/aborted point.
            values = [g[metric] if ok else math.nan for g,ok in zip(rows,valid)]
            errors = [g['latency_std'] if ok else math.nan for g,ok in zip(rows,valid)]
            artist = ax.errorbar(rates, values, yerr=errors if metric=='latency' and seeds>1 else None,
                label=label, color=color, marker=marker, linestyle=linestyle,
                linewidth=2.5 if policy=='hop_age' else 1.6, markersize=5, capsize=2)
            handles.append(artist)
        failed = [rate for rate in rates if any(g['scenario']==scenario and g['rate']==rate
                  and g['completed']!=g['total'] for g in groups)]
        if failed:
            ax.plot(failed, [.98]*len(failed), 'x', color='#b84a3a', markersize=6,
                    transform=ax.get_xaxis_transform())
        ax.set_title(scenario if scenario!='hotspot' else 'hotspot (node 0)')
        ax.set_xlabel('Offered packets / cycle / source')
        ax.set_ylabel(next(m[2] for m in metrics if m[0]==metric))
        ax.set_xlim(min(rates)*.8, max(rates)*1.03)
        ax.grid(alpha=.25)
        return handles
    footer = ('Mesh 4x4 | 4 VCs x 8 flits | 4-flit packets | '+str(seeds)+
              (' seed' if seeds==1 else ' seeds')+' | 20 offered loads per traffic pattern.\n'+
              ('Completed groups only; '+('single seed, no uncertainty estimate; ' if seeds==1 else 'latency bars: SD; ')+
               'x at top: some runs aborted (not a latency value).'))
    for metric, name, ylabel in metrics:
        fig, axes = plt.subplots(3, 1, figsize=(12, 19))
        for ax, (scenario, (_,rates)) in zip(axes, SCENARIOS.items()):
            handles=draw(ax, metric, scenario, rates)
        fig.suptitle('BookSim: '+ylabel, fontsize=20, y=.985)
        fig.legend(handles=handles, labels=[styles[p][0] for p in POLICIES], loc='lower center',
                   bbox_to_anchor=(.5,.035), ncol=3, frameon=False, fontsize=11)
        fig.text(.5,.009,footer,ha='center',fontsize=10,color='#555555')
        fig.subplots_adjust(left=.12,right=.97,top=.95,bottom=.15,hspace=.32)
        fig.savefig(output/(name+'.png'),dpi=180,facecolor='white')
        plt.close(fig)
    print('Saved exactly three PNG plots to '+str(output),flush=True)


if __name__=='__main__':main()
