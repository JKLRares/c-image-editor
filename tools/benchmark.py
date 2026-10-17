"""Measure operations without I/O; compare deterministic results against Git P0."""
import argparse
import json
import os
from pathlib import Path
import platform
import shlex
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref', help='Optional commit containing the P0 monolith')
    parser.add_argument('--size', type=int, default=2048)
    parser.add_argument('--iterations', type=int, default=3)
    parser.add_argument('--trials', type=int, default=7)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if not (256 <= args.size <= 8192 and 1 <= args.iterations <= 99 and 1 <= args.trials <= 99):
        parser.error('size 256..8192, iterations/trials 1..99 required')
    compiler = shlex.split(os.environ.get('CC', 'gcc'))
    flags = ['-std=c99', '-O2', '-D_POSIX_C_SOURCE=200809L', '-I', str(ROOT / 'editor')]
    workloads = ['rotate_180', 'blur_128x128', 'blur_full']
    report = {'platform': platform.platform(), 'compiler': subprocess.check_output(
        [*compiler, '--version'], text=True).splitlines()[0], 'flags': flags,
        'baseline_ref': args.baseline_ref, 'size': args.size, 'iterations': args.iterations,
        'trials': args.trials, 'clock': 'CLOCK_PROCESS_CPUTIME_ID', 'results': {}}
    with tempfile.TemporaryDirectory(prefix='image-editor-benchmark-') as temp:
        work = Path(temp)
        current = work / 'current'
        sources = [ROOT / 'bench/operations.c', ROOT / 'editor/image.c', ROOT / 'editor/operations.c']
        subprocess.run([*compiler, *flags, *map(str, sources), '-o', str(current)], check=True)
        binaries = {'current': current}
        if args.baseline_ref:
            source = subprocess.check_output(['git', 'show', f'{args.baseline_ref}:image_editor.c'], cwd=ROOT)
            (work / 'p0.c').write_bytes(source)
            baseline = work / 'baseline'
            subprocess.run([*compiler, *flags, '-DBENCH_P0', '-I', str(work),
                            str(ROOT / 'bench/operations.c'), '-o', str(baseline)], check=True)
            binaries['baseline'] = baseline
        for operation, name in enumerate(workloads):
            samples = {version: [] for version in binaries}
            hashes = set()
            # Warm up; alternate order to reduce systematic run-order bias.
            for trial in range(args.trials + 1):
                versions = list(binaries)
                if trial % 2:
                    versions.reverse()
                for version in versions:
                    run = subprocess.check_output([str(binaries[version]), str(args.size),
                                                   str(args.iterations), str(operation)], text=True)
                    elapsed, fingerprint = run.split()
                    hashes.add(fingerprint)
                    if trial:
                        samples[version].append(float(elapsed))
            if len(hashes) != 1:
                raise RuntimeError(f'Output mismatch for {name}: {hashes}')
            medians = {version: statistics.median(values) for version, values in samples.items()}
            result = {'seconds_per_operation': medians, 'raw_samples': samples,
                      'output_fingerprint': next(iter(hashes))}
            if 'baseline' in medians:
                result['baseline_over_current'] = medians['baseline'] / medians['current']
            report['results'][name] = result
            print(name, json.dumps(medians), 'matching output hash', flush=True)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
