"""Build/test an isolated source-only tree, including paths containing spaces."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from export_source import export_source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='image editor clean source ') as temp:
        export_source(Path(temp))
        subprocess.run(['make', '-j2', 'test', 'test-release'], cwd=temp, check=True)
        if args.sanitize:
            subprocess.run(['make', '-j2', 'test-sanitize'], cwd=temp, check=True)
    print('Clean source export builds and tests without assignment assets.')


if __name__ == '__main__':
    main()
