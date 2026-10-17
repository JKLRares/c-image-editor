"""Copy an explicit source allowlist; never copy coursework fixtures or binaries."""
import argparse
from pathlib import Path, PurePosixPath
import shutil

ROOT = Path(__file__).resolve().parents[1]


def source_files():
    names = [line.strip() for line in (ROOT / 'tools/source-files.txt').read_text().splitlines()
             if line.strip() and not line.lstrip().startswith('#')]
    if len(names) != len(set(names)):
        raise ValueError('duplicate source allowlist entry')
    for name in names:
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError(f'invalid source path: {name}')
        source = ROOT / name
        if source.is_symlink() or not source.is_file():
            raise ValueError(f'missing/nonregular source file: {name}')
    return names


def export_source(destination):
    names = source_files()
    destination = Path(destination).resolve()
    if destination == ROOT or (destination.exists() and any(destination.iterdir())):
        raise ValueError('export requires a new or empty directory')
    destination.mkdir(parents=True, exist_ok=True)
    for name in names:
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, target)
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path)
    parser.add_argument('--list', action='store_true')
    args = parser.parse_args()
    if args.list:
        print('\n'.join(source_files()))
    elif args.destination:
        names = export_source(args.destination)
        print(f'Exported {len(names)} source files to {args.destination}')
    else:
        parser.error('provide --list or --destination')


if __name__ == '__main__':
    main()
