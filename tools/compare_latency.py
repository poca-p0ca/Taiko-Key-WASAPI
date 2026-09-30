"""Compare independent keyboard receipt -> WASAPI loopback onsets, not physical latency."""
import argparse
import csv
import json
import math
from pathlib import Path
import numpy as np


def stats(values):
    if not values:
        return {'n': 0}
    v = np.sort(values)
    return {'n': len(values), 'median_ms': float(np.median(v)),
            'p95_ms': float(v[math.ceil(len(v) * .95) - 1]),
            'min_ms': float(v[0]), 'max_ms': float(v[-1])}


def analyze(folder, fraction=.01):
    folder = Path(folder)
    meta = dict(line.split('=', 1) for line in (folder/'metadata.txt').read_text(encoding='utf-8').splitlines() if '=' in line)
    rate, frequency, frames = int(meta['sample_rate']), int(meta['qpc_frequency']), int(meta['frames'])
    if rate <= 0 or frequency <= 0 or frames <= 0:
        raise ValueError(f'{folder}: no valid audio capture')
    if int(meta.get('input_overflow', 0)) or int(meta.get('capacity_reached', 0)):
        raise ValueError(f'{folder}: collector overflow; repeat the run')
    pcm = np.memmap(folder/'stereo.f32', dtype='<f4', mode='r')
    if pcm.size != frames * 2 or not np.all(np.isfinite(pcm)):
        raise ValueError(f'{folder}: invalid PCM file')
    envelope = np.maximum(np.abs(pcm[0::2]), np.abs(pcm[1::2]))
    times = np.empty(frames, dtype=np.float64)
    valid = np.ones(frames, dtype=bool)
    boundaries = []
    packet_ranges = []
    expected_first = 0
    previous_end = None
    for row in csv.DictReader((folder/'packets.csv').read_text(encoding='utf-8').splitlines()):
        start, count, flags = int(row['first_frame']), int(row['frames']), int(row['flags'])
        if start != expected_first or count <= 0 or start + count > frames:
            raise ValueError(f'{folder}: inconsistent packet index')
        at = int(row['qpc_100ns']) / 1e7
        times[start:start+count] = at + np.arange(count)/rate
        valid[start:start+count] = (flags & 5) == 0  # discontinuity | timestamp error
        packet_ranges.append((start, start + count))
        if previous_end is not None and abs(at - previous_end) > 2/rate:
            boundaries.append((min(at, previous_end), max(at, previous_end)))
        previous_end = at + count/rate
        expected_first += count
    if expected_first != frames:
        raise ValueError(f'{folder}: missing audio timestamps; no numeric comparison')
    differences = np.diff(times)
    backsteps = np.flatnonzero(differences <= 0)
    if len(backsteps) and np.min(differences[backsteps]) < -1/rate:
        raise ValueError(f'{folder}: audio timestamp reversal exceeds one sample; no numeric comparison')
    # Independent packet timestamps have small clock jitter. A boundary can overlap
    # the previous packet by a fraction of a sample. Exclude BOTH entire packets;
    # make only the search index monotonic. No accepted onset is retimestamped.
    packet_starts = np.array([a for a, _ in packet_ranges])
    for backstep in backsteps:
        packet = int(np.searchsorted(packet_starts, backstep, side='right') - 1)
        a = packet_ranges[packet][0]
        b = packet_ranges[min(packet + 1, len(packet_ranges) - 1)][1]
        valid[a:b] = False
    if len(backsteps):
        np.maximum.accumulate(times, out=times)
    key_rows = list(csv.DictReader((folder/'keys.csv').read_text(encoding='utf-8').splitlines()))
    keys = np.array([int(k['qpc'])/frequency for k in key_rows])
    if np.any(np.diff(keys) <= 0):
        raise ValueError(f'{folder}: non-monotonic key timestamps')
    samples = []
    for index, key_time in enumerate(keys):
        row = {'sequence': int(key_rows[index]['sequence']), 'latency_ms': None, 'reason': ''}
        before, after = key_time-.03, key_time+.45
        a, k, b = np.searchsorted(times, [before, key_time, after])
        if (index and key_time-keys[index-1] < .65) or (index+1 < len(keys) and keys[index+1]-key_time < .65):
            row['reason'] = 'keys_too_close'
        elif a == 0 or b >= frames or k <= a or b <= k:
            row['reason'] = 'incomplete_window'
        elif not np.all(valid[a:b]) or any(x < after and y > before for x, y in boundaries):
            row['reason'] = 'capture_discontinuity_or_timestamp_error'
        else:
            peak = float(np.max(envelope[k:b]))
            threshold = max(peak*fraction, 1e-5)
            if peak < .0005:
                row['reason'] = 'no_audible_signal'
            elif np.max(envelope[a:k]) >= threshold * .5:
                row['reason'] = 'preexisting_audio_or_tail'
            else:
                hits = np.flatnonzero(envelope[k:b] >= threshold)
                if not len(hits):
                    row['reason'] = 'onset_not_found'
                else:
                    onset = k + int(hits[0])
                    row['latency_ms'] = float((times[onset]-key_time)*1000)
                    row['peak'] = peak
                    row['threshold'] = threshold
        samples.append(row)
    accepted = [s['latency_ms'] for s in samples if s['latency_ms'] is not None]
    rejected = {}
    for s in samples:
        if s['reason']:
            rejected[s['reason']] = rejected.get(s['reason'], 0) + 1
    return {'label': meta['label'], 'folder': str(folder), 'endpoint': meta['endpoint'],
            'sample_rate': rate, 'period_before': meta['period_before'], 'period_after': meta['period_after'],
            'timestamp_boundary_overlaps': int(len(backsteps)),
            'max_timestamp_reversal_us': float(max(0, -np.min(differences)) * 1e6),
            'threshold_fraction': fraction, 'stats': stats(accepted), 'rejected': rejected, 'samples': samples}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folders', nargs='+', type=Path, help='A1, B1, optionally A2 in that order')
    parser.add_argument('--output', type=Path, default=Path('latency-comparison.json'))
    args = parser.parse_args()
    runs = [analyze(p) for p in args.folders]
    identity = {(r['endpoint'], r['sample_rate']) for r in runs}
    if len(identity) != 1:
        parser.error('Endpoints/sample rates differ; repeat with identical output settings')
    print('Raw Input receipt -> endpoint loopback timestamp; NOT physical key-to-headphone latency.')
    print('Keep the same volume, sound, quiet background, device and workload across A/B/A.')
    comparisons = []
    for run in runs:
        s = run['stats']
        print(f"\n{run['label']}: accepted {s['n']}, rejected {run['rejected']}")
        if s['n']:
            print(f"  median {s['median_ms']:.3f} ms; p95 {s['p95_ms']:.3f} ms; min/max {s['min_ms']:.3f}/{s['max_ms']:.3f} ms")
        print(f"  engine observation: {run['period_before']} -> {run['period_after']}")
        if s['n'] < 100:
            print('  Fewer than 100 accepted hits: collect more for a stable comparison.')
        # Repeat at 2% and 5% attack thresholds; large shifts expose detection sensitivity.
        run['threshold_sensitivity'] = {str(f): analyze(run['folder'], f)['stats'] for f in (.02, .05)}
    if len(runs) > 1:
        reference = runs[1]
        for old in runs[::2]:
            if min(old['stats']['n'], reference['stats']['n']) < 30:
                print(f"\n{old['label']} minus {reference['label']}: insufficient accepted hits (need >=30; target >=100)")
                continue
            delta = old['stats']['median_ms'] - reference['stats']['median_ms']
            p95 = old['stats']['p95_ms'] - reference['stats']['p95_ms']
            comparisons.append({'baseline': old['label'], 'candidate': reference['label'],
                                'median_difference_ms': delta, 'p95_difference_ms': p95})
            print(f"\n{old['label']} minus {reference['label']}: median difference {delta:+.3f} ms; p95 difference {p95:+.3f} ms")
            print('  Positive means the candidate was earlier at the loopback observation point.')
    args.output.write_text(json.dumps({'measurement_scope': __doc__, 'runs': runs, 'comparisons': comparisons}, indent=2), encoding='utf-8')
    print(f'\nSaved auditable samples and exclusions: {args.output}')


if __name__ == '__main__':
    main()
