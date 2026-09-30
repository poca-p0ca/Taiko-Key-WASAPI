"""Summarize app-internal timing, never physical input-to-output latency. No third-party deps."""
import argparse
import csv
import math
from pathlib import Path
import re
import statistics


def summary(values):
    if not values:
        return 'no samples'
    ordered = sorted(values)
    return (f'n={len(values)} median={statistics.median(ordered):.4f} '
            f'p95={ordered[max(0, math.ceil(len(ordered) * .95) - 1)]:.4f} '
            f'min={ordered[0]:.4f} max={ordered[-1]:.4f}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path, help='Folder containing session.log and timings.csv')
    args = parser.parse_args()
    log = (args.folder / 'session.log').read_text(encoding='utf-8')
    match = re.search(r'QPC frequency=(\d+)', log)
    if not match:
        parser.error('QPC frequency missing; use the session.log from the same run')
    frequency = int(match[1])
    if frequency <= 0:
        parser.error('Invalid QPC frequency')
    groups = {}
    for name in ('timings.previous.csv', 'timings.csv'):
        path = args.folder / name
        if not path.exists():
            continue
        for row in csv.DictReader(path.open(encoding='utf-8', newline='')):
            data = groups.setdefault(int(row['generation']), dict(padding=[], render=[], interval=[], input=[]))
            if row['kind'] == '0':
                data['padding'].append(int(row['padding_frames']))
                data['render'].append(int(row['render_ticks']) * 1000 / frequency)
                if int(row['event_interval_ticks']):
                    data['interval'].append(int(row['event_interval_ticks']) * 1000 / frequency)
            elif row['kind'] == '1':
                data['input'].append(int(row['input_to_render_ticks']) * 1000 / frequency)
    print('Application internal measurements only. Input QPC is not physical key contact time.')
    for generation, data in sorted(groups.items()):
        print(f'\nGeneration {generation}')
        for key, values in data.items():
            unit = 'frames' if key == 'padding' else 'ms'
            print(f'  {key} ({unit}): {summary(values)}')
    if not groups:
        print('No audio-render records yet.')


if __name__ == '__main__':
    main()
