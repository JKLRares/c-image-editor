"""Replay original inputs and grading rules without changing assignment assets."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'image_editor')
    parser.add_argument('--cases', nargs='*', help='Case numbers, e.g. 00 28 31')
    parser.add_argument('--report', type=Path)
    parser.add_argument('--valgrind', action='store_true')
    args = parser.parse_args()
    binary = args.binary.resolve()
    manifest = json.loads((ROOT / 'tests.json').read_text())
    wanted = set(args.cases) if args.cases else None
    results = []
    with tempfile.TemporaryDirectory(prefix='image-editor-assignment-') as temp:
        work = Path(temp)
        for name in ['input', 'ref', 'tests.json']:
            (work / name).symlink_to(ROOT / name)
        shutil.copyfile(ROOT / 'dummy', work / 'dummy')
        for name, spec in manifest.items():
            if wanted is not None and name.split('-')[0] not in wanted:
                continue
            output = work / 'output'
            if output.exists():
                shutil.rmtree(output)
            output.mkdir()
            stem = ROOT / 'tasks' / 'image_editor' / 'tests' / name / name
            command = [str(binary)]
            if args.valgrind:
                command = ['valgrind', '--leak-check=full', '--show-leak-kinds=all',
                           '--errors-for-leak-kinds=all', '--track-origins=yes',
                           '--error-exitcode=99', *command]
            started = time.monotonic()
            try:
                run = subprocess.run(command, input=stem.with_suffix('.in').read_bytes(),
                                     cwd=work, capture_output=True, timeout=180)
                # Match the grader's whitespace and blank-line policy.
                diff = subprocess.run(['diff', '-w', '-B', str(stem.with_suffix('.ref')), '-'],
                                      input=run.stdout, capture_output=True)
                images = subprocess.run([str(ROOT / 'image_check'), 'tests.json', name],
                                        cwd=work, capture_output=True, timeout=180)
                ok = run.returncode == 0 and diff.returncode == 0 and images.returncode == 0
                errors = []
                if run.returncode:
                    errors.append(f'editor exit {run.returncode}: {run.stderr.decode(errors="replace")}')
                if diff.returncode:
                    errors.append('stdout: ' + diff.stdout.decode(errors='replace'))
                if images.returncode:
                    errors.append('image checker: ' + images.stderr.decode(errors='replace'))
                if not args.valgrind and run.stderr:
                    ok = False
                    errors.append('unexpected stderr: ' + run.stderr.decode(errors='replace'))
                details = '\n'.join(errors)
            except subprocess.TimeoutExpired as error:
                ok, details = False, str(error)
            result = {'case': name, 'passed': ok, 'details': details,
                      'seconds': round(time.monotonic() - started, 3),
                      'reference_images': len(spec.get('files', []))}
            results.append(result)
            print(('PASS' if ok else 'FAIL'), name, spec.get('verbose_name', ''), flush=True)
            if details:
                print(details, flush=True)
    if not results:
        parser.error('no assignment cases selected')
    passed = sum(result['passed'] for result in results)
    print(f'{passed}/{len(results)} original cases passed using original stdout/image rules.')
    report = {'binary': str(binary), 'valgrind': args.valgrind,
              'passed': passed, 'total': len(results), 'cases': results}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
