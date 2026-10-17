"""Independent CLI regressions in temporary directories; standard library only."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'image_editor'
FAULT_BINARY = ROOT / 'build/debug/fault-injection'
VALGRIND = False


def decode_plain(path):
    parts = path.read_bytes().split()
    return (parts[0], int(parts[1]), int(parts[2]), int(parts[3]),
            list(map(int, parts[4:])))


class EditorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='image-editor-test-')
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.write('tiny.pgm', b'P2\n3 2\n255\n1 2 3 4 5 6\n')

    def write(self, name, data):
        (self.work / name).write_bytes(data)

    def run_editor(self, commands, **kwargs):
        if isinstance(commands, str):
            commands = commands.encode()
        command = [str(BINARY)]
        if VALGRIND:
            command = ['valgrind', '--quiet', '--leak-check=full',
                       '--show-leak-kinds=all', '--errors-for-leak-kinds=all',
                       '--track-origins=yes', '--error-exitcode=99', *command]
        result = subprocess.run(command, input=commands,
                                cwd=self.work, capture_output=True,
                                timeout=15, **kwargs)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        self.assertEqual(result.stderr, b'')
        return result.stdout.decode()

    def test_invalid_numeric_commands_do_not_mutate(self):
        bad = ['SELECT - 0 1 1', 'SELECT 0 0 1-2 1', 'ROTATE abc',
               'ROTATE 90junk', 'HISTOGRAM 10 abc', 'HISTOGRAM 10 2junk',
               'SELECT 0 0 9999999999999999999999 1', 'ROTATE 2147483648']
        text = self.run_editor('LOAD tiny.pgm\n' + '\n'.join(bad) +
                               '\nSAVE check.pgm ascii\nEXIT\n')
        self.assertEqual(text.count('Invalid command\n'), len(bad))
        self.assertEqual(decode_plain(self.work / 'check.pgm')[-1], [1, 2, 3, 4, 5, 6])

    def test_exact_command_names_and_argument_counts(self):
        bad = ['LOAD', 'LOAD tiny.pgm extra', 'SAVE', 'SAVE out binary',
               'SAVE out ascii extra', 'SELECT ALLjunk', 'SELECT 0 0',
               'CROPjunk', 'CROP extra', 'EQUALIZE extra', 'ROTATE',
               'ROTATE 90 extra', 'EXITjunk', 'EXIT extra', 'HISTOGRAM']
        text = self.run_editor('LOAD tiny.pgm\n' + '\n'.join(bad) +
                               '\nSAVE check.pgm ascii\nEXIT\n')
        self.assertEqual(text.count('Invalid command\n'), len(bad))
        self.assertEqual(decode_plain(self.work / 'check.pgm')[-1], [1, 2, 3, 4, 5, 6])

    def test_filter_names_are_exact(self):
        text = self.run_editor('LOAD tiny.pgm\nAPPLY BLURjunk\nAPPLY\nEXIT\n')
        self.assertIn('APPLY parameter invalid\nInvalid command\n', text)

    def test_oversized_line_is_drained(self):
        text = self.run_editor('LOAD tiny.pgm\n' + 'CROP' + ' ' * 600 +
                               'EXIT\nSAVE check.pgm ascii\nEXIT\n')
        self.assertEqual(text.count('Invalid command\n'), 1)
        self.assertNotIn('Image cropped', text)
        self.assertTrue((self.work / 'check.pgm').exists())

    def test_embedded_nul_is_rejected(self):
        text = self.run_editor(b'LOAD tiny.pgm\nCROP\0extra\nEXIT\n')
        self.assertIn('Invalid command', text)
        self.assertNotIn('Image cropped', text)

    def test_whitespace_and_unterminated_final_command(self):
        text = self.run_editor(' \tLOAD\ttiny.pgm\r\nSELECT\tALL\nSAVE check.pgm ascii')
        self.assertIn('Selected ALL', text)
        self.assertTrue((self.work / 'check.pgm').exists())

    def test_comments_in_plain_headers_and_samples(self):
        self.write('comments.ppm', b'P3 # kind\n# dims\n2\t1\n15 # max\n'
                   b'0 1 # pixel\n2 13 14 15 # end')
        text = self.run_editor('LOAD comments.ppm\nSAVE check.ppm ascii\nEXIT\n')
        self.assertIn('Loaded comments.ppm', text)
        self.assertEqual(decode_plain(self.work / 'check.ppm'),
                         (b'P3', 2, 1, 15, [0, 1, 2, 13, 14, 15]))

    def test_binary_first_byte_is_never_skipped(self):
        for magic, suffix in [(b'P5', 'pgm'), (b'P6', 'ppm')]:
            for first in [0, 9, 10, 13, 32, 35, 255]:
                with self.subTest(magic=magic, first=first):
                    samples = bytes([first]) if magic == b'P5' else bytes([first, 10, 35])
                    self.write('raw.' + suffix, magic + b'\n1 1\n255\n' + samples)
                    self.run_editor(f'LOAD raw.{suffix}\nSAVE check.{suffix} ascii\nEXIT\n')
                    self.assertEqual(decode_plain(self.work / ('check.' + suffix))[-1],
                                     list(samples))

    def test_malformed_images_are_not_loaded(self):
        malformed = [b'P1\n1 1\n0', b'P2\n', b'P2\n0 1\n255\n',
                     b'P2\n-1 1\n255\n', b'P2\n1 1\n0\n0',
                     b'P5\n1 1\n256\n\0\0', b'P2\n1 1\n65535\n2',
                     b'P2\n2147483647 2147483647\n255\n',
                     b'P2\n999999999999999999999 1\n255\n',
                     b'P2\n2 1\n255\n1', b'P3\n1 1\n255\n1 2',
                     b'P5\n2 1\n255\n\x01', b'P6\n1 1\n255\n\x01\x02',
                     b'P2\n1 1\n15\n16', b'P5\n1 1\n15\n\x10',
                     b'P2\n1 1\n255\n-1', b'P2\n1 1\n255\n1junk',
                     b'P2\n1 1\n255\n1 2', b'P5\n1 1\n255']
        for data in malformed:
            with self.subTest(data=data):
                self.write('bad.pnm', data)
                text = self.run_editor('LOAD tiny.pgm\nLOAD bad.pnm\nCROP\nEXIT\n')
                self.assertIn('Failed to load bad.pnm\nNo image loaded\n', text)
                self.assertNotIn('Loaded bad.pnm', text)

    def test_eof_and_exit_are_clean(self):
        for commands in ['LOAD tiny.pgm\n', 'LOAD tiny.pgm\nEXIT\n', '', 'EXIT']:
            with self.subTest(commands=commands):
                self.run_editor(commands)

    def test_rejected_rectangle_does_not_change_pixels(self):
        text = self.run_editor('LOAD tiny.pgm\nSELECT 0 1 3 2\nROTATE 90\n'
                               'SAVE check.pgm ascii\nEXIT\n')
        self.assertIn('The selection must be square\n', text)
        self.assertNotIn('Rotated 90', text)
        self.assertEqual(decode_plain(self.work / 'check.pgm')[-1], [1, 2, 3, 4, 5, 6])

    def test_reload_resets_selection(self):
        self.run_editor('LOAD tiny.pgm\nSELECT 0 0 2 2\nLOAD tiny.pgm\n'
                        'ROTATE 90\nSAVE check.pgm ascii\nEXIT\n')
        self.assertEqual(decode_plain(self.work / 'check.pgm'),
                         (b'P2', 2, 3, 255, [4, 1, 5, 2, 6, 3]))

    def test_full_rotation_mappings(self):
        expected = {90: (2, 3, [4, 1, 5, 2, 6, 3]),
                    180: (3, 2, [6, 5, 4, 3, 2, 1]),
                    270: (2, 3, [3, 6, 2, 5, 1, 4])}
        for color in [False, True]:
            channels = 3 if color else 1
            pixels = [component for v in range(1, 7)
                      for component in ([v, v + 10, v + 20] if color else [v])]
            self.write('rotate.pnm', (f"P{3 if color else 2}\n3 2\n255\n" +
                                     ' '.join(map(str, pixels))).encode())
            for angle, (width, height, values) in expected.items():
                for equivalent in [angle, angle - 360, angle + 360]:
                    with self.subTest(color=color, angle=equivalent):
                        self.run_editor(f'LOAD rotate.pnm\nSELECT 0 0 3 2\nROTATE {equivalent}\n'
                                        'SAVE check.pnm ascii\nEXIT\n')
                        result = decode_plain(self.work / 'check.pnm')
                        self.assertEqual(result[1:3], (width, height))
                        self.assertEqual(result[-1], [component for v in values for component in
                                                      ([v, v + 10, v + 20] if channels == 3 else [v])])

    def test_square_rotation_and_exterior(self):
        self.write('square.pgm', b'P2\n4 3\n255\n1 2 3 4 5 6 7 8 9 10 11 12')
        self.run_editor('LOAD square.pgm\nSELECT 1 0 3 2\nROTATE 90\n'
                        'SAVE check.pgm ascii\nEXIT\n')
        self.assertEqual(decode_plain(self.work / 'check.pgm')[-1],
                         [1, 6, 2, 4, 5, 7, 3, 8, 9, 10, 11, 12])

    def test_crop_coordinate_normalization_and_reset(self):
        self.run_editor('LOAD tiny.pgm\nSELECT 3 2 1 0\nCROP\nROTATE 90\n'
                        'SAVE check.pgm ascii\nEXIT\n')
        self.assertEqual(decode_plain(self.work / 'check.pgm'),
                         (b'P2', 2, 2, 255, [5, 2, 6, 3]))

    def test_save_open_and_buffered_write_failures(self):
        text = self.run_editor('LOAD tiny.pgm\nSAVE absent/out.pgm\n'
                               'SAVE check.pgm ascii\nEXIT\n')
        self.assertIn('Failed to save absent/out.pgm\n', text)
        self.assertNotIn('Saved absent/out.pgm', text)
        self.assertTrue((self.work / 'check.pgm').exists())
        if Path('/dev/full').exists():
            text = self.run_editor('LOAD tiny.pgm\nSAVE /dev/full\nEXIT\n')
            self.assertIn('Failed to save /dev/full\n', text)
            self.assertNotIn('Saved /dev/full', text)

    def test_binary_and_ascii_roundtrips(self):
        for magic, channels in [('P2', 1), ('P3', 3), ('P5', 1), ('P6', 3)]:
            with self.subTest(magic=magic):
                values = [0, 1, 7, 10, 15, 2] * channels
                raster = (' '.join(map(str, values)).encode() if magic in ['P2', 'P3']
                          else bytes(values))
                self.write('original', f'{magic}\n3 2\n15\n'.encode() + raster)
                self.run_editor('LOAD original\nSAVE binary\nLOAD binary\n'
                                'SAVE check.pnm ascii\nEXIT\n')
                self.assertEqual(decode_plain(self.work / 'check.pnm'),
                                 (b'P3' if channels == 3 else b'P2', 3, 2, 15, values))

    def test_equalization_edge_cases_and_rounding(self):
        cases = [([0], 255, [255]), ([0, 0, 0], 255, [255, 255, 255]),
                 ([17, 17], 255, [255, 255]), ([255], 255, [255]),
                 ([0, 0, 1, 2], 255, [128, 128, 191, 255]),
                 ([0, 1, 2, 3], 3, [1, 2, 2, 3])]
        for values, maximum, expected in cases:
            with self.subTest(values=values, maximum=maximum):
                self.write('equal.pgm', (f'P2\n{len(values)} 1\n{maximum}\n' +
                                       ' '.join(map(str, values))).encode())
                self.run_editor('LOAD equal.pgm\nEQUALIZE\nSAVE check.pgm ascii\nEXIT\n')
                self.assertEqual(decode_plain(self.work / 'check.pgm')[-1], expected)

    def test_histogram_small_maximum_and_empty_bins(self):
        self.write('low.pgm', b'P2\n4 1\n1\n0 1 1 1')
        text = self.run_editor('LOAD low.pgm\nHISTOGRAM 6 4\nEXIT\n')
        bars = [int(line.split()[0]) for line in text.splitlines() if '|' in line]
        self.assertEqual(bars, [2, 0, 6, 0])
        text = self.run_editor('LOAD low.pgm\nHISTOGRAM 1 256\nEXIT\n')
        self.assertEqual(sum('|' in line for line in text.splitlines()), 256)

    def test_histogram_argument_validation(self):
        bad = ['HISTOGRAM -1 2', 'HISTOGRAM 4 0', 'HISTOGRAM 4 1',
               'HISTOGRAM 4 3', 'HISTOGRAM 4 512']
        text = self.run_editor('LOAD tiny.pgm\n' + '\n'.join(bad) +
                               '\nHISTOGRAM 0 2\nEXIT\n')
        self.assertEqual(text.count('Invalid set of parameters'), len(bad))
        self.assertEqual(text.count('0\t|\t\n'), 2)

    def test_large_histogram_product(self):
        self.write('many.pgm', b'P5\n30000 1\n255\n' + bytes(30000))
        text = self.run_editor('LOAD many.pgm\nHISTOGRAM 100000 2\nEXIT\n')
        self.assertIn('100000\t|\t' + '*' * 100000 + '\n0\t|\t\n', text)

    def test_large_equalization_numerator(self):
        # 256 * 9,000,000 exceeds INT_MAX in the original formula.
        self.write('large.pgm', b'P5\n3000 3000\n255\n' + bytes([17]) * 9000000)
        self.run_editor('LOAD large.pgm\nEQUALIZE\nSAVE check.pgm\nEXIT\n')
        raw = (self.work / 'check.pgm').read_bytes()
        self.assertEqual(raw, b'P5\n3000 3000\n255\n' + bytes([255]) * 9000000)

    def test_filters_preserve_border_and_respect_maximum(self):
        for maximum in [15, 255]:
            values = [0] * 27
            values[12:15] = [maximum] * 3
            self.write('impulse.ppm', (f'P3\n3 3\n{maximum}\n' +
                                     ' '.join(map(str, values))).encode())
            for operation, center in [('SHARPEN', maximum), ('EDGE', maximum),
                                      ('BLUR', (maximum + 4) // 9),
                                      ('GAUSSIAN_BLUR', (maximum + 2) // 4)]:
                with self.subTest(maximum=maximum, operation=operation):
                    self.run_editor(f'LOAD impulse.ppm\nAPPLY {operation}\n'
                                    'SAVE check.ppm ascii\nEXIT\n')
                    expected = [0] * 27
                    expected[12:15] = [center] * 3
                    self.assertEqual(decode_plain(self.work / 'check.ppm')[-1], expected)

    def test_tiny_images_and_rotation_identity(self):
        for width, height in [(1, 1), (1, 5), (5, 1), (3, 3), (4, 4)]:
            with self.subTest(width=width, height=height):
                values = list(range(width * height))
                self.write('geometry.pgm', (f'P2\n{width} {height}\n255\n' +
                                           ' '.join(map(str, values))).encode())
                self.run_editor('LOAD geometry.pgm\nROTATE 90\nROTATE 90\n'
                                'ROTATE 90\nROTATE 90\nSAVE check.pgm ascii\nEXIT\n')
                self.assertEqual(decode_plain(self.work / 'check.pgm'),
                                 (b'P2', width, height, 255, values))

    def test_header_comments_with_cr_line_endings(self):
        self.write('cr.pgm', b'P2\r# comment\r2 1\r15\r1 15\r# tail\r')
        self.run_editor('LOAD cr.pgm\nSAVE check.pgm ascii\nEXIT\n')
        self.assertEqual(decode_plain(self.work / 'check.pgm'), (b'P2', 2, 1, 15, [1, 15]))

    def test_binary_header_delimiters(self):
        for separator in [b' ', b'\t', b'\r', b'\n', b'\v', b'\f']:
            with self.subTest(separator=separator):
                self.write('raw.pgm', b'P5\n# header\n1 1\n255' + separator + b'\x23')
                self.run_editor('LOAD raw.pgm\nSAVE check.pgm ascii\nEXIT\n')
                self.assertEqual(decode_plain(self.work / 'check.pgm')[-1], [35])

    def test_nul_inside_plain_tokens_is_rejected(self):
        for data in [b'P2\n1\x00junk 1\n255\n1', b'P2\n1 1\n255\n1\x00junk']:
            self.write('nul.pgm', data)
            text = self.run_editor('LOAD nul.pgm\nEXIT\n')
            self.assertIn('Failed to load nul.pgm', text)

    def test_command_line_exact_boundary(self):
        valid = 'SELECT ALL' + ' ' * (255 - len('SELECT ALL'))
        text = self.run_editor('LOAD tiny.pgm\n' + valid + '\n' + valid + ' \nEXIT\n')
        self.assertEqual(text.count('Selected ALL'), 1)
        self.assertEqual(text.count('Invalid command'), 1)

    def test_invalid_selection_and_angle_preserve_state(self):
        text = self.run_editor('LOAD tiny.pgm\nSELECT 1 0 3 2\nSELECT -1 0 2 2\n'
                               'SELECT 0 0 0 1\nROTATE 47\nCROP\n'
                               'SAVE check.pgm ascii\nEXIT\n')
        self.assertEqual(text.count('Invalid set of coordinates'), 2)
        self.assertIn('Unsupported rotation angle', text)
        self.assertEqual(decode_plain(self.work / 'check.pgm'),
                         (b'P2', 2, 2, 255, [2, 3, 5, 6]))

    def test_standard_stream_failures_return_nonzero(self):
        descriptor = os.open(self.work, os.O_RDONLY)
        try:
            run = subprocess.run([str(BINARY)], stdin=descriptor, capture_output=True,
                                 cwd=self.work, timeout=15)
            self.assertNotEqual(run.returncode, 0)
            self.assertEqual(run.stderr, b'')
        finally:
            os.close(descriptor)
        if Path('/dev/full').exists():
            # Cover both a final-flush failure and a write failure during output.
            for commands in (b'EXIT\n',
                             b'LOAD tiny.pgm\nHISTOGRAM 10000 2\nEXIT\n'):
                with self.subTest(commands=commands):
                    with open('/dev/full', 'wb') as output:
                        run = subprocess.run([str(BINARY)], input=commands, stdout=output,
                                             stderr=subprocess.PIPE, cwd=self.work, timeout=15)
                    self.assertNotEqual(run.returncode, 0)
                    self.assertEqual(run.stderr, b'')

    def test_atomic_save_replaces_complete_file_and_cleans_temporary(self):
        self.write('destination.pgm', b'previous content')
        os.chmod(self.work / 'destination.pgm', 0o640)
        text = self.run_editor('LOAD tiny.pgm\nSAVE destination.pgm ascii\nEXIT\n')
        self.assertIn('Saved destination.pgm', text)
        self.assertEqual(decode_plain(self.work / 'destination.pgm')[-1], [1, 2, 3, 4, 5, 6])
        self.assertEqual((self.work / 'destination.pgm').stat().st_mode & 0o777, 0o640)
        self.assertEqual(list(self.work.glob('.image-editor-*')), [])

    def test_atomic_save_new_file_mode_and_nested_directory(self):
        (self.work / 'nested').mkdir()
        self.run_editor('LOAD tiny.pgm\nSAVE nested/new.pgm\nEXIT\n')
        self.assertEqual((self.work / 'nested/new.pgm').stat().st_mode & 0o777, 0o600)
        self.assertEqual(list((self.work / 'nested').glob('.image-editor-*')), [])
        self.assertEqual((self.work / 'nested/new.pgm').read_bytes(),
                         b'P5\n3 2\n255\n' + bytes([1, 2, 3, 4, 5, 6]))

    def test_atomic_save_rejects_symlinks_and_directories(self):
        self.write('target', b'preserve target')
        (self.work / 'link').symlink_to('target')
        (self.work / 'directory').mkdir()
        text = self.run_editor('LOAD tiny.pgm\nSAVE link\nSAVE directory\nEXIT\n')
        self.assertIn('Failed to save link', text)
        self.assertIn('Failed to save directory', text)
        self.assertTrue((self.work / 'link').is_symlink())
        self.assertEqual((self.work / 'target').read_bytes(), b'preserve target')
        self.assertTrue((self.work / 'directory').is_dir())
        self.assertEqual(list(self.work.glob('.image-editor-*')), [])

    def test_small_selected_filter_reads_unchanged_neighbors(self):
        self.write('selected.ppm', b'P3\n5 3\n255\n' +
                   b'0 0 0 ' * 6 + b'90 90 90 ' + b'0 0 0 ' * 8)
        self.run_editor('LOAD selected.ppm\nSELECT 2 1 4 2\nAPPLY BLUR\n'
                        'SAVE check.ppm ascii\nEXIT\n')
        expected = [0] * 45
        expected[18:21] = [90] * 3  # Outside selection remains unchanged.
        expected[21:24] = [10] * 3  # Reads the original left neighbor.
        self.assertEqual(decode_plain(self.work / 'check.ppm')[-1], expected)

    def test_color_square_selection_rotation_keeps_exterior(self):
        values = [component for v in range(1, 17) for component in [v, v + 20, v + 40]]
        self.write('square.ppm', (b'P3\n4 4\n255\n' +
                   ' '.join(map(str, values)).encode()))
        self.run_editor('LOAD square.ppm\nSELECT 1 1 4 4\nROTATE 90\n'
                        'SAVE check.ppm ascii\nEXIT\n')
        labels = [1, 2, 3, 4, 5, 14, 10, 6, 9, 15, 11, 7, 13, 16, 12, 8]
        expected = [component for v in labels for component in [v, v + 20, v + 40]]
        self.assertEqual(decode_plain(self.work / 'check.ppm')[-1], expected)

    def test_fault_injection(self):
        command = [str(FAULT_BINARY)]
        if VALGRIND:
            command = ['valgrind', '--quiet', '--leak-check=full',
                       '--show-leak-kinds=all', '--errors-for-leak-kinds=all',
                       '--track-origins=yes', '--error-exitcode=99', *command]
        run = subprocess.run(command, cwd=self.work, capture_output=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stderr.decode(errors='replace'))
        self.assertEqual(run.stderr, b'')
        self.assertIn(b'Fault injection assertions passed', run.stdout)
        self.assertNotIn(b'Saved ', run.stdout)
        self.assertNotIn(b'Rotated ', run.stdout)
        self.assertNotIn(b'Image cropped', run.stdout)
        self.assertNotIn(b'APPLY BLUR done', run.stdout)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=BINARY)
    parser.add_argument('--fault-binary', type=Path, default=FAULT_BINARY)
    parser.add_argument('--valgrind', action='store_true')
    args, remaining = parser.parse_known_args()
    BINARY = args.binary.resolve()
    FAULT_BINARY = args.fault_binary.resolve()
    VALGRIND = args.valgrind
    unittest.main(argv=[__file__, *remaining])
