"""Build BookSim and check hop_age through complete network simulations.

Run from any directory: python3 /path/to/booksim2/scripts/test_hop_age.py
These are integration checks, not direct C++ class tests.
"""
from pathlib import Path
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'src'
OUT = ROOT / 'tests/hop_age/.build'
OUT.mkdir(parents=True, exist_ok=True)

def run(command, name, cwd=None):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
    (OUT / (name + '.log')).write_text(result.stdout + result.stderr)
    return result

result = run(['make', '-j4'], 'build', SRC)
if result.returncode:
    raise SystemExit('Build failed; see ' + str(OUT / 'build.log'))
report = []
for name, extra in [
    ('input-first', []),
    ('output-first', ['sw_allocator=separable_output_first(hop_age)']),
    ('tree', ['sw_allocator=separable_input_first(tree(5,hop_age))']),
]:
    result = run([str(SRC / 'booksim'), str(ROOT / 'configs/hop_age_mesh.cfg'), *extra], name)
    log = result.stdout + result.stderr
    good = (result.returncode == 255 and 'Overall Traffic Statistics' in result.stdout
            and 'Error' not in log and 'Aborting simulation' not in log)
    if not good:
        raise SystemExit('Network simulation failed: ' + name)
    report.append({'name': name, 'passed': True})
    print('PASS: network ' + name, flush=True)

for name, extra in [
    ('invalid-priority', ['priority=none']),
    ('invalid-donation', ['vc_priority_donation=1']),
    ('invalid-speculation', ['speculative=1']),
    ('invalid-hold', ['hold_switch_for_packet=1']),
    ('invalid-vc-allocator', ['vc_allocator=separable_input_first(hop_age)']),
]:
    result = run([str(SRC / 'booksim'), str(ROOT / 'configs/hop_age_mesh.cfg'), *extra], name)
    log = result.stdout + result.stderr
    expected = ('hop_age is supported for switch allocation only.' if name == 'invalid-vc-allocator'
                else 'hop_age requires priority=hop_count')
    if expected not in log or 'Overall Traffic Statistics' in log:
        raise SystemExit('Invalid configuration was not rejected: ' + name)
    report.append({'name': name, 'rejected': True})
    print('PASS: reject ' + name, flush=True)
(OUT / 'verification.json').write_text(json.dumps({
    'scope': 'Python integration checks through the BookSim executable; no direct C++ unit tests',
    'checks': report,
}, indent=2))
