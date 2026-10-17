"""Compare valid deterministic editing pipelines with a preserved P0 executable."""
import argparse
from pathlib import Path
import random
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    args = parser.parse_args()
    baseline, candidate = args.baseline.resolve(), args.candidate.resolve()
    rng = random.Random(313)
    checked = 0
    with tempfile.TemporaryDirectory(prefix='image-editor-equivalence-') as temp:
        root = Path(temp)
        for index in range(32):
            width, height = 3 + index % 7, 3 + (index * 3) % 7
            magic = ['P2', 'P3', 'P5', 'P6'][index % 4]
            color = magic in ['P3', 'P6']
            maximum = 15 if index % 3 == 0 else 255
            values = [rng.randrange(maximum + 1) for _ in range(width * height * (3 if color else 1))]
            raster = bytes(values) if magic in ['P5', 'P6'] else ' '.join(map(str, values)).encode()
            fixture = f'{magic}\n{width} {height}\n{maximum}\n'.encode() + raster
            commands = ['LOAD fixture', 'SAVE before ascii', 'SELECT 1 1 3 3', 'ROTATE -90']
            if color:
                commands += ['APPLY SHARPEN', 'APPLY EDGE', 'APPLY BLUR', 'APPLY GAUSSIAN_BLUR']
            else:
                commands += ['HISTOGRAM 8 4', 'EQUALIZE']
            commands += ['SAVE selected ascii', 'SELECT ALL', 'ROTATE 180', 'ROTATE 270',
                         'SAVE rotated', 'SELECT 2 2 0 0', 'CROP', 'SAVE cropped ascii', 'EXIT']
            outputs = []
            for version, binary in [('p0', baseline), ('p1', candidate)]:
                work = root / f'{index}-{version}'
                work.mkdir()
                (work / 'fixture').write_bytes(fixture)
                run = subprocess.run([str(binary)], input=('\n'.join(commands) + '\n').encode(),
                                     cwd=work, capture_output=True, timeout=20, check=True)
                if run.stderr:
                    raise RuntimeError(run.stderr.decode(errors='replace'))
                outputs.append((run.stdout, [(work / name).read_bytes() for name in
                                            ['before', 'selected', 'rotated', 'cropped']]))
            if outputs[0] != outputs[1]:
                raise RuntimeError(f'P0/P1 mismatch in pipeline {index}')
            checked += 1
    print(f'{checked}/32 P0/P1 pipelines have identical stdout and saved bytes (128 images).')


if __name__ == '__main__':
    main()
