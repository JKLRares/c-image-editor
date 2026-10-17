# Project history

The original project is university coursework by Rares Constantinescu
(313CA), POLITEHNICA Bucharest. Later hardening, modularization, testing and
documentation were AI-assisted.

| Development checkpoint | Scope |
| --- | --- |
| `7072885` | Original C implementation, Makefile and README |
| `10e6ee1` | Input validation and memory-safety hardening |
| `cc9565c` | Hardening tests and verification; benchmark baseline (P0) |
| `4c142cb` | Modular implementation, optimized buffer use and atomic saving (P1) |
| `740d524` | Source-only `main` snapshot, including the stdout error fix |

The development checkpoints distinguish the coursework implementation from later
changes. The source-only `main` snapshot does not include those earlier commits
as ancestors. Historical comparisons require a checkout retaining them or a separately
preserved baseline executable; a source-only export contains neither Git history
nor that executable.

The coursework algorithms remain the basis for half-open selections, row-copy
cropping, rotation mappings, selected-square layer rotation and 3x3 filters.
Hardening added complete command parsing, bounded NetPBM tokens, checked
allocation/index arithmetic, validated sample ranges and cleanup on EOF or
failure. Modularization separated ownership, parsing, file I/O, operations and
terminal output. Later buffer changes removed full-image allocation for 180°
rotation and restricted convolution scratch storage to selected interior pixels.

## Equalization compatibility

The original formula used a ceiling-style calculation and subtracted `CDF(0)`.
The supplied fixture agrees with rounded cumulative counts without subtraction.
For `input/eq.pgm`, the original formula maps intensity 114 to 1; the reference
`ref/s_e_1.pgm` maps it to 0. The corrected P5 output matched that reference
byte-for-byte (699,408 bytes). The checker tolerance of five sample levels had
allowed this difference.

The original statement is unavailable; compatibility was established against
the supplied reference. All original input fixtures use Maxval 255. Support
for lower Maxval values is a documented extension of that behavior. The
[current design](P1_IMPLEMENTATION.md#pixel-operations-and-parsing) describes
the formula and its constant-image behavior.

## Hardening verification record

These results describe checkpoint `cc9565c`, before module extraction and atomic
saving. Current commands and results are in [design and testing](P1_IMPLEMENTATION.md#testing).

| Check | Recorded result |
| --- | --- |
| Independent regressions, normal and ASan+UBSan | 31/31 in each mode; no sanitizer diagnostics, LSan disabled |
| Original cases, normal and ASan+UBSan | 69/69 in each mode; 508 image comparisons each |
| Focused Valgrind regressions | 5/5 passed |
| Assignment cases 00, 04, 28, 32 under Valgrind | 4/4 passed; 26 image comparisons; no reported errors/leaks |
| Strict warnings and GCC analyzer | No diagnostics with `-Werror` |
| Original-file hashes | All 1,418 snapshotted files unchanged |
| Whitespace check | `git diff --check` passed |

Baseline checks reproduced a SAVE overflow, malformed-selection double free,
out-of-bounds rotations, division by zero, arithmetic overflow, a null-stream
access on failed SAVE, truncated-raster acceptance and an EOF image leak.
The early hardening checkpoint still wrote directly to destinations and missed
an earlier stdout write error when the final flush succeeded. Both were
addressed in the source-only snapshot: atomic replacement protects existing
destinations, and the command loop checks the stream's error indicator.

Original grading assets and third-party checker materials remain outside the
source distribution. Publication is subject to course rules and applicable
permissions; see [source distribution](REPOSITORY.md).
